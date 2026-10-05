#include "std_include.hpp"

#include "component/vr/engine_stereo_scene_batch_probe.hpp"

#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

namespace
{
	using namespace vr::engine_stereo_scene_batch_probe;

	constexpr std::array<std::size_t, stream_count> count_offsets{
		0x00, 0x18, 0x30, 0x48, 0x60, 0x78,
		0x98, 0xA8, 0xB8, 0xD0, 0xE8, 0xF8,
	};
	constexpr std::array<std::size_t, stream_count> pointer_offsets{
		0x08, 0x20, 0x38, 0x50, 0x68, 0x80,
		0x90, 0xA0, 0xB0, 0xC8, 0xE0, 0xF0,
	};
	constexpr std::array<std::uint32_t, stream_count> strides{
		4, 4, 1, 1, 1, 1, 16, 32, 32, 32, 8, 8,
	};
	constexpr std::array<std::uintptr_t, local_consumer_count> consumers{
		ssr_consumer, code_trans_consumer, glass_consumer, spark_consumer,
	};
	constexpr std::array<local_consumer_kind, local_consumer_count> consumer_kinds{
		local_consumer_kind::ssr,
		local_consumer_kind::code_trans,
		local_consumer_kind::glass,
		local_consumer_kind::spark,
	};
	constexpr std::array<std::uint32_t, local_consumer_count> consumer_streams{
		1, 7, 9, 10,
	};
	constexpr std::array<std::size_t, local_consumer_count> consumer_cursors{
		0x28, 0xE8, 0x110, 0x128,
	};
	constexpr std::array<std::uintptr_t, local_consumer_count> gate_returns{
		0x140797DAA, 0x14079822F, 0x14079844F, 0x1407981B9,
	};
	constexpr std::array<std::uintptr_t, local_consumer_count> second_pass_returns{
		0x140797DFD, 0x140798262, 0x140798490, 0x1407981EC,
	};

	[[noreturn]] void fail(const char* const message)
	{
		std::cerr << "vr-engine-scene-batch-probe: FAIL; " << message << '\n';
		std::exit(1);
	}

	void require(const bool condition, const char* const message)
	{
		if (!condition) fail(message);
	}

	void write_bytes(std::vector<std::uint8_t>& destination,
		const std::size_t offset, const void* const source,
		const std::size_t size)
	{
		require(offset <= destination.size() && size <= destination.size() - offset,
			"synthetic write exceeded its allocation");
		std::memcpy(destination.data() + offset, source, size);
	}

	template <typename Value>
	void write_value(std::vector<std::uint8_t>& destination,
		const std::size_t offset, const Value& value)
	{
		write_bytes(destination, offset, &value, sizeof(value));
	}

	struct synthetic_backend
	{
		std::vector<std::uint8_t> state_bytes;
		std::vector<std::uint8_t> data_bytes;
		std::array<std::vector<std::uint8_t>, stream_count> owned_payloads;

		explicit synthetic_backend(const std::uint8_t seed)
			: state_bytes(backend_data_offset + sizeof(std::uintptr_t) + 16),
			  data_bytes(descriptor_count_offset + sizeof(std::uint32_t))
		{
			const auto data = reinterpret_cast<std::uintptr_t>(data_bytes.data());
			write_value(state_bytes, backend_data_offset, data);
			for (std::size_t stream{}; stream < owned_payloads.size(); ++stream)
			{
				auto& payload = owned_payloads[stream];
				const auto bytes = static_cast<std::size_t>(strides[stream]) * 5;
				payload.resize(bytes);
				for (std::size_t index{}; index < bytes; ++index)
					payload[index] = static_cast<std::uint8_t>(seed + stream * 11 + index);
			}
		}

		void set_descriptor_count(const std::uint32_t count)
		{
			write_value(data_bytes, descriptor_count_offset, count);
		}

		void fill_descriptor(const std::uint32_t descriptor_index,
			const std::uint8_t seed)
		{
			require(descriptor_index < maximum_descriptors,
				"invalid synthetic descriptor index");
			const auto base = descriptor_array_offset +
				static_cast<std::size_t>(descriptor_index) * descriptor_size;
			for (std::size_t index{}; index < descriptor_size; ++index)
				data_bytes[base + index] = static_cast<std::uint8_t>(seed + index * 3);
			for (std::size_t stream{}; stream < stream_count; ++stream)
			{
				set_stream(descriptor_index, static_cast<std::uint32_t>(stream), 5,
					owned_payloads[stream].data());
			}
		}

