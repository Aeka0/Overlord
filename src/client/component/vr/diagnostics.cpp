#include <std_include.hpp>

#include "diagnostics.hpp"
#include "debug_options.hpp"
#include "build_config.hpp"
#include "diagnostics/status_sections.hpp"
#include "diagnostics/format_helpers.hpp"
#include "region_capture.hpp"
#include "engine_backend_probe.hpp"
#include "engine_stereo_binding.hpp"
#include "engine_stereo_backend_target.hpp"
#include "engine_stereo_backend_view.hpp"
#include "engine_stereo_bridge.hpp"
#include "engine_stereo_draw_indexed.hpp"
#include "engine_stereo_effect_timeline.hpp"
#include "engine_stereo_execution.hpp"
#include "engine_stereo_eye_resources.hpp"
#include "engine_stereo_gpu_census.hpp"
#include "engine_stereo_gpu_timing.hpp"
#include "engine_stereo_material_buffer_probe.hpp"
#include "engine_stereo_particle_buffer_probe.hpp"
#include "engine_stereo_output_merger.hpp"
#include "engine_stereo_owner_pass.hpp"
#include "engine_stereo_resource_ops.hpp"
#include "engine_stereo_renderer.hpp"
#include "engine_stereo_scene_batch_probe.hpp"
#include "engine_stereo_ssr_history_probe.hpp"
#include "engine_stereo_ssr_consumer_probe.hpp"
#include "engine_view_probe.hpp"
#include "head_pose_bridge.hpp"
#include "gameplay/hands/status.hpp"
#include "native_render_session.hpp"
#include "vr_runtime.hpp"

#include <exception/minidump.hpp>
#include <utils/io.hpp>

#include "../console.hpp"
#include "../d3d11.hpp"
#include "loader/target_identity.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <windows.h>

namespace vr::diagnostics
{
	using detail::available;
	using detail::yes_no;

	namespace
	{
		using clock = std::chrono::steady_clock;

		struct timing_state
		{
			std::uint64_t renderer_count{};
			std::uint64_t gui_count{};
			std::uint64_t present_pre_count{};
			std::uint64_t present_post_count{};
			std::uint64_t resize_before_count{};
			std::uint64_t present_frame_index{};
			std::uint64_t resize_index{};
			UINT resize_width{};
			UINT resize_height{};
			DXGI_FORMAT resize_format{DXGI_FORMAT_UNKNOWN};
			clock::time_point renderer{};
			clock::time_point gui{};
			clock::time_point present_pre{};
			clock::time_point present_post{};
			clock::time_point resize_before{};
		};

		std::mutex timing_mutex;
		timing_state timings;
		std::mutex status_snapshot_mutex;

		struct trace_entry
		{
			std::atomic<std::uint64_t> sequence{};
			std::atomic<std::uint64_t> timestamp{};
			std::atomic<std::uint32_t> thread_id{};
			std::atomic<std::uint16_t> event{};
			std::atomic<std::uint64_t> value_a{};
			std::atomic<std::uint64_t> value_b{};
		};

		constexpr std::size_t trace_capacity = 256;
		std::array<trace_entry, trace_capacity> trace_ring;
		std::atomic<std::uint64_t> trace_sequence{};
		std::atomic<std::uint64_t> scene_hook_sequence{};
		std::atomic<std::uint64_t> scene_hook_entered_tick{};
		std::atomic<std::uint32_t> scene_hook_thread_id{};
		std::atomic<std::uint32_t> scene_hook_depth{};
		std::atomic<std::uint64_t> gpu_interop_sequence{};
		std::atomic<std::uint64_t> gpu_interop_entered_tick{};
		std::atomic<std::uint32_t> gpu_interop_thread_id{};
		std::atomic<std::uint32_t> gpu_interop_depth{};
		std::atomic<std::uintptr_t> gpu_interop_source{};
		std::atomic<std::uintptr_t> gpu_interop_destination{};

		const char* trace_event_name(const trace_event event) noexcept
		{
			switch (event)
			{
			case trace_event::renderer_frame: return "renderer_frame";
			case trace_event::present_pre: return "present_pre";
			case trace_event::present_post: return "present_post";
			case trace_event::resize_before: return "resize_before";
			case trace_event::runtime_prepare_frame: return "runtime_prepare_frame";
			case trace_event::runtime_prepare_frame_result: return "runtime_prepare_frame_result";
			case trace_event::runtime_wait_get_poses: return "runtime_wait_get_poses";
			case trace_event::runtime_wait_get_poses_result: return "runtime_wait_get_poses_result";
			case trace_event::runtime_direct_submission_frame: return "runtime_direct_submission_frame";
			case trace_event::runtime_direct_thread_contract: return "runtime_direct_thread_contract";
			case trace_event::runtime_prepare_scene: return "runtime_prepare_scene";
			case trace_event::runtime_prepare_scene_result: return "runtime_prepare_scene_result";
			case trace_event::runtime_source_pair: return "runtime_source_pair";
			case trace_event::runtime_source_pair_result: return "runtime_source_pair_result";
			case trace_event::runtime_render_eye: return "runtime_render_eye";
			case trace_event::runtime_render_eye_result: return "runtime_render_eye_result";
			case trace_event::runtime_submit: return "runtime_submit";
			case trace_event::runtime_submit_result: return "runtime_submit_result";
			case trace_event::runtime_interop_copy: return "runtime_interop_copy";
			case trace_event::runtime_interop_copy_result: return "runtime_interop_copy_result";
			case trace_event::runtime_interop_copy_eye: return "runtime_interop_copy_eye";
			case trace_event::runtime_interop_copy_eye_result: return "runtime_interop_copy_eye_result";
			case trace_event::runtime_interop_copy_query_end: return "runtime_interop_copy_query_end";
			case trace_event::runtime_interop_copy_query_end_result: return "runtime_interop_copy_query_end_result";
			case trace_event::runtime_interop_copy_query_poll: return "runtime_interop_copy_query_poll";
			case trace_event::runtime_interop_copy_query_poll_result: return "runtime_interop_copy_query_poll_result";
			case trace_event::runtime_submission_retire: return "runtime_submission_retire";
			case trace_event::runtime_post_present_handoff: return "runtime_post_present_handoff";
			case trace_event::runtime_post_present_handoff_result: return "runtime_post_present_handoff_result";
			case trace_event::scene_hook_enter: return "scene_hook_enter";
			case trace_event::scene_native_gate: return "scene_native_gate";
			case trace_event::scene_native_target: return "scene_native_target";
			case trace_event::scene_native_call: return "scene_native_call";
			case trace_event::scene_native_return: return "scene_native_return";
			case trace_event::scene_original_call: return "scene_original_call";
			case trace_event::native_session_ensure: return "native_session_ensure";
			case trace_event::native_session_ensure_result: return "native_session_ensure_result";
			case trace_event::native_capture: return "native_capture";
			case trace_event::native_capture_result: return "native_capture_result";
			case trace_event::capture_produce: return "capture_produce";
			case trace_event::capture_direct: return "capture_direct";
			case trace_event::capture_copy: return "capture_copy";
			case trace_event::capture_copy_begin: return "capture_copy_begin";
			case trace_event::capture_copy_end: return "capture_copy_end";
			case trace_event::capture_query_end: return "capture_query_end";
			case trace_event::capture_query_end_begin: return "capture_query_end_begin";
			case trace_event::capture_query_end_end: return "capture_query_end_end";
			case trace_event::capture_poll: return "capture_poll";
			case trace_event::capture_poll_begin: return "capture_poll_begin";
			case trace_event::capture_poll_end: return "capture_poll_end";
			case trace_event::capture_open_shared: return "capture_open_shared";
			case trace_event::capture_retain_direct: return "capture_retain_direct";
			case trace_event::scene_source_ensure_begin: return "scene_source_ensure_begin";
			case trace_event::scene_source_ensure_result: return "scene_source_ensure_result";
			case trace_event::scene_gpu_copy_begin: return "scene_gpu_copy_begin";
			case trace_event::scene_gpu_copy_end: return "scene_gpu_copy_end";
			case trace_event::scene_gpu_fence: return "scene_gpu_fence";
			case trace_event::scene_gpu_fence_result: return "scene_gpu_fence_result";
			case trace_event::native_conversion_begin: return "native_conversion_begin";
			case trace_event::native_conversion_end: return "native_conversion_end";
			case trace_event::native_command_list_begin: return "native_command_list_begin";
			case trace_event::native_command_list_end: return "native_command_list_end";
			case trace_event::native_pair_release: return "native_pair_release";
			case trace_event::runtime_state_change: return "runtime_state_change";
			case trace_event::runtime_event: return "runtime_event";
			case trace_event::runtime_teardown_begin: return "runtime_teardown_begin";
			case trace_event::runtime_teardown_stage: return "runtime_teardown_stage";
			case trace_event::dynamic_upload_defer: return "dynamic_upload_defer";
			case trace_event::dynamic_upload_advance: return "dynamic_upload_advance";
			case trace_event::dynamic_upload_return: return "dynamic_upload_return";
			case trace_event::dynamic_upload_recover: return "dynamic_upload_recover";
			case trace_event::dynamic_upload_reject: return "dynamic_upload_reject";
			case trace_event::vehicle_aim_prepare: return "vehicle_aim_prepare";
			case trace_event::vehicle_shot: return "vehicle_shot";
			case trace_event::vehicle_fx: return "vehicle_fx";
			default: return "unknown";
			}
		}

