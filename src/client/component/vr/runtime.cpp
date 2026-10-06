#include <std_include.hpp>
#include "vr_runtime.hpp"
#include "runtime_backend.hpp"
#include <atomic>

namespace vr
{
	const char* to_string(const runtime_state state) noexcept
	{
		switch (state)
		{
		case runtime_state::disabled:
			return "disabled";
		case runtime_state::waiting_for_graphics:
			return "waiting_for_graphics";
		case runtime_state::sdk_headers_unavailable:
			return "sdk_headers_unavailable";
		case runtime_state::loader_missing:
			return "loader_missing";
		case runtime_state::runtime_unavailable:
			return "runtime_unavailable";
		case runtime_state::no_hmd:
			return "no_hmd";
		case runtime_state::graphics_mismatch:
			return "graphics_mismatch";
		case runtime_state::session_idle:
			return "session_idle";
		case runtime_state::running:
			return "running";
		case runtime_state::recoverable_error:
			return "recoverable_error";
		case runtime_state::fatal_for_vr:
			return "fatal_for_vr";
		}
		return "unknown";
	}
}

namespace vr
{
	// Both SDK adapters execute on the real DXGI Present owner. The host forwards
	// lifecycle/settings only; it never creates a competing runtime worker.
	class runtime::implementation final
	{
	  public:
		~implementation()
		{
			backend.shutdown();
		}
		runtime_backend backend;
		std::atomic_bool shutting_down{};
	};

	runtime& runtime::get()
	{
		static runtime value;
		return value;
	}
	runtime::runtime() : implementation_(std::make_unique<implementation>())
	{
	}
	runtime::~runtime() = default;
	void runtime::set_desired_enabled(const bool enabled)
	{
		implementation_->backend.set_desired_enabled(enabled);
	}
	void runtime::set_scene_mode(const scene_mode mode)
	{
		implementation_->backend.set_scene_mode(mode);
	}
	void runtime::request_reinitialize()
	{
		implementation_->backend.request_reinitialize();
	}
	void runtime::prepare_frame(const d3d11::device_snapshot& graphics, const std::uint64_t frame_index)
	{
		implementation_->backend.prepare_frame(graphics, frame_index);
	}
	bool runtime::initialize(const d3d11::device_snapshot& graphics)
	{
		return implementation_->backend.initialize(graphics);
	}
	void runtime::on_present(const d3d11::device_snapshot& graphics, const std::uint64_t frame)
	{
		implementation_->backend.on_present(graphics, frame);
	}
	void runtime::on_present(const d3d11::present_event& event)
	{
		implementation_->backend.on_present(event);
	}
	void runtime::on_present_post(const d3d11::present_event& event, const HRESULT result)
	{
		implementation_->backend.on_present_post(event, result);
	}
	void runtime::capture_present(const d3d11::present_event& event)
	{
		implementation_->backend.capture_present(event);
	}
	bool runtime::capture_engine_texture(const d3d11::device_snapshot& graphics,
	                                     ID3D11Texture2D* const source,
	                                     const capture_frame_tag tag)
	{
		return implementation_->backend.capture_engine_texture(graphics, source, tag);
	}
	void runtime::poll_capture(const d3d11::device_snapshot& graphics)
	{
		implementation_->backend.poll_capture(graphics);
	}
	void runtime::on_resize_before(const d3d11::resize_event& event) noexcept
	{
		implementation_->backend.on_resize_before(event);
	}
	void runtime::on_device_destroying(const d3d11::device_snapshot& graphics) noexcept
	{
		implementation_->backend.on_device_destroying(graphics);
	}
	void runtime::shutdown() noexcept
	{
		implementation_->shutting_down.store(true, std::memory_order_release);
		implementation_->backend.shutdown();
	}
	bool runtime::shutdown_complete() noexcept
	{
		return implementation_->backend.shutdown_complete();
	}
	bool runtime::requested_enabled() const
	{
		return implementation_->backend.requested_enabled();
	}
	bool runtime::applied_enabled() const
	{
		return implementation_->backend.applied_enabled();
	}
	runtime_status runtime::get_status() const
	{
		auto status = implementation_->backend.get_status();
		status.worker_phase = !implementation_->shutting_down.load(std::memory_order_acquire)
		                          ? "present_owner"
		                      : !status.loader_loaded ? "present_owner_stopped"
		                                              : "present_owner_waiting_post";
		return status;
	}
}
