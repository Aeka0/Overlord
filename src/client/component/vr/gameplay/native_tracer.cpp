#include <std_include.hpp>
#include "native_tracer.hpp"
#include "tracer_events.hpp"
#include "native_ballistics.hpp"
#include "weapon_instance_cache.hpp"
#include "weapon_carry_runtime.hpp"
#include "native_carry.hpp"
#include "../settings.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/fastfiles.hpp"
#include "component/scheduler.hpp"
#include "component/scripting.hpp"
#include "game/dvars.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>
#include <mutex>

namespace vr::gameplay::weapons::tracers
{
	namespace
	{
		constexpr std::uintptr_t bullet = 0x1404AAD60, temporary = 0x1405183E0, event = 0x140373630;
		constexpr std::uintptr_t impact = 0x1403B6050, filter = 0x1403B5FB0, spawn = 0x1406E6BA0;
		constexpr std::uintptr_t ordinary = 0x1406A8F80, signature = 0x1406A8B00, entities = 0x141C328F0;
		utils::hook::detour bullet_hook, temporary_hook, event_hook, impact_hook, filter_hook, spawn_hook, cadence_hook;
		game::dvar_t* option{};
		std::atomic_bool ready{}, alive{true};
		std::atomic_uint64_t shots{}, stamped{}, matched{}, submitted{}, rejected{}, overflows{};
		std::atomic_uint64_t neutral_material_missing{};
		std::mutex mutex;
		event_bridge<> bridge;
		struct instance_sequence { sequence clock; weapon_identity lease; };
		instance_cache<instance_sequence, 640> sequences;
		std::uint64_t epoch{1}, timeline{};
		int server_time{}, client_time{}, prune_time{};
		style previous_style{style::native};
		struct allocation { game::gentity_s* entity{}; std::uint64_t serial{}; };
		struct capture
		{
			weapon_identity owner{}, lease{};
			unsigned weapon{}, shooter{};
			style selected{style::native};
			std::uint64_t epoch{}, timeline{};
			std::array<allocation, 512> events{};
			std::size_t count{};
		};
		thread_local capture* active_shot{};
		struct presentation { event_key identity{}; delivery item{}; };
		thread_local presentation* active_event{};
		thread_local game::TracerDef* active_definition{};
		thread_local unsigned active_weapon{};

