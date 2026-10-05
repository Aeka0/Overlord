// Standalone runtime tests have no HKS VM or native command allocator. Keep
// their native-menu boundary explicit while running the real overlay presenter.
#include <std_include.hpp>
#include "component/vr/native_menu.hpp"
namespace vr::tests
{
	std::optional<native_menu::presentation> presentation_override;
	native_menu::state menu_state;
	std::shared_ptr<const native_menu::images> menu_images;
	native_menu::pointer menu_pointer;
}
namespace vr::native_menu
{
	state current() noexcept {return tests::menu_state;}
	presentation current_presentation() noexcept
	{return tests::presentation_override.value_or(presentation{tests::menu_state.enabled,tests::menu_state.frontend,!tests::menu_state.frontend&&!tests::menu_state.video,tests::menu_state.video});}
	void set_requested(bool) noexcept {}
	void invalidate_images() noexcept {}
	std::shared_ptr<const images> latest() noexcept {return tests::menu_images;}
	void publish_pointer(pointer p) noexcept {tests::menu_pointer=p;}
	void clear_pointer() noexcept {tests::menu_pointer={};}
}
