#pragma once
#include <array>
#include <cstdint>

namespace vr
{
	struct menu_layer_status
	{
		std::uint32_t slot{}, source_width{}, source_height{};
		std::uint64_t source_sequence{}, source_tick{}, flags{};
		bool cylinder{}, head_ray_hit{};
		float width{}, height{}, radius{}, distance{}, facing{};
		// Exact SDK layer pose, plus the surface's central point in the current
		// head frame (-Z forward). A ray hit is geometry evidence, not HMD output.
		std::array<float, 3> position{}, head_center{};
		std::array<float, 4> orientation{};
	};
	struct menu_preparation_status
	{
		// Static reason names: recording the frame requires no string allocation.
		const char* state{"not_prepared"};
		bool movie{};
		std::uint64_t tick{};
		std::uint32_t layers{}, candidates{}, missing{}, invalid{}, generation_mismatch{}, stale_cursor{}, owner_mismatch{};
		std::uint64_t session{}, revision{}, image_session{}, image_revision{}, device_generation{};
		std::array<menu_layer_status, 7> geometry{};
	};
	struct frame_submission_status
	{
		// Process lifetime, counted only after a successful OpenXR xrEndFrame.
		std::uint64_t completed{}, world{}, menu_only{}, empty{}, last_tick{};
		std::uint32_t last_world_layers{}, last_menu_layers{};
		menu_preparation_status menu;
	};
}
