#include "std_include.hpp"

#include "component/vr/engine_stereo_ssr_history_probe.hpp"
#include "component/vr/engine_stereo_view.hpp"

#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>

namespace
{
	// These values are intentionally independent of the production probe
	// constants. A shared bad declaration must not make this test self-proving.
	constexpr std::size_t expected_current_view_projection_offset = 0x80;
	constexpr std::size_t expected_current_origin_offset = 0x100;
	constexpr std::size_t expected_current_axis_offset = 0x10C;
	constexpr std::size_t expected_history_view_projection_offset = 0x2D40;
	constexpr std::size_t expected_history_origin_offset = 0x2DC0;
	constexpr std::size_t expected_previous_view_projection_constant_offset = 0xF90;
	constexpr std::size_t expected_previous_eye_position_constant_offset = 0xFD0;
	static_assert(vr::engine_stereo_ssr_history_probe::current_view_projection_offset ==
		expected_current_view_projection_offset);
	static_assert(vr::engine_stereo_ssr_history_probe::current_origin_offset ==
		expected_current_origin_offset);
	static_assert(vr::engine_stereo_ssr_history_probe::history_view_projection_offset ==
		expected_history_view_projection_offset);
	static_assert(vr::engine_stereo_ssr_history_probe::history_origin_offset ==
		expected_history_origin_offset);
	static_assert(vr::engine_stereo_ssr_history_probe::previous_view_projection_constant_offset ==
		expected_previous_view_projection_constant_offset);
	static_assert(vr::engine_stereo_ssr_history_probe::previous_eye_position_constant_offset ==
		expected_previous_eye_position_constant_offset);
	static_assert(vr::engine_stereo_ssr_history_probe::current_origin_offset +
		sizeof(float) * 3 == expected_current_axis_offset);

	using record = std::array<std::uint8_t,
		vr::engine_stereo_ssr_history_probe::record_size>;

	[[noreturn]] void fail(const char* const message)
	{
		std::cerr << "vr-engine-ssr-history-probe: FAIL; " << message << '\n';
		std::exit(1);
	}

	void require(const bool condition, const char* const message)
	{
		if (!condition) fail(message);
	}

	template <std::size_t Size>
	void write_floats(record& destination, const std::size_t offset,
		const std::array<float, Size>& values)
	{
		std::memcpy(destination.data() + offset, values.data(), sizeof(float) * Size);
	}

	std::array<float, 16> matrix(const float seed)
	{
		std::array<float, 16> output{};
		for (std::size_t index{}; index < output.size(); ++index)
		{
			output[index] = seed + static_cast<float>(index) * 0.25f;
		}
		return output;
	}

	void prepare_eye(record& destination, const std::array<float, 16>& current,
		const std::array<float, 3>& current_origin,
		const std::array<float, 16>& history,
		const std::array<float, 3>& history_origin, const float scale)
	{
		using namespace vr::engine_stereo_ssr_history_probe;
		write_floats(destination, expected_current_view_projection_offset, current);
		write_floats(destination, expected_current_origin_offset, current_origin);
		write_floats(destination, expected_history_view_projection_offset, history);
		write_floats(destination, expected_history_origin_offset, history_origin);
		write_floats(destination, expected_previous_view_projection_constant_offset,
			history);
		std::array<float, 4> previous_eye{
			(current_origin[0] - history_origin[0]) * scale,
			(current_origin[1] - history_origin[1]) * scale,
			(current_origin[2] - history_origin[2]) * scale,
			scale,
		};
		write_floats(destination, expected_previous_eye_position_constant_offset,
			previous_eye);
	}

