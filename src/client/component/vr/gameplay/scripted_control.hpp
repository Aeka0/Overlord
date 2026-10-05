#pragma once
#include <cstdint>

namespace vr::gameplay::scripted_control
{
	// This is native weapon permission, not a test for arbitrary animation,
	// player linking, or vehicle occupancy. Those can allow scripted combat.
	inline constexpr std::uint32_t weapons_disabled = 0x80;
	inline bool permits_weapons(std::uint32_t flags) noexcept { return !(flags & weapons_disabled); }

	// Freeze presentation selection while scripts own the player. Inventory
	// reconciliation continues separately, so removed weapons cannot reappear.
	class selection_pause
	{
	public:
		void reset() noexcept { *this = {}; }
		bool suspended() const noexcept { return suspended_; }
		void suspend(std::uint32_t selected) noexcept
		{
			if (!suspended_) before_ = selected;
			suspended_ = true;
		}
		// A changed final native selection is authoritative, including empty
		// hands. Never force a cached weapon over a script's final loadout.
		bool resume(std::uint32_t selected) noexcept
		{
			const bool retain = suspended_ && selected == before_;
			reset();
			return retain;
		}
	private:
		bool suspended_{};
		std::uint32_t before_{};
	};
}
