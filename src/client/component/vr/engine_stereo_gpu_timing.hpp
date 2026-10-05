#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <d3d11.h>

namespace vr::engine_stereo_gpu_timing
{
	// Timestamp/disjoint queries are a diagnostic experiment, not part of the
	// native stereo transport contract. They must never arm implicitly in the
	// production client: on the real H2/NVIDIA path the old automatic one-shot
	// began exactly at the eighth retired pair and added otherwise unnecessary
	// GPU commands to a stable frame. The dedicated probe target defines
	// H2VR_GPU_TIMING_TESTING and continues to exercise the full implementation.
#ifdef H2VR_GPU_TIMING_TESTING
	inline constexpr bool instrumentation_enabled = true;
#else
	inline constexpr bool instrumentation_enabled = false;
#endif

	inline constexpr std::size_t timestamp_count = 8;
	inline constexpr std::uint64_t minimum_stable_pair_releases = 8;
	inline constexpr std::uint32_t maximum_retirement_polls = 8;

	enum class capture_state : std::uint8_t
	{
		unprepared,
		ready,
		recording,
		pending,
		complete,
		unavailable,
	};

	enum class failure : std::uint8_t
	{
		none,
		invalid_argument,
		query_creation,
		device_generation,
		context,
		thread,
		pair,
		sequence,
		incomplete_pair,
		native_ring,
		native_conversion,
		query,
		query_timeout,
		disjoint,
		timestamp_order,
	};

	enum class timestamp_point : std::uint8_t
	{
		left_owner_begin,
		left_owner_end,
		left_conversion_begin,
		left_conversion_end,
		right_owner_begin,
		right_owner_end,
		right_conversion_begin,
		right_conversion_end,
	};

	struct report
	{
		capture_state state{capture_state::unprepared};
		failure error{failure::none};
		std::uint64_t prepare_attempts{};
		std::uint64_t prepare_completions{};
		std::uint64_t prepare_failures{};
		std::uint64_t eligibility_checks{};
		std::uint64_t eligibility_skips{};
		std::uint64_t capture_attempts{};
		std::uint64_t capture_completions{};
		std::uint64_t capture_failures{};
		std::uint64_t pair_id{};
		std::uint64_t device_generation{};
		std::uintptr_t device{};
		std::uintptr_t context{};
		std::uint32_t owner_thread{};
		std::uint32_t marker_mask{};
		std::uint64_t retired_pair{};
		bool retirement_observed{};
		std::uint32_t retirement_polls{};
		std::uint32_t not_ready_polls{};
		std::uint64_t get_data_calls{};
		std::uint32_t get_data_flags{};
		HRESULT disjoint_create_result{E_PENDING};
		std::array<HRESULT, timestamp_count> timestamp_create_results{};
		HRESULT disjoint_get_data_result{E_PENDING};
		std::array<HRESULT, timestamp_count> timestamp_get_data_results{};
		bool disjoint{};
		std::uint64_t frequency{};
		std::array<std::uint64_t, timestamp_count> timestamps{};
		std::uint64_t left_owner_ns{};
		std::uint64_t left_conversion_ns{};
		std::uint64_t right_owner_ns{};
		std::uint64_t right_conversion_ns{};
		std::uint64_t owner_to_left_conversion_ns{};
		std::uint64_t between_eyes_ns{};
		std::uint64_t owner_to_right_conversion_ns{};
		std::uint64_t pair_window_ns{};
	};

	// Allocates the fixed query set before an eligible pair begins. Failure is
	// diagnostic-only and must never change native-render-session admission.
	[[nodiscard]] bool prepare_device(ID3D11Device* device,
		ID3D11DeviceContext* context, std::uint64_t device_generation) noexcept;

	// Starts exactly one pair after the caller has proved that the same native
	// ring already retired enough complete pairs to be stable. An ineligible pair
	// leaves the prepared one-shot untouched.
	[[nodiscard]] bool begin_pair(std::uint64_t pair_id,
		ID3D11DeviceContext* context, std::uint64_t device_generation,
		bool stable_native_transport) noexcept;
	[[nodiscard]] bool begin_owner(std::uint64_t pair_id, std::uint32_t eye,
		ID3D11DeviceContext* context, std::uint64_t device_generation) noexcept;
	[[nodiscard]] bool end_owner(std::uint64_t pair_id, std::uint32_t eye,
		ID3D11DeviceContext* context, std::uint64_t device_generation) noexcept;
	[[nodiscard]] bool begin_native_conversion(std::uint64_t pair_id,
		std::uint32_t eye, ID3D11DeviceContext* context,
		std::uint64_t device_generation) noexcept;
	[[nodiscard]] bool end_native_conversion(std::uint64_t pair_id,
		std::uint32_t eye, ID3D11DeviceContext* context,
		std::uint64_t device_generation) noexcept;

	// The renderer closes the logical transaction after native publication has
	// either succeeded or become terminal. An incomplete sampled pair permanently
	// disables this one-shot, but never changes production state.
	void finish_pair(std::uint64_t pair_id, ID3D11DeviceContext* context,
		std::uint64_t device_generation, bool successfully_published) noexcept;
	void fail_pair(std::uint64_t pair_id, ID3D11DeviceContext* context,
		std::uint64_t device_generation, failure reason) noexcept;

	// Called only from a proven native-resource retirement boundary. Every query
	// read uses DONOTFLUSH and returns immediately when the GPU has not completed.
	void poll_retired_pair(std::uint64_t retired_pair_id,
		ID3D11DeviceContext* context, std::uint64_t device_generation) noexcept;
	void invalidate_device(ID3D11DeviceContext* context,
		std::uint64_t device_generation) noexcept;

	[[nodiscard]] report get_report() noexcept;
	[[nodiscard]] const char* to_string(capture_state value) noexcept;
	[[nodiscard]] const char* to_string(failure value) noexcept;

#ifdef H2VR_GPU_TIMING_TESTING
	// Test processes are quiescent when resetting. Production is permanently
	// one-shot and does not expose a reset path.
	void reset_for_tests() noexcept;
#endif
}