	void expect_shared_history_gap()
	{
		using namespace vr::engine_stereo_ssr_history_probe;
		reset();
		record left{};
		record right{};
		const auto shared_history = matrix(30.0f);
		const std::array<float, 3> shared_origin{10.0f, 20.0f, 30.0f};
		prepare_eye(left, matrix(1.0f), {11.0f, 21.0f, 31.0f},
			shared_history, shared_origin, 0.5f);
		prepare_eye(right, matrix(2.0f), {9.0f, 19.0f, 29.0f},
			shared_history, shared_origin, 0.5f);
		const auto axis_canary = (std::numeric_limits<float>::quiet_NaN)();
		write_floats(left, expected_current_axis_offset,
			std::array<float, 1>{axis_canary});
		write_floats(right, expected_current_axis_offset,
			std::array<float, 1>{axis_canary});
		write_floats(left, expected_history_origin_offset + sizeof(float) * 3,
			std::array<float, 1>{axis_canary});
		write_floats(right, expected_history_origin_offset + sizeof(float) * 3,
			std::array<float, 1>{axis_canary});
		const auto left_before = left;
		const auto right_before = right;
		observe(101, 202, left.data(), right.data());
		const auto result = get_report();
		require(result.state == capture_state::complete && result.attempts == 1 &&
			result.completions == 1 && result.failures == 0,
			"shared-history capture did not complete exactly once");
		require(result.shared_matrix_history_gap &&
			!result.distinct_per_eye_matrix_history,
			"shared history with distinct current views was not classified as a gap");
		require(!result.current_view_projection_equal &&
			result.history_view_projection_equal &&
			result.previous_view_projection_constants_equal,
			"shared-history cross-eye equality fields are inconsistent");
		for (const auto& eye : result.eyes)
		{
			require(eye.finite && eye.previous_view_projection_matches_history &&
				eye.previous_eye_position_matches_scaled_origin_delta,
				"origin sampling crossed into the axis canary or the eye-local "
				"constant relation was not recognized");
		}
		require(left == left_before && right == right_before,
			"observer modified a source record");
	}

	void expect_distinct_per_eye_history()
	{
		using namespace vr::engine_stereo_ssr_history_probe;
		reset();
		record left{};
		record right{};
		prepare_eye(left, matrix(1.0f), {11.0f, 21.0f, 31.0f},
			matrix(40.0f), {10.0f, 20.0f, 30.0f}, 0.25f);
		prepare_eye(right, matrix(2.0f), {9.0f, 19.0f, 29.0f},
			matrix(50.0f), {8.0f, 18.0f, 28.0f}, 0.25f);
		observe(303, 404, left.data(), right.data());
		const auto result = get_report();
		require(!result.shared_matrix_history_gap &&
			result.distinct_per_eye_matrix_history,
			"distinct per-eye history was not recognized");
		require(!result.history_view_projection_equal &&
			!result.previous_view_projection_constants_equal,
			"distinct per-eye history equality fields are inconsistent");
		for (const auto& eye : result.eyes)
		{
			require(eye.previous_view_projection_matches_history &&
				eye.previous_eye_position_matches_scaled_origin_delta,
				"normal per-eye constant relation was not recognized");
		}
	}

	void expect_post_clone_origin_warning()
	{
		using namespace vr::engine_stereo_ssr_history_probe;
		reset();
		record left{};
		record right{};
		prepare_eye(left, matrix(1.0f), {11.0f, 21.0f, 31.0f},
			matrix(40.0f), {10.0f, 20.0f, 30.0f}, 0.5f);
		prepare_eye(right, matrix(2.0f), {9.0f, 19.0f, 29.0f},
			matrix(50.0f), {8.0f, 18.0f, 28.0f}, 0.5f);
		const std::array<float, 3> rewritten_rebase{100.0f, 200.0f, 300.0f};
		write_floats(right, history_origin_offset, rewritten_rebase);
		observe(505, 606, left.data(), right.data());
		const auto result = get_report();
		require(!result.history_origin_reliable_for_ssr,
			"post-clone history-origin warning was lost");
		require(!result.eyes[1].previous_eye_position_matches_scaled_origin_delta,
			"rewritten XModel rebase was misclassified as valid SSR history");
	}