		void set_stream(const std::uint32_t descriptor_index,
			const std::uint32_t stream_index, const std::uint32_t count,
			const void* const pointer)
		{
			const auto base = descriptor_array_offset +
				static_cast<std::size_t>(descriptor_index) * descriptor_size;
			const auto address = reinterpret_cast<std::uintptr_t>(pointer);
			write_value(data_bytes, base + count_offsets[stream_index], count);
			write_value(data_bytes, base + pointer_offsets[stream_index], address);
		}

		[[nodiscard]] void* state() noexcept
		{
			return state_bytes.data();
		}
	};

	struct synthetic_local_batch
	{
		std::vector<std::uint8_t> source;
		std::vector<std::uint8_t> complex;
		std::vector<std::uint8_t> simple;
		std::array<std::uint8_t, 0x20> header{};
		std::vector<std::uint8_t> context_owner;
		std::array<std::uint8_t, 0xA0> technique{};
		std::array<std::uint8_t, 0x158> material{};
		std::array<std::uint8_t, 0x28> state_bits{};
		std::array<char, 64> material_name{"synthetic_effect_atlas"};
		std::array<char, 64> technique_name{"synthetic_technique"};
		std::array<std::vector<std::uint8_t>, local_consumer_count> payloads{};
		std::uint16_t pass_count{1};

		explicit synthetic_local_batch(const std::uint32_t count,
			const std::uint16_t selected_pass_count = 1)
			: source(descriptor_size), complex(local_complex_state_size),
			  simple(local_simple_state_size), context_owner(0x1960),
			  pass_count(selected_pass_count)
		{
			for (std::size_t index{}; index < consumers.size(); ++index)
			{
				const auto stream = consumer_streams[index];
				payloads[index].resize(static_cast<std::size_t>(count) *
					strides[stream]);
				for (std::size_t byte{}; byte < payloads[index].size(); ++byte)
					payloads[index][byte] = static_cast<std::uint8_t>(index * 31 + byte);
				write_value(source, count_offsets[stream], count);
				const auto pointer = reinterpret_cast<std::uintptr_t>(
					payloads[index].data());
				write_value(source, pointer_offsets[stream], pointer);
			}
			const auto marker = reinterpret_cast<std::uintptr_t>(source.data()) + 0x100;
			std::memcpy(header.data() + 0x10, &marker, sizeof(marker));
			const auto secondary = reinterpret_cast<std::uintptr_t>(
				context_owner.data());
			const auto state = reinterpret_cast<std::uintptr_t>(technique.data());
			std::memcpy(header.data() + 0x08, &secondary, sizeof(secondary));
			write_value(context_owner, 0x1948, state);
			std::memcpy(technique.data() + 0x0A, &pass_count, sizeof(pass_count));
			const auto material_pointer = reinterpret_cast<std::uintptr_t>(material.data());
			const auto pass_pointer = state + 0x10;
			write_value(context_owner, 0x1938, material_pointer);
			write_value(context_owner, 0x1940, std::uint32_t{13});
			write_value(context_owner, 0x1950, pass_pointer);
			const auto material_label = reinterpret_cast<std::uintptr_t>(material_name.data());
			const auto technique_label = reinterpret_cast<std::uintptr_t>(technique_name.data());
			std::memcpy(material.data(), &material_label, sizeof(material_label));
			std::memcpy(technique.data(), &technique_label, sizeof(technique_label));
			material[0xA] = 4;
			material[0xB] = 8;
			material[0x12E] = 1;
			const auto state_table = reinterpret_cast<std::uintptr_t>(state_bits.data());
			std::memcpy(material.data() + 0x150, &state_table, sizeof(state_table));
			initialize_complex(count);
			initialize_simple(count);
		}

		void initialize_complex(const std::uint32_t count)
		{
			const std::uint32_t group_count = 1;
			const std::uint32_t entry_count =
				static_cast<std::uint32_t>(consumers.size());
			write_value(complex, 4, entry_count);
			write_value(complex, 8, group_count);
			const auto marker = reinterpret_cast<std::uintptr_t>(source.data()) + 0x100;
			write_value(complex, 0xC08, marker);
			for (std::size_t index{}; index < consumers.size(); ++index)
			{
				const auto entry = 0xC50 + index * 0x28;
				const std::uint32_t group{};
				write_value(complex, entry + 4, group);
				write_value(complex, entry + 0x18, consumers[index]);
				write_transformed_cursor(complex, 0x10, index, count);
			}
		}