		const char* gpu_queue_client_name(const d3d11::gpu_queue_client client) noexcept
		{
			switch (client)
			{
			case d3d11::gpu_queue_client::present: return "present";
			case d3d11::gpu_queue_client::resize_buffers: return "resize_buffers";
			case d3d11::gpu_queue_client::openvr: return "openvr";
			default: return "unknown";
			}
		}

		void append_device_creation_evidence(std::ostringstream& output,
			const d3d11::graphics_status& graphics)
		{
			const auto& creation = graphics.active_device_creation;
			output << std::dec
				<< "Successful Present count: " << graphics.successful_present_count << "\r\n"
				<< "Failed Present count: " << graphics.failed_present_count << "\r\n"
				<< "Active device creation available: " << (creation.available ? "yes" : "no") << "\r\n"
				<< "D3D11 create index: " << creation.d3d11_create_index << "\r\n"
				<< "Factory1 count before device: " << creation.factory1_count_before_create << "\r\n"
				<< "Factory1 result/query/parent: 0x" << std::hex
				<< static_cast<std::uint32_t>(creation.factory1_result) << "/0x"
				<< static_cast<std::uint32_t>(creation.factory1_query_result) << "/0x"
				<< static_cast<std::uint32_t>(creation.factory1_parent_result) << "\r\n"
				<< "Requested/actual device flags: 0x" << creation.requested_flags << "/0x"
				<< creation.creation_flags << "\r\n"
				<< "Single-threaded device: "
				<< ((creation.creation_flags & D3D11_CREATE_DEVICE_SINGLETHREADED) != 0 ? "yes" : "no")
				<< "\r\n"
				<< "Multithread query/protected: 0x"
				<< static_cast<std::uint32_t>(creation.multithread_query_result) << "/"
				<< (creation.multithread_protected ? "yes" : "no") << "\r\n"
				<< std::dec << "Device creation thread: " << creation.creation_thread_id << "\r\n"
				<< "GPU queue gate acquire/contention: " << graphics.gpu_queue.acquire_count << "/"
				<< graphics.gpu_queue.contention_count << "\r\n"
				<< "GPU queue gate Present/Resize/OpenVR: "
				<< graphics.gpu_queue.present_acquire_count << "/"
				<< graphics.gpu_queue.resize_acquire_count << "/"
				<< graphics.gpu_queue.openvr_acquire_count << "\r\n"
				<< "GPU queue gate wait total/max/last us: "
				<< graphics.gpu_queue.total_wait_us << "/"
				<< graphics.gpu_queue.maximum_wait_us << "/"
				<< graphics.gpu_queue.last_wait_us << "\r\n"
				<< "GPU queue gate last client/thread: "
				<< gpu_queue_client_name(graphics.gpu_queue.last_client) << "/"
				<< graphics.gpu_queue.last_thread_id << "\r\n"
				<< "GPU queue gate waiter active/client/thread/tick: "
				<< (graphics.gpu_queue.waiter_active ? "yes" : "no") << "/"
				<< gpu_queue_client_name(graphics.gpu_queue.waiter_client) << "/"
				<< graphics.gpu_queue.waiter_thread_id << "/"
				<< graphics.gpu_queue.waiter_started_tick << "\r\n";
		}

		double milliseconds(const clock::duration duration)
		{
			return std::chrono::duration<double, std::milli>(duration).count();
		}

		void append_duration(std::ostringstream& output, const clock::duration duration)
		{
			if (duration == clock::duration{})
			{
				output << "unavailable";
				return;
			}

			output << std::fixed << std::setprecision(3) << milliseconds(duration) << " ms";
		}

		void append_age(std::ostringstream& output, const clock::time_point timestamp,
			const clock::time_point now)
		{
			if (timestamp == clock::time_point{})
			{
				output << "unavailable";
				return;
			}

			output << std::fixed << std::setprecision(3) << milliseconds(now - timestamp) << " ms ago";
		}

		void append_graphics_timestamp(std::ostringstream& output, const char* name,
			const clock::time_point timestamp, const clock::time_point now)
		{
			output << "  " << name << "=";
			append_age(output, timestamp, now);
			output << '\n';
		}

	}

	void record_trace(const trace_event event, const std::uint64_t value_a,
		const std::uint64_t value_b) noexcept
	{
		if (region_capture::enabled())
		{
			// Stable region-file event IDs, independent of the diagnostic trace enum.
			std::uint64_t code{};
			switch (event)
			{
			case trace_event::present_pre: code = 1; break;
			case trace_event::present_post: code = 2; break;
			case trace_event::runtime_wait_get_poses: code = 3; break;
			case trace_event::runtime_wait_get_poses_result: code = 4; break;
			case trace_event::runtime_submit: code = 5; break;
			case trace_event::runtime_submit_result: code = 6; break;
			case trace_event::native_conversion_begin: code = 7; break;
			case trace_event::native_conversion_end: code = 8; break;
			case trace_event::dynamic_upload_advance: code = 9; break;
			case trace_event::dynamic_upload_return: code = 10; break;
			case trace_event::native_pair_release: code = 11; break;
			case trace_event::runtime_state_change: code = 12; break;
			default: break;
			}
			if (code) region_capture::event(region_capture::kind::trace, value_a, value_b, code);
		}
		const auto sequence = trace_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
		auto& entry = trace_ring[sequence % trace_capacity];
		entry.sequence.store(0, std::memory_order_relaxed);
		entry.timestamp.store(GetTickCount64(), std::memory_order_relaxed);
		entry.thread_id.store(GetCurrentThreadId(), std::memory_order_relaxed);
		entry.event.store(static_cast<std::uint16_t>(event), std::memory_order_relaxed);
		entry.value_a.store(value_a, std::memory_order_relaxed);
		entry.value_b.store(value_b, std::memory_order_relaxed);
		entry.sequence.store(sequence, std::memory_order_release);
	}

	namespace
	{
		constexpr std::size_t live_view_trace_records = 4096;

