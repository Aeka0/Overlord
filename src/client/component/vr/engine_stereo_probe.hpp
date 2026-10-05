#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace vr::engine_stereo_probe
{
	enum class mode : std::uint8_t
	{
		off,
		observe,
	};

	enum class target_gate : std::uint8_t
	{
		unconfigured,
		matched,
		fingerprint_mismatch,
		renderer_boundary_not_executable,
		scene_hook_not_installed,
	};

	enum class abi_state : std::uint8_t
	{
		unverified,
		observation_only,
	};

	enum class boundary_phase : std::uint8_t
	{
		enter,
		before_original,
		after_original,
	};

	struct target_descriptor
	{
		std::string_view original_sha256;
		std::string_view loaded_sha256;
		std::uint32_t pe_timestamp{};
		std::uint32_t image_size{};
		bool renderer_boundary_executable{};
		bool scene_hook_installed{};
	};

	struct shared_camera_sample
	{
		float fov_x{};
		float fov_y{};
		std::array<float, 3> origin{};
		std::array<std::array<float, 3>, 3> axis{};
		std::array<float, 3> view_angles{};
	};

	struct renderer_frame_token
	{
		std::uint64_t frame_id{};
		std::uint64_t started_nanoseconds{};
		std::uint32_t thread_id{};
		std::uint32_t recursion_depth{};
		bool camera_valid{};
		std::uint64_t camera_hash{};

		[[nodiscard]] explicit operator bool() const noexcept
		{
			return frame_id != 0;
		}
	};

	struct trace_event
	{
		boundary_phase phase{boundary_phase::enter};
		std::uint64_t sequence{};
		std::uint64_t renderer_frame_id{};
		std::uint64_t timestamp_nanoseconds{};
		std::uint32_t thread_id{};
		std::uint32_t recursion_depth{};
		bool camera_valid{};
		std::uint64_t camera_hash{};
	};

	struct status
	{
		mode requested_mode{mode::off};
		target_gate gate{target_gate::unconfigured};
		abi_state abi{abi_state::unverified};
		bool active{};
		bool trace_enabled{};
		std::uint64_t renderer_frame_count{};
		std::uint64_t renderer_boundary_count{};
		std::uint64_t present_count{};
		std::uint64_t correlation_count{};
		std::uint64_t correlation_miss_count{};
		std::uint64_t thread_mismatch_count{};
		std::uint64_t invalid_camera_count{};
		std::uint64_t recursion_count{};
		std::uint64_t trace_overwrite_count{};
		std::uint64_t resize_epoch{};
		std::uint64_t device_generation{};
		std::uint64_t last_renderer_frame_id{};
		std::uint64_t last_present_frame_index{};
		std::uint64_t last_camera_hash{};
		std::uint64_t max_renderer_duration_nanoseconds{};
		std::int64_t last_renderer_to_present_microseconds{};
		std::uint32_t renderer_thread_id{};
		std::uint32_t present_thread_id{};
		bool last_camera_valid{};
		bool same_thread{};
	};

	void configure_target(const target_descriptor& descriptor) noexcept;
	[[nodiscard]] bool is_active() noexcept;
	void set_mode(mode requested) noexcept;
	void set_trace_enabled(bool enabled) noexcept;
	void stop() noexcept;
	void reset() noexcept;

	[[nodiscard]] renderer_frame_token begin_renderer_frame(const shared_camera_sample& sample) noexcept;
	void record_renderer_boundary(const renderer_frame_token& token, boundary_phase phase) noexcept;
	void end_renderer_frame(const renderer_frame_token& token) noexcept;
	void record_present(std::uint64_t present_frame_index, std::uint64_t device_generation) noexcept;
	void record_resize() noexcept;
	void record_device_destroying(std::uint64_t device_generation) noexcept;

	[[nodiscard]] status get_status() noexcept;
	[[nodiscard]] std::size_t read_recent_events(trace_event* output, std::size_t capacity) noexcept;
	[[nodiscard]] bool validate_camera_sample(const shared_camera_sample& sample) noexcept;
	[[nodiscard]] const char* to_string(mode value) noexcept;
	[[nodiscard]] const char* to_string(target_gate value) noexcept;
	[[nodiscard]] const char* to_string(abi_state value) noexcept;
	[[nodiscard]] const char* to_string(boundary_phase value) noexcept;
}
