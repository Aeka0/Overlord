#pragma once

#include <cstdint>
#include <memory>

#include "openxr_dispatch.hpp"
#include "vr_runtime.hpp"
#include "openxr_startup.hpp"

namespace vr::openxr
{
	using startup_query = startup_configuration (*)();
	class runtime_backend final
	{
	  public:
		explicit runtime_backend(startup_query query = nullptr);
		~runtime_backend();

		void set_desired_enabled(bool enabled);
		void set_scene_mode(scene_mode mode);
		void request_reinitialize();
		void prepare_frame(const d3d11::device_snapshot& graphics, std::uint64_t frame_index);
		[[nodiscard]] bool initialize(const d3d11::device_snapshot& graphics);
		void on_present(const d3d11::device_snapshot& graphics, std::uint64_t frame_index);
		void on_present(const d3d11::present_event& event);
		void on_present_post(const d3d11::present_event& event, HRESULT result);
		void capture_present(const d3d11::present_event& event);
		[[nodiscard]] bool capture_engine_texture(const d3d11::device_snapshot& graphics,
		                                          ID3D11Texture2D* source,
		                                          capture_frame_tag tag);
		void poll_capture(const d3d11::device_snapshot& graphics);
		void on_resize_before(const d3d11::resize_event& event) noexcept;
		void on_device_destroying(const d3d11::device_snapshot& graphics) noexcept;
		void shutdown() noexcept;
		[[nodiscard]] bool shutdown_complete() const noexcept;

		[[nodiscard]] bool requested_enabled() const;
		[[nodiscard]] bool applied_enabled() const;
		[[nodiscard]] runtime_status get_status() const;

	  private:
		class implementation;
		std::unique_ptr<implementation> implementation_;
	};
}