		std::string format_trace(const std::size_t view_record_limit)
		{
			std::ostringstream output;
			const auto newest = trace_sequence.load(std::memory_order_acquire);
			const auto first = newest > trace_capacity ? newest - trace_capacity + 1 : 1;
			output << "VR trace (newest=" << newest << ", capacity=" << trace_capacity << ")\r\n";
			for (auto sequence = first; sequence <= newest; ++sequence)
			{
				const auto& entry = trace_ring[sequence % trace_capacity];
				const auto observed = entry.sequence.load(std::memory_order_acquire);
				if (observed != sequence) continue;
				const auto timestamp = entry.timestamp.load(std::memory_order_relaxed);
				const auto thread_id = entry.thread_id.load(std::memory_order_relaxed);
				const auto event = static_cast<trace_event>(entry.event.load(std::memory_order_relaxed));
				const auto value_a = entry.value_a.load(std::memory_order_relaxed);
				const auto value_b = entry.value_b.load(std::memory_order_relaxed);
				output << sequence << " tick=" << timestamp << " tid=" << thread_id << ' '
					<< trace_event_name(event) << " a=0x" << std::hex << value_a
					<< " b=0x" << value_b << std::dec << "\r\n";
			}
			output << "\r\n";
			output << "\r\n" << engine_view_probe::format_recent(view_record_limit);
			output << "\r\n" << engine_backend_probe::format_recent(view_record_limit);
			return output.str();
		}
	}

	std::string crash_trace()
	{
		// One-shot crash/watchdog reports preserve the complete CPU ring. The
		// periodically replaced live checkpoint uses a smaller multi-second window
		// below so durable diagnostics do not become a renderer-adjacent I/O load.
		return format_trace(engine_view_probe::trace_capacity);
	}

	void record_renderer_frame() noexcept
	{
		const std::lock_guard lock(timing_mutex);
		++timings.renderer_count;
		timings.renderer = clock::now();
	}

	void record_gui_frame() noexcept
	{
		const std::lock_guard lock(timing_mutex);
		++timings.gui_count;
		timings.gui = clock::now();
	}

	void record_present_pre(const d3d11::present_event& event) noexcept
	{
		const std::lock_guard lock(timing_mutex);
		++timings.present_pre_count;
		timings.present_frame_index = event.frame_index;
		timings.present_pre = event.timestamp == clock::time_point{} ? clock::now() : event.timestamp;
	}

	void record_present_post(const d3d11::present_event& event, const std::int32_t result) noexcept
	{
		if (FAILED(result))
		{
			record_trace(trace_event::present_post, event.frame_index,
				static_cast<std::uint32_t>(result));
		}
		const std::lock_guard lock(timing_mutex);
		++timings.present_post_count;
		timings.present_post = clock::now();
	}

	void record_resize_before(const d3d11::resize_event& event) noexcept
	{
		record_trace(trace_event::resize_before, event.resize_index,
			(static_cast<std::uint64_t>(event.width) << 32) | event.height);
		const std::lock_guard lock(timing_mutex);
		++timings.resize_before_count;
		timings.resize_index = event.resize_index;
		timings.resize_width = event.width;
		timings.resize_height = event.height;
		timings.resize_format = event.format;
		timings.resize_before = event.timestamp == clock::time_point{} ? clock::now() : event.timestamp;
	}

