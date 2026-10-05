#include <std_include.hpp>

#include "engine_stereo_probe.hpp"

#include <atomic>
#include <bit>
#include <cmath>
#include <cstring>

namespace vr::engine_stereo_probe
{
	namespace
	{
		constexpr std::size_t trace_capacity = 256;
		constexpr std::uint64_t fnv_offset_basis = 14695981039346656037ull;
		constexpr std::uint64_t fnv_prime = 1099511628211ull;

		struct trace_slot
		{
			std::atomic<std::uint64_t> version{};
			std::atomic<boundary_phase> phase{boundary_phase::enter};
			std::atomic<std::uint64_t> sequence{};
			std::atomic<std::uint64_t> renderer_frame_id{};
			std::atomic<std::uint64_t> timestamp_nanoseconds{};
			std::atomic<std::uint32_t> thread_id{};
			std::atomic<std::uint32_t> recursion_depth{};
			std::atomic_bool camera_valid{};
			std::atomic<std::uint64_t> camera_hash{};
		};

		std::atomic<mode> requested_mode{mode::off};
		std::atomic<target_gate> gate{target_gate::unconfigured};
		std::atomic<abi_state> abi{abi_state::unverified};
		std::atomic_bool active{};
		std::atomic_bool trace_enabled{};
		std::atomic<std::uint64_t> renderer_frame_count{};
		std::atomic<std::uint64_t> renderer_boundary_count{};
		std::atomic<std::uint64_t> present_count{};
		std::atomic<std::uint64_t> correlation_count{};
		std::atomic<std::uint64_t> correlation_miss_count{};
		std::atomic<std::uint64_t> thread_mismatch_count{};
		std::atomic<std::uint64_t> invalid_camera_count{};
		std::atomic<std::uint64_t> recursion_count{};
		std::atomic<std::uint64_t> trace_sequence{};
		std::atomic<std::uint64_t> resize_epoch{};
		std::atomic<std::uint64_t> device_generation{};
		std::atomic<std::uint64_t> last_renderer_frame_id{};
		std::atomic<std::uint64_t> last_renderer_timestamp{};
		std::atomic<std::uint64_t> last_present_frame_index{};
		std::atomic<std::uint64_t> last_camera_hash{};
		std::atomic<std::uint64_t> max_renderer_duration{};
		std::atomic<std::int64_t> last_correlation_microseconds{};
		std::atomic<std::uint32_t> renderer_thread_id{};
		std::atomic<std::uint32_t> present_thread_id{};
		std::atomic_bool last_camera_valid{};
		std::atomic_bool same_thread{};
		std::array<trace_slot, trace_capacity> trace{};
		thread_local std::uint32_t renderer_depth{};

		std::uint64_t now_nanoseconds() noexcept
		{
			return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count());
		}

		void update_max(std::atomic<std::uint64_t>& value, const std::uint64_t candidate) noexcept
		{
			auto current = value.load(std::memory_order_relaxed);
			while (current < candidate && !value.compare_exchange_weak(current, candidate,
				std::memory_order_relaxed, std::memory_order_relaxed))
			{
			}
		}

		std::uint64_t hash_camera(const shared_camera_sample& sample) noexcept
		{
			const auto* bytes = reinterpret_cast<const std::uint8_t*>(&sample);
			std::uint64_t hash = fnv_offset_basis;
			for (std::size_t index = 0; index < sizeof(sample); ++index)
			{
				hash ^= bytes[index];
				hash *= fnv_prime;
			}
			return hash;
		}

		void publish_trace(trace_event event) noexcept
		{
			if (!trace_enabled.load(std::memory_order_relaxed))
			{
				return;
			}

			event.sequence = trace_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
			auto& slot = trace[event.sequence % trace_capacity];
			const auto writing_version = event.sequence * 2 + 1;
			slot.version.store(writing_version, std::memory_order_release);
			slot.phase.store(event.phase, std::memory_order_relaxed);
			slot.sequence.store(event.sequence, std::memory_order_relaxed);
			slot.renderer_frame_id.store(event.renderer_frame_id, std::memory_order_relaxed);
			slot.timestamp_nanoseconds.store(event.timestamp_nanoseconds, std::memory_order_relaxed);
			slot.thread_id.store(event.thread_id, std::memory_order_relaxed);
			slot.recursion_depth.store(event.recursion_depth, std::memory_order_relaxed);
			slot.camera_valid.store(event.camera_valid, std::memory_order_relaxed);
			slot.camera_hash.store(event.camera_hash, std::memory_order_relaxed);
			slot.version.store(writing_version + 1, std::memory_order_release);
		}