		void initialize_simple(const std::uint32_t count)
		{
			const std::uint32_t entry_count =
				static_cast<std::uint32_t>(consumers.size());
			write_value(simple, 4, entry_count);
			for (std::size_t index{}; index < consumers.size(); ++index)
			{
				const auto entry = 0x150 + index * 0x28;
				write_value(simple, entry + 0x18, consumers[index]);
				write_transformed_cursor(simple, 0x08, index, count);
			}
		}

		void write_transformed_cursor(std::vector<std::uint8_t>& state,
			const std::size_t descriptor_base, const std::size_t consumer_index,
			const std::uint32_t count)
		{
			const auto stream = consumer_streams[consumer_index];
			const auto begin = reinterpret_cast<std::uintptr_t>(
				payloads[consumer_index].data());
			const auto bytes = static_cast<std::uintptr_t>(count) * strides[stream];
			const auto end = consumer_index == 0 ? begin + bytes - strides[stream] :
				begin + bytes;
			const auto cursor = descriptor_base + consumer_cursors[consumer_index];
			write_value(state, cursor, begin);
			write_value(state, cursor + sizeof(std::uintptr_t), end);
		}

		void consume_all(std::vector<std::uint8_t>& state,
			const std::size_t descriptor_base)
		{
			for (std::size_t index{}; index < consumers.size(); ++index)
			{
				const auto cursor = descriptor_base + consumer_cursors[index];
				std::uintptr_t end{};
				std::memcpy(&end, state.data() + cursor + sizeof(std::uintptr_t),
					sizeof(end));
				write_value(state, cursor, end);
			}
		}
	};

	void capture_pair(const std::uint64_t pair_id, synthetic_backend& left,
		synthetic_backend& right, const std::uint32_t ordinal = 3)
	{
		const auto thread = GetCurrentThreadId();
		require(arm(), "probe did not arm");
		require(begin_pair(pair_id, thread), "probe did not begin its pair");
		require(begin_eye(pair_id, 0), "probe did not begin output0");
		observe_backend_view_copy(pair_id, 0, ordinal,
			reinterpret_cast<const void*>(0x1000), left.state());
		require(end_eye(pair_id, 0), "probe did not end output0");
		require(begin_eye(pair_id, 1), "probe did not begin output1");
		observe_backend_view_copy(pair_id, 1, ordinal,
			reinterpret_cast<const void*>(0x2000), right.state());
		require(end_eye(pair_id, 1), "probe did not end output1");
		end_pair(pair_id, true);
	}

	void expect_identical_pair_and_no_writes()
	{
		synthetic_backend backend{7};
		backend.set_descriptor_count(2);
		backend.fill_descriptor(0, 19);
		backend.fill_descriptor(1, 37);
		const auto state_before = backend.state_bytes;
		const auto data_before = backend.data_bytes;
		const auto payloads_before = backend.owned_payloads;

		capture_pair(101, backend, backend);
		report result{};
		get_report(result);
		require(result.current == state::complete && result.pair_id == 101 &&
			result.pair_completions == 1 && result.owner_complete,
			"identical pair did not complete");
		require(result.observations == std::array<std::uint32_t, 2>{1, 1} &&
			result.unique_boundaries == std::array<std::uint32_t, 2>{1, 1} &&
			result.compared_boundaries == 1 && result.compared_descriptors == 2 &&
			result.compared_streams == 2 * stream_count,
			"identical pair did not compare the expected topology");
		require(result.lifecycle_mismatches == 0 &&
			result.boundary_mismatches == 0 &&
			result.descriptor_count_mismatches == 0 &&
			result.backend_data_mismatches == 0 &&
			result.descriptor_mismatches == 0 &&
			result.stream_pointer_mismatches == 0 &&
			result.stream_count_mismatches == 0 &&
			result.stream_content_mismatches == 0 &&
			result.unreadable_sources == 0 && result.sample_count == 0,
			"identical pair reported a false mismatch");
		require(backend.state_bytes == state_before &&
			backend.data_bytes == data_before &&
			backend.owned_payloads == payloads_before,
			"probe modified an identical synthetic backend");
	}

