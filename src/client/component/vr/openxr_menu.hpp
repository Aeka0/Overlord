#pragma once
#include "openxr_d3d11.hpp"
#include "native_menu.hpp"
#include "native_hud_capture.hpp"
#include "texture_blit.hpp"

#if H2V_OPENXR_HEADERS_AVAILABLE
namespace vr::openxr
{
	class menu_layers
	{
		struct surface
		{
			eye_swapchain swapchain;
			DXGI_FORMAT format{DXGI_FORMAT_UNKNOWN};
			std::uint64_t id{};
			float depth{};
		};
		std::array<surface, menu_surface::surface_count> surfaces_;
		std::array<XrCompositionLayerQuad, menu_surface::surface_count> quads_;
		std::array<XrCompositionLayerCylinderKHR, menu_surface::surface_count> cylinders_;
		std::array<const XrCompositionLayerBaseHeader*, menu_surface::surface_count> layers_{};
		std::array<std::shared_ptr<native_hud_capture::frame>, 3> movie_pool_;
		std::shared_ptr<const native_hud_capture::frame> movie_frame_;
		menu_surface::anchor anchor_;
		menu_surface::pointer_owner pointer_owner_;
		texture_blit::renderer blit_;
		std::uint64_t session_{}, reference_{}, generation_{}, last_time_{}, movie_sequence_{};
		bool movie_{};
		unsigned count_{};
		bool copy_movie(const d3d11::device_snapshot&, IDXGISwapChain*, std::string&);

	  public:
		struct presentation_input
		{
			XrSession session;
			XrSpace space;
			const d3d11::device_snapshot& graphics;
			IDXGISwapChain* swapchain;
			const head_pose_bridge::tracking_pose& head;
			bool cylinder_supported;
			DXGI_FORMAT format;
		};
		struct preparation_result
		{
			call_result call;
			std::string detail;
			explicit operator bool() const noexcept
			{
				return bool(call) && call.code != XR_TIMEOUT_EXPIRED;
			}
		};
		preparation_result prepare(const dispatch_table&, const presentation_input&);
		void observe_backdrop(const head_pose_bridge::tracking_pose&, std::uint64_t generation) noexcept;
		std::span<const XrCompositionLayerBaseHeader* const> layers() const noexcept
		{
			return {layers_.data(), count_};
		}
		call_result destroy(const dispatch_table&) noexcept;
	};
}
#endif
