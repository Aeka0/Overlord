#include <std_include.hpp>
#include "menu_backdrop.hpp"

namespace vr::menu_backdrop
{
	namespace
	{
		std::mutex mutex;
		snapshot current;
	}
	snapshot latest() noexcept
	{
		const std::lock_guard lock(mutex);
		return current;
	}
	void publish(snapshot value) noexcept
	{
		const std::lock_guard lock(mutex);
		current = std::move(value);
	}
	void publish_for(const native_menu::state& state,
	                 const std::shared_ptr<const native_menu::images>& image,
	                 const menu_surface::anchor& anchor,
	                 std::uint64_t reference,
	                 std::uint64_t device,
	                 std::uint64_t now) noexcept
	{
		snapshot result;
		const auto canvas = image && state.count && state.count <= menu_surface::maximum_menus
		                        ? image->layers[state.count - 1]
		                        : nullptr;
		if (!state.frontend && state.count && canvas && image->owner.session == state.session &&
		    image->owner.revision == state.revision && canvas->generation == device && canvas->width &&
		    canvas->height && now >= canvas->timestamp && now - canvas->timestamp <= 250)
		{
			auto placement = menu_surface::layout(anchor, false, float(canvas->width) / canvas->height, 0);
			if (const auto& map = canvas->canvas; map.valid())
			{
				const float x = (map.x + map.width * .5f) / map.target_width - .5f;
				const float y = .5f - (map.y + map.height * .5f) / map.target_height;
				placement.basis.position =
				    menu_surface::add(placement.basis.position,
				                      menu_surface::add(menu_surface::mul(anchor.right, x * placement.width),
				                                        menu_surface::mul(anchor.up, y * placement.height)));
				placement.width *= map.width / map.target_width;
				placement.height *= map.height / map.target_height;
			}
			result = {canvas, placement, state.session, state.revision, reference, now};
		}
		publish(std::move(result));
	}
}