	void expect_temporal_history_namespace()
	{
		using namespace vr::engine_stereo_view;
		temporal_history_state committed{};
		scene_record_pair first{};
		first.pair_id = 10;
		first.publication = 10;
		const auto left_current = matrix(1.0f);
		const auto right_current = matrix(2.0f);
		const std::array<float, 3> left_origin{10.0f, 20.0f, 30.0f};
		const std::array<float, 3> right_origin{11.0f, 21.0f, 31.0f};
		std::memcpy(first.left.data() + h2_current_view_projection_offset,
			left_current.data(), sizeof(left_current));
		std::memcpy(first.right.data() + h2_current_view_projection_offset,
			right_current.data(), sizeof(right_current));
		std::memcpy(first.left.data() + h2_view_origin_offset,
			left_origin.data(), sizeof(left_origin));
		std::memcpy(first.right.data() + h2_view_origin_offset,
			right_origin.data(), sizeof(right_origin));
		constexpr float scale = 0.5f;
		std::memcpy(first.left.data() + h2_previous_eye_position_constant_offset +
			sizeof(float) * 3, &scale, sizeof(scale));
		std::memcpy(first.right.data() + h2_previous_eye_position_constant_offset +
			sizeof(float) * 3, &scale, sizeof(scale));
		const std::array<std::uint8_t, 12> rebase_canary{
			1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
		std::memcpy(first.left.data() + expected_history_origin_offset,
			rebase_canary.data(), rebase_canary.size());
		std::memcpy(first.right.data() + expected_history_origin_offset,
			rebase_canary.data(), rebase_canary.size());

		temporal_history_preparation first_prepared{};
		require(prepare_temporal_history(first, 7, committed, first_prepared) &&
			first_prepared.valid && first_prepared.seeded,
			"first temporal pair was not seeded");
		require(std::memcmp(first.left.data() +
			h2_temporal_history_view_projection_offset, left_current.data(),
			sizeof(left_current)) == 0 &&
			std::memcmp(first.right.data() +
				h2_temporal_history_view_projection_offset, right_current.data(),
				sizeof(right_current)) == 0,
			"first temporal pair borrowed the other eye");
		require(std::memcmp(first.left.data() + expected_history_origin_offset,
			rebase_canary.data(), rebase_canary.size()) == 0 &&
			std::memcmp(first.right.data() + expected_history_origin_offset,
				rebase_canary.data(), rebase_canary.size()) == 0,
			"temporal injection modified the XModel rebase origin");
		require(commit_temporal_history(committed, first_prepared),
			"first temporal pair did not commit");

		scene_record_pair second{};
		second.pair_id = 14;
		second.publication = 14;
		const auto next_left = matrix(3.0f);
		const auto next_right = matrix(4.0f);
		const std::array<float, 3> next_left_origin{12.0f, 24.0f, 36.0f};
		const std::array<float, 3> next_right_origin{14.0f, 27.0f, 40.0f};
		std::memcpy(second.left.data() + h2_current_view_projection_offset,
			next_left.data(), sizeof(next_left));
		std::memcpy(second.right.data() + h2_current_view_projection_offset,
			next_right.data(), sizeof(next_right));
		std::memcpy(second.left.data() + h2_view_origin_offset,
			next_left_origin.data(), sizeof(next_left_origin));
		std::memcpy(second.right.data() + h2_view_origin_offset,
			next_right_origin.data(), sizeof(next_right_origin));
		std::memcpy(second.left.data() + h2_previous_eye_position_constant_offset +
			sizeof(float) * 3, &scale, sizeof(scale));
		std::memcpy(second.right.data() + h2_previous_eye_position_constant_offset +
			sizeof(float) * 3, &scale, sizeof(scale));
		temporal_history_preparation second_prepared{};
		require(prepare_temporal_history(second, 7, committed, second_prepared) &&
			second_prepared.valid && !second_prepared.seeded,
			"second temporal pair did not consume committed per-eye history");
		require(std::memcmp(second.left.data() +
			h2_previous_view_projection_constant_offset, left_current.data(),
			sizeof(left_current)) == 0 &&
			std::memcmp(second.right.data() +
				h2_previous_view_projection_constant_offset, right_current.data(),
				sizeof(right_current)) == 0,
			"second temporal pair did not retain output-eye identity");
		std::array<float, 4> left_delta{};
		std::array<float, 4> right_delta{};
		std::memcpy(left_delta.data(), second.left.data() +
			h2_previous_eye_position_constant_offset, sizeof(left_delta));
		std::memcpy(right_delta.data(), second.right.data() +
			h2_previous_eye_position_constant_offset, sizeof(right_delta));
		require(left_delta == std::array<float, 4>{1.0f, 2.0f, 3.0f, scale} &&
			right_delta == std::array<float, 4>{1.5f, 3.0f, 4.5f, scale},
			"per-eye temporal origin delta is incorrect");
		record natural{};
		require(copy_eye_local_record_fields(natural.data(), second.left) &&
			std::memcmp(natural.data() + h2_previous_view_projection_constant_offset,
				left_current.data(), sizeof(left_current)) == 0,
			"natural left record did not receive the exact temporal ranges");
	}
}

int main()
{
	expect_shared_history_gap();
	expect_distinct_per_eye_history();
	expect_post_clone_origin_warning();
	expect_temporal_history_namespace();
	std::cout << "vr-engine-ssr-history-probe: PASS\n";
	return 0;
}