	std::string format_status_text(const bool dvar_enabled)
	{
		const auto identity = target_identity::get();
		const auto device = d3d11::get_device_snapshot();
		const auto graphics = d3d11::get_graphics_status();
		const auto runtime_status = runtime::get().get_status();
		const auto bridge_status = engine_stereo_bridge::get_status();
		const auto view_probe_status = engine_view_probe::get_status();
		const auto backend_probe_status = engine_backend_probe::get_status();
		const auto stereo_binding_status = engine_stereo_binding::get_status();
		const auto backend_view_status = engine_stereo_backend_view::get_status();
		const auto backend_target_status = engine_stereo_backend_target::get_status();
		const auto backend_target_frame_status =
			engine_stereo_backend_target::get_frame_status();
		const auto output_merger_status = engine_stereo_output_merger::get_status();
		const auto draw_indexed_status = engine_stereo_draw_indexed::get_status();
		const auto execution_status = engine_stereo_execution::get_status();
		const auto owner_pass_status = engine_stereo_owner_pass::get_report();
		auto scene_batch_storage =
			std::make_unique<engine_stereo_scene_batch_probe::report>();
		engine_stereo_scene_batch_probe::get_report(*scene_batch_storage);
		const auto& scene_batch_status = *scene_batch_storage;
		const auto material_buffer_status =
			engine_stereo_material_buffer_probe::get_report();
		const auto particle_buffer_status =
			engine_stereo_particle_buffer_probe::get_report();
		const auto ssr_history_status = engine_stereo_ssr_history_probe::get_report();
		auto ssr_consumer_storage =
			std::make_unique<engine_stereo_ssr_consumer_probe::report>();
		engine_stereo_ssr_consumer_probe::get_report(*ssr_consumer_storage);
		const auto& ssr_consumer_status = *ssr_consumer_storage;
		auto effect_timeline_storage =
			std::make_unique<engine_stereo_effect_timeline::report>();
		engine_stereo_effect_timeline::get_report(*effect_timeline_storage);
		const auto& effect_timeline_status = *effect_timeline_storage;
		const auto eye_resource_status = engine_stereo_eye_resources::get_status();
		auto gpu_census_storage =
			std::make_unique<engine_stereo_gpu_census::report>();
		engine_stereo_gpu_census::get_report(*gpu_census_storage);
		const auto& gpu_census_status = *gpu_census_storage;
		const auto& constant_buffer_status =
			gpu_census_status.constant_buffer_probe;
		const auto backend_watchdog = engine_backend_probe::get_backend_watchdog_status();
		const auto head_status = head_pose_bridge::get_status();
		const auto culling_status = engine_stereo_renderer::get_culling_union_status();
		const auto interop_status = get_gpu_interop_watchdog_status();
		const auto now = clock::now();
		const auto tick_now = GetTickCount64();
		const auto interop_age_ms = interop_status.depth != 0 &&
			interop_status.entered_tick != 0 && tick_now >= interop_status.entered_tick
			? tick_now - interop_status.entered_tick : 0;

		timing_state timing;
		{
			const std::lock_guard lock(timing_mutex);
			timing = timings;
		}

		std::ostringstream output;
		output << "[VR] status\n";
		output << "target identity:\n";
		output << "  original_path=" << available(identity.original_path) << '\n';
		output << "  original_size=" << identity.original_file_size << " bytes";
		output << " sha256=" << available(identity.original_sha256) << '\n';
		output << "  original_pe_timestamp=0x" << std::hex << std::uppercase << identity.original_pe_timestamp;
		output << " image_size=0x" << identity.original_image_size;
		output << " checksum=0x" << identity.original_pe_checksum;
		output << std::dec << std::nouppercase << " valid=" << yes_no(identity.original_valid_pe) << '\n';
		output << "  loaded_path=" << available(identity.loaded_path) << '\n';
		output << "  loaded_size=" << identity.loaded_file_size << " bytes";
		output << " actual_sha256=" << available(identity.loaded_sha256) << '\n';
		output << "  expected_loaded_sha256=" << available(identity.expected_loaded_sha256);
		output << " cache_matches_expected=" << yes_no(identity.cache_matches_expected) << '\n';
		output << "  loaded_pe_timestamp=0x" << std::hex << std::uppercase << identity.loaded_pe_timestamp;
		output << " image_size=0x" << identity.loaded_image_size;
		output << " checksum=0x" << identity.loaded_pe_checksum;
		output << std::dec << std::nouppercase << " valid=" << yes_no(identity.loaded_valid_pe) << '\n';
		output << "  compatibility_probe_passed=" << yes_no(identity.compatibility_probe_passed) << '\n';

		output << "control:\n";
		output << "  build=" << build_config::name << " optimized=" << yes_no(build_config::optimized) << '\n';
		output << "  debug_loaded:";
		for (std::size_t i{}; i < debug_options::names.size(); ++i)
			output << ' ' << debug_options::names[i] << '=' << yes_no(
				debug_options::enabled(static_cast<debug_options::probe>(i)));
		output << " (startup selection; restart required)\n";
		output << "  vr_enable=" << yes_no(dvar_enabled);
		output << " desired=" << yes_no(runtime_status.desired_enabled);
		output << " applied=" << yes_no(runtime_status.applied_enabled);
		output << " reinitialize_pending=" << yes_no(runtime_status.reinitialize_pending) << '\n';

		output << "d3d11 device:\n";
		output << "  available=" << yes_no(static_cast<bool>(device));
		output << " device=" << device.device.Get() << " context=" << device.context.Get();
		output << " feature_level=0x" << std::hex << std::uppercase << static_cast<unsigned int>(device.feature_level);
		output << std::dec << std::nouppercase << " generation=" << device.generation << '\n';

		output << "d3d11 graphics:\n";
		const auto& creation = graphics.active_device_creation;
		output << "  dxgi_factory1_creates=" << graphics.dxgi_factory1_create_count;
		output << " d3d11_creates=" << graphics.d3d11_create_count;
		output << " active_creation=" << yes_no(creation.available);
		output << " create_index=" << creation.d3d11_create_index;
		output << " factory1_before_device=" << creation.factory1_count_before_create;
		output << " factory_result=0x" << std::hex << std::uppercase
			<< static_cast<std::uint32_t>(creation.factory1_result)
			<< " query=0x" << static_cast<std::uint32_t>(creation.factory1_query_result)
			<< " device_parent=0x" << static_cast<std::uint32_t>(creation.factory1_parent_result)
			<< std::dec << std::nouppercase;
		output << " adapter_explicit=" << yes_no(creation.adapter_explicit);
		output << " identity_match=" << yes_no(creation.factory_identity_match) << '\n';
		output << "  requested_flags=0x" << std::hex << std::uppercase << creation.requested_flags;
		output << " device_flags=0x" << creation.creation_flags;
		output << " single_threaded=" << yes_no(
			(creation.creation_flags & D3D11_CREATE_DEVICE_SINGLETHREADED) != 0);
		output << " multithread_query=0x"
			<< static_cast<std::uint32_t>(creation.multithread_query_result)
			<< std::dec << std::nouppercase;
		output << " multithread_protected=" << yes_no(creation.multithread_protected);
		output << " creation_thread=" << creation.creation_thread_id;
		output << " device=" << reinterpret_cast<void*>(creation.device_address);
		output << " context=" << reinterpret_cast<void*>(creation.context_address) << '\n';
		output << "  hooks: present=" << yes_no(graphics.present_hook_installed);
		output << " resize=" << yes_no(graphics.resize_hook_installed);
		output << " targets_valid=" << yes_no(graphics.hook_targets_valid) << '\n';
		output << "  generation=" << graphics.generation << " output_window=" << graphics.active_output_window;
		output << " size=" << graphics.active_width << 'x' << graphics.active_height;
		output << " format=" << static_cast<unsigned int>(graphics.active_format);
		output << " buffers=" << graphics.active_buffer_count << '\n';
		output << "  present_count=" << graphics.present_count;
		output << " successful=" << graphics.successful_present_count;
		output << " failed=" << graphics.failed_present_count;
		output << " resize_count=" << graphics.resize_count << '\n';
		output << "  present_active=" << yes_no(graphics.present_active);
		output << " thread=" << graphics.present_thread_id;
		output << " last_thread=" << graphics.last_present_thread_id;
		output << " last_frame=" << graphics.last_present_frame_index << '\n';
		output << "  present_gpu_scope: active=" << yes_no(graphics.present_gpu_scope_active);
		output << " thread=" << graphics.present_gpu_scope_thread_id;
		output << " count=" << graphics.present_gpu_scope_count << '\n';
		output << "  gpu_queue_gate: acquires=" << graphics.gpu_queue.acquire_count;
		output << " contention=" << graphics.gpu_queue.contention_count;
		output << " present=" << graphics.gpu_queue.present_acquire_count;
		output << " resize=" << graphics.gpu_queue.resize_acquire_count;
		output << " openvr=" << graphics.gpu_queue.openvr_acquire_count << '\n';
		output << "  gpu_queue_wait_us: total=" << graphics.gpu_queue.total_wait_us;
		output << " max=" << graphics.gpu_queue.maximum_wait_us;
		output << " last=" << graphics.gpu_queue.last_wait_us;
		output << " last_client=" << gpu_queue_client_name(graphics.gpu_queue.last_client);
		output << " last_thread=" << graphics.gpu_queue.last_thread_id << '\n';
		output << "  gpu_queue_waiter: active=" << yes_no(graphics.gpu_queue.waiter_active);
		output << " client=" << gpu_queue_client_name(graphics.gpu_queue.waiter_client);
		output << " thread=" << graphics.gpu_queue.waiter_thread_id;
		output << " started_tick=" << graphics.gpu_queue.waiter_started_tick << '\n';
		output << "  gpu_interop_watchdog: depth=" << interop_status.depth;
		output << " thread=" << interop_status.thread_id;
		output << " age_ms=" << interop_age_ms;
		output << " source=0x" << std::hex << std::uppercase << interop_status.source;
		output << " destination=0x" << interop_status.destination;
		output << std::dec << std::nouppercase << '\n';
		output << "  last_present_result=0x" << std::hex << std::uppercase;
		output << static_cast<std::uint32_t>(graphics.last_present_result);
		output << " last_resize_result=0x" << static_cast<std::uint32_t>(graphics.last_resize_result);
		output << " device_removed_reason=0x" << static_cast<std::uint32_t>(graphics.device_removed_reason);
		output << std::dec << std::nouppercase << '\n';
		output << "  d3d11_debug: hook=" << yes_no(graphics.debug_info_hook_installed);
		output << " messages=" << graphics.debug_message_count;
		output << " corruption=" << graphics.debug_corruption_count;
		output << " errors=" << graphics.debug_error_count;
		output << " warnings=" << graphics.debug_warning_count;
		output << " unretained=" << graphics.debug_unretained_count;
		output << " last_severity=" << graphics.last_debug_severity;
		output << " last_id=" << graphics.last_debug_id;
		output << " last_thread=" << graphics.last_debug_thread_id << '\n';
		output << "  d3d11_debug_last=" << available(graphics.last_debug_message) << '\n';
		for (std::uint32_t index{}; index < graphics.debug_sample_count &&
			index < graphics.debug_samples.size(); ++index)
		{
			const auto& sample = graphics.debug_samples[index];
			output << "  d3d11_debug_sample[" << index << "]: count=" << sample.count;
			output << " severity=" << sample.severity;
			output << " id=" << sample.id;
			output << " thread=" << sample.thread_id;
			output << " message=" << available(sample.description) << '\n';
			output << "    first_tick_ms=" << sample.first_tick_ms;
			output << " present=" << sample.first_present;
			output << " generation=" << sample.device_generation;
			output << " stack_size=" << sample.stack_size << " stack=";
			for (std::size_t frame{}; frame < sample.stack_size && frame < sample.stack.size(); ++frame)
			{
				if (frame != 0) output << ',';
				output << "0x" << std::hex << reinterpret_cast<std::uintptr_t>(sample.stack[frame]) << std::dec;
			}
			output << '\n';
		}
		append_graphics_timestamp(output, "last_present_pre", graphics.last_present_pre, now);
		append_graphics_timestamp(output, "last_present_post", graphics.last_present_post, now);
		append_graphics_timestamp(output, "last_resize_before", graphics.last_resize_before, now);
		append_graphics_timestamp(output, "last_resize_completed", graphics.last_resize_completed, now);
		output << "  last_present_duration=";
		append_duration(output, graphics.last_present_duration);
		output << " last_resize_duration=";
		append_duration(output, graphics.last_resize_duration);
		output << '\n';
		output << "  last_error=" << available(graphics.last_error) << '\n';

		output << "component callbacks:\n";
		output << "  renderer: count=" << timing.renderer_count << " last=";
		append_age(output, timing.renderer, now);
		output << '\n';
		output << "  gui: count=" << timing.gui_count << " last=";
		append_age(output, timing.gui, now);
		output << '\n';
		output << "  present_pre: count=" << timing.present_pre_count;
		output << " frame_index=" << timing.present_frame_index << " last=";
		append_age(output, timing.present_pre, now);
		output << '\n';
		output << "  present_post: count=" << timing.present_post_count << " last=";
		append_age(output, timing.present_post, now);
		output << '\n';
		output << "  resize_before: count=" << timing.resize_before_count;
		output << " resize_index=" << timing.resize_index << " size=" << timing.resize_width << 'x' << timing.resize_height;
		output << " format=" << static_cast<unsigned int>(timing.resize_format) << " last=";
		append_age(output, timing.resize_before, now);
		output << '\n';

		detail::append_runtime_status(output, runtime_status, head_status);
		detail::append_frontend_status(output, bridge_status, view_probe_status,
			backend_probe_status, stereo_binding_status, backend_view_status, backend_target_status,
			backend_target_frame_status, culling_status);
		detail::append_execution_status(output, output_merger_status, draw_indexed_status,
			execution_status, owner_pass_status);
		detail::append_scene_status(output, owner_pass_status, scene_batch_status);
		detail::append_history_status(output, ssr_history_status, ssr_consumer_status,
			eye_resource_status);
		detail::append_effect_status(output, owner_pass_status, effect_timeline_status);
		detail::append_owner_status(output, owner_pass_status, material_buffer_status,
			particle_buffer_status);
		detail::append_census_status(output, gpu_census_status);
		detail::append_resource_status(output, gpu_census_status, constant_buffer_status);
		output << "  backend_cpu: transactions=" << backend_probe_status.backend_transactions;
		output << " post_bind=" << backend_probe_status.backend_post_binds;
		output << " dispatch_enter=" << backend_probe_status.backend_dispatch_enters;
		output << " dispatch_return="
			<< backend_probe_status.backend_cpu_dispatch_returns;
		output << " skipped_null=" << backend_probe_status.backend_dispatch_skipped_null;
		output << " incomplete=" << backend_probe_status.backend_incomplete;
		output << " active=" << backend_probe_status.backend_active;
		output << " max_active=" << backend_probe_status.backend_maximum_active << '\n';
		output << "  backend_faults: orphan_dispatch="
			<< backend_probe_status.backend_orphan_dispatches;
		output << " record=" << backend_probe_status.backend_record_mismatches;
		output << " thread=" << backend_probe_status.backend_thread_mismatches;
		output << " type=" << backend_probe_status.backend_record_type_mismatches;
		output << " target=" << backend_probe_status.backend_target_invalid;
		output << " command=" << backend_probe_status.backend_command_mismatches << '\n';
		output << "  backend_target_prepare: calls="
			<< backend_probe_status.target_prepare_calls;
		output << " valid=" << backend_probe_status.target_prepare_valid_records;
		output << " invalid=" << backend_probe_status.target_prepare_invalid_records;
		output << " record_changed="
			<< backend_probe_status.target_prepare_changed_records;
		output << " target_changed="
			<< backend_probe_status.target_prepare_target_changes << '\n';
		output << "  backend_last: id=" << backend_probe_status.last_backend_id;
		output << " epoch=" << backend_probe_status.last_frontend_epoch;
		output << " frontend_tx=" << backend_probe_status.last_frontend_transaction_id;
		output << " record=" << reinterpret_cast<const void*>(backend_probe_status.last_record);
		output << " frontend=" << reinterpret_cast<const void*>(backend_probe_status.last_frontend);
		output << " command=" << reinterpret_cast<const void*>(
			backend_probe_status.last_command_stream);
		output << " index=" << backend_probe_status.last_record_index;
		output << " type=" << backend_probe_status.last_record_type;
		output << " target=" << backend_probe_status.last_target_id;
		output << " thread=" << backend_probe_status.last_backend_thread_id << '\n';
		output << "  backend_watchdog: sequence=" << backend_watchdog.sequence;
		output << " depth=" << backend_watchdog.depth;
		output << " phase=" << engine_backend_probe::to_string(backend_watchdog.phase);
		output << " thread=" << backend_watchdog.thread_id;
		output << " entered_tick=" << backend_watchdog.entered_tick;
		output << " last_progress_tick=" << backend_watchdog.last_progress_tick;
		output << " record=" << reinterpret_cast<const void*>(backend_watchdog.record);
		output << " command=" << reinterpret_cast<const void*>(backend_watchdog.command_stream)
			<< '\n';
		output << "  h2_gpu_marker: publications=" << backend_probe_status.query_publications;
		output << " results=" << backend_probe_status.query_results;
		output << " pending=" << backend_probe_status.query_pending_results;
		output << " complete=" << backend_probe_status.query_complete_results;
		output << " failed=" << backend_probe_status.query_failed_results;
		output << " identity_mismatch=" << backend_probe_status.query_identity_mismatches;
		output << " generation_mismatch=" << backend_probe_status.query_generation_mismatches;
		output << " device_mismatch=" << backend_probe_status.query_device_mismatches;
		output << " stale=" << backend_probe_status.query_stale_generations;
		output << " present_miss=" << backend_probe_status.query_present_prerequisite_misses;
		output << " unbound_complete=" << backend_probe_status.query_unbound_completions << '\n';
		output << "  observer_contract: completion=cpu_dispatch_return_only";
		output << " gpu_marker_proof=unbound_h2_event_query";
		output << " additional_gpu_commands=0 d3d_metadata_queries="
			<< output_merger_status.metadata_queries;
		output << " openvr_calls=0 ownership_changes=0\n";
		output << "  ipd_meters=" << bridge_status.ipd_meters;
		output << " world_scale=" << bridge_status.world_scale;
		output << " half_eye_offset_units=" << bridge_status.half_eye_offset_units;
		output << " view_publications=" << bridge_status.view_publications;
		output << " view_family_id=" << bridge_status.view_family_id << '\n';
		for (std::size_t index{}; index < bridge_status.eyes.size(); ++index)
		{
			const auto& projection = bridge_status.eyes[index];
			output << "  projection[" << index << "]: tangents=";
			output << projection.tan_left << ',' << projection.tan_right << ',';
			output << projection.tan_down << ',' << projection.tan_up;
			output << " symmetric_half=" << projection.symmetric_tan_half_x() << 'x';
			output << projection.symmetric_tan_half_y();
			output << " optical_aspect=" << projection.optical_aspect() << '\n';
		}
		output << "  stereo_frames=" << bridge_status.stereo_frames;
		output << " coherent_pairs=" << bridge_status.coherent_stereo_pairs;
		output << " incoherent_pairs=" << bridge_status.incoherent_stereo_pairs;
		output << " scene_hook_entries=" << bridge_status.scene_hook_entries;
		output << " scene_calls=" << bridge_status.scene_calls;
		output << " invalid_viewports=" << bridge_status.invalid_viewports;
		output << " restore_conflicts=" << bridge_status.restore_conflicts << '\n';
		output << "  render_targets: observations=" << bridge_status.target_observations;
		output << " misses=" << bridge_status.target_misses;
		output << " distinct_pairs=" << bridge_status.distinct_target_pairs;
		output << " aliased_pairs=" << bridge_status.aliased_target_pairs;
		output << " native_candidate_frame=" << bridge_status.native_target_candidate_frame << '\n';
		output << "  last_target: valid=" << yes_no(bridge_status.last_target.valid);
		output << " frame=" << bridge_status.last_target.frame_id;
		output << " pair=" << bridge_status.last_target.pair_id;
		output << " eye=" << bridge_status.last_target.output_eye;
		output << " phase=" << (bridge_status.last_target.phase ==
			engine_stereo_bridge::target_observation_phase::after_scene ? "after" : "before");
		output << " resource=" << reinterpret_cast<const void*>(bridge_status.last_target.resource);
		output << " size=" << bridge_status.last_target.width << 'x' << bridge_status.last_target.height;
		output << " format=" << bridge_status.last_target.format;
		output << " samples=" << bridge_status.last_target.sample_count << '\n';
		output << "  source_viewport=" << bridge_status.last_full_width << 'x';
		output << bridge_status.last_full_height << " eye_viewport=";
		output << bridge_status.last_left_width << 'x';
		output << bridge_status.last_right_width << '\n';
		const auto native = native_render_session::active().get_status();
		output << "  native_session: available=" << yes_no(native.available);
		output << " generation=" << native.device_generation;
		output << " target=" << native.width << 'x' << native.height;
		output << " source_format=" << native.source_format;
		output << " format=" << native.format;
		output << " mode=" << (native.copy_ring ? "copy" : "render");
		output << " accepting=" << yes_no(native.accepting_pairs);
		output << " expected_pair=" << native.expected_pair_id;
		output << " deferred_conversion=" << yes_no(native.deferred_conversion);
		output << " rebuilds=" << native.rebuilds;
		output << " invalidations=" << native.invalidations;
		output << " pair_acquires=" << native.pair_acquires;
		output << " pair_releases=" << native.pair_releases;
		output << " pair_deferrals=" << native.pair_deferrals;
		output << " pair_quarantines=" << native.pair_quarantines;
		output << " pair_exhaustions=" << native.pair_exhaustions;
		output << " pair_rejections=" << native.pair_rejections;
		output << " capture_requests=" << native.capture_requests;
		output << " capture_failures=" << native.capture_failures << '\n';
		output << "  native_admission: suspends=" << native.acquisition_suspends;
		output << " resumes=" << native.acquisition_resumes;
		output << " rejections=" << native.acquisition_rejections << '\n';
		output << "  native_conversion: attempts=" << native.conversion_attempts;
		output << " complete=" << native.conversion_completions;
		output << " failures=" << native.conversion_failures;
		output << " last=0x" << std::hex << std::uppercase << native.last_conversion_result;
		output << " pipeline=0x" << native.conversion_pipeline_result;
		output << " support=0x" << native.destination_format_support;
		output << std::dec << std::nouppercase << '\n';
		const auto& copy_failure = native.last_copy_failure;
		output << "  native_copy_failure: stage="
			<< native_render_session::to_string(copy_failure.stage);
		output << " pair=" << copy_failure.pair_id << " eye=" << copy_failure.eye;
		output << " generation=" << copy_failure.device_generation;
		output << " rebuild=" << copy_failure.rebuilds;
		output << " tick_ms=" << copy_failure.tick_ms;
		output << " thread=" << copy_failure.thread_id;
		output << " result=0x" << std::hex << static_cast<std::uint32_t>(copy_failure.result);
		output << " source=0x" << copy_failure.source;
		output << " cached_source=0x" << copy_failure.cached_source;
		output << " context=0x" << copy_failure.context;
		output << " expected_context=0x" << copy_failure.expected_context;
		output << " device=0x" << copy_failure.expected_device << std::dec;
		output << " available=" << yes_no(copy_failure.available);
		output << " copy_ring=" << yes_no(copy_failure.copy_ring);
		output << " accepting=" << yes_no(copy_failure.accepting_pairs);
		output << " expected_pair=" << copy_failure.expected_pair_id << '\n';
		if (copy_failure.stage != native_render_session::copy_failure::none)
		{
			const auto& descriptor = copy_failure.source_descriptor;
			output << "  native_copy_failure_source: descriptor_read="
				<< yes_no(copy_failure.source_descriptor_read);
			output << " actual=" << descriptor.Width << 'x' << descriptor.Height;
			output << " format=" << static_cast<std::uint32_t>(descriptor.Format);
			output << " expected=" << copy_failure.expected_width << 'x'
				<< copy_failure.expected_height;
			output << " expected_format="
				<< static_cast<std::uint32_t>(copy_failure.expected_source_format);
			output << " mips=" << descriptor.MipLevels << " array=" << descriptor.ArraySize;
			output << " samples=" << descriptor.SampleDesc.Count << '/' << descriptor.SampleDesc.Quality;
			output << " usage=" << static_cast<std::uint32_t>(descriptor.Usage);
			output << " bind=0x" << std::hex << descriptor.BindFlags;
			output << " cpu=0x" << descriptor.CPUAccessFlags << " misc=0x"
				<< descriptor.MiscFlags << std::dec << '\n';
		}
		output << "  native_command_lists: builds=" << native.command_list_builds;
		output << " build_failures=" << native.command_list_build_failures;
		output << " tag_failures=" << native.command_list_tag_failures;
		output << " executions=" << native.command_list_executions;
		output << " deferred_create=0x" << std::hex << std::uppercase
			<< native.deferred_context_create_result;
		output << " last=0x" << native.last_command_list_result;
		output << std::dec << std::nouppercase << '\n';
		output << "  native_copy_timing:";
		for (std::size_t eye{}; eye < 2; ++eye)
		{
			const auto lock_average = native.copy_lock_wait_samples[eye] != 0 ?
				native.copy_lock_wait_total_us[eye] / native.copy_lock_wait_samples[eye] : 0;
			const auto execute_average = native.command_list_execute_samples[eye] != 0 ?
				native.command_list_execute_total_us[eye] /
					native.command_list_execute_samples[eye] : 0;
			const auto removed_average = native.removed_reason_samples[eye] != 0 ?
				native.removed_reason_total_us[eye] / native.removed_reason_samples[eye] : 0;
			output << " eye[" << eye << "]_lock(samples/last/max/avg_us)="
				<< native.copy_lock_wait_samples[eye] << '/'
				<< native.copy_lock_wait_last_us[eye] << '/'
				<< native.copy_lock_wait_max_us[eye] << '/' << lock_average;
			output << " execute=" << native.command_list_execute_samples[eye] << '/'
				<< native.command_list_execute_last_us[eye] << '/'
				<< native.command_list_execute_max_us[eye] << '/' << execute_average;
			output << " removed=" << native.removed_reason_samples[eye] << '/'
				<< native.removed_reason_last_us[eye] << '/'
				<< native.removed_reason_max_us[eye] << '/' << removed_average;
		}
		output << '\n';
		output << "  native_source_view: cached=" << yes_no(native.source_view_cached);
		output << " creations=" << native.source_view_creations;
		output << " matches=" << native.source_identity_matches;
		output << " mismatches=" << native.source_identity_mismatches;
		output << " texture=0x" << std::hex << native.last_source_texture;
		output << " view=0x" << native.last_source_view;
		output << " create=0x" << std::uppercase
			<< native.last_source_view_create_result;
		output << std::dec << std::nouppercase << '\n';
		output << "  capability_probe=" << available(native.capability_probe) << '\n';
		for (std::size_t index = 0; index < runtime_status.eyes.size(); ++index)
		{
			const auto& eye = runtime_status.eyes[index];
			output << "  eye[" << index << "]: " << eye.width << 'x' << eye.height;
			output << " acquired=" << eye.acquired << " released=" << eye.released << '\n';
		}
		output << "  submitted_frames=" << runtime_status.submitted_frames;
		output << " last_xr_result=" << runtime_status.last_xr_result;
		output << " (" << available(runtime_status.last_xr_result_name) << ")\n";
		output << "  last_error=" << available(runtime_status.last_error) << '\n';
		output << gameplay::hands::status();
		output << "  native_renderer: ready=" << yes_no(runtime_status.native_renderer_ready);
		output << " failures=" << runtime_status.native_renderer_failure_count;
		output << " error=" << available(runtime_status.native_renderer_error) << '\n';
		if (!runtime_status.sdk_headers_available)
		{
			output << "  OpenXR SDK facade=unavailable\n";
		}

		return output.str();
	}

