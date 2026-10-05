#include <std_include.hpp>

#include "engine_stereo_ssr_history_probe.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

namespace vr::engine_stereo_ssr_history_probe
{
	namespace
	{
		static_assert(current_view_projection_offset + sizeof(float) * 16 <= record_size);
		static_assert(current_origin_offset + sizeof(float) * 3 <= record_size);
		static_assert(history_view_projection_offset + sizeof(float) * 16 <= record_size);
		static_assert(history_origin_offset + sizeof(float) * 3 <= record_size);
		static_assert(previous_view_projection_constant_offset + sizeof(float) * 16 <=
			record_size);
		static_assert(previous_eye_position_constant_offset + sizeof(float) * 4 <=
			record_size);

		std::atomic<capture_state> state{capture_state::empty};
		std::atomic_uint64_t attempts{};
		std::atomic_uint64_t completions{};
		std::atomic_uint64_t failures{};
		report captured{};

		template <std::size_t Size>
		void read_floats(const std::uint8_t* const record, const std::size_t offset,
			std::array<float, Size>& output) noexcept
		{
			std::memcpy(output.data(), record + offset, sizeof(float) * Size);
		}

		template <typename Value>
		[[nodiscard]] bool bytes_equal(const Value& left, const Value& right) noexcept
		{
			return std::memcmp(&left, &right, sizeof(Value)) == 0;
		}

		[[nodiscard]] bool approximately_equal(const float left, const float right) noexcept
		{
			if (!std::isfinite(left) || !std::isfinite(right)) return false;
			if (std::memcmp(&left, &right, sizeof(left)) == 0) return true;
			const auto scale = std::max({1.0f, std::abs(left), std::abs(right)});
			return std::abs(left - right) <= 0.00001f * scale;
		}

		template <std::size_t Size>
		[[nodiscard]] bool all_finite(const std::array<float, Size>& values) noexcept
		{
			return std::all_of(values.begin(), values.end(), [](const float value)
			{
				return std::isfinite(value);
			});
		}

		void inspect_eye(const void* const opaque_record, eye_report& output) noexcept
		{
			const auto* const record = static_cast<const std::uint8_t*>(opaque_record);
			output.record = reinterpret_cast<std::uintptr_t>(record);
			read_floats(record, current_view_projection_offset,
				output.current_view_projection);
			read_floats(record, current_origin_offset, output.current_origin);
			read_floats(record, history_view_projection_offset,
				output.history_view_projection);
			read_floats(record, history_origin_offset, output.history_origin);
			read_floats(record, previous_view_projection_constant_offset,
				output.previous_view_projection_constant);
			read_floats(record, previous_eye_position_constant_offset,
				output.previous_eye_position_constant);

			output.finite = all_finite(output.current_view_projection) &&
				all_finite(output.current_origin) &&
				all_finite(output.history_view_projection) &&
				all_finite(output.history_origin) &&
				all_finite(output.previous_view_projection_constant) &&
				all_finite(output.previous_eye_position_constant);
			output.previous_view_projection_matches_history = bytes_equal(
				output.previous_view_projection_constant, output.history_view_projection);

			output.previous_eye_position_matches_raw_origin_delta = true;
			output.previous_eye_position_matches_scaled_origin_delta = true;
			const auto h2_scale = output.previous_eye_position_constant[3];
			for (std::size_t component{}; component < output.origin_delta.size(); ++component)
			{
				const auto delta = output.current_origin[component] -
					output.history_origin[component];
				output.origin_delta[component] = delta;
				output.previous_eye_position_matches_raw_origin_delta =
					output.previous_eye_position_matches_raw_origin_delta &&
					approximately_equal(output.previous_eye_position_constant[component], delta);
				output.previous_eye_position_matches_scaled_origin_delta =
					output.previous_eye_position_matches_scaled_origin_delta &&
					approximately_equal(output.previous_eye_position_constant[component],
						delta * h2_scale);
			}
			output.previous_eye_position_matches_scaled_origin_delta =
				output.previous_eye_position_matches_scaled_origin_delta &&
				std::isfinite(h2_scale);
		}
	}

	void observe(const std::uint64_t pair_id, const std::uint64_t publication,
		const void* const left_record, const void* const right_record) noexcept
	{
		if (state.load(std::memory_order_acquire) != capture_state::empty) return;
		attempts.fetch_add(1, std::memory_order_relaxed);
		if (pair_id == 0 || publication == 0 || left_record == nullptr ||
			right_record == nullptr)
		{
			failures.fetch_add(1, std::memory_order_relaxed);
			return;
		}

		auto expected = capture_state::empty;
		if (!state.compare_exchange_strong(expected, capture_state::writing,
			std::memory_order_acq_rel, std::memory_order_acquire)) return;

		report next{};
		next.state = capture_state::complete;
		next.pair_id = pair_id;
		next.publication = publication;
		inspect_eye(left_record, next.eyes[0]);
		inspect_eye(right_record, next.eyes[1]);
		next.current_view_projection_equal = bytes_equal(
			next.eyes[0].current_view_projection,
			next.eyes[1].current_view_projection);
		next.current_origin_equal = bytes_equal(next.eyes[0].current_origin,
			next.eyes[1].current_origin);
		next.history_view_projection_equal = bytes_equal(
			next.eyes[0].history_view_projection,
			next.eyes[1].history_view_projection);
		next.history_origin_equal = bytes_equal(next.eyes[0].history_origin,
			next.eyes[1].history_origin);
		next.previous_view_projection_constants_equal = bytes_equal(
			next.eyes[0].previous_view_projection_constant,
			next.eyes[1].previous_view_projection_constant);
		next.previous_eye_position_constants_equal = bytes_equal(
			next.eyes[0].previous_eye_position_constant,
			next.eyes[1].previous_eye_position_constant);

		const auto current_is_distinct = !next.current_view_projection_equal ||
			!next.current_origin_equal;
		const auto both_a0_match =
			next.eyes[0].previous_view_projection_matches_history &&
			next.eyes[1].previous_view_projection_matches_history;
		next.shared_matrix_history_gap = current_is_distinct &&
			next.history_view_projection_equal &&
			next.previous_view_projection_constants_equal && both_a0_match;
		next.distinct_per_eye_matrix_history = current_is_distinct &&
			!next.history_view_projection_equal &&
			!next.previous_view_projection_constants_equal && both_a0_match;
		next.history_origin_reliable_for_ssr = false;

		captured = next;
		completions.fetch_add(1, std::memory_order_relaxed);
		state.store(capture_state::complete, std::memory_order_release);
	}

	report get_report() noexcept
	{
		report output{};
		const auto current = state.load(std::memory_order_acquire);
		if (current == capture_state::complete)
		{
			output = captured;
		}
		output.state = current;
		output.attempts = attempts.load(std::memory_order_relaxed);
		output.completions = completions.load(std::memory_order_relaxed);
		output.failures = failures.load(std::memory_order_relaxed);
		return output;
	}

	const char* to_string(const capture_state value) noexcept
	{
		switch (value)
		{
		case capture_state::empty:
			return "empty";
		case capture_state::writing:
			return "writing";
		case capture_state::complete:
			return "complete";
		default:
			return "unknown";
		}
	}

	void reset() noexcept
	{
		state.store(capture_state::writing, std::memory_order_release);
		captured = {};
		attempts.store(0, std::memory_order_relaxed);
		completions.store(0, std::memory_order_relaxed);
		failures.store(0, std::memory_order_relaxed);
		state.store(capture_state::empty, std::memory_order_release);
	}
}
