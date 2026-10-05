#include <std_include.hpp>

#include "engine_view_probe.hpp"
#include "debug_options.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <iomanip>
#include <sstream>
#include <vector>

namespace vr::engine_view_probe
{
	namespace
	{
		static_assert((trace_capacity & (trace_capacity - 1)) == 0,
			"the trace capacity must remain a power of two");
		static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
			"the renderer probe requires lock-free 64-bit atomics");
		static_assert(unpack_low_u32(pack_u32_pair(0x89ABCDEFu, 0x01234567u)) ==
			0x89ABCDEFu);
		static_assert(unpack_high_u32(pack_u32_pair(0x89ABCDEFu, 0x01234567u)) ==
			0x01234567u);
		static_assert(unpack_low_i32(pack_i32_pair(-7, 11)) == -7);
		static_assert(unpack_high_i32(pack_i32_pair(-7, 11)) == 11);
		inline constexpr std::uintptr_t test_frontend = 0x10000000;
		static_assert(derive_slot_index(test_frontend,
			slot_base_address(test_frontend) + 3 * frontend_slot_stride, 4).index == 3);
		static_assert(derive_slot_index(test_frontend,
			slot_base_address(test_frontend) + 3 * frontend_slot_stride, 4).within_count);
		static_assert(!derive_slot_index(test_frontend,
			slot_base_address(test_frontend) + 3 * frontend_slot_stride + 1, 4).aligned);
		inline constexpr std::uint32_t expected_scene_calls = 1;

		struct trace_slot
		{
			std::atomic<std::uint64_t> version{};
			std::atomic<std::uint64_t> timestamp_qpc{};
			std::atomic<std::uint64_t> transaction_id{};
			std::atomic<std::uint64_t> frontend_frame_id{};
			std::atomic<std::uint64_t> metadata{};
			std::array<std::atomic<std::uint64_t>, 8> values{};
		};

		std::array<trace_slot, trace_capacity> trace{};
		std::atomic_bool probe_enabled{};
		std::atomic<std::uint64_t> trace_sequence{};
		std::atomic<std::uint64_t> transaction_sequence{};
		std::atomic<std::uint64_t> dropped_records{};
		std::atomic<std::uint64_t> orphan_records{};
		std::atomic<std::uint64_t> duplicate_records{};
		std::atomic<std::uint64_t> invalid_slot_records{};
		std::atomic<std::uint64_t> count_mismatch_records{};
		std::atomic<std::uint64_t> thread_mismatch_records{};
		std::atomic<std::uint64_t> view_state_snapshots{};
		std::atomic<std::uint64_t> stable_slot_comparisons{};
		std::atomic<std::uint64_t> changed_slot_comparisons{};
		std::atomic<std::uint64_t> matching_output_copies{};
		std::atomic<std::uint64_t> mismatching_output_copies{};
		std::atomic<std::uint64_t> camera_state_snapshots{};
		std::atomic<std::uint64_t> scoped_camera_state_snapshots{};
		std::atomic<std::uint64_t> unscoped_camera_state_snapshots{};
		std::atomic<std::uint64_t> set_viewpos_calls{};
		std::atomic<std::uint64_t> camera_helper_calls{};
		std::atomic<std::uint64_t> slot_initializer_calls{};
		std::atomic<std::uint64_t> slot_initializer_slot_changes{};
		std::atomic<std::uint64_t> slot_initializer_shared_changes{};
		std::atomic<std::uint64_t> slot_initializer_invalid_slots{};
		std::atomic<std::uint64_t> descriptor_contract_samples{};
		std::atomic<std::uint64_t> descriptor_contract_stable{};
		std::atomic<std::uint64_t> descriptor_contract_changes{};
		std::atomic<std::uint64_t> descriptor_contract_unreadable{};

		[[nodiscard]] std::uint64_t timestamp_qpc() noexcept
		{
			LARGE_INTEGER value{};
			(void)QueryPerformanceCounter(&value);
			return static_cast<std::uint64_t>(value.QuadPart);
		}

		[[nodiscard]] constexpr std::uint8_t compact_depth(
			const std::uint16_t depth) noexcept
		{
			return static_cast<std::uint8_t>((std::min)(depth,
				static_cast<std::uint16_t>((std::numeric_limits<std::uint8_t>::max)())));
		}

		[[nodiscard]] constexpr std::uint64_t pack_metadata(const std::uint32_t thread_id,
			const std::uint16_t depth, const event_kind kind,
			const observation_stage stage, const record_flags flags) noexcept
		{
			return static_cast<std::uint64_t>(thread_id) |
				(static_cast<std::uint64_t>(compact_depth(depth)) << 32) |
				(static_cast<std::uint64_t>(kind) << 40) |
				(static_cast<std::uint64_t>(stage) << 48) |
				(static_cast<std::uint64_t>(flags) << 56);
		}

		void unpack_metadata(const std::uint64_t metadata, record& output) noexcept
		{
			output.thread_id = static_cast<std::uint32_t>(metadata);
			output.depth = static_cast<std::uint16_t>((metadata >> 32) & 0xFF);
			output.kind = static_cast<event_kind>((metadata >> 40) & 0xFF);
			output.stage = static_cast<observation_stage>((metadata >> 48) & 0xFF);
			output.flags = static_cast<record_flags>(metadata >> 56);
		}

		[[nodiscard]] record_flags normalize_flags(const transaction_token& token,
			record_flags flags, const std::uint32_t current_thread_id) noexcept
		{
			if (!token) flags = with_flag(flags, record_flag::orphan);
			if (token.depth > 1) flags = with_flag(flags, record_flag::nested);
			if (token.owner_thread_id != 0 && token.owner_thread_id != current_thread_id)
			{
				flags = with_flag(flags, record_flag::thread_mismatch);
			}
			return flags;
		}