	std::string format_engine_probe_text(
		const engine_stereo_probe::status& probe_status)
	{
		std::ostringstream output;
		output << "[VR] engine_probe\n";
		output << "  mode=" << engine_stereo_probe::to_string(probe_status.requested_mode);
		output << " gate=" << engine_stereo_probe::to_string(probe_status.gate);
		output << " abi=" << engine_stereo_probe::to_string(probe_status.abi);
		output << " active=" << yes_no(probe_status.active);
		output << " trace=" << yes_no(probe_status.trace_enabled) << '\n';
		output << "  renderer_frames=" << probe_status.renderer_frame_count;
		output << " boundaries=" << probe_status.renderer_boundary_count;
		output << " presents=" << probe_status.present_count;
		output << " correlations=" << probe_status.correlation_count;
		output << " misses=" << probe_status.correlation_miss_count << '\n';
		output << "  thread_mismatch=" << probe_status.thread_mismatch_count;
		output << " invalid_camera=" << probe_status.invalid_camera_count;
		output << " recursion=" << probe_status.recursion_count;
		output << " trace_overwrite=" << probe_status.trace_overwrite_count << '\n';
		output << "  resize_epoch=" << probe_status.resize_epoch;
		output << " device_generation=" << probe_status.device_generation;
		output << " renderer_thread=" << probe_status.renderer_thread_id;
		output << " present_thread=" << probe_status.present_thread_id;
		output << " same_thread=" << yes_no(probe_status.same_thread) << '\n';
		output << "  last_renderer_frame=" << probe_status.last_renderer_frame_id;
		output << " last_present_frame=" << probe_status.last_present_frame_index;
		output << " camera_valid=" << yes_no(probe_status.last_camera_valid);
		output << " camera_hash=" << probe_status.last_camera_hash << '\n';
		output << "  renderer_to_present=" << probe_status.last_renderer_to_present_microseconds << " us";
		output << " max_renderer_duration=" << probe_status.max_renderer_duration_nanoseconds << " ns\n";
		return output.str();
	}