		bool hash_equals(const std::string_view actual, const std::string_view expected) noexcept
		{
			if (actual.size() != expected.size() || actual.empty())
			{
				return false;
			}
			for (std::size_t index = 0; index < actual.size(); ++index)
			{
				const auto left = static_cast<unsigned char>(actual[index]);
				const auto right = static_cast<unsigned char>(expected[index]);
				if (std::toupper(left) != std::toupper(right))
				{
					return false;
				}
			}
			return true;
		}
	}

	void configure_target(const target_descriptor& descriptor) noexcept
	{
		constexpr std::string_view expected_original_hash =
			"9D554376FBE78248A423F6B776634453142FAFA1218645CD64B2499967512AAE";
		constexpr std::string_view expected_loaded_hash =
			"58CB70D63E05DC239997420C3ED91F04D3D324AF0BDF8A9623F1012164E0ED9D";
		constexpr std::uint32_t expected_timestamp = 0x6023ED08;
		constexpr std::uint32_t expected_image_size = 0x11E6BC00;

		const bool fingerprint_matches = hash_equals(descriptor.original_sha256, expected_original_hash) &&
			hash_equals(descriptor.loaded_sha256, expected_loaded_hash) &&
			descriptor.pe_timestamp == expected_timestamp && descriptor.image_size == expected_image_size;
		if (!fingerprint_matches)
		{
			gate.store(target_gate::fingerprint_mismatch, std::memory_order_release);
			abi.store(abi_state::unverified, std::memory_order_release);
			active.store(false, std::memory_order_release);
			return;
		}
		if (!descriptor.renderer_boundary_executable)
		{
			gate.store(target_gate::renderer_boundary_not_executable, std::memory_order_release);
			abi.store(abi_state::unverified, std::memory_order_release);
			active.store(false, std::memory_order_release);
			return;
		}
		if (!descriptor.scene_hook_installed)
		{
			gate.store(target_gate::scene_hook_not_installed, std::memory_order_release);
			abi.store(abi_state::unverified, std::memory_order_release);
			active.store(false, std::memory_order_release);
			return;
		}

		gate.store(target_gate::matched, std::memory_order_release);
		const auto requested = requested_mode.load(std::memory_order_acquire);
		abi.store(requested == mode::observe ? abi_state::observation_only :
			abi_state::unverified, std::memory_order_release);
		active.store(requested != mode::off, std::memory_order_release);
	}

	bool is_active() noexcept
	{
		return active.load(std::memory_order_acquire);
	}

	void set_mode(const mode requested) noexcept
	{
		requested_mode.store(requested, std::memory_order_release);
		const auto matched = gate.load(std::memory_order_acquire) == target_gate::matched;
		active.store(requested != mode::off && matched, std::memory_order_release);
		abi.store(matched && requested == mode::observe
			? abi_state::observation_only : abi_state::unverified,
			std::memory_order_release);
	}

	void set_trace_enabled(const bool enabled) noexcept
	{
		trace_enabled.store(enabled, std::memory_order_release);
	}

	void stop() noexcept
	{
		active.store(false, std::memory_order_release);
		requested_mode.store(mode::off, std::memory_order_release);
		abi.store(abi_state::unverified, std::memory_order_release);
	}

	void reset() noexcept
	{
		renderer_frame_count.store(0, std::memory_order_relaxed);
		renderer_boundary_count.store(0, std::memory_order_relaxed);
		present_count.store(0, std::memory_order_relaxed);
		correlation_count.store(0, std::memory_order_relaxed);
		correlation_miss_count.store(0, std::memory_order_relaxed);
		thread_mismatch_count.store(0, std::memory_order_relaxed);
		invalid_camera_count.store(0, std::memory_order_relaxed);
		recursion_count.store(0, std::memory_order_relaxed);
		trace_sequence.store(0, std::memory_order_relaxed);
		resize_epoch.store(0, std::memory_order_relaxed);
		device_generation.store(0, std::memory_order_relaxed);
		last_renderer_frame_id.store(0, std::memory_order_relaxed);
		last_renderer_timestamp.store(0, std::memory_order_relaxed);
		last_present_frame_index.store(0, std::memory_order_relaxed);
		last_camera_hash.store(0, std::memory_order_relaxed);
		max_renderer_duration.store(0, std::memory_order_relaxed);
		last_correlation_microseconds.store(0, std::memory_order_relaxed);
		renderer_thread_id.store(0, std::memory_order_relaxed);
		present_thread_id.store(0, std::memory_order_relaxed);
		last_camera_valid.store(false, std::memory_order_relaxed);
		same_thread.store(false, std::memory_order_relaxed);
		for (auto& slot : trace)
		{
			slot.version.store(0, std::memory_order_relaxed);
			slot.sequence.store(0, std::memory_order_relaxed);
		}
	}

	bool validate_camera_sample(const shared_camera_sample& sample) noexcept
	{
		const auto finite = [](const float value)
		{
			return std::isfinite(value);
		};
		if (!finite(sample.fov_x) || !finite(sample.fov_y) || sample.fov_x <= 0.0f || sample.fov_y <= 0.0f ||
			sample.fov_x > 180.0f || sample.fov_y > 180.0f)
		{
			return false;
		}
		for (const auto value : sample.origin)
		{
			if (!finite(value)) return false;
		}
		for (const auto& row : sample.axis)
		{
			for (const auto value : row)
			{
				if (!finite(value) || std::abs(value) > 2.0f) return false;
			}
		}
		for (const auto value : sample.view_angles)
		{
			if (!finite(value)) return false;
		}
		return true;
	}

	renderer_frame_token begin_renderer_frame(const shared_camera_sample& sample) noexcept
	{
		if (!active.load(std::memory_order_acquire)) return {};
		const auto frame_id = renderer_frame_count.fetch_add(1, std::memory_order_relaxed) + 1;
		const auto timestamp = now_nanoseconds();
		const auto thread_id = GetCurrentThreadId();
		const auto depth = ++renderer_depth;
		if (depth > 1) recursion_count.fetch_add(1, std::memory_order_relaxed);
		const auto valid = validate_camera_sample(sample);
		const auto camera_hash = hash_camera(sample);
		if (!valid) invalid_camera_count.fetch_add(1, std::memory_order_relaxed);
		publish_trace({boundary_phase::enter, 0, frame_id, timestamp, thread_id, depth, valid, camera_hash});
		renderer_boundary_count.fetch_add(1, std::memory_order_relaxed);
		return {frame_id, timestamp, thread_id, depth, valid, camera_hash};
	}

	void record_renderer_boundary(const renderer_frame_token& token, const boundary_phase phase) noexcept
	{
		if (!token || !active.load(std::memory_order_acquire)) return;
		publish_trace({phase, 0, token.frame_id, now_nanoseconds(), GetCurrentThreadId(),
			token.recursion_depth, token.camera_valid, token.camera_hash});
		renderer_boundary_count.fetch_add(1, std::memory_order_relaxed);
	}

	void end_renderer_frame(const renderer_frame_token& token) noexcept
	{
		if (!token) return;
		const auto timestamp = now_nanoseconds();
		update_max(max_renderer_duration, timestamp - token.started_nanoseconds);
		record_renderer_boundary(token, boundary_phase::after_original);
		renderer_thread_id.store(token.thread_id, std::memory_order_relaxed);
		last_camera_hash.store(token.camera_hash, std::memory_order_relaxed);
		last_camera_valid.store(token.camera_valid, std::memory_order_relaxed);
		last_renderer_timestamp.store(timestamp, std::memory_order_relaxed);
		last_renderer_frame_id.store(token.frame_id, std::memory_order_release);
		if (renderer_depth > 0) --renderer_depth;
	}

	void record_present(const std::uint64_t present_frame_index, const std::uint64_t generation) noexcept
	{
		if (!active.load(std::memory_order_acquire)) return;
		present_count.fetch_add(1, std::memory_order_relaxed);
		last_present_frame_index.store(present_frame_index, std::memory_order_relaxed);
		device_generation.store(generation, std::memory_order_relaxed);
		const auto present_thread = GetCurrentThreadId();
		present_thread_id.store(present_thread, std::memory_order_relaxed);
		const auto renderer_frame = last_renderer_frame_id.load(std::memory_order_acquire);
		const auto renderer_timestamp = last_renderer_timestamp.load(std::memory_order_acquire);
		if (renderer_frame == 0 || renderer_timestamp == 0)
		{
			correlation_miss_count.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		const auto current_time = now_nanoseconds();
		last_correlation_microseconds.store(static_cast<std::int64_t>((current_time - renderer_timestamp) / 1000),
			std::memory_order_relaxed);
		const bool matching_thread = renderer_thread_id.load(std::memory_order_relaxed) == present_thread;
		same_thread.store(matching_thread, std::memory_order_relaxed);
		if (!matching_thread) thread_mismatch_count.fetch_add(1, std::memory_order_relaxed);
		correlation_count.fetch_add(1, std::memory_order_relaxed);
	}

	void record_resize() noexcept
	{
		resize_epoch.fetch_add(1, std::memory_order_relaxed);
		last_renderer_frame_id.store(0, std::memory_order_release);
		last_renderer_timestamp.store(0, std::memory_order_release);
	}

	void record_device_destroying(const std::uint64_t generation) noexcept
	{
		device_generation.store(generation, std::memory_order_relaxed);
		record_resize();
	}

	status get_status() noexcept
	{
		status result;
		result.requested_mode = requested_mode.load(std::memory_order_acquire);
		result.gate = gate.load(std::memory_order_acquire);
		result.abi = abi.load(std::memory_order_acquire);
		result.active = active.load(std::memory_order_acquire);
		result.trace_enabled = trace_enabled.load(std::memory_order_acquire);
		result.renderer_frame_count = renderer_frame_count.load(std::memory_order_relaxed);
		result.renderer_boundary_count = renderer_boundary_count.load(std::memory_order_relaxed);
		result.present_count = present_count.load(std::memory_order_relaxed);
		result.correlation_count = correlation_count.load(std::memory_order_relaxed);
		result.correlation_miss_count = correlation_miss_count.load(std::memory_order_relaxed);
		result.thread_mismatch_count = thread_mismatch_count.load(std::memory_order_relaxed);
		result.invalid_camera_count = invalid_camera_count.load(std::memory_order_relaxed);
		result.recursion_count = recursion_count.load(std::memory_order_relaxed);
		const auto sequence = trace_sequence.load(std::memory_order_relaxed);
		result.trace_overwrite_count = sequence > trace_capacity ? sequence - trace_capacity : 0;
		result.resize_epoch = resize_epoch.load(std::memory_order_relaxed);
		result.device_generation = device_generation.load(std::memory_order_relaxed);
		result.last_renderer_frame_id = last_renderer_frame_id.load(std::memory_order_relaxed);
		result.last_present_frame_index = last_present_frame_index.load(std::memory_order_relaxed);
		result.last_camera_hash = last_camera_hash.load(std::memory_order_relaxed);
		result.max_renderer_duration_nanoseconds = max_renderer_duration.load(std::memory_order_relaxed);
		result.last_renderer_to_present_microseconds = last_correlation_microseconds.load(std::memory_order_relaxed);
		result.renderer_thread_id = renderer_thread_id.load(std::memory_order_relaxed);
		result.present_thread_id = present_thread_id.load(std::memory_order_relaxed);
		result.last_camera_valid = last_camera_valid.load(std::memory_order_relaxed);
		result.same_thread = same_thread.load(std::memory_order_relaxed);
		return result;
	}

	std::size_t read_recent_events(trace_event* const output, const std::size_t capacity) noexcept
	{
		if (output == nullptr || capacity == 0) return 0;
		const auto latest = trace_sequence.load(std::memory_order_acquire);
		const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(latest,
			std::min<std::uint64_t>(trace_capacity, capacity)));
		const auto first = latest >= count ? latest - count + 1 : 1;
		std::size_t copied{};
		for (auto sequence = first; sequence <= latest; ++sequence)
		{
			const auto& slot = trace[sequence % trace_capacity];
			const auto before = slot.version.load(std::memory_order_acquire);
			if ((before & 1) != 0 || before != sequence * 2 + 2) continue;
			const trace_event event{
				slot.phase.load(std::memory_order_relaxed),
				slot.sequence.load(std::memory_order_relaxed),
				slot.renderer_frame_id.load(std::memory_order_relaxed),
				slot.timestamp_nanoseconds.load(std::memory_order_relaxed),
				slot.thread_id.load(std::memory_order_relaxed),
				slot.recursion_depth.load(std::memory_order_relaxed),
				slot.camera_valid.load(std::memory_order_relaxed),
				slot.camera_hash.load(std::memory_order_relaxed),
			};
			const auto after = slot.version.load(std::memory_order_acquire);
			if (before == after)
			{
				output[copied++] = event;
			}
		}
		return copied;
	}

	const char* to_string(const mode value) noexcept
	{
		switch (value) { case mode::off: return "off"; case mode::observe: return "observe";
		default: return "unknown"; }
	}

	const char* to_string(const target_gate value) noexcept
	{
		switch (value) { case target_gate::unconfigured: return "unconfigured"; case target_gate::matched: return "matched";
		case target_gate::fingerprint_mismatch: return "fingerprint_mismatch";
		case target_gate::renderer_boundary_not_executable: return "renderer_boundary_not_executable";
		case target_gate::scene_hook_not_installed: return "scene_hook_not_installed"; default: return "unknown"; }
	}

	const char* to_string(const abi_state value) noexcept
	{
		switch (value) { case abi_state::unverified: return "unverified";
		case abi_state::observation_only: return "observation_only";
		default: return "unknown"; }
	}

	const char* to_string(const boundary_phase value) noexcept
	{
		switch (value) { case boundary_phase::enter: return "enter";
		case boundary_phase::before_original: return "before_original";
		case boundary_phase::after_original: return "after_original"; default: return "unknown"; }
	}
}
