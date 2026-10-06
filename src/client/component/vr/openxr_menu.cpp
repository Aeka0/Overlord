#include <std_include.hpp>
#include "openxr_menu.hpp"
#include "spatial_math.hpp"
#include "movie_presentation.hpp"
#include "menu_backdrop.hpp"

#if H2V_OPENXR_HEADERS_AVAILABLE
namespace vr::openxr
{
	void menu_layers::observe_backdrop(const head_pose_bridge::tracking_pose& head,
	                                   std::uint64_t device) noexcept
	{
		auto current = native_menu::current();
		const auto input = controller_input::latest();
		const auto mode = native_menu::current_presentation();
		const auto now = GetTickCount64();
		const bool fresh = current.enabled && now >= current.timestamp && now - current.timestamp <= 250;
		if (menu_surface::movie_theater(mode.enabled,
		                                mode.video,
		                                mode.frontend,
		                                mode.scene,
		                                fresh,
		                                current.count,
		                                current.briefing,
		                                mode.fullscreen_video))
			current.session = 1ull << 63;
		if (session_ != current.session || reference_ != input.reference_generation ||
		    generation_ != device || !anchor_.valid)
		{
			anchor_ = menu_surface::anchored(head);
			session_ = current.session;
			reference_ = input.reference_generation;
			generation_ = device;
			pointer_owner_.reset();
		}
		menu_backdrop::publish_for(
		    current, native_menu::latest(), anchor_, input.reference_generation, device, GetTickCount64());
	}
	bool menu_layers::copy_movie(const d3d11::device_snapshot& graphics,
	                             IDXGISwapChain* chain,
	                             std::string& error)
	{
		if (!chain)
		{
			error = "native fullscreen video has no swap chain";
			return false;
		}
		Microsoft::WRL::ComPtr<ID3D11Texture2D> source;
		if (FAILED(chain->GetBuffer(0, IID_PPV_ARGS(&source))))
		{
			error = "native fullscreen video buffer unavailable";
			return false;
		}
		D3D11_TEXTURE2D_DESC description{};
		source->GetDesc(&description);
		if (!description.Width || !description.Height || description.Width > 8192 ||
		    description.Height > 8192 || description.MipLevels != 1 || description.ArraySize != 1 ||
		    description.SampleDesc.Count != 1)
		{
			error = "native video descriptor is unsupported";
			return false;
		}
		for (auto& slot : movie_pool_)
		{
			if (slot && slot.use_count() != 1)
				continue;
			if (!slot)
				slot = std::make_shared<native_hud_capture::frame>();
			D3D11_TEXTURE2D_DESC old{};
			if (slot->texture)
				slot->texture->GetDesc(&old);
			if (slot->generation != graphics.generation || old.Width != description.Width ||
			    old.Height != description.Height || old.Format != description.Format)
			{
				*slot = {};
				description.Usage = D3D11_USAGE_DEFAULT;
				description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
				description.CPUAccessFlags = description.MiscFlags = 0;
				if (FAILED(graphics.device->CreateTexture2D(&description, nullptr, &slot->texture)))
				{
					error = "native video copy texture creation failed";
					return false;
				}
			}
			{
				const auto queue = d3d11::acquire_gpu_queue_interop();
				graphics.context->CopyResource(slot->texture.Get(), source.Get());
			}
			slot->generation = graphics.generation;
			slot->width = description.Width;
			slot->height = description.Height;
			slot->timestamp = GetTickCount64();
			slot->sequence = ++movie_sequence_;
			movie_frame_ = slot;
			return true;
		}
		error = "native video capture slots are still leased";
		return false;
	}
	menu_layers::preparation_result menu_layers::prepare(const dispatch_table& xr,
	                                                     const presentation_input& presentation)
	{
		const auto& [session, space, graphics, chain, head, curved, menu_format] = presentation;
		XrResult result{XR_SUCCESS};
		std::string error;
		const char* operation = "native menu validation";
		const auto failure = [&]() -> preparation_result { return {{result, operation}, std::move(error)}; };
		count_ = 0;
		const auto now = GetTickCount64();
		auto current = native_menu::current();
		auto images = native_menu::latest();
		const auto mode = native_menu::current_presentation();
		const bool fresh = current.enabled && now >= current.timestamp && now - current.timestamp <= 250;
		const bool movie = menu_surface::movie_theater(mode.enabled,
		                                               mode.video,
		                                               mode.frontend,
		                                               mode.scene,
		                                               fresh,
		                                               current.count,
		                                               current.briefing,
		                                               mode.fullscreen_video);
		if (movie_ != movie)
		{
			movie_ = movie;
			anchor_ = {};
			movie_frame_.reset();
		}
		if (movie)
		{
			if (!copy_movie(graphics, chain, error))
			{
				result = XR_ERROR_RUNTIME_FAILURE;
				operation = "native fullscreen video copy";
				return failure();
			}
			current = {};
			current.enabled = current.frontend = current.blocked = true;
			current.session = 1ull << 63;
			current.revision = movie_sequence_;
			current.timestamp = now;
			auto publication = std::make_shared<native_menu::images>();
			publication->owner = current;
			publication->layers[menu_surface::background_slot] = movie_frame_;
			images = publication;
		}
		if (!current.enabled || current.count > menu_surface::maximum_menus || now < current.timestamp ||
		    now - current.timestamp > 250 || (!current.frontend && !current.count) || !images ||
		    images->owner.session != current.session)
		{
			pointer_owner_.cancel_input();
			native_menu::clear_pointer();
			return {};
		}
		const auto input = controller_input::latest();
		if (session_ != current.session || reference_ != input.reference_generation ||
		    generation_ != graphics.generation || !anchor_.valid)
		{
			anchor_ = menu_surface::anchored(head);
			session_ = current.session;
			reference_ = input.reference_generation;
			generation_ = graphics.generation;
			pointer_owner_.reset();
		}
		if (!anchor_.valid)
		{
			native_menu::clear_pointer();
			return {};
		}
		const float elapsed = last_time_ && now >= last_time_ ? float(now - last_time_) * .001f : 0;
		last_time_ = now;
		const auto prior = surfaces_;
		const unsigned active = current.count ? current.count - 1 : menu_surface::surface_count;
		menu_surface::geometry active_geometry;
		bool active_ready = false;
		// SDK layer order is back-to-front, independent of texture slot order.
		std::array<unsigned, menu_surface::surface_count> order{};
		unsigned write = 0;
		order[write++] = menu_surface::background_slot;
		for (unsigned index = 0; index < menu_surface::maximum_menus; ++index)
		{
			if (index == active)
				order[write++] = menu_surface::backdrop_slot;
			order[write++] = index;
		}
		if (active >= menu_surface::maximum_menus)
			order[write++] = menu_surface::backdrop_slot;
		order[write] = menu_surface::cursor_slot;
		for (const auto index : order)
		{
			const auto& image = images->layers[index];
			auto& target = surfaces_[index];
			const bool menu = index < current.count,
			           background = index == menu_surface::background_slot && current.frontend;
			const bool backdrop = index == menu_surface::backdrop_slot && current.count,
			           cursor = index == menu_surface::cursor_slot && current.interactive();
			const bool latest = image && images->owner.revision == current.revision &&
			                    now >= image->timestamp && now - image->timestamp <= 250;
			if ((!menu && !background && !backdrop && !cursor) || !image || !image->texture ||
			    image->generation != graphics.generation || !image->width || !image->height ||
			    (cursor && !latest))
				continue;
			if (menu && images->owner.menus[index].id != current.menus[index].id)
				continue;
			auto geometry = menu_surface::layout(anchor_,
			                                     current.frontend,
			                                     float(image->width) / image->height,
			                                     menu ? current.count - index - 1 : 0);
			float depth = background ? menu_surface::theater_depth +
			                               menu_surface::maximum_menus * menu_surface::layer_gap
			                         : geometry.distance;
			if (background)
			{
				geometry.radius = depth;
				geometry.width = depth * menu_surface::pi * .5f;
				geometry.height = geometry.width * image->height / image->width;
			}
			if (backdrop)
				depth += menu_surface::dark_layer_gap;
			const auto id = menu ? current.menus[index].id : std::uint64_t(index + 1);
			if (target.id != id)
			{
				target.depth = depth;
				for (const auto& previous : prior)
					if (previous.id == id)
						target.depth = previous.depth;
				target.id = id;
			}
			target.depth = backdrop ? depth : menu_surface::approach_depth(target.depth, depth, elapsed);
			geometry.distance = target.depth;
			if (!curved)
				geometry.radius = 0;
			const DXGI_FORMAT format = menu_format;
			if (target.swapchain.width != image->width || target.swapchain.height != image->height ||
			    target.format != format)
			{
				operation = "xrDestroySwapchain";
				if (target.swapchain.handle != XR_NULL_HANDLE &&
				    !destroy_eye_swapchain(xr, target.swapchain, result))
					return failure();
				XrViewConfigurationView description{XR_TYPE_VIEW_CONFIGURATION_VIEW};
				description.recommendedImageRectWidth = image->width;
				description.recommendedImageRectHeight = image->height;
				description.recommendedSwapchainSampleCount = 1;
				operation = "OpenXR menu swapchain creation";
				if (!create_eye_swapchain(xr,
				                          session,
				                          graphics.device.Get(),
				                          format,
				                          description,
				                          target.swapchain,
				                          error,
				                          result))
					return failure();
				target.format = format;
			}
			auto& swap = target.swapchain;
			const XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
			operation = "xrAcquireSwapchainImage";
			result = xr.acquire_swapchain_image(swap.handle, &acquire, &swap.acquired_index);
			if (XR_FAILED(result))
				return failure();
			swap.acquired = true;
			const XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, nullptr, 10'000'000};
			operation = "xrWaitSwapchainImage";
			result = xr.wait_swapchain_image(swap.handle, &wait);
			if (XR_FAILED(result) || result == XR_TIMEOUT_EXPIRED)
				return failure();
			swap.waited = true;
			if (swap.acquired_index >= swap.render_targets.size())
			{
				result = XR_ERROR_RUNTIME_FAILURE;
				return failure();
			}
			operation = "OpenXR menu texture transfer";
			const texture_blit::renderer::draw_request transfer{
			    .graphics = graphics,
			    .source = image->texture.Get(),
			    .destination = swap.render_targets[swap.acquired_index].Get(),
			    .width = image->width,
			    .height = image->height,
			    .source_encoding = movie && background ? texture_blit::encoding::encoded_opaque
			                                           : texture_blit::encoding::encoded_premultiplied};
			if (!blit_.draw(transfer, error))
			{
				result = XR_ERROR_RUNTIME_FAILURE;
				return failure();
			}
			{
				const auto queue = d3d11::acquire_gpu_queue_interop();
				graphics.context->Flush();
			}
			operation = "xrReleaseSwapchainImage";
			if (!release_acquired_image(xr, swap, result))
				return failure();
			const auto rotation =
			    spatial_math::from_axis({anchor_.right, anchor_.up, menu_surface::mul(anchor_.forward, -1)});
			const auto center = menu_surface::add(
			    anchor_.position, menu_surface::mul(anchor_.forward, geometry.distance - geometry.radius));
			XrPosef pose{{rotation[0], rotation[1], rotation[2], rotation[3]},
			             {center[0], center[1], center[2]}};
			const XrSwapchainSubImage sub{swap.handle, {{0, 0}, {int(swap.width), int(swap.height)}}, 0};
			const auto flags = movie && background ? XrCompositionLayerFlags{0}
			                                       : XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
			if (geometry.radius)
			{
				auto& layer = cylinders_[index];
				layer = {XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR,
				         nullptr,
				         flags,
				         space,
				         XR_EYE_VISIBILITY_BOTH,
				         sub,
				         pose,
				         geometry.radius,
				         geometry.width / geometry.radius,
				         geometry.width / geometry.height};
				layers_[count_++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer);
			}
			else
			{
				auto& layer = quads_[index];
				layer = {XR_TYPE_COMPOSITION_LAYER_QUAD,
				         nullptr,
				         flags,
				         space,
				         XR_EYE_VISIBILITY_BOTH,
				         sub,
				         pose,
				         {geometry.width, geometry.height}};
				layers_[count_++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer);
			}
			if (index == active)
			{
				active_geometry = geometry;
				active_ready = latest && std::abs(target.depth - depth) < .001f;
			}
		}
		native_menu::pointer pointer;
		pointer.session = current.session;
		pointer.target = current.revision;
		pointer.stamp = now;
		pointer.ready = active_ready && current.interactive() && input.focused;
		pointer_owner_.update(input, pointer.ready, controller_input::clock::now());
		pointer.hand = pointer_owner_.hand();
		if (pointer.ready && input.runtime_aim[pointer.hand].valid)
		{
			const auto& aim = input.runtime_aim[pointer.hand].tracking;
			const auto hit = menu_surface::intersect(
			    active_geometry,
			    aim.position_meters,
			    {-aim.orientation[0][2], -aim.orientation[1][2], -aim.orientation[2][2]});
			pointer.hit = hit.valid;
			pointer.u = hit.u;
			pointer.v = hit.v;
			if (pointer.hit && images->layers[active]->canvas.valid())
				pointer.hit = images->layers[active]->canvas.source_uv(pointer.u, pointer.v);
		}
		native_menu::publish_pointer(pointer);
		return {};
	}
	call_result menu_layers::destroy(const dispatch_table& xr) noexcept
	{
		XrResult result{XR_SUCCESS};
		count_ = 0;
		native_menu::clear_pointer();
		menu_backdrop::publish({});
		for (auto& target : surfaces_)
		{
			auto& eye = target.swapchain;
			if (eye.acquired)
			{
				if (!eye.waited)
				{
					const XrSwapchainImageWaitInfo wait{
					    XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, nullptr, 10'000'000};
					result = xr.wait_swapchain_image(eye.handle, &wait);
					if (XR_FAILED(result) || result == XR_TIMEOUT_EXPIRED)
						return {result, "xrWaitSwapchainImage"};
					eye.waited = true;
				}
				if (!release_acquired_image(xr, eye, result))
					return {result, "xrReleaseSwapchainImage"};
			}
			if (eye.handle != XR_NULL_HANDLE && !destroy_eye_swapchain(xr, eye, result))
				return {result, "xrDestroySwapchain"};
			target = {};
		}
		blit_.reset();
		anchor_ = {};
		pointer_owner_.reset();
		movie_pool_ = {};
		movie_frame_.reset();
		movie_ = false;
		session_ = reference_ = generation_ = last_time_ = movie_sequence_ = 0;
		return {};
	}
}
#endif