	bool write_status_snapshot(const bool dvar_enabled) noexcept
	{
		try
		{
			// Serialize the complete format-and-replace transaction. Both the manual
			// command and watchdog use the same fixed .tmp sibling; formatting outside
			// this lock could also let an older snapshot overwrite a newer one.
			const std::lock_guard lock(status_snapshot_mutex);
			const auto status = format_status_text(dvar_enabled);
			const auto engine_probe = format_engine_probe_text(
				engine_stereo_probe::get_status());
			return utils::io::write_file_atomic(
				status_snapshot_path, status + engine_probe);
		}
		catch (...)
		{
			return false;
		}
	}

	void print_status(const bool dvar_enabled)
	{
		try
		{
			std::string status;
			std::string engine_probe;
			bool saved{};
			{
				const std::lock_guard lock(status_snapshot_mutex);
				status = format_status_text(dvar_enabled);
				engine_probe = format_engine_probe_text(
					engine_stereo_probe::get_status());
				saved = utils::io::write_file_atomic(status_snapshot_path, status + engine_probe);
			}
			console::print_text(console::con_type_info, status);
			console::print_text(console::con_type_info, engine_probe);
			if (saved) console::info("[VR] Complete report saved to %s. Share this file.\n",status_snapshot_path);
			else console::error("[VR] Report save FAILED; an older file may remain. Share this console output.\n");
		}
		catch (...)
		{
			console::error("[VR] status formatting failed; no snapshot was written\n");
		}
	}