	void expect_structural_and_content_mismatches()
	{
		synthetic_backend left{11};
		synthetic_backend right{11};
		left.set_descriptor_count(1);
		right.set_descriptor_count(2);
		left.fill_descriptor(0, 23);
		right.fill_descriptor(0, 23);
		right.fill_descriptor(1, 41);

		// Keep all but three source pointers shared so each mismatch class has a
		// precise expected lower bound. Stream 0 differs only by pointer, stream 1
		// differs by count, and stream 2 has equal shape but different contents.
		for (std::uint32_t stream = 3; stream < stream_count; ++stream)
			right.set_stream(0, stream, 5, left.owned_payloads[stream].data());
		right.owned_payloads[0] = left.owned_payloads[0];
		right.owned_payloads[1] = left.owned_payloads[1];
		right.set_stream(0, 0, 5, right.owned_payloads[0].data());
		right.set_stream(0, 1, 4, right.owned_payloads[1].data());
		right.owned_payloads[2][0] ^= 0x5A;
		right.set_stream(0, 2, 5, right.owned_payloads[2].data());

		const auto left_state_before = left.state_bytes;
		const auto left_data_before = left.data_bytes;
		const auto left_payloads_before = left.owned_payloads;
		const auto right_state_before = right.state_bytes;
		const auto right_data_before = right.data_bytes;
		const auto right_payloads_before = right.owned_payloads;

		capture_pair(202, left, right);
		report result{};
		get_report(result);
		require(result.current == state::complete && result.owner_complete,
			"mismatching pair did not complete non-fatally");
		require(result.descriptor_count_mismatches == 1 &&
			result.backend_data_mismatches == 1 &&
			result.descriptor_mismatches >= 2,
			"descriptor topology mismatch was not reported");
		require(result.stream_pointer_mismatches == 3 &&
			result.stream_count_mismatches == 1 &&
			result.stream_content_mismatches == 1,
			"stream pointer/count/content mismatches were not separated");
		require(result.sample_count != 0 &&
			result.sample_count <= maximum_mismatch_samples,
			"mismatch details were not bounded and retained");
		require(left.state_bytes == left_state_before &&
			left.data_bytes == left_data_before &&
			left.owned_payloads == left_payloads_before &&
			right.state_bytes == right_state_before &&
			right.data_bytes == right_data_before &&
			right.owned_payloads == right_payloads_before,
			"probe modified a mismatching synthetic backend");
	}

	void expect_lifecycle_bounds_and_invalid_pointer_guard()
	{
		synthetic_backend backend{31};
		backend.set_descriptor_count(1);
		backend.fill_descriptor(0, 53);
		backend.set_stream(0, 0, 4, reinterpret_cast<const void*>(1));
		const auto state_before = backend.state_bytes;
		const auto data_before = backend.data_bytes;
		const auto payloads_before = backend.owned_payloads;
		const auto thread = GetCurrentThreadId();

		require(arm(), "lifecycle probe did not arm");
		require(!begin_pair(0, thread), "zero pair id was accepted");
		require(begin_pair(303, thread), "valid pair after rejection was not accepted");
		require(!arm(), "arm disturbed an executing pair");
		require(!begin_eye(303, 1), "output1 began before output0");
		require(begin_eye(303, 0), "output0 did not begin after lifecycle rejection");
		observe_backend_view_copy(303, 0,
			static_cast<std::uint32_t>(maximum_boundaries_per_eye), nullptr,
			backend.state());
		observe_backend_view_copy(303, 0, 0, nullptr, backend.state());
		require(end_eye(303, 0), "output0 did not end");
		require(begin_eye(303, 1), "output1 did not begin");
		require(end_eye(303, 1), "output1 did not end");
		end_pair(303, false);

		report result{};
		get_report(result);
		require(result.current == state::complete && !result.owner_complete &&
			result.lifecycle_mismatches >= 4,
			"lifecycle violations were not retained non-fatally");
		require(result.boundary_overflows == 1 &&
			result.boundary_mismatches == 1 && result.unreadable_sources >= 1,
			"boundary or invalid-pointer evidence was lost");
		require(backend.state_bytes == state_before &&
			backend.data_bytes == data_before &&
			backend.owned_payloads == payloads_before,
			"guarded lifecycle observation modified engine memory");

		require(arm(), "teardown-mismatch probe did not arm");
		require(begin_pair(304, thread), "teardown-mismatch pair did not begin");
		end_pair(999, false);
		get_report(result);
		require(result.current == state::complete &&
			result.lifecycle_mismatches == 1 && !result.owner_complete,
			"mismatched teardown stranded the one-shot in recording");
	}