		void count_flags(const record_flags flags) noexcept
		{
			if (has_flag(flags, record_flag::orphan))
				orphan_records.fetch_add(1, std::memory_order_relaxed);
			if (has_flag(flags, record_flag::duplicate))
				duplicate_records.fetch_add(1, std::memory_order_relaxed);
			if (has_flag(flags, record_flag::invalid_slot))
				invalid_slot_records.fetch_add(1, std::memory_order_relaxed);
			if (has_flag(flags, record_flag::count_mismatch))
				count_mismatch_records.fetch_add(1, std::memory_order_relaxed);
			if (has_flag(flags, record_flag::thread_mismatch))
				thread_mismatch_records.fetch_add(1, std::memory_order_relaxed);
		}

		void publish_record(const std::uint64_t transaction_id,
			const std::uint64_t frontend_frame_id, const std::uint32_t thread_id,
			const std::uint16_t depth, const event_kind kind,
			const observation_stage stage, const record_flags flags,
			const std::array<std::uint64_t, 8>& values) noexcept
		{
			// Tokens still correlate the production stereo publication when the
			// optional event ring is unloaded. Do not gate begin()/set_enabled().
			if (!debug_options::enabled(debug_options::probe::view)) return;
			const auto sequence = trace_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
			auto& slot = trace[(sequence - 1) & (trace_capacity - 1)];
			const auto writing_version = (sequence << 1) | 1;

			// A writer owns a slot only after changing one committed (even) version to
			// its private in-progress (odd) version. Never overwrite an odd slot: the
			// previous writer may be stalled inside the renderer hook. Also reject an
			// older writer that resumes after a newer sequence already committed. A
			// failed claim is deliberately dropped instead of waiting on another render
			// thread; the drop counter makes that loss explicit in every diagnostic.
			auto observed_version = slot.version.load(std::memory_order_acquire);
			if ((observed_version & 1) != 0 || (observed_version >> 1) >= sequence ||
				!slot.version.compare_exchange_strong(observed_version, writing_version,
					std::memory_order_acq_rel, std::memory_order_acquire))
			{
				dropped_records.fetch_add(1, std::memory_order_relaxed);
				return;
			}

			slot.timestamp_qpc.store(timestamp_qpc(), std::memory_order_relaxed);
			slot.transaction_id.store(transaction_id, std::memory_order_relaxed);
			slot.frontend_frame_id.store(frontend_frame_id, std::memory_order_relaxed);
			slot.metadata.store(pack_metadata(thread_id, depth, kind, stage, flags),
				std::memory_order_relaxed);
			for (std::size_t index{}; index < values.size(); ++index)
			{
				slot.values[index].store(values[index], std::memory_order_relaxed);
			}
			slot.version.store(sequence << 1, std::memory_order_release);
			count_flags(flags);
		}

		void publish(const transaction_token& token, const event_kind kind,
			const observation_stage stage, record_flags flags,
			const std::array<std::uint64_t, 8>& values) noexcept
		{
			if (!probe_enabled.load(std::memory_order_relaxed)) return;

			const auto thread_id = GetCurrentThreadId();
			flags = normalize_flags(token, flags, thread_id);
			publish_record(token.transaction_id, token.frontend_frame_id, thread_id,
				token.depth, kind, stage, flags, values);
		}

		void append_flags(std::ostringstream& output, const record_flags flags)
		{
			if (has_flag(flags, record_flag::orphan)) output << " orphan";
			if (has_flag(flags, record_flag::duplicate)) output << " duplicate";
			if (has_flag(flags, record_flag::nested)) output << " nested";
			if (has_flag(flags, record_flag::thread_mismatch)) output << " thread_mismatch";
			if (has_flag(flags, record_flag::inside_r_end_frame)) output << " inside_r_end_frame";
			if (has_flag(flags, record_flag::invalid_slot)) output << " invalid_slot";
			if (has_flag(flags, record_flag::count_mismatch)) output << " count_mismatch";
			if (has_flag(flags, record_flag::selector_changed)) output << " selector_changed";
		}