	void print_engine_probe(const engine_stereo_probe::status& probe_status)
	{
		console::print_text(console::con_type_info,
			format_engine_probe_text(probe_status));
	}

	void scene_hook_entered() noexcept
	{
		region_capture::event(region_capture::kind::frontend_begin);
		const auto depth = scene_hook_depth.fetch_add(1, std::memory_order_acq_rel);
		if (depth == 0)
		{
			scene_hook_entered_tick.store(GetTickCount64(), std::memory_order_release);
			scene_hook_thread_id.store(GetCurrentThreadId(), std::memory_order_release);
			scene_hook_sequence.fetch_add(1, std::memory_order_acq_rel);
		}
	}

	void scene_hook_exited() noexcept
	{
		region_capture::event(region_capture::kind::frontend_end);
		const auto depth = scene_hook_depth.load(std::memory_order_acquire);
		if (depth != 0)
		{
			scene_hook_depth.fetch_sub(1, std::memory_order_acq_rel);
		}
	}

	scene_hook_watchdog_status get_scene_hook_watchdog_status() noexcept
	{
		return {
			scene_hook_sequence.load(std::memory_order_acquire),
			scene_hook_entered_tick.load(std::memory_order_acquire),
			scene_hook_thread_id.load(std::memory_order_acquire),
			scene_hook_depth.load(std::memory_order_acquire),
		};
	}

	void gpu_interop_entered(const std::uintptr_t source,
		const std::uintptr_t destination) noexcept
	{
		const auto depth = gpu_interop_depth.fetch_add(1, std::memory_order_acq_rel);
		if (depth == 0)
		{
			gpu_interop_source.store(source, std::memory_order_release);
			gpu_interop_destination.store(destination, std::memory_order_release);
			gpu_interop_entered_tick.store(GetTickCount64(), std::memory_order_release);
			gpu_interop_thread_id.store(GetCurrentThreadId(), std::memory_order_release);
			gpu_interop_sequence.fetch_add(1, std::memory_order_acq_rel);
		}
	}

	void gpu_interop_exited() noexcept
	{
		const auto depth = gpu_interop_depth.load(std::memory_order_acquire);
		if (depth != 0)
		{
			gpu_interop_depth.fetch_sub(1, std::memory_order_acq_rel);
		}
	}

	gpu_interop_watchdog_status get_gpu_interop_watchdog_status() noexcept
	{
		return {
			gpu_interop_sequence.load(std::memory_order_acquire),
			gpu_interop_entered_tick.load(std::memory_order_acquire),
			gpu_interop_thread_id.load(std::memory_order_acquire),
			gpu_interop_depth.load(std::memory_order_acquire),
			gpu_interop_source.load(std::memory_order_acquire),
			gpu_interop_destination.load(std::memory_order_acquire),
		};
	}

	bool write_soft_freeze_report(const d3d11::graphics_status& graphics,
		const std::chrono::milliseconds age)
	{
		if (!graphics.present_active || graphics.present_started == clock::time_point{})
		{
			return false;
		}

		tm local_time{};
		char timestamp[64]{};
		const auto raw_time = _time64(nullptr);
		_localtime64_s(&local_time, &raw_time);
		strftime(timestamp, sizeof(timestamp), "%Y-%m-%d-%H-%M-%S", &local_time);
		const auto stem = std::string("minidumps/overlord-soft-freeze-") + timestamp;
		std::ostringstream report;
		report << "Overlord soft-freeze report\r\n"
			<< "Present age: " << age.count() << " ms\r\n"
			<< "Present thread: " << graphics.present_thread_id << "\r\n"
			<< "Last Present thread: " << graphics.last_present_thread_id << "\r\n"
			<< "Last Present frame: " << graphics.last_present_frame_index << "\r\n"
			<< "Present GPU scope active: " << (graphics.present_gpu_scope_active ? "yes" : "no") << "\r\n"
			<< "Present GPU scope thread: " << graphics.present_gpu_scope_thread_id << "\r\n"
			<< "Generation: " << graphics.generation << "\r\n"
			<< "Last Present result: 0x" << std::hex
			<< static_cast<std::uint32_t>(graphics.last_present_result) << "\r\n"
			<< "Device removed reason: 0x"
			<< static_cast<std::uint32_t>(graphics.device_removed_reason) << "\r\n";
		append_device_creation_evidence(report, graphics);
		report << "VR trace:\r\n" << crash_trace();
		utils::io::write_file(stem + ".txt", report.str(), false);
		const auto dump_written = exception::write_process_minidump(stem + ".dmp");
		return dump_written;
	}

