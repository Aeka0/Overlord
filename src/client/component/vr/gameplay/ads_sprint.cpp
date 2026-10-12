#include <std_include.hpp>
#include "ads_sprint.hpp"
#include "ads_sprint_bridge.hpp"
#include "native_ammunition.hpp"
#include "../head_pose_bridge.hpp"
#include "../settings.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "game/game.hpp"
#include "game/dvars.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::gameplay::ads_sprint
{
	namespace
	{
		game::dvar_t* enabled{};
		std::atomic_bool installed{}, alive{true};
		std::array<std::atomic_uint64_t, 4> relaxed{};
		std::uint32_t test(const game::playerState_s* ps, std::uint32_t mask,
			std::uint32_t value, boundary point) noexcept
		{
			const auto filtered = filter_mask(mask, point, allowed(ps));
			if ((value & mask) != (value & filtered))
				relaxed[static_cast<std::size_t>(point)].fetch_add(1, std::memory_order_relaxed);
			return value & filtered;
		}
	}

	bool allowed(const game::playerState_s* ps) noexcept
	{
		return alive.load(std::memory_order_relaxed) && installed.load(std::memory_order_relaxed) &&
			enabled && enabled->current.enabled && game::CL_IsCgameInitialized() &&
			weapons::native_ammunition::local_role(ps) >= 0 && head_pose_bridge::get_status().enabled;
	}

	class component final : public component_interface
	{
		void post_unpack() override
		{
			enabled = dvars::register_bool(settings::ads_sprint.name, settings::ads_sprint.default_value,
				game::DVAR_FLAG_SAVED, "Allow native sprint while aiming down sights in VR");
			command::add("vr_ads_sprint_status", [] {
				console::info("[VR ADS sprint] installed=%d enabled=%d sustain_flags=%llu sustain_buttons=%llu "
					"start_buttons=%llu start_flags=%llu (relaxed checks, not runtime acceptance)\n",
					installed.load(), enabled && enabled->current.enabled,
					relaxed[0].load(), relaxed[1].load(), relaxed[2].load(), relaxed[3].load());
			});
			std::array<std::uint8_t, 10> exact{}; exact.fill(255);
			for (const auto& site : sites)
				if (!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(site.address),
					{site.bytes.data(), exact.data(), site.size}))
				{
					console::error("[VR ADS sprint] native predicate contract rejected; feature disabled\n");
					return;
				}
			std::array<void*, sites.size()> relays{};
			for (std::size_t i = 0; i < sites.size(); ++i)
			{
				const auto& site = sites[i];
				const auto bridge = utils::hook::assemble([&](auto& a) {
					emit_bridge(a, static_cast<boundary>(i), reinterpret_cast<std::uintptr_t>(test),
						site.resume, site.blocked, site.alternate);
				});
				relays[i] = utils::hook::create_preserving_near_jump(0x140000000, bridge);
				if (!relays[i] || utils::hook::is_relatively_far(reinterpret_cast<void*>(site.address), relays[i]))
				{
					console::error("[VR ADS sprint] preserving relay unavailable; feature disabled\n");
					return;
				}
			}
			for (std::size_t i = 0; i < sites.size(); ++i)
			{
				utils::hook::nop(sites[i].address, sites[i].size);
				utils::hook::jump(sites[i].address, relays[i]);
			}
			installed.store(true, std::memory_order_release);
		}
		void pre_destroy() override { alive = false; }
	};
}
REGISTER_COMPONENT(vr::gameplay::ads_sprint::component)