		void append_payload(std::ostringstream& output, const record& value)
		{
			const auto& v = value.values;
			switch (value.kind)
			{
			case event_kind::begin:
				output << " scene_descriptor=0x" << std::hex << v[0]
					<< " prefix_hash=0x" << v[1] << std::dec
					<< " local=" << unpack_low_i32(v[2])
					<< " scene_record_index=" << unpack_high_u32(v[2])
					<< " lod=" << float_from_bits(unpack_low_u32(v[3]))
					<< " draw_type=" << unpack_low_i32(v[4])
					<< " parent=" << v[5];
				break;
			case event_kind::callstack:
			case event_kind::callstack_tail:
			{
				const auto base_index = value.kind == event_kind::callstack_tail ? 8u : 0u;
				for (std::size_t index{}; index < v.size(); ++index)
				{
					if (v[index] == 0) break;
					output << " frame[" << base_index + index << "]=0x" << std::hex << v[index];
					if (v[index] >= 0x140000000ull && v[index] < 0x152000000ull)
					{
						output << "(h2+0x" << (v[index] - 0x140000000ull) << ')';
					}
					output << std::dec;
				}
				break;
			}
			case event_kind::view_call:
				output << " call=" << static_cast<std::uint32_t>(v[0])
					<< " frontend=0x" << std::hex << v[1]
					<< " selector=0x" << v[2] << std::dec
					<< " count=" << static_cast<std::uint32_t>(v[3])
					<< " scene_descriptor=0x" << std::hex << v[4]
					<< " prefix_hash=0x" << v[5] << std::dec
					<< " global_record_count=" << static_cast<std::uint32_t>(v[6])
					<< " current_record_index=" << unpack_low_u32(v[7])
					<< " record_count=" << unpack_high_u32(v[7]);
				break;
			case event_kind::frontend:
				output << " frontend=0x" << std::hex << v[0]
					<< " selector=0x" << v[1] << std::dec
					<< " slot_count=" << v[2]
					<< " current_record_index=" << unpack_low_u32(v[3])
					<< " record_count=" << unpack_high_u32(v[3])
					<< " global_record_count=" << static_cast<std::uint32_t>(v[4])
					<< " slot_base=0x" << std::hex << v[5]
					<< " aux=0x" << v[6] << std::dec;
				break;
			case event_kind::slot:
				output << " frontend=0x" << std::hex << v[0]
					<< " slot=0x" << v[1] << std::dec
					<< " count=" << unpack_low_u32(v[2]) << "->" << unpack_high_u32(v[2])
					<< " index=" << static_cast<std::uint32_t>(v[3])
					<< " selector=0x" << std::hex << v[4]
					<< std::dec << " current_record_index=" << unpack_low_u32(v[5])
					<< " record_count=" << unpack_high_u32(v[5])
					<< " global_record_count=" << static_cast<std::uint32_t>(v[6])
					<< " base=0x" << std::hex << v[7] << std::dec;
				break;
			case event_kind::slot_initializer:
			{
				constexpr auto packed_invalid = (std::numeric_limits<std::uint16_t>::max)();
				const auto changed_bytes = unpack_u16(v[7], 0);
				const auto first_changed = unpack_u16(v[7], 1);
				const auto last_changed = unpack_u16(v[7], 2);
				const auto slot_index = unpack_u16(v[7], 3);
				output << " scene_descriptor=0x" << std::hex << v[0]
					<< " slot=0x" << v[1]
					<< " prefix_hash=0x" << v[2]
					<< " slot_hash=0x" << v[3] << "->0x" << v[4]
					<< " shared_hash=0x" << v[5] << "->0x" << v[6]
					<< std::dec << " changed_bytes=" << changed_bytes;
				if (first_changed != packed_invalid)
				{
					output << " changed_range=" << first_changed << ".." << last_changed;
				}
				else
				{
					output << " changed_range=none";
				}
				if (slot_index != packed_invalid) output << " slot_index=" << slot_index;
				else output << " slot_index=invalid";
				output << " shared_unchanged=" << (v[5] == v[6] ? "yes" : "no");
				break;
			}
			case event_kind::descriptor_contract:
				output << " scene_descriptor=0x" << std::hex << v[0]
					<< std::dec << " size=" << v[1]
					<< " full_hash=0x" << std::hex << v[2] << "->0x" << v[3]
					<< std::dec << " readable_before=" << ((v[4] & 1) != 0 ? "yes" : "no")
					<< " readable_after=" << ((v[4] & 2) != 0 ? "yes" : "no")
					<< " stable=" << (v[2] == v[3] && (v[4] & 3) == 3 ? "yes" : "no");
				break;
			case event_kind::record_reservation:
				output << " frontend=0x" << std::hex << v[0] << "->0x" << v[1]
					<< std::dec << " global_record_count=" << unpack_low_u32(v[2])
					<< "->" << unpack_high_u32(v[2])
					<< " output_index=" << static_cast<std::uint32_t>(v[3])
					<< " current_record_index=" << unpack_low_u32(v[4])
					<< "->" << unpack_high_u32(v[4])
					<< " record_count=" << unpack_low_u32(v[5])
					<< "->" << unpack_high_u32(v[5])
					<< " slot_count=" << unpack_low_u32(v[6])
					<< "->" << unpack_high_u32(v[6])
					<< " selector=" << unpack_low_u32(v[7])
					<< "->" << unpack_high_u32(v[7]);
				break;
			case event_kind::generator:
				output << " local=" << unpack_low_i32(v[0])
					<< " scene_record_index=" << unpack_high_u32(v[0])
					<< " scratch=0x" << std::hex << v[1]
					<< " selected=0x" << v[2] << " slot=0x" << v[3]
					<< " output=0x" << v[4] << std::dec
					<< " draw_type=" << unpack_low_i32(v[5])
					<< " frontend=0x" << std::hex << v[6] << std::dec
					<< " slot_count=" << unpack_low_u32(v[7])
					<< " slot_index=" << unpack_high_u32(v[7]);
				break;
			case event_kind::view_state:
			{
				const auto relations = static_cast<view_state_relations>(
					unpack_high_u32(v[0]));
				output << " source=" << to_string(static_cast<view_state_source>(
					unpack_low_u32(v[0])))
					<< " subject=0x" << std::hex << v[1]
					<< " slot_hash=0x" << v[2]
					<< " output_hash=0x" << v[3]
					<< " scene_globals_hash=0x" << v[4]
					<< " camera_primary_hash=0x" << v[5]
					<< " camera_secondary_hash=0x" << v[6]
					<< " owner_globals_hash=0x" << v[7] << std::dec;
				if (has_relation(relations, view_state_relation::slot_compared))
				{
					output << " slot_unchanged=" <<
						(has_relation(relations, view_state_relation::slot_unchanged)
							? "yes" : "no");
				}
				if (has_relation(relations, view_state_relation::output_compared))
				{
					output << " output_matches_slot=" <<
						(has_relation(relations, view_state_relation::output_matches_slot)
							? "yes" : "no");
				}
				break;
			}
			case event_kind::camera_state:
				output << " source=" << to_string(static_cast<camera_state_source>(
					unpack_low_u32(v[0])))
					<< " scalar=" << float_from_bits(unpack_high_u32(v[0]))
					<< " caller=0x" << std::hex << v[1]
					<< " input=0x" << v[2]
					<< " output=0x" << v[3]
					<< " input_hash=0x" << v[4]
					<< " output_hash=0x" << v[5]
					<< " refdef_prefix_hash=0x" << v[6]
					<< " owner_globals_hash=0x" << v[7] << std::dec;
				break;
			case event_kind::call_return:
				output << " source=" << to_string(static_cast<return_source>(v[0]))
					<< " object=0x" << std::hex << v[1] << " result=0x" << v[2] << std::dec
					<< " count=" << unpack_low_u32(v[3])
					<< " index=" << unpack_high_u32(v[3])
					<< " selector=0x" << std::hex << v[4]
					<< std::dec << " current_record_index=" << unpack_low_u32(v[5])
					<< " record_count=" << unpack_high_u32(v[5])
					<< " draw_type=" << unpack_low_i32(v[6])
					<< " aux=0x" << std::hex << v[7] << std::dec;
				break;
			case event_kind::end:
				output << " count=" << unpack_low_u32(v[0]) << "->" << unpack_high_u32(v[0])
					<< " slot_calls=" << unpack_low_u32(v[1])
					<< " generator_calls=" << unpack_high_u32(v[1])
					<< " return_calls=" << static_cast<std::uint32_t>(v[2])
					<< " expected_scene_calls=" << expected_scene_calls
					<< " last_slot=0x" << std::hex << v[3] << std::dec
					<< " last_index=" << static_cast<std::uint32_t>(v[4])
					<< " selector=0x" << std::hex << v[5] << "->0x" << v[6]
					<< " signature=0x" << v[7] << std::dec;
				break;
			case event_kind::flip:
				output << " frontend=0x" << std::hex << v[0] << "->0x" << v[1]
					<< std::dec << " selector=" << unpack_low_u32(v[2])
					<< "->" << unpack_high_u32(v[2])
					<< " slot_count=" << unpack_low_u32(v[3])
					<< "->" << unpack_high_u32(v[3])
					<< " current_record_index=" << unpack_low_u32(v[4])
					<< "->" << unpack_high_u32(v[4])
					<< " record_count=" << unpack_low_u32(v[5])
					<< "->" << unpack_high_u32(v[5])
					<< " global_record_count=" << unpack_low_u32(v[6])
					<< "->" << unpack_high_u32(v[6])
					<< " reason=0x" << v[7] << std::dec;
				break;
			case event_kind::ownership_boundary:
				output << " boundary=" << to_string(
					static_cast<ownership_boundary_kind>(v[0]))
					<< " frontend=0x" << std::hex << v[1]
					<< " backend_frontend=0x" << v[2]
					<< " record_arena=0x" << v[3] << std::dec
					<< " selector=" << unpack_low_u32(v[4])
					<< " slot_count=" << unpack_high_u32(v[4])
					<< " current_record_index=" << unpack_low_u32(v[5])
					<< " record_count=" << unpack_high_u32(v[5])
					<< " global_record_count=" << unpack_low_u32(v[6])
					<< " owner_record_index=" << unpack_high_u32(v[6])
					<< " owner_view_hash=0x" << std::hex << v[7] << std::dec;
				break;
			default:
				for (std::size_t index{}; index < v.size(); ++index)
				{
					output << " v" << index << "=0x" << std::hex << v[index] << std::dec;
				}
				break;
			}
		}
	}

	void set_enabled(const bool enabled) noexcept
	{
		probe_enabled.store(enabled, std::memory_order_release);
	}

	bool is_enabled() noexcept
	{
		return probe_enabled.load(std::memory_order_acquire);
	}

	void reset() noexcept
	{
		probe_enabled.store(false, std::memory_order_release);
		trace_sequence.store(0, std::memory_order_relaxed);
		transaction_sequence.store(0, std::memory_order_relaxed);
		dropped_records.store(0, std::memory_order_relaxed);
		orphan_records.store(0, std::memory_order_relaxed);
		duplicate_records.store(0, std::memory_order_relaxed);
		invalid_slot_records.store(0, std::memory_order_relaxed);
		count_mismatch_records.store(0, std::memory_order_relaxed);
		thread_mismatch_records.store(0, std::memory_order_relaxed);
		view_state_snapshots.store(0, std::memory_order_relaxed);
		stable_slot_comparisons.store(0, std::memory_order_relaxed);
		changed_slot_comparisons.store(0, std::memory_order_relaxed);
		matching_output_copies.store(0, std::memory_order_relaxed);
		mismatching_output_copies.store(0, std::memory_order_relaxed);
		camera_state_snapshots.store(0, std::memory_order_relaxed);
		scoped_camera_state_snapshots.store(0, std::memory_order_relaxed);
		unscoped_camera_state_snapshots.store(0, std::memory_order_relaxed);
		set_viewpos_calls.store(0, std::memory_order_relaxed);
		camera_helper_calls.store(0, std::memory_order_relaxed);
		slot_initializer_calls.store(0, std::memory_order_relaxed);
		slot_initializer_slot_changes.store(0, std::memory_order_relaxed);
		slot_initializer_shared_changes.store(0, std::memory_order_relaxed);
		slot_initializer_invalid_slots.store(0, std::memory_order_relaxed);
		descriptor_contract_samples.store(0, std::memory_order_relaxed);
		descriptor_contract_stable.store(0, std::memory_order_relaxed);
		descriptor_contract_changes.store(0, std::memory_order_relaxed);
		descriptor_contract_unreadable.store(0, std::memory_order_relaxed);
		for (auto& slot : trace)
		{
			slot.version.store(0, std::memory_order_relaxed);
		}
	}

	transaction_token begin(const std::uint64_t frontend_frame_id,
		const std::uint16_t depth, const outer_observation& observation,
		record_flags flags) noexcept
	{
		if (!probe_enabled.load(std::memory_order_relaxed)) return {};
		const transaction_token token{
			transaction_sequence.fetch_add(1, std::memory_order_relaxed) + 1,
			frontend_frame_id,
			GetCurrentThreadId(),
			depth,
		};
		if (depth > 1) flags = with_flag(flags, record_flag::nested);
		publish(token, event_kind::begin, observation_stage::enter, flags, {
			static_cast<std::uint64_t>(observation.scene_descriptor),
			observation.scene_descriptor_prefix_hash,
			pack_u32_pair(static_cast<std::uint32_t>(observation.local_client),
				observation.scene_record_index),
			float_bits(observation.lod_scale),
			static_cast<std::uint32_t>(observation.draw_type),
			observation.parent_transaction_id,
			0,
			0,
		});
		return token;
	}

	void record_callstack(const transaction_token& token,
		const callstack_observation& observation, const record_flags flags) noexcept
	{
		std::array<std::uint64_t, 8> head{};
		std::array<std::uint64_t, 8> tail{};
		bool has_tail{};
		for (std::size_t index{}; index < head.size(); ++index)
		{
			head[index] = static_cast<std::uint64_t>(observation.frames[index]);
			tail[index] = static_cast<std::uint64_t>(observation.frames[index + head.size()]);
			has_tail = has_tail || tail[index] != 0;
		}
		publish(token, event_kind::callstack, observation_stage::snapshot, flags, head);
		if (has_tail)
		{
			publish(token, event_kind::callstack_tail, observation_stage::snapshot, flags, tail);
		}
	}

	void record_view_call(const transaction_token& token,
		const view_call_observation& observation, const observation_stage stage,
		const record_flags flags) noexcept
	{
		publish(token, event_kind::view_call, stage, flags, {
			observation.call_index,
			static_cast<std::uint64_t>(observation.frontend),
			observation.selector,
			observation.slot_count,
			static_cast<std::uint64_t>(observation.scene_descriptor),
			observation.scene_descriptor_prefix_hash,
			observation.global_record_count,
			pack_u32_pair(observation.current_record_index, observation.record_count),
		});
	}

	void record_frontend(const transaction_token& token,
		const frontend_observation& observation, const observation_stage stage,
		const record_flags flags) noexcept
	{
		const auto base = observation.slot_base != 0
			? observation.slot_base : slot_base_address(observation.frontend);
		publish(token, event_kind::frontend, stage, flags, {
			static_cast<std::uint64_t>(observation.frontend),
			observation.selector,
			observation.slot_count,
			pack_u32_pair(observation.current_record_index, observation.record_count),
			observation.global_record_count,
			static_cast<std::uint64_t>(base),
			observation.auxiliary,
			0,
		});
	}

	void record_slot(const transaction_token& token, const slot_observation& observation,
		const observation_stage stage, record_flags flags) noexcept
	{
		const auto derived = derive_slot_index(observation.frontend, observation.slot,
			observation.count_after);
		auto index = observation.slot_index;
		if (index == invalid_slot_index && derived) index = derived.index;
		if (!derived || (observation.slot_index != invalid_slot_index &&
			observation.slot_index != derived.index))
		{
			flags = with_flag(flags, record_flag::invalid_slot);
		}
		if (observation.count_before ==
			(std::numeric_limits<std::uint32_t>::max)() ||
			observation.count_after != observation.count_before + 1)
		{
			flags = with_flag(flags, record_flag::count_mismatch);
		}
		publish(token, event_kind::slot, stage, flags, {
			static_cast<std::uint64_t>(observation.frontend),
			static_cast<std::uint64_t>(observation.slot),
			pack_u32_pair(observation.count_before, observation.count_after),
			index,
			observation.selector,
			pack_u32_pair(observation.current_record_index, observation.record_count),
			observation.global_record_count,
			static_cast<std::uint64_t>(slot_base_address(observation.frontend)),
		});
	}

	void record_slot_initializer(const transaction_token& token,
		const slot_initializer_observation& observation,
		const observation_stage stage, record_flags flags) noexcept
	{
		if (!probe_enabled.load(std::memory_order_relaxed)) return;
		constexpr auto packed_invalid = (std::numeric_limits<std::uint16_t>::max)();
		const auto bounded = [](const std::uint32_t value) noexcept
		{
			return static_cast<std::uint16_t>((std::min)(value,
				static_cast<std::uint32_t>((std::numeric_limits<std::uint16_t>::max)())));
		};
		const auto packed_index = observation.slot_index == invalid_slot_index
			? packed_invalid : bounded(observation.slot_index);
		const auto packed_first = observation.first_changed == invalid_slot_index
			? packed_invalid : bounded(observation.first_changed);
		const auto packed_last = observation.last_changed == invalid_slot_index
			? packed_invalid : bounded(observation.last_changed);

		slot_initializer_calls.fetch_add(1, std::memory_order_relaxed);
		if (observation.slot_before_hash != observation.slot_after_hash)
		{
			slot_initializer_slot_changes.fetch_add(1, std::memory_order_relaxed);
		}
		if (observation.shared_before_hash != observation.shared_after_hash)
		{
			slot_initializer_shared_changes.fetch_add(1, std::memory_order_relaxed);
		}
		if (observation.slot_index == invalid_slot_index)
		{
			slot_initializer_invalid_slots.fetch_add(1, std::memory_order_relaxed);
			flags = with_flag(flags, record_flag::invalid_slot);
		}
		publish(token, event_kind::slot_initializer, stage, flags, {
			static_cast<std::uint64_t>(observation.scene_descriptor),
			static_cast<std::uint64_t>(observation.slot),
			observation.scene_descriptor_prefix_hash,
			observation.slot_before_hash,
			observation.slot_after_hash,
			observation.shared_before_hash,
			observation.shared_after_hash,
			pack_u16_quad(bounded(observation.changed_bytes), packed_first,
				packed_last, packed_index),
		});
	}

	void record_descriptor_contract(const transaction_token& token,
		const descriptor_contract_observation& observation,
		const observation_stage stage, const record_flags flags) noexcept
	{
		if (!probe_enabled.load(std::memory_order_relaxed) || !token) return;
		descriptor_contract_samples.fetch_add(1, std::memory_order_relaxed);
		if (!observation.readable_before || !observation.readable_after)
		{
			descriptor_contract_unreadable.fetch_add(1, std::memory_order_relaxed);
		}
		else if (observation.before_hash == observation.after_hash)
		{
			descriptor_contract_stable.fetch_add(1, std::memory_order_relaxed);
		}
		else
		{
			descriptor_contract_changes.fetch_add(1, std::memory_order_relaxed);
		}
		publish(token, event_kind::descriptor_contract, stage, flags, {
			static_cast<std::uint64_t>(observation.scene_descriptor),
			observation.observed_size,
			observation.before_hash,
			observation.after_hash,
			(observation.readable_before ? 1ull : 0ull) |
				(observation.readable_after ? 2ull : 0ull),
			0, 0, 0,
		});
	}

	void record_reservation(const transaction_token& token,
		const record_reservation_observation& observation,
		const observation_stage stage, record_flags flags) noexcept
	{
		if (observation.frontend_before != observation.frontend_after ||
			observation.selector_before != observation.selector_after)
		{
			flags = with_flag(flags, record_flag::selector_changed);
		}
		// record_count_before belongs to the reused ping-pong arena and is not
		// required to equal the frame-global counter before this call overwrites it.
		const auto sequential = observation.frontend_before == observation.frontend_after &&
			observation.selector_before == observation.selector_after &&
			observation.global_count_before !=
				(std::numeric_limits<std::uint32_t>::max)() &&
			observation.output_index == observation.global_count_before &&
			observation.global_count_after == observation.global_count_before + 1 &&
			observation.current_index_after == observation.output_index &&
			observation.record_count_after == observation.global_count_after &&
			observation.slot_count_before == observation.slot_count_after;
		if (!sequential || observation.output_index >= frontend_record_capacity)
		{
			flags = with_flag(flags, record_flag::count_mismatch);
		}
		publish(token, event_kind::record_reservation, stage, flags, {
			static_cast<std::uint64_t>(observation.frontend_before),
			static_cast<std::uint64_t>(observation.frontend_after),
			pack_u32_pair(observation.global_count_before, observation.global_count_after),
			observation.output_index,
			pack_u32_pair(observation.current_index_before,
				observation.current_index_after),
			pack_u32_pair(observation.record_count_before,
				observation.record_count_after),
			pack_u32_pair(observation.slot_count_before, observation.slot_count_after),
			pack_u32_pair(observation.selector_before, observation.selector_after),
		});
	}

	void record_generator(const transaction_token& token,
		const generator_observation& observation, const observation_stage stage,
		record_flags flags) noexcept
	{
		const auto derived = derive_slot_index(observation.frontend, observation.slot,
			observation.slot_count);
		auto index = observation.slot_index;
		if (index == invalid_slot_index && derived) index = derived.index;
		if (!derived || (observation.slot_index != invalid_slot_index &&
			observation.slot_index != derived.index))
		{
			flags = with_flag(flags, record_flag::invalid_slot);
		}
		publish(token, event_kind::generator, stage, flags, {
			pack_u32_pair(static_cast<std::uint32_t>(observation.local_client),
				observation.scene_record_index),
			static_cast<std::uint64_t>(observation.scratch),
			static_cast<std::uint64_t>(observation.selected),
			static_cast<std::uint64_t>(observation.slot),
			static_cast<std::uint64_t>(observation.per_client_output),
			static_cast<std::uint32_t>(observation.draw_type),
			static_cast<std::uint64_t>(observation.frontend),
			pack_u32_pair(observation.slot_count, index),
		});
	}

	void record_view_state(const transaction_token& token,
		const view_state_observation& observation, const observation_stage stage,
		const record_flags flags) noexcept
	{
		if (!probe_enabled.load(std::memory_order_relaxed)) return;
		view_state_snapshots.fetch_add(1, std::memory_order_relaxed);
		if (has_relation(observation.relations, view_state_relation::slot_compared))
		{
			auto& counter = has_relation(observation.relations,
				view_state_relation::slot_unchanged)
				? stable_slot_comparisons : changed_slot_comparisons;
			counter.fetch_add(1, std::memory_order_relaxed);
		}
		if (has_relation(observation.relations, view_state_relation::output_compared))
		{
			auto& counter = has_relation(observation.relations,
				view_state_relation::output_matches_slot)
				? matching_output_copies : mismatching_output_copies;
			counter.fetch_add(1, std::memory_order_relaxed);
		}
		publish(token, event_kind::view_state, stage, flags, {
			pack_u32_pair(static_cast<std::uint32_t>(observation.source),
				observation.relations),
			static_cast<std::uint64_t>(observation.subject),
			observation.view_slot_hash,
			observation.per_client_output_hash,
			observation.scene_globals_hash,
			observation.camera_primary_hash,
			observation.camera_secondary_hash,
			observation.owner_globals_hash,
		});
	}

	namespace
	{
		void count_camera_state(const camera_state_observation& observation,
			const observation_stage stage, const bool scoped) noexcept
		{
			camera_state_snapshots.fetch_add(1, std::memory_order_relaxed);
			(scoped ? scoped_camera_state_snapshots : unscoped_camera_state_snapshots)
				.fetch_add(1, std::memory_order_relaxed);
			if (observation.source == camera_state_source::set_viewpos_now &&
				stage == observation_stage::enter)
			{
				set_viewpos_calls.fetch_add(1, std::memory_order_relaxed);
			}
			if (observation.source == camera_state_source::camera_helper &&
				stage == observation_stage::before_call)
			{
				camera_helper_calls.fetch_add(1, std::memory_order_relaxed);
			}
		}

		std::array<std::uint64_t, 8> camera_state_values(
			const camera_state_observation& observation) noexcept
		{
			return {
				pack_u32_pair(static_cast<std::uint32_t>(observation.source),
					observation.scalar_bits),
				static_cast<std::uint64_t>(observation.caller),
				static_cast<std::uint64_t>(observation.input),
				static_cast<std::uint64_t>(observation.output),
				observation.input_hash,
				observation.output_hash,
				observation.refdef_prefix_hash,
				observation.owner_globals_hash,
			};
		}
	}

	void record_camera_state(const transaction_token& token,
		const camera_state_observation& observation, const observation_stage stage,
		const record_flags flags) noexcept
	{
		if (!probe_enabled.load(std::memory_order_relaxed)) return;
		count_camera_state(observation, stage, true);
		publish(token, event_kind::camera_state, stage, flags,
			camera_state_values(observation));
	}

	void record_unscoped_camera_state(const std::uint64_t frontend_frame_id,
		const camera_state_observation& observation, const observation_stage stage,
		const record_flags flags) noexcept
	{
		if (!probe_enabled.load(std::memory_order_relaxed)) return;
		count_camera_state(observation, stage, false);
		publish_record(0, frontend_frame_id, GetCurrentThreadId(), 0,
			event_kind::camera_state, stage, flags, camera_state_values(observation));
	}

	void record_return(const transaction_token& token,
		const return_observation& observation, const observation_stage stage,
		const record_flags flags) noexcept
	{
		publish(token, event_kind::call_return, stage, flags, {
			static_cast<std::uint64_t>(observation.source),
			static_cast<std::uint64_t>(observation.object),
			observation.result,
			pack_u32_pair(observation.slot_count, observation.slot_index),
			observation.selector,
			pack_u32_pair(observation.current_record_index, observation.record_count),
			static_cast<std::uint32_t>(observation.draw_type),
			observation.auxiliary,
		});
	}

	void end(const transaction_token& token, const end_observation& observation,
		record_flags flags) noexcept
	{
		constexpr auto expected = expected_scene_calls;
		const auto count_matches = observation.count_begin <=
			(std::numeric_limits<std::uint32_t>::max)() - expected &&
			observation.count_end == observation.count_begin + expected;
		if (observation.slot_calls != expected || observation.generator_calls != expected ||
			observation.return_calls != expected * 2u || !count_matches)
		{
			flags = with_flag(flags, record_flag::count_mismatch);
		}
		if (observation.slot_calls > expected || observation.generator_calls > expected)
		{
			flags = with_flag(flags, record_flag::duplicate);
		}
		if (observation.selector_begin != observation.selector_end)
		{
			flags = with_flag(flags, record_flag::selector_changed);
		}
		publish(token, event_kind::end, observation_stage::leave, flags, {
			pack_u32_pair(observation.count_begin, observation.count_end),
			pack_u32_pair(observation.slot_calls, observation.generator_calls),
			observation.return_calls,
			static_cast<std::uint64_t>(observation.last_slot),
			observation.last_slot_index,
			observation.selector_begin,
			observation.selector_end,
			observation.slot_signature,
		});
	}

	void record_flip(const transaction_token& token, const flip_observation& observation,
		record_flags flags) noexcept
	{
		if (observation.selector_before != observation.selector_after ||
			observation.frontend_before != observation.frontend_after)
		{
			flags = with_flag(flags, record_flag::selector_changed);
		}
		publish(token, event_kind::flip, observation_stage::snapshot, flags, {
			static_cast<std::uint64_t>(observation.frontend_before),
			static_cast<std::uint64_t>(observation.frontend_after),
			pack_u32_pair(observation.selector_before, observation.selector_after),
			pack_u32_pair(observation.count_before, observation.count_after),
			pack_u32_pair(observation.current_record_index_before,
				observation.current_record_index_after),
			pack_u32_pair(observation.record_count_before, observation.record_count_after),
			pack_u32_pair(observation.global_record_count_before,
				observation.global_record_count_after),
			observation.reason,
		});
	}

	void record_frame_flip(const std::uint64_t frontend_frame_id, const std::uint16_t depth,
		const flip_observation& observation, const record_flags flags) noexcept
	{
		auto frame_flags = flags;
		if (depth > 1) frame_flags = with_flag(frame_flags, record_flag::nested);
		// A frontend flip is a valid frame-level event and therefore deliberately
		// has transaction_id zero without being classified as an orphan.
		if (!probe_enabled.load(std::memory_order_relaxed)) return;
		if (observation.selector_before != observation.selector_after ||
			observation.frontend_before != observation.frontend_after)
		{
			frame_flags = with_flag(frame_flags, record_flag::selector_changed);
		}
		const std::array<std::uint64_t, 8> values{
			static_cast<std::uint64_t>(observation.frontend_before),
			static_cast<std::uint64_t>(observation.frontend_after),
			pack_u32_pair(observation.selector_before, observation.selector_after),
			pack_u32_pair(observation.count_before, observation.count_after),
			pack_u32_pair(observation.current_record_index_before,
				observation.current_record_index_after),
			pack_u32_pair(observation.record_count_before, observation.record_count_after),
			pack_u32_pair(observation.global_record_count_before,
				observation.global_record_count_after),
			observation.reason,
		};
		publish_record(0, frontend_frame_id, GetCurrentThreadId(), depth,
			event_kind::flip, observation_stage::snapshot, frame_flags, values);
	}

	void record_ownership_boundary(const std::uint64_t frontend_frame_id,
		const std::uint16_t depth,
		const ownership_boundary_observation& observation,
		const observation_stage stage, record_flags flags) noexcept
	{
		if (!probe_enabled.load(std::memory_order_relaxed)) return;
		if (depth > 1) flags = with_flag(flags, record_flag::nested);
		const std::array<std::uint64_t, 8> values{
			static_cast<std::uint64_t>(observation.boundary),
			static_cast<std::uint64_t>(observation.frontend),
			static_cast<std::uint64_t>(observation.backend_frontend),
			static_cast<std::uint64_t>(observation.record_arena),
			pack_u32_pair(observation.selector, observation.slot_count),
			pack_u32_pair(observation.current_record_index, observation.record_count),
			pack_u32_pair(observation.global_record_count,
				observation.owner_record_index),
			observation.owner_view_hash,
		};
		publish_record(0, frontend_frame_id, GetCurrentThreadId(), depth,
			event_kind::ownership_boundary, stage, flags, values);
	}

	status get_status() noexcept
	{
		const auto newest = trace_sequence.load(std::memory_order_acquire);
		return {
			probe_enabled.load(std::memory_order_acquire),
			newest,
			transaction_sequence.load(std::memory_order_relaxed),
			newest > trace_capacity ? newest - trace_capacity : 0,
			dropped_records.load(std::memory_order_relaxed),
			orphan_records.load(std::memory_order_relaxed),
			duplicate_records.load(std::memory_order_relaxed),
			invalid_slot_records.load(std::memory_order_relaxed),
			count_mismatch_records.load(std::memory_order_relaxed),
			thread_mismatch_records.load(std::memory_order_relaxed),
			view_state_snapshots.load(std::memory_order_relaxed),
			stable_slot_comparisons.load(std::memory_order_relaxed),
			changed_slot_comparisons.load(std::memory_order_relaxed),
			matching_output_copies.load(std::memory_order_relaxed),
			mismatching_output_copies.load(std::memory_order_relaxed),
			camera_state_snapshots.load(std::memory_order_relaxed),
			scoped_camera_state_snapshots.load(std::memory_order_relaxed),
			unscoped_camera_state_snapshots.load(std::memory_order_relaxed),
			set_viewpos_calls.load(std::memory_order_relaxed),
			camera_helper_calls.load(std::memory_order_relaxed),
			slot_initializer_calls.load(std::memory_order_relaxed),
			slot_initializer_slot_changes.load(std::memory_order_relaxed),
			slot_initializer_shared_changes.load(std::memory_order_relaxed),
			slot_initializer_invalid_slots.load(std::memory_order_relaxed),
			descriptor_contract_samples.load(std::memory_order_relaxed),
			descriptor_contract_stable.load(std::memory_order_relaxed),
			descriptor_contract_changes.load(std::memory_order_relaxed),
			descriptor_contract_unreadable.load(std::memory_order_relaxed),
		};
	}

	std::size_t read_recent(record* const output, const std::size_t capacity) noexcept
	{
		if (output == nullptr || capacity == 0) return 0;
		const auto newest = trace_sequence.load(std::memory_order_acquire);
		const auto count = static_cast<std::size_t>((std::min<std::uint64_t>)(newest,
			(std::min<std::uint64_t>)(trace_capacity, capacity)));
		const auto first = newest >= count ? newest - count + 1 : 1;
		std::size_t copied{};
		for (std::size_t offset{}; offset < count; ++offset)
		{
			const auto sequence = first + offset;
			const auto& slot = trace[(sequence - 1) & (trace_capacity - 1)];
			const auto before = slot.version.load(std::memory_order_acquire);
			if ((before & 1) != 0 || before != (sequence << 1)) continue;

			record value{};
			value.sequence = sequence;
			value.timestamp_qpc = slot.timestamp_qpc.load(std::memory_order_relaxed);
			value.transaction_id = slot.transaction_id.load(std::memory_order_relaxed);
			value.frontend_frame_id = slot.frontend_frame_id.load(std::memory_order_relaxed);
			unpack_metadata(slot.metadata.load(std::memory_order_relaxed), value);
			for (std::size_t index{}; index < value.values.size(); ++index)
			{
				value.values[index] = slot.values[index].load(std::memory_order_relaxed);
			}
			const auto after = slot.version.load(std::memory_order_acquire);
			if (before == after) output[copied++] = value;
		}
		return copied;
	}

	std::string format_recent(const std::size_t maximum_records)
	{
		const auto limit = (std::min)(maximum_records, trace_capacity);
		std::vector<record> records(limit);
		const auto count = read_recent(records.data(), records.size());
		const auto probe_status = get_status();
		LARGE_INTEGER frequency{};
		(void)QueryPerformanceFrequency(&frequency);

		std::ostringstream output;
		output << "H2 CPU view-slot trace (newest=" << probe_status.newest_sequence
			<< ", capacity=" << trace_capacity
			<< ", overwritten=" << probe_status.overwrite_count
			<< ", dropped=" << probe_status.dropped_records
			<< ", qpc_frequency=" << frequency.QuadPart << ")\r\n";
		for (std::size_t index{}; index < count; ++index)
		{
			const auto& value = records[index];
			output << value.sequence << " qpc=" << value.timestamp_qpc
				<< " tid=" << value.thread_id
				<< " frame=" << value.frontend_frame_id
				<< " tx=" << value.transaction_id
				<< " depth=" << value.depth
				<< ' ' << to_string(value.kind)
				<< '/' << to_string(value.stage)
				<< " flags=0x" << std::hex << static_cast<unsigned int>(value.flags)
				<< std::dec;
			append_flags(output, value.flags);
			append_payload(output, value);
			output << "\r\n";
		}
		return output.str();
	}

	const char* to_string(const event_kind value) noexcept
	{
		switch (value)
		{
		case event_kind::begin: return "begin";
		case event_kind::callstack: return "callstack";
		case event_kind::callstack_tail: return "callstack_tail";
		case event_kind::view_call: return "view_call";
		case event_kind::frontend: return "frontend";
		case event_kind::slot: return "slot";
		case event_kind::record_reservation: return "record_reservation";
		case event_kind::generator: return "generator";
		case event_kind::view_state: return "view_state";
		case event_kind::camera_state: return "camera_state";
		case event_kind::call_return: return "return";
		case event_kind::end: return "end";
		case event_kind::flip: return "flip";
		case event_kind::slot_initializer: return "slot_initializer";
		case event_kind::descriptor_contract: return "descriptor_contract";
		case event_kind::ownership_boundary: return "ownership_boundary";
		default: return "unknown";
		}
	}

	const char* to_string(const observation_stage value) noexcept
	{
		switch (value)
		{
		case observation_stage::unknown: return "unknown";
		case observation_stage::enter: return "enter";
		case observation_stage::before_call: return "before_call";
		case observation_stage::after_call: return "after_call";
		case observation_stage::leave: return "leave";
		case observation_stage::snapshot: return "snapshot";
		default: return "unknown";
		}
	}

	const char* to_string(const return_source value) noexcept
	{
		switch (value)
		{
		case return_source::unknown: return "unknown";
		case return_source::allocator: return "allocator";
		case return_source::generator: return "generator";
		case return_source::outer: return "outer";
		default: return "unknown";
		}
	}

	const char* to_string(const view_state_source value) noexcept
	{
		switch (value)
		{
		case view_state_source::unknown: return "unknown";
		case view_state_source::outer_scene: return "outer_scene";
		case view_state_source::draw_surface_generator: return "draw_surface_generator";
		default: return "unknown";
		}
	}

	const char* to_string(const camera_state_source value) noexcept
	{
		switch (value)
		{
		case camera_state_source::unknown: return "unknown";
		case camera_state_source::set_viewpos_now: return "set_viewpos_now";
		case camera_state_source::camera_helper: return "camera_helper";
		default: return "unknown";
		}
	}

	const char* to_string(const ownership_boundary_kind value) noexcept
	{
		switch (value)
		{
		case ownership_boundary_kind::frame_state_transition:
			return "frame_state_transition";
		case ownership_boundary_kind::command_cleanup: return "command_cleanup";
		case ownership_boundary_kind::frontend_handoff: return "frontend_handoff";
		default: return "unknown";
		}
	}
}
