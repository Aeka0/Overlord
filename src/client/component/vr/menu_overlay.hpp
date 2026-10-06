#pragma once
#include "native_menu.hpp"
#include "menu_backdrop.hpp"
#include <openvr.h>
#include <d3d11.h>

namespace vr::menu_overlay
{
	using backdrop = menu_backdrop::snapshot;
	// Immutable texture lease and placement only; the eye renderer never calls
	// OpenVR or reaches into the present owner's mutable overlay instance.
	backdrop latest_backdrop() noexcept;
	// Called only by the existing OpenVR Present owner. Texture leases stay
	// attached until replacement or ClearOverlayTexture acknowledges release.
	class presenter
	{
		struct surface
		{
			::vr::VROverlayHandle_t handle{};
			std::uint64_t id{},sequence{};
			float depth{};
			float width{},curvature{-1};
			std::uint32_t order{UINT32_MAX};
			::vr::HmdMatrix34_t transform{};
			bool shown{};
			bool opaque{};
			std::shared_ptr<const native_hud_capture::frame> lease;
			std::shared_ptr<const native_hud_capture::frame> failed_lease;
		};
		::vr::IVROverlay* api_{};
		std::array<surface,menu_surface::surface_count> surfaces_;
		menu_surface::anchor anchor_;
		std::uint64_t session_{},reference_{},last_time_{},device_{};
		menu_surface::pointer_owner pointer_owner_;
		std::uint64_t errors_{};
		int last_error_{};
		bool movie_active_{};
		std::uint64_t movie_session_{1ull<<63},movie_sequence_{};
		std::array<std::shared_ptr<native_hud_capture::frame>,3> movie_slots_;
		std::shared_ptr<const native_hud_capture::frame> movie_frame_;
		bool release(surface&) noexcept;
	public:
		void initialize(::vr::IVROverlay*) noexcept;
		void update(const head_pose_bridge::tracking_pose&,std::uint64_t device,bool render_available,std::uint64_t timestamp=0) noexcept;
		// Only for positively identified native 2D movie/loading presentation.
		// Caller drops the temporary swap-chain buffer after this GPU copy.
		bool capture_movie(ID3D11DeviceContext*,ID3D11Texture2D*,std::uint64_t device,bool active) noexcept;
		void shutdown() noexcept;
		// Only after VR_Shutdown: failed SDK cleanup no longer owns any source.
		void runtime_detached() noexcept;
		std::uint64_t errors() const noexcept {return errors_;}
	};
}