	bool write_scene_hook_stall_report(const d3d11::graphics_status& graphics,
		const scene_hook_watchdog_status& scene, const std::chrono::milliseconds age)
	{
		if (scene.sequence == 0 || scene.depth == 0 || scene.entered_tick == 0)
		{
			return false;
		}

		tm local_time{};
		char timestamp[64]{};
		const auto raw_time = _time64(nullptr);
		_localtime64_s(&local_time, &raw_time);
		strftime(timestamp, sizeof(timestamp), "%Y-%m-%d-%H-%M-%S", &local_time);
		const auto stem = std::string("minidumps/overlord-scene-stall-") + timestamp;
		std::ostringstream report;
		report << "Overlord scene-hook stall report\r\n"
			<< "Scene sequence: " << scene.sequence << "\r\n"
			<< "Scene thread: " << scene.thread_id << "\r\n"
			<< "Scene depth: " << scene.depth << "\r\n"
			<< "Scene age: " << age.count() << " ms\r\n"
			<< "Present active: " << (graphics.present_active ? "yes" : "no") << "\r\n"
			<< "Present thread: " << graphics.present_thread_id << "\r\n"
			<< "Last Present thread: " << graphics.last_present_thread_id << "\r\n"
			<< "Last Present frame: " << graphics.last_present_frame_index << "\r\n"
			<< "Present GPU scope active: " << (graphics.present_gpu_scope_active ? "yes" : "no") << "\r\n"
			<< "Present GPU scope thread: " << graphics.present_gpu_scope_thread_id << "\r\n"
			<< "Generation: " << graphics.generation << "\r\n"
			<< "Last Present result: 0x" << std::hex
			<< static_cast<std::uint32_t>(graphics.last_present_result) << "\r\n"
			<< "Device removed reason: 0x"
			<< static_cast<std::uint32_t>(graphics.device_removed_reason) << "\r\n";
		append_device_creation_evidence(report, graphics);
		report << "VR trace:\r\n" << crash_trace();
		utils::io::write_file(stem + ".txt", report.str(), false);
		return exception::write_process_minidump(stem + ".dmp");
	}

	bool write_gpu_interop_stall_report(const d3d11::graphics_status& graphics,
		const gpu_interop_watchdog_status& interop, const std::chrono::milliseconds age)
	{
		if (interop.sequence == 0 || interop.depth == 0 || interop.entered_tick == 0)
		{
			return false;
		}

		tm local_time{};
		char timestamp[64]{};
		const auto raw_time = _time64(nullptr);
		_localtime64_s(&local_time, &raw_time);
		strftime(timestamp, sizeof(timestamp), "%Y-%m-%d-%H-%M-%S", &local_time);
		const auto stem = std::string("minidumps/overlord-interop-stall-") + timestamp;
		std::ostringstream report;
		report << "Overlord GPU-interop stall report\r\n"
			<< "Interop sequence: " << interop.sequence << "\r\n"
			<< "Interop thread: " << interop.thread_id << "\r\n"
			<< "Interop depth: " << interop.depth << "\r\n"
			<< "Interop age: " << age.count() << " ms\r\n"
			<< "Source: 0x" << std::hex << interop.source << "\r\n"
			<< "Destination: 0x" << interop.destination << "\r\n"
			<< "Present active: " << (graphics.present_active ? "yes" : "no") << "\r\n"
			<< "Present thread: " << std::dec << graphics.present_thread_id << "\r\n"
			<< "Last Present thread: " << graphics.last_present_thread_id << "\r\n"
			<< "Last Present frame: " << graphics.last_present_frame_index << "\r\n"
			<< "Present GPU scope active: " << (graphics.present_gpu_scope_active ? "yes" : "no") << "\r\n"
			<< "Present GPU scope thread: " << graphics.present_gpu_scope_thread_id << "\r\n"
			<< "Generation: " << graphics.generation << "\r\n"
			<< "Last Present result: 0x" << std::hex
			<< static_cast<std::uint32_t>(graphics.last_present_result) << "\r\n"
			<< "Device removed reason: 0x"
			<< static_cast<std::uint32_t>(graphics.device_removed_reason) << "\r\n";
		append_device_creation_evidence(report, graphics);
		report << "VR trace:\r\n" << crash_trace();
		utils::io::write_file(stem + ".txt", report.str(), false);
		return exception::write_process_minidump(stem + ".dmp");
	}

	bool write_backend_stall_report(const d3d11::graphics_status& graphics,
		const engine_backend_probe::backend_watchdog_status& backend,
		const std::chrono::milliseconds age)
	{
		if (backend.sequence == 0 || backend.depth == 0 || backend.entered_tick == 0)
		{
			return false;
		}

		tm local_time{};
		char timestamp[64]{};
		const auto raw_time = _time64(nullptr);
		_localtime64_s(&local_time, &raw_time);
		strftime(timestamp, sizeof(timestamp), "%Y-%m-%d-%H-%M-%S", &local_time);
		const auto stem = std::string("minidumps/overlord-backend-stall-") + timestamp;
		std::ostringstream report;
		report << "Overlord backend CPU stall report\r\n"
			<< "Backend sequence: " << backend.sequence << "\r\n"
			<< "Backend thread: " << backend.thread_id << "\r\n"
			<< "Backend depth: " << backend.depth << "\r\n"
			<< "Backend phase: " << engine_backend_probe::to_string(backend.phase) << "\r\n"
			<< "Backend age: " << age.count() << " ms\r\n"
			<< "Backend entered tick: " << backend.entered_tick << "\r\n"
			<< "Backend last progress tick: " << backend.last_progress_tick << "\r\n"
			<< "Record: 0x" << std::hex << backend.record << "\r\n"
			<< "Command stream: 0x" << backend.command_stream << "\r\n"
			<< "Present active: " << (graphics.present_active ? "yes" : "no") << "\r\n"
			<< "Present thread: " << std::dec << graphics.present_thread_id << "\r\n"
			<< "Last Present thread: " << graphics.last_present_thread_id << "\r\n"
			<< "Last Present frame: " << graphics.last_present_frame_index << "\r\n"
			<< "Generation: " << graphics.generation << "\r\n"
			<< "Last Present result: 0x" << std::hex
			<< static_cast<std::uint32_t>(graphics.last_present_result) << "\r\n"
			<< "Device removed reason: 0x"
			<< static_cast<std::uint32_t>(graphics.device_removed_reason) << "\r\n";
		append_device_creation_evidence(report, graphics);
		report << "VR trace:\r\n" << crash_trace();
		utils::io::write_file(stem + ".txt", report.str(), false);
		return exception::write_process_minidump(stem + ".dmp");
	}

	bool write_live_trace_snapshot(const d3d11::graphics_status& graphics) noexcept
	{
		try
		{
			std::ostringstream report;
			report << "Overlord live trace snapshot\r\n"
				<< "Tick: " << GetTickCount64() << "\r\n"
				<< "Generation: " << graphics.generation << "\r\n"
				<< "Present active: " << (graphics.present_active ? "yes" : "no") << "\r\n"
				<< "Present thread: " << graphics.present_thread_id << "\r\n"
				<< "Last Present thread: " << graphics.last_present_thread_id << "\r\n"
				<< "Last Present frame: " << graphics.last_present_frame_index << "\r\n"
				<< "Present GPU scope active: " << (graphics.present_gpu_scope_active ? "yes" : "no") << "\r\n"
				<< "Present GPU scope thread: " << graphics.present_gpu_scope_thread_id << "\r\n"
				<< "Present GPU scope count: " << graphics.present_gpu_scope_count << "\r\n"
				<< "Present count: " << graphics.present_count << "\r\n"
				<< "Last Present result: 0x" << std::hex
				<< static_cast<std::uint32_t>(graphics.last_present_result) << "\r\n"
				<< "Recorded device removed reason: 0x"
				<< static_cast<std::uint32_t>(graphics.device_removed_reason) << "\r\n";
			append_device_creation_evidence(report, graphics);
			report << "VR trace:\r\n" << format_trace(live_view_trace_records);
			// Never truncate the last complete snapshot in place. A kernel bugcheck can
			// interrupt buffered I/O after truncation and previously left this file as
			// 18 KiB of zeros. Flush a sibling first, then atomically replace the final
			// path so a sudden reset preserves the preceding complete checkpoint.
			return utils::io::write_file_atomic(
				"minidumps/overlord-live-trace.txt", report.str());
		}
		catch (...)
		{
			return false;
		}
	}
}
