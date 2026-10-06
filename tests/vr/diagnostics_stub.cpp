#include "component/vr/diagnostics.hpp"
#include "component/d3d11.hpp"
#include "component/vr/engine_stereo_owner_pass.hpp"
#include "component/vr/engine_scene_resolution.hpp"

#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace
{
	std::recursive_mutex gpu_queue_interop_mutex;
	std::mutex gpu_queue_probe_mutex;
	std::condition_variable gpu_queue_probe_cv;
	std::uint64_t gpu_queue_attempts{};
	std::uint64_t gpu_queue_acquisitions{};
}

namespace d3d11
{
	graphics_status get_graphics_status() { return {}; }
	bool is_inside_present_gpu_scope() noexcept { return false; }

	std::unique_lock<std::recursive_mutex> acquire_gpu_queue_interop(const gpu_queue_client)
	{
		{
			const std::lock_guard probe_lock(gpu_queue_probe_mutex);
			++gpu_queue_attempts;
		}
		gpu_queue_probe_cv.notify_all();
		std::unique_lock result(gpu_queue_interop_mutex);
		{
			const std::lock_guard probe_lock(gpu_queue_probe_mutex);
			++gpu_queue_acquisitions;
		}
		gpu_queue_probe_cv.notify_all();
		return result;
	}
}

namespace vr::tests
{
	void reset_gpu_queue_gate_probe()
	{
		const std::lock_guard lock(gpu_queue_probe_mutex);
		gpu_queue_attempts = 0;
		gpu_queue_acquisitions = 0;
	}

	void wait_for_gpu_queue_gate_attempts(const std::uint64_t expected)
	{
		std::unique_lock lock(gpu_queue_probe_mutex);
		gpu_queue_probe_cv.wait(lock, [expected]
		{
			return gpu_queue_attempts >= expected;
		});
	}

	std::uint64_t gpu_queue_gate_acquisitions()
	{
		const std::lock_guard lock(gpu_queue_probe_mutex);
		return gpu_queue_acquisitions;
	}
}

namespace vr::diagnostics
{
	void record_trace(const trace_event, const std::uint64_t, const std::uint64_t) noexcept {}
	void gpu_interop_entered(const std::uintptr_t, const std::uintptr_t) noexcept {}
	void gpu_interop_exited() noexcept {}
}

namespace vr::engine_stereo_owner_pass
{
	report get_report() noexcept { return {}; }
	void request_temporal_history_reset() noexcept {} // No native temporal history in no-loader tests.
}

namespace vr::tests { std::optional<engine_scene_resolution::extent> native_resolution_override; }

namespace vr::engine_scene_resolution
{
	// No-loader executables cannot claim to resize the real H2 renderer.
	bool request(extent desired, std::string& error)
	{
		if(tests::native_resolution_override && desired.width==tests::native_resolution_override->width && desired.height==tests::native_resolution_override->height)return true;
		error = "native H2 scene resolution requires the real engine";
		return false;
	}
	bool ready() noexcept { return tests::native_resolution_override.has_value(); }
	bool accepts_source(extent desired) noexcept {return tests::native_resolution_override && desired.width==tests::native_resolution_override->width && desired.height==tests::native_resolution_override->height;}
	report get_report() { return {}; }
}
