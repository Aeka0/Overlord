#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace vr::engine_stereo_ssr_history_probe
{
	inline constexpr std::size_t record_size = 0x8090;
	inline constexpr std::size_t current_view_projection_offset = 0x80;
	inline constexpr std::size_t current_origin_offset = 0x100;
	inline constexpr std::size_t history_view_projection_offset = 0x2D40;
	inline constexpr std::size_t history_origin_offset = 0x2DC0;
	inline constexpr std::size_t previous_view_projection_constant_offset = 0xF90;
	inline constexpr std::size_t previous_eye_position_constant_offset = 0xFD0;

	enum class capture_state : std::uint8_t
	{
		empty,
		writing,
		complete,
	};

	struct eye_report
	{
		std::uintptr_t record{};
		std::array<float, 16> current_view_projection{};
		std::array<float, 3> current_origin{};
		std::array<float, 16> history_view_projection{};
		std::array<float, 3> history_origin{};
		std::array<float, 16> previous_view_projection_constant{};
		std::array<float, 4> previous_eye_position_constant{};
		std::array<float, 3> origin_delta{};
		bool finite{};
		bool previous_view_projection_matches_history{};
		bool previous_eye_position_matches_raw_origin_delta{};
		bool previous_eye_position_matches_scaled_origin_delta{};
	};

	struct report
	{
		capture_state state{capture_state::empty};
		std::uint64_t attempts{};
		std::uint64_t completions{};
		std::uint64_t failures{};
		std::uint64_t pair_id{};
		std::uint64_t publication{};
		std::array<eye_report, 2> eyes{};
		bool current_view_projection_equal{};
		bool current_origin_equal{};
		bool history_view_projection_equal{};
		bool history_origin_equal{};
		bool previous_view_projection_constants_equal{};
		bool previous_eye_position_constants_equal{};
		bool shared_matrix_history_gap{};
		bool distinct_per_eye_matrix_history{};
		// clone_scene_records has already repurposed record+0x2DC0 for the
		// XModel rebase by the time this observer runs. It is captured as evidence,
		// but must not be treated as an authoritative SSR previous-eye source.
		bool history_origin_reliable_for_ssr{};
	};

	// Captures exactly one cloned pair. This function only reads CPU memory; it
	// never calls H2/D3D/OpenVR and never writes either record.
	void observe(std::uint64_t pair_id, std::uint64_t publication,
		const void* left_record, const void* right_record) noexcept;
	[[nodiscard]] report get_report() noexcept;
	[[nodiscard]] const char* to_string(capture_state value) noexcept;

	// Intended for a quiescent unit-test process. Production never resets the
	// one-shot capture, which keeps get_report() lock-free after publication.
	void reset() noexcept;
}