	void expect_large_payload_window_is_bounded()
	{
		synthetic_backend left{67};
		synthetic_backend right{67};
		left.set_descriptor_count(1);
		right.set_descriptor_count(1);
		left.fill_descriptor(0, 89);
		right.fill_descriptor(0, 89);
		for (std::uint32_t stream{}; stream < stream_count; ++stream)
		{
			right.set_stream(0, stream, 5, left.owned_payloads[stream].data());
		}

		constexpr std::uint32_t large_stream = 7;
		constexpr std::size_t large_bytes = maximum_hashed_bytes_per_stream * 2;
		left.owned_payloads[large_stream].resize(large_bytes);
		right.owned_payloads[large_stream].resize(large_bytes);
		for (std::size_t index{}; index < large_bytes; ++index)
		{
			left.owned_payloads[large_stream][index] =
				static_cast<std::uint8_t>(index * 17 + 3);
		}
		right.owned_payloads[large_stream] = left.owned_payloads[large_stream];
		right.owned_payloads[large_stream].back() ^= 0x5A;
		const auto large_count = static_cast<std::uint32_t>(
			large_bytes / strides[large_stream]);
		left.set_stream(0, large_stream, large_count,
			left.owned_payloads[large_stream].data());
		right.set_stream(0, large_stream, large_count,
			right.owned_payloads[large_stream].data());

		capture_pair(404, left, right);
		report result{};
		get_report(result);
		constexpr std::uint64_t other_stream_bytes = 540;
		require(result.current == state::complete && result.owner_complete &&
			result.stream_content_mismatches == 1,
			"large payload windows did not retain the tail difference");
		require(result.sampled_payload_bytes ==
			2 * (maximum_hashed_bytes_per_stream + other_stream_bytes),
			"large payload hashing exceeded its declared per-stream budget");
	}

	void capture_local_eye(const std::uint64_t pair_id, const std::uint32_t eye,
		synthetic_local_batch& batch, const local_executor_kind kind,
		const bool consume)
	{
		std::array<std::uintptr_t, 2> pair16{
			0x11110000u + eye, 0x22220000u + eye,
		};
		primary_token primary{};
		executor_token executor{};
		require(begin_primary(pair_id, eye, &batch, pair16.data(),
			batch.source.data(), 7, 0x14072DA62, primary),
			"local primary scope did not begin");
		auto& local_state = kind == local_executor_kind::complex ?
			batch.complex : batch.simple;
		require(begin_executor(pair_id, eye, kind, batch.header.data(),
			local_state.data(),
			kind == local_executor_kind::complex ? 0x1407A2FA3 : 0x1407A291B,
			executor), "local executor scope did not begin");
		for (std::size_t index{}; index < consumer_kinds.size(); ++index)
		{
			const auto accepted = index != 2;
			observe_consumer_gate(pair_id, eye, consumer_kinds[index],
				batch.header.data(), 0xCAFE0000ull + index, accepted,
				gate_returns[index]);
			if (accepted && batch.pass_count != 1)
			{
				observe_consumer_second_pass(pair_id, eye, consumer_kinds[index],
					batch.header.data(), second_pass_returns[index]);
			}
		}
		if (consume)
			batch.consume_all(local_state,
				kind == local_executor_kind::complex ? 0x10 : 0x08);
		require(end_executor(executor, batch.header.data(), local_state.data()),
			"local executor scope did not end");
		require(end_primary(primary, &batch, pair16.data(), batch.source.data()),
			"local primary scope did not end");
	}