		template<class T> T read(const void* source, std::size_t offset)
		{ T result{}; std::memcpy(&result, static_cast<const std::byte*>(source)+offset, sizeof(result)); return result; }
		event_key identify(const void* state) { return key({static_cast<const std::byte*>(state), 0x94}); }
		void clear_locked()
		{
			++epoch; bridge.clear(); sequences = {}; server_time = client_time = prune_time = 0;
			timeline = native_ammunition::timeline();
		}
		void clear() { const std::lock_guard lock(mutex); clear_locked(); }
		style selected()
		{
			const auto* vr = game::Dvar_FindVar("vr_enable");
			return ready && alive && vr && vr->current.enabled && option && option->type == game::dvar_type::enumeration
				? sanitize(option->current.integer) : style::native;
		}
		bool local(const game::gentity_s* source, const game::gentity_s* attacker)
		{
			return source && attacker && game::CL_IsCgameInitialized() && game::g_entities[0].client &&
				(source == &game::g_entities[0] || attacker == &game::g_entities[0]);
		}
		void publish(capture& shot)
		{
			// Do not acquire carry/ammunition locks while holding the bridge lock.
			std::array<event_key, 512> keys{};
			for (std::size_t i = 0; i < shot.count; ++i) keys[i] = identify(shot.events[i].entity);
			const std::lock_guard lock(mutex);
			if (shot.epoch != epoch || shot.timeline != native_ammunition::timeline()) return;
			instance_sequence* clock{}; appearance visual{};
			for (std::size_t i = 0; i < shot.count; ++i)
			{
				const auto& identity = keys[i];
				if (!valid(identity) || identity.weapon != shot.weapon || identity.shooter != shot.shooter) continue;
				if (!clock)
				{
					clock = sequences.acquire(shot.owner);
					if (!clock) { ++overflows; return; }
					clock->lease = shot.lease;
					visual = {shot.selected, clock->clock.take(shot.selected)}; ++shots;
				}
				if (bridge.publish(identity, shot.events[i].serial, visual)) ++stamped;
				else ++rejected;
			}
		}
		int bullets(game::gentity_s* source, float spread, native_ballistics::parameters* packet,
			game::gentity_s* attacker, int time, bool ricochet, float range)
		{
			const auto mode = selected();
			const auto previous = active_shot;
			// Native recursive ricochets remain part of their originating shot.
			if (ricochet && previous)
				return bullet_hook.invoke<int>(source, spread, packet, attacker, time, ricochet, range);
			active_shot = nullptr;
			const auto restore = gsl::finally([&] { active_shot = previous; });
			if (mode == style::native || !packet || !local(source, attacker) || !packet->weapon || source->s.entityNum >= entity_limit)
				return bullet_hook.invoke<int>(source, spread, packet, attacker, time, ricochet, range);
			capture shot;
			shot.weapon = packet->weapon; shot.shooter = source->s.entityNum; shot.selected = mode;
			shot.owner = detail::owner.id.weapon == shot.weapon ? detail::owner.id : native_ammunition::projected_identity(shot.weapon);
			if (!shot.owner.weapon) shot.owner = {shot.weapon, 0}; // Native/mounted weapon, never a guessed carried copy.
			shot.lease = detail::owner.id == shot.owner ? detail::owner.lease : shot.owner;
			// Retire departed physical instances outside the bridge lock.
			struct retirement { weapon_identity id{}, lease{}; };
			std::array<retirement, 640> retired{}; std::size_t retire_count{};
			{
				const std::lock_guard lock(mutex);
				if (time < prune_time || std::int64_t(time)-prune_time >= 1000)
				{
					prune_time = time;
					for (const auto& entry : sequences.entries()) if (entry.id.generation) retired[retire_count++] = {entry.id, entry.value.lease};
				}
			}
			for (std::size_t i = 0; i < retire_count; ++i)
				if (carry::contains(retired[i].lease) || native_carry::tracks(retired[i].lease)) retired[i].id = {};
			{
				const std::lock_guard lock(mutex);
				if (timeline != native_ammunition::timeline() || time < server_time) clear_locked();
				server_time = time;
				if (previous_style != mode) { sequences = {}; previous_style = mode; }
				sequences.retain([&](weapon_identity id) {
					return id == shot.owner || std::none_of(retired.begin(), retired.begin()+retire_count,
						[&](const retirement& item) { return item.id == id; });
				});
				shot.epoch = epoch; shot.timeline = timeline;
			}
			active_shot = &shot;
			const int result = bullet_hook.invoke<int>(source, spread, packet, attacker, time, ricochet, range);
			active_shot = nullptr; publish(shot);
			return result;
		}
		game::gentity_s* temporary_entity(const float* position, int kind)
		{
			auto* result = temporary_hook.invoke<game::gentity_s*>(position, kind);
			if (!ready || !alive || !result || result->s.entityNum >= entity_limit) return result;
			std::uint64_t serial{};
			{ const std::lock_guard lock(mutex); serial = bridge.allocate(result->s.entityNum); }
			if (active_shot && bullet_event(kind))
			{
				if (active_shot->count < active_shot->events.size()) active_shot->events[active_shot->count++] = {result, serial};
				else ++overflows;
			}
			return result;
		}
		void client_event(int client, void* entity, int kind, int flags)
		{
			auto* previous = active_event; active_event = nullptr;
			const auto restore = gsl::finally([&] { active_event = previous; });
			presentation current;
			if (ready && alive && client == 0 && entity && bullet_event(kind))
			{
				current.identity = identify(static_cast<const std::byte*>(entity)+0x128);
				const auto now = game::CG_GetGameTime(client);
				const std::lock_guard lock(mutex);
				if (timeline != native_ammunition::timeline() || now < client_time) clear_locked();
				client_time = now;
				if (int(current.identity.type) == kind+0x10) current.item = bridge.consume(current.identity);
				if (current.item.matched) { active_event = &current; ++matched; }
			}
			event_hook.invoke<void>(client, entity, kind, flags);
		}
		game::TracerDef* definition(unsigned weapon, bool alternate, bool special)
		{
			if (active_definition && active_weapon == weapon) return active_definition;
			return utils::hook::invoke<game::TracerDef*>(special ? signature : ordinary, weapon, alternate);
		}
		game::TracerDef* ordinary_definition(unsigned weapon, bool alternate) { return definition(weapon, alternate, false); }
		game::TracerDef* signature_definition(unsigned weapon, bool alternate) { return definition(weapon, alternate, true); }
		void create(int client, const float* start, const float* end, void* source, unsigned weapon, bool alternate, int special)
		{
			if (!active_event || !active_event->item.draw) return;
			active_event->item.draw = false;
			for (unsigned i = 0; i < 3; ++i)
				if (!start || !end || !std::isfinite(start[i]) || !std::isfinite(end[i])) { ++rejected; return; }
			auto* original = definition(weapon, alternate, special != 0);
			// Same resident default used by the native creator for missing definitions.
			if (!original) original = *reinterpret_cast<game::TracerDef**>(0x141B7C5B8);
			if (!original || !original->material || !std::isfinite(original->speed) || original->speed <= 0 ||
				!std::isfinite(original->screwDist) || original->screwDist <= 0) { ++rejected; return; }
			auto copy = *original;
			const auto visual = active_event->item.visual;
			if (colored(visual.selected))
			{
				// The stock BC6H image is orange before vertex tint is applied.
				// A separately registered neutral HDR material preserves its shape,
				// exposure and fade without changing Native, Blaze or NPC resources.
				constexpr auto name = "vr_tracer_neutral";
				auto* material = game::DB_XAssetExists(game::ASSET_TYPE_MATERIAL, name) &&
					!game::DB_IsXAssetDefault(game::ASSET_TYPE_MATERIAL, name)
					? game::DB_FindXAssetHeader(game::ASSET_TYPE_MATERIAL, name, 0).material : nullptr;
				if (!material)
				{
					if (neutral_material_missing.fetch_add(1) == 0)
						console::warn("[VR tracer] h2_vr_tracers.ff is missing; retaining native tracer material\n");
				}
				else copy.material = material;
			}
			apply(copy, visual.selected, visual.shot);
			auto* previous = active_definition; const auto previous_weapon = active_weapon;
			active_definition = &copy; active_weapon = weapon;
			const auto restore = gsl::finally([&] { active_definition = previous; active_weapon = previous_weapon; });
			spawn_hook.invoke<void>(client, start, end, source, weapon, alternate, special); ++submitted;
		}
		void tracer(int client, const float* start, const float* end, void* source, unsigned weapon, bool alternate, int special)
		{
			if (ready && active_event && client == 0 && active_event->identity.weapon == weapon)
			{
				// Only EV_BULLET_TRACER supplies its own final segment here. Impact
				// events use bullet_impact's serialized start, never a recomputed
				// selected-weapon muzzle (which may belong to the other VR hand).
				if (active_event->identity.type == 0x54) create(client, start, end, source, weapon, alternate, special);
			}
			else spawn_hook.invoke<void>(client, start, end, source, weapon, alternate, special);
		}
		int cadence(int client, unsigned weapon, bool alternate, int special, void* source)
		{
			if (ready && active_event && client == 0 && active_event->identity.weapon == weapon)
				return active_event->item.draw ? 1 : 0;
			return cadence_hook.invoke<int>(client, weapon, alternate, special, source);
		}
		int should_draw(int client, void* source, unsigned target, unsigned weapon, bool alternate, int special, int kind)
		{
			// Owned events are drawn once from their serialized start/end. Bypass
			// only their stock appearance (and its cadence), never impact/audio logic.
			if (ready && active_event && client == 0 && active_event->identity.weapon == weapon && source &&
				read<std::uint16_t>(source, 0x1b4) == active_event->identity.shooter) return 0;
			return filter_hook.invoke<int>(client, source, target, weapon, alternate, special, kind);
		}
		void bullet_impact(int client, std::uint16_t shooter, std::uint16_t target, unsigned weapon, bool alternate,
			int special, const float* start, const float* end, int surface, int kind, unsigned char flags)
		{
			impact_hook.invoke<void>(client, shooter, target, weapon, alternate, special, start, end, surface, kind, flags);
			if (ready && active_event && client == 0 && shooter == active_event->identity.shooter &&
				weapon == active_event->identity.weapon && shooter < entity_limit)
				create(client, start, end, reinterpret_cast<void*>(entities+std::uintptr_t(shooter)*0x200), weapon, alternate, special);
		}
		template<std::size_t N> bool verify(std::uintptr_t address, const std::array<std::uint8_t, N>& bytes)
		{
			std::array<std::uint8_t, N> mask; mask.fill(255);
			return bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(address), {bytes.data(), mask.data(), N}));
		}
		bool install()
		{
			static_assert(offsetof(game::WeaponDef, tracerType) == 0x528 && offsetof(game::WeaponDef, signatureTracerType) == 0x530);
			static_assert(offsetof(game::TracerDef, colors) == 0x30 && sizeof(game::TracerDef) == 0x80);
			if (!verify(bullet, std::array<std::uint8_t, 23>{0x4c,0x8b,0xdc,0x55,0x57,0x41,0x55,0x41,0x57,0x49,0x8d,0xab,0xf8,0xf3,0xff,0xff,0x48,0x81,0xec,0xe8,0x0c,0,0}) ||
				!verify(temporary, std::array<std::uint8_t, 15>{0x48,0x89,0x5c,0x24,8,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x50}) ||
				!verify(event, std::array<std::uint8_t, 20>{0x45,0x85,0xc0,0x0f,0x84,0xa2,0x2d,0,0,0x4c,0x8b,0xdc,0x55,0x53,0x41,0x55,0x41,0x56,0x41,0x57}) ||
				!verify(impact, std::array<std::uint8_t, 20>{0x40,0x53,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x81,0xec,0xa8,0,0,0}) ||
				!verify(filter, std::array<std::uint8_t, 15>{0x48,0x89,0x5c,0x24,8,0x44,0x89,0x4c,0x24,0x20,0x57,0x48,0x83,0xec,0x30}) ||
				!verify(spawn, std::array<std::uint8_t, 22>{0x4c,0x8b,0xdc,0x49,0x89,0x5b,0x18,0x49,0x89,0x6b,0x20,0x56,0x57,0x41,0x56,0x48,0x81,0xec,0xb0,0,0,0}) ||
				!verify(0x1406E6EC0, std::array<std::uint8_t, 15>{0x48,0x83,0xec,0x28,0x8b,0xc2,0x41,0x0f,0xb6,0xd0,0x8b,0xc8,0x45,0x85,0xc9}) ||
				!verify(0x140517AA5, std::array<std::uint8_t, 3>{0x8b,0x7b,0x78}) ||
				!verify(0x140517AF4, std::array<std::uint8_t, 6>{0x8d,0x47,0x01,0x89,0x43,0x78}) ||
				!verify(0x1406E6C8C, std::array<std::uint8_t, 5>{0xe8,0x6f,0x1e,0xfc,0xff}) ||
				!verify(0x1406E6C93, std::array<std::uint8_t, 5>{0xe8,0xe8,0x22,0xfc,0xff}) ||
				utils::hook::is_relatively_far(reinterpret_cast<void*>(0x1406E6C8C), reinterpret_cast<void*>(signature_definition)) ||
				utils::hook::is_relatively_far(reinterpret_cast<void*>(0x1406E6C93), reinterpret_cast<void*>(ordinary_definition))) return false;
			try
			{
				bullet_hook.create(bullet, bullets); temporary_hook.create(temporary, temporary_entity);
				event_hook.create(event, client_event); impact_hook.create(impact, bullet_impact);
				filter_hook.create(filter, should_draw); spawn_hook.create(spawn, tracer);
				cadence_hook.create(0x1406E6EC0, cadence);
				utils::hook::call(0x1406E6C8C, signature_definition); utils::hook::call(0x1406E6C93, ordinary_definition);
			}
			catch (const std::exception& e) { console::error("[VR tracer] Native hooks unavailable: %s\n", e.what()); return false; }
			return true;
		}
	}
	std::string status()
	{
		return std::format("tracer_ready={} style={} shots={} events={} matched={} submitted={} rejected={} overflow={} neutral_material_missing={}\n",
			ready.load(), int(selected()), shots.load(), stamped.load(), matched.load(), submitted.load(), rejected.load(), overflows.load(), neutral_material_missing.load());
	}
	class component final : public component_interface
	{
		void post_unpack() override
		{
			static auto values = settings::tracer_style.values;
			option = dvars::register_enum(settings::tracer_style.name, values.data(), 0, game::DVAR_FLAG_SAVED,
				"Player tracer style; native preserves stock behavior, other styles trace every shot");
			ready = install();
			if (!ready) console::error("[VR tracer] Native contracts rejected; original tracers retained\n");
			fastfiles::on_pre_unload(clear);
			scheduler::loop([] {
				const auto mode = selected();
				const std::lock_guard lock(mutex);
				if (previous_style != mode) { sequences = {}; previous_style = mode; }
			}, scheduler::pipeline::main);
			scripting::on_level_start(clear);
			scripting::on_shutdown([](bool, bool after) { if (!after) clear(); });
			command::add("vr_tracer_status", [] { console::info("%s", status().c_str()); });
		}
		void pre_destroy() override { alive = false; clear(); }
	};
}

REGISTER_COMPONENT(vr::gameplay::weapons::tracers::component)
