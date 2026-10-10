#pragma once
#include <cstdint>

namespace vr
{
	struct menu_preparation_status
	{
		// Static reason names: recording the frame requires no string allocation.
		const char* state{"not_prepared"};
		bool movie{};
		std::uint64_t tick{};
		std::uint32_t layers{}, candidates{}, missing{}, invalid{}, generation_mismatch{}, stale_cursor{}, owner_mismatch{};
	};
	struct frame_submission_status
	{
		// Process lifetime, counted only after a successful OpenXR xrEndFrame.
		std::uint64_t completed{}, world{}, menu_only{}, empty{}, last_tick{};
		std::uint32_t last_world_layers{}, last_menu_layers{};
		menu_preparation_status menu;
	};
}