	void expect_complex_local_consumer_census()
	{
		synthetic_local_batch left{5, 2};
		synthetic_local_batch right{3, 2};
		const std::array<std::uintptr_t, local_hook_count> targets{
			0x1407A2E60, 0x1407A2A90, 0x1407A2B70,
			0x1407A48B0, 0x1407A48B0, 0x1407A48B0, 0x1407A48B0,
			0x1407A4950, 0x1407A4950, 0x1407A4950, 0x1407A4950,
		};
		note_hooks(true, 1, 0, targets);
		require(arm(), "complex local census did not arm");
		const auto thread = GetCurrentThreadId();
		require(begin_pair(505, thread), "complex local pair did not begin");
		require(begin_eye(505, 0), "complex local output0 did not begin");
		capture_local_eye(505, 0, left, local_executor_kind::complex, true);
		require(end_eye(505, 0), "complex local output0 did not end");
		require(begin_eye(505, 1), "complex local output1 did not begin");
		capture_local_eye(505, 1, right, local_executor_kind::complex, true);
		require(end_eye(505, 1), "complex local output1 did not end");
		end_pair(505, true);

		report result{};
		get_report(result);
		require(result.current == state::complete && result.owner_complete &&
			result.hooks_installed && result.hook_install_attempts == 1 &&
			result.hook_install_failures == 0 && result.hook_targets == targets,
			"complex local hook metadata was not retained");
		require(result.hook_callsites_observed[0] &&
			!result.hook_callsites_observed[1] &&
			result.hook_callsites_observed[2],
			"complex local call-site coverage is incorrect");
		require(result.primary_calls == std::array<std::uint64_t, 2>{1, 1} &&
			result.primary_source_mutations == std::array<std::uint64_t, 2>{0, 0} &&
			result.primary_pair16_mutations == std::array<std::uint64_t, 2>{0, 0},
			"complex local primary observation was not read-only");
		require(result.complex_executor_calls ==
				std::array<std::uint64_t, 2>{1, 1} &&
			result.complex_group_counts == std::array<std::uint64_t, 2>{1, 1} &&
			result.complex_entry_counts == std::array<std::uint64_t, 2>{4, 4} &&
			result.local_parse_failures == 0 &&
			result.local_capacity_overflows == 0 &&
			result.local_lifecycle_mismatches == 0,
			"complex local topology was not parsed exactly");
		require(result.local_payload_comparisons == local_consumer_count &&
			result.local_payload_mismatches == local_consumer_count &&
			result.local_payload_unreadable == 0 &&
			result.local_payload_pair_misses == 0 &&
			result.local_source_pointer_mismatches == local_consumer_count &&
			result.local_source_count_mismatches == local_consumer_count,
			"complex local payload differences were not classified exactly");
		require(result.local_gate_comparisons == local_consumer_count &&
			result.local_gate_result_mismatches == 0 &&
			result.local_gate_key_mismatches == 0 &&
			result.local_gate_pass_count_mismatches == 0 &&
			result.local_gate_context_unreadable == 0 &&
			result.local_gate_pair_misses == 0 &&
			result.local_gate_sample_counts ==
				std::array<std::size_t, 2>{local_consumer_count,
					local_consumer_count},
			"complex local gate observations did not pair exactly");
		for (std::size_t index{}; index < local_consumer_count; ++index)
		{
			const auto& consumer = result.local_consumers[index];
			require(consumer.consumer == consumers[index],
				"complex local consumer identity is incorrect");
			require(consumer.eyes[0].calls == 1 &&
				consumer.eyes[1].calls == 1 &&
				consumer.eyes[0].entries == 5 &&
				consumer.eyes[1].entries == 3 &&
				consumer.eyes[0].cursor_advances == 1 &&
				consumer.eyes[1].cursor_advances == 1 &&
				consumer.eyes[0].cursor_regressions == 0 &&
				consumer.eyes[1].cursor_regressions == 0 &&
				consumer.eyes[0].unresolved_sources == 0 &&
				consumer.eyes[1].unresolved_sources == 0 &&
				consumer.eyes[0].source_shape_mismatches == 0 &&
				consumer.eyes[1].source_shape_mismatches == 0 &&
				consumer.eyes[0].payload_hashes == 1 &&
				consumer.eyes[1].payload_hashes == 1 &&
				consumer.eyes[0].gate_calls == 1 &&
				consumer.eyes[1].gate_calls == 1 &&
				consumer.eyes[0].latest_gate_pass_count == 2 &&
				consumer.eyes[1].latest_gate_pass_count == 2 &&
				consumer.eyes[0].second_pass_calls == (index == 2 ? 0 : 1) &&
				consumer.eyes[1].second_pass_calls == (index == 2 ? 0 : 1),
				"complex local consumer aggregation is incorrect");
		}
		require(result.local_sample_counts ==
			std::array<std::size_t, 2>{4, 4} &&
			result.dropped_local_samples == std::array<std::uint64_t, 2>{0, 0},
			"complex local samples were not retained exactly");
	}

