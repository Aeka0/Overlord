#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace vr::engine_stereo_ssr_consumer_window
{
	inline constexpr std::uint32_t minimum_sampled_pairs = 2;
	inline constexpr std::uint32_t maximum_sampled_pairs = 4;
	inline constexpr std::uint32_t maximum_successful_pairs_without_sample = 256;
	inline constexpr std::size_t maximum_candidates_per_eye = 24;
	inline constexpr std::size_t maximum_cross_pair_candidates =
		maximum_candidates_per_eye * maximum_sampled_pairs;

	enum class observation_path : std::uint8_t
	{
		ignore,
		detailed_sample,
		candidate_only,
	};

	// Detailed report storage is diagnostic-only. Once it is exhausted, a shader
	// that already has the static declaration/name evidence must still reach the
	// direct binding check and the per-eye candidate intersection.
	[[nodiscard]] constexpr observation_path select_observation_path(
		const bool static_candidate_evidence,
		const bool detailed_sample_available) noexcept
	{
		if (detailed_sample_available) return observation_path::detailed_sample;
		return static_candidate_evidence ? observation_path::candidate_only :
			observation_path::ignore;
	}

	enum class phase : std::uint8_t
	{
		waiting,
		sampling,
		complete,
		failed,
	};

	enum class failure : std::uint8_t
	{
		none,
		pair_order,
		eye_order,
		shader_conflict,
		insufficient_evidence,
		sample_timeout,
	};

	struct state
	{
		phase current{phase::waiting};
		failure error{failure::none};
		bool pair_active{};
		std::uint64_t active_pair{};
		std::uint64_t last_pair{};
		bool last_pair_successful{};
		std::uint32_t successful_pairs{};
		std::uint32_t unsuccessful_pairs{};
		std::uint32_t skipped_seed_pairs{};
		std::uint32_t sampled_pairs{};
		std::array<std::uint32_t, 2> sampled_eye_pairs{};
		std::uintptr_t candidate_shader{};
		std::array<std::uintptr_t, maximum_cross_pair_candidates>
			cross_pair_candidates{};
		std::array<std::uint32_t, maximum_cross_pair_candidates>
			cross_pair_candidate_pairs{};
		std::size_t cross_pair_candidate_count{};
		std::array<std::array<std::uintptr_t, maximum_candidates_per_eye>, 2>
			active_eye_candidates{};
		std::array<std::size_t, 2> active_eye_candidate_counts{};
		bool active_candidate_overflow{};
		std::uint8_t active_eye{2};
		std::uint8_t completed_eye_mask{};
		std::uint8_t consumer_eye_mask{};
	};

	[[nodiscard]] constexpr bool terminal(const state& value) noexcept
	{
		return value.current == phase::complete || value.current == phase::failed;
	}

	[[nodiscard]] constexpr bool begin_pair(state& value,
		const std::uint64_t pair_id, const bool seeded) noexcept
	{
		if (terminal(value)) return false;
		if (value.pair_active || pair_id == 0 || pair_id < value.last_pair ||
			(pair_id == value.last_pair && value.last_pair_successful))
		{
			value.current = phase::failed;
			value.error = failure::pair_order;
			return false;
		}
		if (pair_id > value.last_pair)
		{
			value.last_pair = pair_id;
			value.last_pair_successful = false;
		}
		if (seeded)
		{
			++value.skipped_seed_pairs;
			return false;
		}
		value.current = phase::sampling;
		value.pair_active = true;
		value.active_pair = pair_id;
		value.active_eye = 2;
		value.completed_eye_mask = 0;
		value.consumer_eye_mask = 0;
		value.active_eye_candidates = {};
		value.active_eye_candidate_counts = {};
		value.active_candidate_overflow = false;
		return true;
	}

	[[nodiscard]] constexpr bool begin_eye(state& value,
		const std::uint64_t pair_id, const std::uint32_t eye) noexcept
	{
		const auto expected_eye = value.completed_eye_mask == 0 ? 0u : 1u;
		if (!value.pair_active || value.active_pair != pair_id || eye >= 2 ||
			value.active_eye < 2 || eye != expected_eye ||
			(value.completed_eye_mask & (1u << eye)) != 0)
		{
			value.current = phase::failed;
			value.error = failure::eye_order;
			return false;
		}
		value.active_eye = static_cast<std::uint8_t>(eye);
		return true;
	}

	[[nodiscard]] constexpr bool note_consumer(state& value,
		const std::uint64_t pair_id, const std::uint32_t eye,
		const std::uintptr_t shader) noexcept
	{
		if (!value.pair_active || value.active_pair != pair_id || eye >= 2 ||
			value.active_eye != eye || shader == 0)
		{
			return false;
		}
		auto& candidates = value.active_eye_candidates[eye];
		auto& count = value.active_eye_candidate_counts[eye];
		for (std::size_t index{}; index < count; ++index)
		{
			if (candidates[index] == shader)
			{
				value.consumer_eye_mask |= static_cast<std::uint8_t>(1u << eye);
				return true;
			}
		}
		if (count >= candidates.size())
		{
			value.active_candidate_overflow = true;
			return false;
		}
		candidates[count++] = shader;
		value.consumer_eye_mask |= static_cast<std::uint8_t>(1u << eye);
		return true;
	}

	[[nodiscard]] constexpr bool end_eye(state& value,
		const std::uint64_t pair_id, const std::uint32_t eye) noexcept
	{
		if (!value.pair_active || value.active_pair != pair_id || eye >= 2 ||
			value.active_eye != eye)
		{
			value.current = phase::failed;
			value.error = failure::eye_order;
			return false;
		}
		value.completed_eye_mask |= static_cast<std::uint8_t>(1u << eye);
		value.active_eye = 2;
		return true;
	}

	[[nodiscard]] constexpr bool end_pair(state& value,
		const std::uint64_t pair_id, const bool successful) noexcept
	{
		if (!value.pair_active || value.active_pair != pair_id)
		{
			value.current = phase::failed;
			value.error = failure::pair_order;
			return false;
		}
		if (successful &&
			(value.completed_eye_mask != 0x3 || value.active_eye != 2))
		{
			value.pair_active = false;
			value.active_pair = 0;
			value.active_eye = 2;
			value.current = phase::failed;
			value.error = failure::eye_order;
			return false;
		}
		value.pair_active = false;
		value.active_pair = 0;
		value.active_eye = 2;
		if (!successful)
		{
			value.last_pair_successful = false;
			++value.unsuccessful_pairs;
			value.completed_eye_mask = 0;
			value.consumer_eye_mask = 0;
			value.active_eye_candidates = {};
			value.active_eye_candidate_counts = {};
			value.active_candidate_overflow = false;
			return true;
		}

		++value.successful_pairs;
		value.last_pair_successful = true;
		if (value.active_candidate_overflow)
		{
			value.current = phase::failed;
			value.error = failure::insufficient_evidence;
			value.completed_eye_mask = 0;
			value.consumer_eye_mask = 0;
			value.active_eye_candidates = {};
			value.active_eye_candidate_counts = {};
			value.active_candidate_overflow = false;
			return false;
		}
		std::array<std::uintptr_t, maximum_candidates_per_eye>
			pair_candidates{};
		std::size_t pair_candidate_count{};
		if (value.consumer_eye_mask == 0x3)
		{
			for (std::size_t left{};
				left < value.active_eye_candidate_counts[0]; ++left)
			{
				for (std::size_t right{};
					right < value.active_eye_candidate_counts[1]; ++right)
				{
					if (value.active_eye_candidates[0][left] ==
						value.active_eye_candidates[1][right])
					{
						pair_candidates[pair_candidate_count++] =
							value.active_eye_candidates[0][left];
						break;
					}
				}
			}
		}
		if (pair_candidate_count != 0)
		{
			for (std::size_t pair_index{};
				pair_index < pair_candidate_count; ++pair_index)
			{
				std::size_t candidate_index{};
				for (; candidate_index < value.cross_pair_candidate_count;
					++candidate_index)
				{
					if (value.cross_pair_candidates[candidate_index] ==
						pair_candidates[pair_index]) break;
				}
				if (candidate_index == value.cross_pair_candidate_count)
				{
					if (candidate_index >= value.cross_pair_candidates.size())
					{
						value.current = phase::failed;
						value.error = failure::insufficient_evidence;
						return false;
					}
					value.cross_pair_candidates[candidate_index] =
						pair_candidates[pair_index];
					++value.cross_pair_candidate_count;
				}
				const auto pair_count =
					++value.cross_pair_candidate_pairs[candidate_index];
				if (pair_count > value.sampled_pairs)
				{
					value.sampled_pairs = pair_count;
					value.sampled_eye_pairs[0] = pair_count;
					value.sampled_eye_pairs[1] = pair_count;
				}
			}
		}
		value.completed_eye_mask = 0;
		value.consumer_eye_mask = 0;
		value.active_eye_candidates = {};
		value.active_eye_candidate_counts = {};
		value.active_candidate_overflow = false;

		if (value.sampled_pairs >= minimum_sampled_pairs)
		{
			for (std::size_t index{};
				index < value.cross_pair_candidate_count; ++index)
			{
				if (value.cross_pair_candidate_pairs[index] >=
					minimum_sampled_pairs && (value.candidate_shader == 0 ||
					value.cross_pair_candidates[index] < value.candidate_shader))
					value.candidate_shader = value.cross_pair_candidates[index];
			}
			if (value.candidate_shader == 0)
			{
				value.current = phase::failed;
				value.error = failure::insufficient_evidence;
				return false;
			}
			value.current = phase::complete;
			return true;
		}
		if (value.successful_pairs >= maximum_sampled_pairs)
		{
			value.current = phase::failed;
			value.error = failure::insufficient_evidence;
			return false;
		}
		if (value.successful_pairs >= maximum_successful_pairs_without_sample)
		{
			value.current = phase::failed;
			value.error = failure::sample_timeout;
			return false;
		}
		return true;
	}

	inline constexpr std::uint32_t maximum_focused_pairs = 8;
	inline constexpr std::uint32_t all_resource_subresources = 0xFFFFFFFFu;
	inline constexpr std::uint32_t unknown_output_target = 0xFFFFFFFFu;

	enum class resource_write_operation : std::uint8_t
	{
		unknown,
		draw_render_target,
		draw_depth_stencil,
		draw_unordered_access,
		dispatch_unordered_access,
		clear_render_target,
		clear_depth_stencil,
		clear_unordered_access,
		copy_resource,
		copy_subresource,
		copy_structure_count,
		resolve_subresource,
		update_subresource,
		generate_mips,
	};

	// CPU submission provenance only. Recording one of these events never performs
	// a D3D operation and does not claim that the submitted GPU write has completed.
	struct resource_write_metadata
	{
		std::uint64_t sequence{};
		std::uint64_t pair_id{};
		std::uintptr_t context{};
		std::uintptr_t destination{};
		std::uintptr_t source{};
		std::uintptr_t caller{};
		std::uint32_t thread_id{};
		std::uint32_t eye{2};
		std::uint32_t destination_subresource{all_resource_subresources};
		std::uint32_t source_subresource{all_resource_subresources};
		std::uint32_t output_target{unknown_output_target};
		resource_write_operation operation{resource_write_operation::unknown};

		[[nodiscard]] constexpr explicit operator bool() const noexcept
		{
			return sequence != 0 && destination != 0 &&
				operation != resource_write_operation::unknown;
		}
	};

	// A fixed ring retains the most recent writes without allocating in H2's render
	// thread. Consumers take a value copy at draw admission, so later ring reuse
	// cannot mutate evidence already stored in a selected sample.
	template <std::size_t Capacity>
	struct resource_write_window
	{
		static_assert(Capacity != 0);
		std::array<resource_write_metadata, Capacity> records{};
		std::size_t count{};
		std::size_t next{};
		std::uint64_t next_sequence{1};
		std::uint64_t overflows{};
	};

	template <std::size_t Capacity>
	[[nodiscard]] constexpr bool note_resource_write(
		resource_write_window<Capacity>& value,
		resource_write_metadata metadata) noexcept
	{
		if (metadata.destination == 0 ||
			metadata.operation == resource_write_operation::unknown)
		{
			return false;
		}
		metadata.sequence = value.next_sequence++;
		if (metadata.sequence == 0)
		{
			metadata.sequence = value.next_sequence++;
		}
		value.records[value.next] = metadata;
		value.next = (value.next + 1) % Capacity;
		if (value.count < Capacity)
		{
			++value.count;
		}
		else
		{
			++value.overflows;
		}
		return true;
	}

	template <std::size_t Capacity>
	[[nodiscard]] constexpr bool find_last_resource_write(
		const resource_write_window<Capacity>& value,
		const std::uintptr_t resource,
		resource_write_metadata& output) noexcept
	{
		output = {};
		if (resource == 0) return false;
		for (std::size_t offset{}; offset < value.count; ++offset)
		{
			const auto index = (value.next + Capacity - 1 - offset) % Capacity;
			const auto& candidate = value.records[index];
			if (candidate.destination != resource) continue;
			output = candidate;
			return static_cast<bool>(output);
		}
		return false;
	}

	struct resource_copy_source_lineage
	{
		resource_write_metadata copy{};
		resource_write_metadata source_last_write{};
		bool source_last_write_known{};
	};

	template <std::size_t Capacity>
	[[nodiscard]] constexpr bool capture_copy_source_lineage(
		const resource_write_window<Capacity>& value,
		const resource_write_metadata& copy,
		resource_copy_source_lineage& output) noexcept
	{
		output = {};
		if (copy.operation != resource_write_operation::copy_resource ||
			copy.destination == 0 || copy.source == 0)
		{
			return false;
		}
		output.copy = copy;
		output.source_last_write_known = find_last_resource_write(value,
			copy.source, output.source_last_write);
		return true;
	}

	enum class focused_phase : std::uint8_t
	{
		waiting,
		pending,
		pair_active,
		complete,
		failed,
	};

	enum class focused_failure : std::uint8_t
	{
		none,
		invalid_candidate,
		pair_order,
		eye_order,
		sample_timeout,
	};

	struct focused_state
	{
		focused_phase current{focused_phase::waiting};
		focused_failure error{focused_failure::none};
		std::uintptr_t candidate_shader{};
		std::uint64_t active_pair{};
		std::uint64_t last_pair{};
		bool last_pair_successful{};
		std::uint32_t attempts{};
		std::uint32_t skipped_seed_pairs{};
		std::uint8_t active_eye{2};
		std::uint8_t completed_eye_mask{};
		std::uint8_t sample_eye_mask{};
		std::uint8_t content_complete_eye_mask{};
	};

	[[nodiscard]] constexpr bool focused_terminal(
		const focused_state& value) noexcept
	{
		return value.current == focused_phase::complete ||
			value.current == focused_phase::failed;
	}

	[[nodiscard]] constexpr bool arm_focused(focused_state& value,
		const std::uintptr_t candidate_shader) noexcept
	{
		if (value.current != focused_phase::waiting || candidate_shader == 0)
		{
			value.current = focused_phase::failed;
			value.error = focused_failure::invalid_candidate;
			return false;
		}
		value.candidate_shader = candidate_shader;
		value.current = focused_phase::pending;
		return true;
	}

	[[nodiscard]] constexpr bool begin_focused_pair(focused_state& value,
		const std::uint64_t pair_id, const bool seeded) noexcept
	{
		if (focused_terminal(value))
		{
			return false;
		}
		if (value.current == focused_phase::pair_active ||
			pair_id == 0 || pair_id < value.last_pair ||
			(pair_id == value.last_pair && value.last_pair_successful))
		{
			value.current = focused_phase::failed;
			value.error = focused_failure::pair_order;
			return false;
		}
		if (pair_id > value.last_pair)
		{
			value.last_pair = pair_id;
			value.last_pair_successful = false;
		}
		if (seeded)
		{
			++value.skipped_seed_pairs;
			return false;
		}
		if (value.current != focused_phase::pending ||
			value.attempts >= maximum_focused_pairs)
		{
			value.current = focused_phase::failed;
			value.error = focused_failure::sample_timeout;
			return false;
		}
		++value.attempts;
		value.current = focused_phase::pair_active;
		value.active_pair = pair_id;
		value.active_eye = 2;
		value.completed_eye_mask = 0;
		value.sample_eye_mask = 0;
		value.content_complete_eye_mask = 0;
		return true;
	}

	[[nodiscard]] constexpr bool begin_focused_eye(focused_state& value,
		const std::uint64_t pair_id, const std::uint32_t eye) noexcept
	{
		const auto expected_eye = value.completed_eye_mask == 0 ? 0u : 1u;
		if (value.current != focused_phase::pair_active ||
			value.active_pair != pair_id || eye >= 2 || value.active_eye < 2 ||
			eye != expected_eye || (value.completed_eye_mask & (1u << eye)) != 0)
		{
			value.current = focused_phase::failed;
			value.error = focused_failure::eye_order;
			return false;
		}
		value.active_eye = static_cast<std::uint8_t>(eye);
		return true;
	}

	[[nodiscard]] constexpr bool note_focused_sample(focused_state& value,
		const std::uint64_t pair_id, const std::uint32_t eye,
		const std::uintptr_t candidate_shader,
		const bool content_complete) noexcept
	{
		if (value.current != focused_phase::pair_active ||
			value.active_pair != pair_id || eye >= 2 || value.active_eye != eye ||
			candidate_shader != value.candidate_shader)
		{
			return false;
		}
		value.sample_eye_mask |= static_cast<std::uint8_t>(1u << eye);
		if (content_complete)
			value.content_complete_eye_mask |= static_cast<std::uint8_t>(1u << eye);
		return true;
	}

	[[nodiscard]] constexpr bool end_focused_eye(focused_state& value,
		const std::uint64_t pair_id, const std::uint32_t eye) noexcept
	{
		if (value.current != focused_phase::pair_active ||
			value.active_pair != pair_id || eye >= 2 || value.active_eye != eye)
		{
			value.current = focused_phase::failed;
			value.error = focused_failure::eye_order;
			return false;
		}
		value.completed_eye_mask |= static_cast<std::uint8_t>(1u << eye);
		value.active_eye = 2;
		return true;
	}

	[[nodiscard]] constexpr bool end_focused_pair(focused_state& value,
		const std::uint64_t pair_id, const bool successful) noexcept
	{
		if (value.current != focused_phase::pair_active ||
			value.active_pair != pair_id)
		{
			value.current = focused_phase::failed;
			value.error = focused_failure::pair_order;
			return false;
		}
		if (successful &&
			(value.completed_eye_mask != 0x3 || value.active_eye != 2))
		{
			value.current = focused_phase::failed;
			value.error = focused_failure::eye_order;
			return false;
		}
		value.active_pair = 0;
		value.active_eye = 2;
		value.last_pair_successful = successful;
		if (successful && value.sample_eye_mask == 0x3)
		{
			value.current = focused_phase::complete;
			return true;
		}
		if (value.attempts >= maximum_focused_pairs)
		{
			value.current = focused_phase::failed;
			value.error = focused_failure::sample_timeout;
			return false;
		}
		value.current = focused_phase::pending;
		value.completed_eye_mask = 0;
		value.sample_eye_mask = 0;
		value.content_complete_eye_mask = 0;
		return true;
	}

	[[nodiscard]] constexpr const char* to_string(const phase value) noexcept
	{
		switch (value)
		{
		case phase::waiting: return "waiting";
		case phase::sampling: return "sampling";
		case phase::complete: return "complete";
		case phase::failed: return "failed";
		default: return "unknown";
		}
	}

	[[nodiscard]] constexpr const char* to_string(const failure value) noexcept
	{
		switch (value)
		{
		case failure::none: return "none";
		case failure::pair_order: return "pair_order";
		case failure::eye_order: return "eye_order";
		case failure::shader_conflict: return "shader_conflict";
		case failure::insufficient_evidence: return "insufficient_evidence";
		case failure::sample_timeout: return "sample_timeout";
		default: return "unknown";
		}
	}

	[[nodiscard]] constexpr const char* to_string(
		const focused_phase value) noexcept
	{
		switch (value)
		{
		case focused_phase::waiting: return "waiting";
		case focused_phase::pending: return "pending";
		case focused_phase::pair_active: return "pair_active";
		case focused_phase::complete: return "complete";
		case focused_phase::failed: return "failed";
		default: return "unknown";
		}
	}

	[[nodiscard]] constexpr const char* to_string(
		const focused_failure value) noexcept
	{
		switch (value)
		{
		case focused_failure::none: return "none";
		case focused_failure::invalid_candidate: return "invalid_candidate";
		case focused_failure::pair_order: return "pair_order";
		case focused_failure::eye_order: return "eye_order";
		case focused_failure::sample_timeout: return "sample_timeout";
		default: return "unknown";
		}
	}

	[[nodiscard]] constexpr const char* to_string(
		const resource_write_operation value) noexcept
	{
		switch (value)
		{
		case resource_write_operation::unknown: return "unknown";
		case resource_write_operation::draw_render_target: return "draw_render_target";
		case resource_write_operation::draw_depth_stencil: return "draw_depth_stencil";
		case resource_write_operation::draw_unordered_access:
			return "draw_unordered_access";
		case resource_write_operation::dispatch_unordered_access:
			return "dispatch_unordered_access";
		case resource_write_operation::clear_render_target: return "clear_render_target";
		case resource_write_operation::clear_depth_stencil: return "clear_depth_stencil";
		case resource_write_operation::clear_unordered_access:
			return "clear_unordered_access";
		case resource_write_operation::copy_resource: return "copy_resource";
		case resource_write_operation::copy_subresource: return "copy_subresource";
		case resource_write_operation::copy_structure_count:
			return "copy_structure_count";
		case resource_write_operation::resolve_subresource: return "resolve_subresource";
		case resource_write_operation::update_subresource: return "update_subresource";
		case resource_write_operation::generate_mips: return "generate_mips";
		default: return "unknown";
		}
	}
}
