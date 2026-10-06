#pragma once
#include "native_menu.hpp"
#include "native_hud_capture.hpp"

namespace vr::menu_backdrop
{
	struct snapshot
	{
		std::shared_ptr<const native_hud_capture::frame> canvas;
		menu_surface::geometry placement;
		std::uint64_t session{}, revision{}, reference{}, stamp{};
	};
	snapshot latest() noexcept;
	void publish(snapshot) noexcept;
	void publish_for(const native_menu::state&,
	                 const std::shared_ptr<const native_menu::images>&,
	                 const menu_surface::anchor&,
	                 std::uint64_t reference,
	                 std::uint64_t device,
	                 std::uint64_t now) noexcept;
}