	void expect_simple_local_consumer_census_is_read_only()
	{
		synthetic_local_batch left{4};
		synthetic_local_batch right{4};
		const auto left_source_before = left.source;
		const auto left_state_before = left.simple;
		const auto right_source_before = right.source;
		const auto right_state_before = right.simple;
		require(arm(), "simple local census did not arm");
		const auto thread = GetCurrentThreadId();
		require(begin_pair(606, thread), "simple local pair did not begin");
		require(begin_eye(606, 0), "simple local output0 did not begin");
		capture_local_eye(606, 0, left, local_executor_kind::simple, false);
		require(end_eye(606, 0), "simple local output0 did not end");
		require(begin_eye(606, 1), "simple local output1 did not begin");
		capture_local_eye(606, 1, right, local_executor_kind::simple, false);
		require(end_eye(606, 1), "simple local output1 did not end");
		end_pair(606, true);

		report result{};
		get_report(result);
		require(result.simple_executor_calls ==
				std::array<std::uint64_t, 2>{1, 1} &&
			result.simple_entry_counts == std::array<std::uint64_t, 2>{4, 4} &&
			result.hook_callsites_observed[1] && result.local_parse_failures == 0,
			"simple local topology was not parsed exactly");
		require(result.local_payload_comparisons == local_consumer_count &&
			result.local_payload_mismatches == 0 &&
			result.local_payload_unreadable == 0 &&
			result.local_payload_pair_misses == 0 &&
			result.local_gate_comparisons == local_consumer_count &&
			result.local_gate_result_mismatches == 0 &&
			result.local_gate_key_mismatches == 0 &&
			result.local_gate_pass_count_mismatches == 0 &&
			result.local_gate_context_unreadable == 0 &&
			result.local_gate_pair_misses == 0,
			"simple local cross-eye evidence was not retained exactly");
		for (const auto& consumer : result.local_consumers)
		{
			require(consumer.eyes[0].calls == 1 &&
				consumer.eyes[1].calls == 1 &&
				consumer.eyes[0].cursor_unchanged == 1 &&
				consumer.eyes[1].cursor_unchanged == 1 &&
				consumer.eyes[0].payload_hashes == 1 &&
				consumer.eyes[1].payload_hashes == 1 &&
				consumer.eyes[0].gate_calls == 1 &&
				consumer.eyes[1].gate_calls == 1 &&
				consumer.eyes[0].second_pass_calls == 0 &&
				consumer.eyes[1].second_pass_calls == 0,
				"simple local cursor result is incorrect");
		}
		require(left.source == left_source_before && left.simple == left_state_before &&
			right.source == right_source_before && right.simple == right_state_before,
			"simple local observer modified synthetic H2 state");
	}

	void expect_gate_family_quotas_and_no_reads_after_budget()
	{
		synthetic_local_batch batch{2};
		const auto material_before = batch.material;
		const auto technique_before = batch.technique;
		constexpr std::array<std::uint32_t, 4> gate_counts{139, 54, 180, 28};
		require(arm() && begin_pair(808, GetCurrentThreadId()), "quota pair did not begin");
		for (std::uint32_t eye{}; eye < 2; ++eye)
		{
			require(begin_eye(808, eye), "quota eye did not begin");
			for (std::size_t family{}; family < local_consumer_count; ++family)
			{
				for (std::uint32_t call{}; call < gate_counts[family]; ++call)
				{
					// No memory inspection is allowed once this family's detail quota
					// is exhausted. An invalid pointer must remain entirely unread.
					const void* context = call < maximum_gate_samples_per_consumer ?
						batch.header.data() : reinterpret_cast<const void*>(1);
					observe_consumer_gate(808, eye, consumer_kinds[family], context,
						call, true, gate_returns[family]);
				}
			}
			require(end_eye(808, eye), "quota eye did not end");
		}
		end_pair(808, true);
		report result{};
		get_report(result);
		require(result.local_gate_comparisons == maximum_local_gate_samples_per_eye &&
			result.selection_comparisons == maximum_local_gate_samples_per_eye &&
			result.selection_unreadable == 0 && result.selection_identity_mismatches == 0 &&
			result.selection_pass_mismatches == 0 && result.selection_atlas_mismatches == 0 &&
			result.selection_state_comparisons == maximum_local_gate_samples_per_eye &&
			result.selection_state_unreadable == 0 && result.selection_state_mismatches == 0,
			"bounded material selection did not compare all four families");
		for (std::size_t family{}; family < local_consumer_count; ++family)
			for (std::uint32_t eye{}; eye < 2; ++eye)
			{
				const auto& value = result.local_consumers[family].eyes[eye];
				require(value.gate_calls == gate_counts[family] &&
					value.gate_samples == maximum_gate_samples_per_consumer &&
					value.gate_samples_dropped == gate_counts[family] - maximum_gate_samples_per_consumer &&
					value.gate_context_failures == 0,
					"first family starved later families or over-budget context was read");
			}
		// A fresh single sampled gate has the same read cost per retained detail;
		// 802 calls above must not perform 802 context/material inspections.
		// Three context/technique reads, four selection/pass/state reads and three
		// optional name reads for each retained sample (not each gate call).
		require(result.guarded_read_timing.calls == 2 * maximum_local_gate_samples_per_eye * 10,
			"over-budget gates performed memory reads");
		require(result.started_utc_ms != 0 && result.completed_utc_ms != 0 &&
			result.pair_wall_ns >= result.eye_wall_ns[0] + result.eye_wall_ns[1] &&
			result.observer_timings[static_cast<std::size_t>(observer_stage::gate)].calls == 802 &&
			result.observer_timings[static_cast<std::size_t>(observer_stage::finish)].calls == 1,
			"observer timing lifecycle is incomplete");
		require(batch.material == material_before && batch.technique == technique_before,
			"selection observer mutated material assets");
		require(arm(), "quota report could not be rearmed");
		get_report(result);
		require(result.pair_wall_ns == 0 && result.selection_comparisons == 0 &&
			result.virtual_query_timing.calls == 0, "rearm retained old timing/selection data");
	}

