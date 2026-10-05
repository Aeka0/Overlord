#pragma once
#include "component/vr/native_menu.hpp"
#include <optional>
namespace vr::tests
{
	extern std::optional<native_menu::presentation> presentation_override;
	extern native_menu::state menu_state;
	extern std::shared_ptr<const native_menu::images> menu_images;
	extern native_menu::pointer menu_pointer;
}
