#pragma once

#include "menu_surface.hpp"
#include "native_menu_commands.hpp"
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace vr::native_hud_capture {struct frame;}
namespace vr::native_menu
{
	struct entry { std::uint64_t id{}; std::uintptr_t element{}; bool modal{}; std::uint32_t generation{}; };
	struct state
	{
		std::uint64_t session{}, revision{}, timestamp{};
		bool enabled{}, frontend{}, video{}, blocked{};
		unsigned count{};
		std::array<entry,menu_surface::maximum_menus> menus{}; // oldest visible ancestor first
		std::array<std::uintptr_t,menu_surface::maximum_menus> backdrops{};
		std::array<std::array<std::uintptr_t,2>,menu_surface::maximum_menus> pause_backgrounds{};
		std::uintptr_t background{},cursor{};
		float scene_dim{}; // Pause-session dim; independent of the current menu page/tree.
		bool accept_screen{},button_blocked{},mouse_blocked{},briefing{};
		bool interactive() const noexcept { return enabled&&count&&!blocked; }
	};
	struct capture_plan
	{
		state owner{};
		std::uintptr_t arena{};
		std::vector<range> ranges;
	};
	struct images
	{
		state owner{};
		std::array<std::shared_ptr<const native_hud_capture::frame>,menu_surface::surface_count> layers;
	};
	state current() noexcept;
	struct presentation {bool enabled{},frontend{},scene{},video{},fullscreen_video{};};
	// Present-owner probe: does not depend on a running LUI VM/scheduler.
	presentation current_presentation() noexcept;
	void set_requested(bool value) noexcept;
	void reset_vm() noexcept;
	void invalidate_images() noexcept;
	void recenter() noexcept;
	void arena_reset(std::uintptr_t arena) noexcept;
	std::shared_ptr<const capture_plan> for_stream(std::uintptr_t begin) noexcept;
	void publish(images value) noexcept;
	std::shared_ptr<const images> latest() noexcept;
	struct pointer {std::uint64_t session{},target{},stamp{},overlay_errors{};float u{},v{};unsigned hand{};int overlay_error{};bool hit{},ready{};};
	void publish_pointer(pointer value) noexcept;
	void clear_pointer() noexcept;
}
