#pragma once
#include <cstdint>

namespace input
{
	// Native UI-owner delivery; these never move the Windows desktop cursor.
	bool vr_ui_key(int key,bool down);
	void vr_ui_pointer(int x,int y);
	void release_vr_ui_input();
	std::uint64_t physical_activity() noexcept;
	struct ui_activity {std::uint64_t mouse_polls{},mouse_moves{},mouse_suppressed{},key_events{};};
	ui_activity ui_activity_counters() noexcept;
}