	void expect_material_selection_differences_and_invalid_pass()
	{
		synthetic_local_batch batch{2};
		require(arm() && begin_pair(809, GetCurrentThreadId()), "selection pair did not begin");
		for (std::uint32_t eye{}; eye < 2; ++eye)
		{
			require(begin_eye(809, eye), "selection eye did not begin");
			if (eye == 1)
			{
				batch.material[0xB] = 16;
				batch.state_bits[0] = 1;
				batch.technique[0x10 + 0x20] = 1; // different pixel-shader asset identity
				write_value(batch.context_owner, 0x1940, std::uint32_t{14});
			}
			observe_consumer_gate(809, eye, local_consumer_kind::glass, batch.header.data(), 1, true, gate_returns[2]);
			write_value(batch.context_owner, 0x1950, std::uintptr_t{1});
			observe_consumer_gate(809, eye, local_consumer_kind::spark, batch.header.data(), 2, true, gate_returns[3]);
			write_value(batch.context_owner, 0x1950, reinterpret_cast<std::uintptr_t>(batch.technique.data()) + 0x10);
			require(end_eye(809, eye), "selection eye did not end");
		}
		end_pair(809, true);
		report result{};
		get_report(result);
		require(result.owner_complete && result.selection_comparisons == 1 &&
			result.selection_unreadable == 1 && result.selection_identity_mismatches == 1 &&
			result.selection_pass_mismatches == 1 && result.selection_atlas_mismatches == 1 &&
			result.selection_state_comparisons == 1 && result.selection_state_mismatches == 1,
			"selection differences or invalid pass were not classified");
	}

	void expect_local_bounds_fail_non_fatally()
	{
		synthetic_local_batch batch{2};
		require(arm(), "bounded local census did not arm");
		const auto thread = GetCurrentThreadId();
		require(begin_pair(707, thread), "bounded local pair did not begin");
		require(begin_eye(707, 0), "bounded local output0 did not begin");
		const std::uint32_t too_many_groups = 10;
		write_value(batch.complex, 8, too_many_groups);
		executor_token token{};
		require(!begin_executor(707, 0, local_executor_kind::complex,
			batch.header.data(),
			batch.complex.data(), 1, token) && !token.active,
			"oversized complex group table was accepted");
		const std::uint32_t too_many_simple_entries = 12;
		write_value(batch.simple, 4, too_many_simple_entries);
		require(!begin_executor(707, 0, local_executor_kind::simple,
			batch.header.data(),
			batch.simple.data(), 2, token) && !token.active,
			"oversized simple entry table was accepted");
		require(end_eye(707, 0), "bounded local output0 did not end");
		require(begin_eye(707, 1), "bounded local output1 did not begin");
		require(end_eye(707, 1), "bounded local output1 did not end");
		end_pair(707, true);
		report result{};
		get_report(result);
		require(result.current == state::complete && result.owner_complete &&
			result.local_parse_failures == 2 &&
			result.local_lifecycle_mismatches == 0 &&
			result.lifecycle_mismatches == 0,
			"invalid local topology affected the production lifecycle");
	}
}

int main()
{
	expect_identical_pair_and_no_writes();
	expect_structural_and_content_mismatches();
	expect_lifecycle_bounds_and_invalid_pointer_guard();
	expect_large_payload_window_is_bounded();
	expect_complex_local_consumer_census();
	expect_simple_local_consumer_census_is_read_only();
	expect_gate_family_quotas_and_no_reads_after_budget();
	expect_material_selection_differences_and_invalid_pass();
	expect_local_bounds_fail_non_fatally();
	std::cout << "vr-engine-scene-batch-probe: PASS\n";
	return 0;
}
