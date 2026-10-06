#include <std_include.hpp>
#include "../h2/entrypoints.hpp"
#include "native_closed_bolt_policy.hpp"
#include "../debug_options.hpp"
#include "native_ammunition.hpp"
#include "manual_feed_boundary.hpp"
#include "weapon_mechanics_profiles.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "game/dvars.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>
#include <utils/io.hpp>
#include <mutex>
#include <sstream>

namespace vr::gameplay::weapons::native_closed_bolt
{
	namespace
	{
		constexpr std::uintptr_t native_capacity = vr::h2::sp::clip_capacity.address();
		utils::hook::detour admission_hook, refill_hook;
		game::dvar_t* enabled{};
		bool installed{};
		std::atomic_bool alive{true};
		struct context
		{
			const void* ps{};
			boundary where{};
			int role{-1}, selected_capacity{};
			native_ammunition::reload_snapshot before{};
		};
		thread_local context* current{};
		struct observation
		{
			std::uint64_t calls{}, plus_one{}, refills{}, mismatches{};
			native_ammunition::snapshot before{}, after{};
			int animation{}, limit{};
		};
		std::mutex report_mutex;
		std::array<observation, 2> observations{};
		native_ammunition::reload_snapshot latest{};
		bool active() noexcept
		{
			return alive.load(std::memory_order_relaxed) && installed && enabled && enabled->current.enabled;
		}
		context enter(const void* ps, int side, boundary where)
		{
			context c;
			c.ps = ps; c.where = where;
			if (!active() || side != 0) return c;
			c.role = native_ammunition::local_role(ps);
			if (c.role >= 0) c.before = native_ammunition::observe_reload(ps);
			return c;
		}
		int capacity_stub(const void* ps, std::uint32_t weapon, bool alternate)
		{
			// Only these two reload-local call sites are patched. WeaponDef, HUD
			// capacity queries, pickups, scripts and global capacity are untouched.
			const int original = utils::hook::invoke<int>(native_capacity, ps, weapon, alternate);
			auto* c = current;
			if (!c || c->ps != ps || c->role < 0 || !c->before.valid || alternate ||
				c->before.ammo.weapon != weapon) return original;
			const auto* profile = native_chamber_profile(c->before.native_name.data(), original);
			if (!profile || c->before.base_capacity != original) return original;
			const auto kind = c->before.animation == 20 ? reload_kind::tactical :
				c->before.animation == 21 ? reload_kind::empty : reload_kind::unknown;
			const auto result = capacity(*profile, original, c->before.ammo.loaded, c->where, kind);
			c->selected_capacity = result;
			if (debug_options::enabled(debug_options::probe::weapon_events))
			{
				const std::lock_guard lock(report_mutex);
				auto& o = observations[c->role];
				++o.calls;
				if (result > original) ++o.plus_one;
			}
			return result;
		}
		int admission_stub(void* ps, int side)
		{
			if (manual_feed::blocks_reload(ps, side)) return 0;
			auto c = enter(ps, side, boundary::admission);
			const auto previous = current;
			current = &c;
			const auto restore = gsl::finally([&] { current = previous; });
			return admission_hook.invoke<int>(ps, side);
		}
		void refill_stub(void* ps, int side)
		{
			if (manual_feed::blocks_reload(ps, side)) return;
			auto c = enter(ps, side, boundary::refill);
			const auto previous = current;
			current = &c;
			const auto restore = gsl::finally([&] { current = previous; });
			refill_hook.invoke<void>(ps, side); // Native alone moves reserve -> loaded.
			if (!debug_options::enabled(debug_options::probe::weapon_events) || !c.selected_capacity || c.role < 0) return;
			const auto after = native_ammunition::observe(ps);
			const auto& before = c.before.ammo;
			const auto moved = std::min(std::max(0, c.selected_capacity - before.loaded), before.reserve);
			const bool exact = after.valid && after.weapon == before.weapon &&
				after.loaded == before.loaded + moved && after.reserve == before.reserve - moved;
			const std::lock_guard lock(report_mutex);
			auto& o = observations[c.role];
			++o.refills;
			if (!exact) ++o.mismatches;
			o.before = before; o.after = after; o.animation = c.before.animation; o.limit = c.selected_capacity;
		}
		std::string report()
		{
			decltype(observations) copy;
			decltype(latest) value;
			{
				const std::lock_guard lock(report_mutex);
				copy = observations; value = latest;
			}
			closed_bolt::state feed{};
			const auto* profile = value.valid ? native_chamber_profile(value.native_name.data(), value.base_capacity) : nullptr;
			const bool partition = profile && closed_bolt::from_native_automatic(*profile, value.ammo.loaded, feed);
			std::ostringstream out;
			out << "[VR chamber] installed=" << installed << " enabled=" << active()
				<< " diagnostics=" << debug_options::enabled(debug_options::probe::weapon_events)
				<< " mode=native_auto valid=" << value.valid << " partition_valid=" << partition
				<< " weapon=" << value.ammo.weapon << " asset=" << (value.valid ? value.native_name.data() : "unavailable")
				<< " loaded=" << value.ammo.loaded << " reserve=" << value.ammo.reserve
				<< " magazine=" << (partition ? feed.magazine_rounds : -1)
				<< " chamber=" << (partition ? int(feed.chamber_loaded) : -1)
				<< " state=" << value.ammo.weapon_state << " anim=" << value.animation << '\n';
			for (unsigned role = 0; role < copy.size(); ++role)
			{
				const auto& o = copy[role];
				out << (role ? "prediction" : "server") << " capacity/plus_one/refill/mismatch="
					<< o.calls << '/' << o.plus_one << '/' << o.refills << '/' << o.mismatches
					<< " last=" << o.before.loaded << '/' << o.before.reserve << "->"
					<< o.after.loaded << '/' << o.after.reserve << " limit=" << o.limit << " anim=" << o.animation << '\n';
			}
			return out.str();
		}
		template<std::size_t N> bool verify(std::uintptr_t address, const std::uint8_t (&bytes)[N])
		{
			std::array<std::uint8_t, N> mask{}; mask.fill(0xff);
			return bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(address), {bytes, mask.data(), N}));
		}
	}
	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			enabled = dvars::register_bool("vr_closedBoltChamber", true, game::DVAR_FLAG_SAVED,
				"Profiled closed-bolt +1 with native reload; does not enable physical reload");
			command::add("vr_chamber_status", [] {
				const auto text = report();
				console::info("%s", text.c_str());
				scheduler::once([text] { utils::io::write_file_atomic("minidumps/overlord-chamber.txt", text); },
					scheduler::pipeline::async);
			});
			constexpr std::uint8_t admission[]{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x74,0x24,0x20};
			constexpr std::uint8_t refill[]{0x89,0x54,0x24,0x10,0x53,0x55,0x56,0x57,0x41,0x55,0x41,0x56,0x48,0x83,0xec,0x28};
			constexpr std::uint8_t capacity_entry[]{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x18,0x89,0x54,0x24,0x10};
			constexpr std::uint8_t admission_call[]{0xe8,0xba,0xd4,0x00,0x00};
			constexpr std::uint8_t refill_call[]{0xe8,0x17,0x58,0x00,0x00};
			constexpr std::uint8_t tactical[]{0x83,0xc8,0x14};
			constexpr std::uint8_t empty[]{0x83,0xc8,0x15};
			if (!native_ammunition::initialize() || !native_ammunition::reload_layout_ready() ||
				!verify(0x1406964D0, admission) ||
				!verify(0x14069E1B0, refill) || !verify(native_capacity, capacity_entry) ||
				!verify(0x1406965A1, admission_call) || !verify(0x14069E244, refill_call) ||
				!verify(0x140694856, tactical) || !verify(0x1406947C3, empty))
			{
				console::error("[VR chamber] native contract rejected; original reload retained\n");
				return;
			}
			admission_hook.create(0x1406964D0, admission_stub);
			refill_hook.create(0x14069E1B0, refill_stub);
			utils::hook::call(0x1406965A1, capacity_stub);
			utils::hook::call(0x14069E244, capacity_stub);
			installed = true;
			manual_feed::set_boundary_ready(2);
			if (!debug_options::enabled(debug_options::probe::weapon_events)) return;
			scheduler::loop([] {
				if (!alive.load(std::memory_order_relaxed)) return;
				const auto value = game::CL_IsCgameInitialized() ?
					native_ammunition::observe_reload(game::g_entities[0].client) : native_ammunition::reload_snapshot{};
				const std::lock_guard lock(report_mutex);
				latest = value;
			}, scheduler::pipeline::server, 250ms);
		}
		void pre_destroy() override { alive.store(false, std::memory_order_relaxed); }
	};
}
REGISTER_COMPONENT(vr::gameplay::weapons::native_closed_bolt::component)
