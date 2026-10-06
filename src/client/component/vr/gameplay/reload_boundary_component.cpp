#include <std_include.hpp>
#include "native_ammunition.hpp"
#include "manual_feed_boundary.hpp"
#include "official_cheats.hpp"
#include "../debug_options.hpp"
#include "weapon_feedback.hpp"
#include "weapon_interaction.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>
#include <utils/io.hpp>
#include <mutex>
#include <sstream>

namespace vr::gameplay::weapons::reload_boundary
{
	namespace
	{
		// Single owner for native fire/consume/reload-start detours. Production
		// mechanics gates share these hooks; optional diagnostics remain read-only.
		utils::hook::detour fire_hook, consume_hook, reload_hook;
		std::atomic<bool> alive{true};
		bool installed{};
		std::mutex mutex;
		struct record
		{
			const char* kind{};
			int role{-1}, command_time{}, amount{}, side{};
			native_ammunition::snapshot before{}, after{};
			std::uintptr_t caller{};bool inside_fire{};
		};
		std::array<record, 64> history{};
		std::uint64_t records{}, revision{};
		std::array<std::array<std::uint64_t, 3>, 2> counts{};
		native_ammunition::snapshot latest;
		bool recorded_enabled{}, recorded_gameplay{};
		thread_local int shot_time{};
		thread_local bool inside_fire{};
		bool observing() noexcept
		{
			return debug_options::enabled(debug_options::probe::weapon_events) &&
				alive.load(std::memory_order_relaxed) && installed;
		}
		void append(record r, size_t kind)
		{
			if (r.role < 0 || r.role > 1)
				return;
			const std::lock_guard lock(mutex);
			history[records++ % history.size()] = r;
			++counts[r.role][kind];
			++revision;
		}
		void fire_stub(game::pmove_t* pm, int unknown, void* native_context, int side)
		{
			const auto* ps = pm ? pm->ps : nullptr;
			const int role = observing() ? native_ammunition::local_role(ps) : -1;
			const auto before = role >= 0 ? native_ammunition::observe(ps) : native_ammunition::snapshot{};
			const auto previous = shot_time;
			const auto previous_inside = inside_fire;
			shot_time = pm ? pm->cmd.serverTime : 0;
			inside_fire = true;
			const auto restore = gsl::finally([&] { shot_time = previous; inside_fire = previous_inside; });
			if (!manual_feed::allow_fire(ps, shot_time, side)) return;
			fire_hook.invoke<void>(pm, unknown, native_context, side);
			if (role >= 0)
				append({"fire", role, shot_time, 0, side, before, native_ammunition::observe(ps)}, 0);
		}
		void consume_stub(void* ps, std::uint32_t weapon, bool alternate, int amount, int side)
		{
			const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			const int role = native_ammunition::local_role(ps);
			const int command=inside_fire ? shot_time : role>=0 ? static_cast<const game::playerState_s*>(ps)->commandTime : 0;
			const auto before = role >= 0 ? native_ammunition::observe_native_boundary(ps,weapon,alternate) : native_ammunition::snapshot{};
			consume_hook.invoke<void>(ps, weapon, alternate, amount, side);
			const auto after = role >= 0 ? native_ammunition::observe_native_boundary(ps,weapon,alternate) : native_ammunition::snapshot{};
			const bool sustained=cheats::sustain_ammo() && before.valid && after.valid && before.loaded==after.loaded;
			if (inside_fire) manual_feed::consumed(ps, command, weapon, alternate, amount, side, before, after,sustained);
			if (inside_fire && role == 0 && !side && !alternate && amount == 1 && before.valid && after.valid &&
				before.weapon == weapon && before.id() == after.id() && after.loaded == before.loaded-int(!sustained) && after.reserve == before.reserve)
			{
				const auto muzzle = current_muzzle();
				const auto input = controller_input::latest();
				const auto owner = current_hold();
				const auto now = controller_input::clock::now();
				if (owner.id() == before.id() && ready(muzzle, owner, input.reference_generation, now))
					feedback::publish({mechanics::effect::shot, owner, input.reference_generation, now, muzzle.position, nullptr});
			}
			if (role >= 0 && observing()) append({"consume", role, command, amount, side, before, after,caller,inside_fire}, 1);
		}
		// PM_BeginWeaponReload precedes both segmented start animations and the
		// ordinary reload leaf. Empty automatic reload can bypass CanReload, so
		// gate here before its events, camera animation and state 11 are emitted.
		utils::hook::detour begin_reload_hook;
		std::array<std::atomic_uint64_t,2> blocked_begins{};
		void begin_reload_stub(void* ps, void* pm, void* pml, int side)
		{
			if (manual_feed::blocks_reload(ps, side))
			{
				const int role=native_ammunition::local_role(ps);
				if(role>=0 && role<2)++blocked_begins[role];
				return;
			}
			begin_reload_hook.invoke<void>(ps,pm,pml,side);
		}
		void reload_stub(void* ps, int side)
		{
			if (manual_feed::blocks_reload(ps, side)) return;
			const int role = observing() ? native_ammunition::local_role(ps) : -1;
			const auto before = role >= 0 ? native_ammunition::observe(ps) : native_ammunition::snapshot{};
			reload_hook.invoke<void>(ps, side);
			if (role >= 0)
				append({"reload_start", role, shot_time, 0, side, before, native_ammunition::observe(ps)}, 2);
		}
		void sample_server()
		{
			const bool active = observing();
			if (!active) return;
			const bool gameplay = game::CL_IsCgameInitialized();
			{
				const std::lock_guard lock(mutex);
				if (active != recorded_enabled || gameplay != recorded_gameplay)
				{
					recorded_enabled = active;
					recorded_gameplay = gameplay;
					++revision;
				}
			}
			if (!active || !gameplay)
				return;
			const auto value = native_ammunition::observe(game::g_entities[0].client);
			const std::lock_guard lock(mutex);
			if (value != latest)
			{
				latest = value;
				++revision;
			}
		}
		std::pair<std::uint64_t, std::string> report()
		{
			std::array<record, 64> copy;
			decltype(counts) counters;
			decltype(latest) value;
			std::uint64_t count{}, version{};
			bool active{}, gameplay{};
			{
				const std::lock_guard lock(mutex);
				copy = history;
				counters = counts;
				value = latest;
				count = records;
				version = revision;
				active = recorded_enabled;
				gameplay = recorded_gameplay;
			}
			std::ostringstream out;
			out << "[VR reload boundary] observation_only=1 hooks=" << installed
				<< " enabled=" << active << " gameplay=" << gameplay << " records=" << count
				<< " state_valid=" << value.valid << " weapon=" << value.weapon << " loaded=" << value.loaded
				<< " reserve=" << value.reserve << '\n';
			for (int role = 0; role < 2; ++role)
				out << (role == 0 ? "server" : "prediction")
					<< " fire/consume/reload_start=" << counters[role][0] << '/' << counters[role][1] << '/'
					<< counters[role][2] << '\n';
			for (auto n = count > copy.size() ? count - copy.size() : 0; n < count; ++n)
			{
				const auto& r = copy[n % copy.size()];
				out << n << ' ' << r.kind << " role=" << r.role << " cmd=" << r.command_time
					<< " amount=" << r.amount << " side=" << r.side << " weapon=" << r.before.weapon
					<< " valid=" << r.before.valid << '/' << r.after.valid << " loaded=" << r.before.loaded
					<< "->" << r.after.loaded << " reserve=" << r.before.reserve << "->" << r.after.reserve
					<< " state=" << r.before.weapon_state << "->" << r.after.weapon_state
					<< " timer=" << r.before.weapon_time << "->" << r.after.weapon_time
					<< " caller=0x" << std::hex << r.caller << std::dec << " inside_fire=" << r.inside_fire << '\n';
			}
			return {version, out.str()};
		}
		template <size_t N> bool verify(std::uintptr_t address, const std::uint8_t (&bytes)[N])
		{
			std::array<std::uint8_t, N> mask{};
			mask.fill(0xff);
			return static_cast<bool>(utils::hook_validation::verify_masked_bytes(
				reinterpret_cast<void*>(address), {bytes, mask.data(), N}));
		}
	} // namespace
	class component final : public component_interface
	{
	  public:
		void post_unpack() override
		{
			command::add("vr_reload_status", [] {
				const auto text = report().second;
				console::info("%s", text.c_str());
				console::info("[VR reload] blocked begin server/prediction=%llu/%llu\n",
					static_cast<unsigned long long>(blocked_begins[0].load()),static_cast<unsigned long long>(blocked_begins[1].load()));
			});
			// Native wrappers verified by call sites and full prologues. No partial
			// instruction patches; detours retain trampolines and all original args.
			constexpr std::uint8_t fire[]{0x89, 0x54, 0x24, 0x10, 0x53, 0x55, 0x56, 0x41, 0x55,
										  0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x58};
			constexpr std::uint8_t consume[]{0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89,
											 0x74, 0x24, 0x18, 0x89, 0x54, 0x24, 0x10};
			constexpr std::uint8_t reload[]{0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c,
											0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20};
			constexpr std::uint8_t begin[]{0x48,0x89,0x5c,0x24,0x20,0x55,0x56,0x41,0x55,0x48,0x83,0xec,0x20};
			constexpr std::uint8_t automatic_begin[]{0xe8,0xc8,0xb8,0xff,0xff};
			constexpr std::uint8_t segmented_query[]{0xe8,0x38,0x40,0x01,0x00};
			if (!native_ammunition::initialize() || !verify(0x140699240, fire) ||
				!verify(0x14069E430, consume) || !verify(0x1406946D0, reload) ||
				!verify(0x140693D00,begin) || !verify(0x140698433,automatic_begin) || !verify(0x140693E53,segmented_query))
			{
				console::error("[VR reload] Native observation signatures rejected\n");
				return;
			}
			fire_hook.create(0x140699240, fire_stub);
			consume_hook.create(0x14069E430, consume_stub);
			reload_hook.create(0x1406946D0, reload_stub);
			begin_reload_hook.create(0x140693D00,begin_reload_stub);
			installed = true;
			manual_feed::set_boundary_ready(1);
			// All production hooks above remain installed with diagnostics off.
			if (!debug_options::enabled(debug_options::probe::weapon_events)) return;
			scheduler::loop(sample_server, scheduler::pipeline::server);
			// The launcher selection is frozen before the hook graph is installed.
			// All disk output is on the async worker, outside native/pose locks.
			scheduler::loop(
				[] {
					if (!observing())
						return;
					static std::uint64_t written = std::numeric_limits<std::uint64_t>::max();
					{
						const std::lock_guard lock(mutex);
						if (revision == written) return;
					}
					const auto [version, text] = report();
					if (utils::io::write_file_atomic("minidumps/overlord-reload-boundary.txt", text))
						written = version;
				},
				scheduler::pipeline::async, 1s);
		}
		void pre_destroy() override
		{
			alive.store(false, std::memory_order_relaxed);
		}
	};
} // namespace vr::gameplay::weapons::reload_boundary
REGISTER_COMPONENT(vr::gameplay::weapons::reload_boundary::component)
