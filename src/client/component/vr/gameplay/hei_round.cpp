#include <std_include.hpp>
#include "hei_round.hpp"
#include "native_ammunition.hpp"
#include "native_weapon_read.hpp"
#include "native_weapon_fx.hpp"
#include "native_launcher_fire.hpp"
#include "native_carry.hpp"
#include "native_scripted_control.hpp"
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

namespace vr::gameplay::weapons::hei_round
{
	namespace
	{
		game::dvar_t *option{}, *damage{}, *radius{};
		bool ready{};
		std::atomic_bool alive{true};
		std::atomic_uint64_t generation{1}, shots{}, contacts{}, blasts{}, effects{}, discarded{}, overflow{};
		std::atomic_uint64_t grenade_requests{}, grenade_spawns{}, grenade_rejections{};
		std::atomic_int grenade_reason{};
		std::array<native_carry::world_key, 64> projectiles{};
		thread_local shot_scope* active{};
		struct visual
		{
			spatial_math::vec position{};
			int time{};
			std::uint64_t epoch{}, serial{};
		};
		std::array<visual, 128> queue{};
		std::size_t count{};
		std::mutex mutex;
		bool enabled() noexcept
		{
			if (!ready || !alive || !option || !option->current.enabled)
				return false;
			const auto* vr = game::Dvar_FindVar("vr_enable");
			return vr && vr->current.enabled;
		}
		void clear()
		{
			++generation;
			projectiles = {};
			grenade_reason = 0;
			native_launcher_fire::reset();
			const std::lock_guard lock(mutex);
			count = 0;
		}
		void tick()
		{
			std::array<visual, 128> batch;
			std::size_t size{};
			{
				const std::lock_guard lock(mutex);
				size = count;
				std::copy_n(queue.begin(), size, batch.begin());
				count = 0;
			}
			if (!size)
				return;
			const auto* paused = game::Dvar_FindVar("cl_paused");
			if (!enabled() || !game::CL_IsCgameInitialized() || !paused || paused->current.integer ||
			    *game::keyCatchers)
			{
				discarded += size;
				return;
			}
			constexpr std::array names{"fx/impacts/small_explosion_a",
			                           "fx/impacts/small_explosion_b",
			                           "fx/impacts/small_explosion_c"};
			const auto now = game::CG_GetGameTime(0);
			for (std::size_t i{}; i < size; ++i)
			{
				const auto& event = batch[i];
				if (event.epoch != generation || now < event.time || std::int64_t(now) - event.time > 250)
				{
					++discarded;
					continue;
				}
				const auto* name = names[event.serial % names.size()];
				// Resolve the original common resource in the current zone. Never
				// retain a map pointer, mutate a definition or synthesize a fallback.
				if (!game::DB_XAssetExists(game::ASSET_TYPE_FX, name) ||
				    game::DB_IsXAssetDefault(game::ASSET_TYPE_FX, name))
				{
					++discarded;
					continue;
				}
				auto* fx = game::DB_FindXAssetHeader(game::ASSET_TYPE_FX, name, 0).fx;
				hands::anchor pose{};
				pose.position = event.position;
				pose.rotation = {0, 0, 0, 1};
				if (native_weapon_fx::play_frontend(fx, pose, event.time))
					++effects;
				else
					++discarded;
			}
		}
	}
	unsigned projectile_definition(unsigned source, bool& requested) noexcept
	{
		requested = false;
		if (!enabled())
			return 0;
		const auto value = native_weapon_read::get(source);
		if (!value || !grenade_weapon(int(value.definition->weapType),
		                               int(value.definition->weapClass),
		                               int(value.definition->inventoryType),
		                               value.name.data()))
			return 0;
		requested = true;
		++grenade_requests;
		// On-demand bounded ownership metadata, never a per-frame entity scan.
		// Native frees/reuses the entity; this feature never deletes projectiles.
		std::size_t active_projectiles{};
		for (auto& key : projectiles)
		{
			if (key.entity <= 0 || key.entity >= 3998 || native_carry::entity_key(key.entity) != key ||
			    !reinterpret_cast<const unsigned char*>(&game::g_entities[key.entity])[0xbc])
				key = {};
			else
				++active_projectiles;
		}
		const auto token = active_projectiles < projectiles.size() ? native_launcher_fire::resident_grenade() : 0;
		if (!token)
		{
			++grenade_rejections;
			const int reason = active_projectiles == projectiles.size() ? 2 : 1;
			if (grenade_reason.exchange(reason) != reason)
				console::warn("[VR HEI] Grenade conversion rejected before ammunition debit: %s\n",
				              reason == 2 ? "64 conversion projectiles are still active"
				                          : "no resident original underbarrel grenade definition");
		}
		else
			grenade_reason = 0;
		return token;
	}
	void projectile_fired(game::gentity_s* entity) noexcept
	{
		if (!entity)
			return;
		const auto number = entity - &game::g_entities[0];
		if (number <= 0 || number >= 3998)
			return;
		for (auto& key : projectiles)
			if (key.entity <= 0)
			{
				key = native_carry::entity_key(int(number));
				break;
			}
		++grenade_spawns;
	}
	void observe(const native_bullet_trace::parameters& p,
	             unsigned weapon,
	             bool alternate,
	             const game::gentity_s* attacker,
	             const native_bullet_trace::result& hit) noexcept
	{
		if (!active || !active->admitted || alternate || p.shooter != 0 || attacker != &game::g_entities[0] ||
		    weapon != active->weapon.weapon || (p.means != 1 && p.means != 2))
			return;
		if (active->contact.record(hit))
			++contacts;
	}
	shot_scope::shot_scope(weapon_identity id) noexcept : weapon(id), previous(active)
	{
		active = this;
		if (previous || !enabled() || !scheduler::is_executing(scheduler::pipeline::server))
			return;
		const auto definition = native_weapon_read::get(id.weapon);
		if (!definition || definition.definition->weapType != game::WEAPTYPE_BULLET ||
		    definition.definition->bulletExplosiveDamage)
			return;
		player = game::g_entities[0].client;
		if (!player)
			return;
		command_time = reinterpret_cast<const game::playerState_s*>(player)->commandTime;
		timeline = native_ammunition::timeline();
		epoch = generation;
		admitted = true;
		++shots;
	}
	shot_scope::~shot_scope()
	{
		active = previous;
	}
	void shot_scope::complete() noexcept
	{
		// Stop observing before radial damage can run scripts or native traces.
		active = previous;
		if (!admitted || !contact.found || !enabled() || epoch != generation ||
		    timeline != native_ammunition::timeline() || game::g_entities[0].client != player ||
		    !game::CL_IsCgameInitialized())
			return;
		admitted = false;
		const auto* ps = reinterpret_cast<const game::playerState_s*>(player);
		if (ps->commandTime != command_time || !scripted_control::firing_allowed(ps))
			return;
		const float amount = damage->current.value, meters = radius->current.value;
		const auto* scale = game::Dvar_FindVar(settings::world_scale.name);
		const float units = scale ? scale->current.value : settings::default_world_scale;
		if (!std::isfinite(amount) || amount < 0 || amount > 100 || !std::isfinite(meters) || meters < .1f ||
		    meters > 2 || !std::isfinite(units) || units < 1 || units > 1000)
			return;
		const auto center = contact.center();
		const auto serial = ++blasts;
		const int time = game::CG_GetGameTime(0);
		{
			const std::lock_guard lock(mutex);
			if (count < queue.size())
				queue[count++] = {center, time, epoch, serial};
			else
				++overflow;
		}
		if (amount == 0)
			return;
		// Same complete G_RadiusDamage ABI/default flags as the original
		// radiusdamage builtin at 0x1404fbc5d..fbcbb. Native occlusion, immunity,
		// damage callbacks and real player/weapon attribution remain authoritative.
		// No builtin EV_RADIUS_DAMAGE (0x71), replacement bullet event or health write.
		utils::hook::invoke<void>(0x1404be5b0,
		                          center.data(),
		                          static_cast<game::gentity_s*>(nullptr),
		                          &game::g_entities[0],
		                          amount,
		                          0.f,
		                          meters * units,
		                          1.f,
		                          static_cast<const float*>(nullptr),
		                          static_cast<game::gentity_s*>(nullptr),
		                          17u,
		                          weapon.weapon,
		                          false,
		                          0,
		                          1);
	}
	class component final : public component_interface
	{
		void post_unpack() override
		{
			option = dvars::register_bool(settings::hei_round.name,
			                              false,
			                              game::DVAR_FLAG_SAVED,
			                              "Add a small explosion to ordinary player bullet impacts");
			damage = dvars::register_float("vr_heiRoundDamage",
			                               20.f,
			                               0.f,
			                               100.f,
			                               game::DVAR_FLAG_SAVED,
			                               "HEI additional center damage; falls to zero at the radius");
			radius = dvars::register_float("vr_heiRoundRadius",
			                               .75f,
			                               .1f,
			                               2.f,
			                               game::DVAR_FLAG_SAVED,
			                               "HEI additional explosion radius in meters");
			constexpr std::uint8_t bytes[]{0x40, 0x55, 0x57, 0x41, 0x55, 0xb8, 0xd0, 0x20, 0, 0, 0xe8};
			std::array<std::uint8_t, sizeof(bytes)> mask{};
			mask.fill(255);
			ready = bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x1404be5b0),
			                                                         {bytes, mask.data(), sizeof(bytes)})) &&
			        native_bullet_trace::add_observer(observe) && native_weapon_fx::initialize();
			fastfiles::on_pre_unload(clear);
			scripting::on_shutdown(
			    [](bool, bool after)
			    {
				    if (!after)
					    clear();
			    });
			scheduler::loop(tick, scheduler::pipeline::main);
			command::add(
			    "vr_hei_round_status",
			    []
			    {
				    console::info(
				        "[VR HEI] ready=%d enabled=%d damage=%.1f radius_m=%.2f shots=%llu contacts=%llu "
				        "blast_requests=%llu fx_requests=%llu discarded=%llu overflow=%llu "
				        "grenade_requests=%llu grenade_spawns=%llu grenade_rejections=%llu grenade_reason=%d\n",
				        ready,
				        enabled(),
				        damage->current.value,
				        radius->current.value,
				        shots.load(),
				        contacts.load(),
				        blasts.load(),
				        effects.load(),
				        discarded.load(),
				        overflow.load(),
				        grenade_requests.load(),
				        grenade_spawns.load(),
				        grenade_rejections.load(),
				        grenade_reason.load());
			    });
		}
		void pre_destroy() override
		{
			alive = false;
			clear();
		}
	};
}
REGISTER_COMPONENT(vr::gameplay::weapons::hei_round::component)
