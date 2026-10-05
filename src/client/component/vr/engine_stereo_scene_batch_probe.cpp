#include <std_include.hpp>

#include "engine_stereo_scene_batch_probe.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <limits>
#include <mutex>

namespace vr::engine_stereo_scene_batch_probe
{
	namespace
	{
		struct stream_layout
		{
			std::size_t count_offset;
			std::size_t pointer_offset;
			std::uint32_t stride;
		};

		constexpr std::array<stream_layout, stream_count> stream_layouts{{
			{0x00, 0x08, 4},
			{0x18, 0x20, 4},
			{0x30, 0x38, 1},
			{0x48, 0x50, 1},
			{0x60, 0x68, 1},
			{0x78, 0x80, 1},
			{0x98, 0x90, 16},
			{0xA8, 0xA0, 32},
			{0xB8, 0xB0, 32},
			{0xD0, 0xC8, 32},
			{0xE8, 0xE0, 8},
			{0xF8, 0xF0, 8},
		}};

		static_assert(descriptor_array_offset + maximum_descriptors * descriptor_size ==
			descriptor_count_offset);
		static_assert([]
		{
			for (const auto& layout : stream_layouts)
			{
				if (layout.count_offset + sizeof(std::uint32_t) > descriptor_size ||
					layout.pointer_offset + sizeof(std::uintptr_t) > descriptor_size)
				{
					return false;
				}
			}
			return true;
		}());

		struct stream_snapshot
		{
			std::uintptr_t pointer{};
			std::uint64_t byte_count{};
			std::uint64_t content_hash{};
			std::uint32_t count{};
			std::uint32_t stride{};
			std::uint32_t sampled_bytes{};
			std::uint8_t sampled_windows{};
			bool readable{};
		};

		struct descriptor_snapshot
		{
			std::uintptr_t address{};
			std::uint64_t content_hash{};
			bool readable{};
			std::array<stream_snapshot, stream_count> streams{};
		};

		struct boundary_snapshot
		{
			bool present{};
			bool backend_data_readable{};
			bool descriptor_count_readable{};
			std::uintptr_t owner_record{};
			std::uintptr_t backend_state{};
			std::uintptr_t backend_data{};
			std::uint32_t descriptor_count{};
			std::array<descriptor_snapshot, maximum_descriptors> descriptors{};
		};

		inline constexpr std::size_t maximum_primary_scopes = 4;
		inline constexpr std::size_t maximum_executor_scopes = 2;
		inline constexpr std::size_t local_entry_stride = 0x28;
		inline constexpr std::size_t local_entry_consumer_offset = 0x18;
		inline constexpr std::size_t complex_descriptor_base = 0x10;
		inline constexpr std::size_t complex_source_marker_base = 0xC08;
		inline constexpr std::size_t complex_entry_base = 0xC50;
		inline constexpr std::size_t simple_descriptor_base = 0x08;
		inline constexpr std::size_t simple_entry_base = 0x150;

		struct local_consumer_layout
		{
			local_consumer_kind kind;
			std::uintptr_t function;
			std::uint32_t source_stream;
			std::size_t cursor_offset;
			std::uint32_t stride;
			bool inclusive_end;
		};

		constexpr std::array<local_consumer_layout, local_consumer_count>
			local_consumer_layouts{{
				{local_consumer_kind::ssr, ssr_consumer, 1, 0x28, 4, true},
				{local_consumer_kind::code_trans, code_trans_consumer, 7, 0xE8,
					32, false},
				{local_consumer_kind::glass, glass_consumer, 9, 0x110, 32, false},
				{local_consumer_kind::spark, spark_consumer, 10, 0x128, 8, false},
			}};

		struct cursor_snapshot
		{
			std::uintptr_t begin{};
			std::uintptr_t end{};
			std::uintptr_t saved{};
		};

		struct primary_scope
		{
			bool active{};
			std::uint64_t cookie{};
			std::uint64_t pair_id{};
			std::uint32_t eye{};
			std::uint32_t thread{};
			std::uintptr_t scene_context{};
			std::uintptr_t pair16{};
			std::uintptr_t source_descriptor{};
			std::uint32_t mode{};
			std::uintptr_t return_address{};
			std::array<std::uint8_t, 16> pair16_before{};
			std::array<std::uint8_t, descriptor_size> source_before{};
		};

		struct local_entry_observation
		{
			bool present{};
			local_consumer_kind consumer{local_consumer_kind::ssr};
			std::uint32_t entry_index{};
			std::uint32_t group_index{};
			std::uint32_t call_ordinal{};
			std::size_t cursor_address_offset{};
			std::uintptr_t source_descriptor{};
			std::uint32_t source_count{};
			std::uintptr_t source_pointer{};
			std::uint64_t source_byte_count{};
			std::uint64_t source_content_hash{};
			std::uint32_t source_sampled_bytes{};
			std::uint8_t source_sampled_windows{};
			bool source_content_readable{};
			cursor_snapshot cursor_before{};
		};

		struct consumer_context_snapshot
		{
			std::uintptr_t address{};
			std::array<std::uintptr_t, 4> words{};
			std::uintptr_t secondary{};
			std::uintptr_t technique{};
			std::uint16_t pass_count{};
			bool readable{};
		};

		struct executor_scope
		{
			bool active{};
			std::uint64_t cookie{};
			std::uint64_t pair_id{};
			std::uint32_t eye{};
			std::uint32_t thread{};
			local_executor_kind kind{local_executor_kind::complex};
			std::uintptr_t header{};
			std::uintptr_t local_state{};
			std::uintptr_t return_address{};
			std::uint32_t entry_cursor{};
			std::uint32_t entry_count{};
			std::uint32_t group_count{};
			std::uint32_t observation_count{};
			std::array<local_entry_observation,
				local_complex_maximum_entries> observations{};
		};

		struct hook_metadata
		{
			bool installed{};
			std::uint64_t attempts{};
			std::uint64_t failures{};
			std::array<std::uintptr_t, local_hook_count> targets{};
		};

		std::mutex state_mutex;
		std::atomic<state> published_state{state::idle};
		report evidence{};
		std::array<boundary_snapshot, maximum_boundaries_per_eye> output0{};
		std::array<bool, maximum_boundaries_per_eye> output1_present{};
		std::array<primary_scope, maximum_primary_scopes> primary_scopes{};
		std::array<executor_scope, maximum_executor_scopes> executor_scopes{};
		std::uint64_t next_scope_cookie{1};
		hook_metadata hooks{};
		std::uint64_t pair_started_ns{};
		std::array<std::uint64_t, 2> eye_started_ns{};

		std::uint64_t now_ns() noexcept
		{
			return static_cast<std::uint64_t>(std::chrono::duration_cast<
				std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
		}

		std::uint64_t utc_ms() noexcept
		{
			return static_cast<std::uint64_t>(std::chrono::duration_cast<
				std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
		}

		void record_timing(timing_sample& sample, const std::uint64_t elapsed) noexcept
		{
			++sample.calls;
			sample.total_ns += elapsed;
			sample.maximum_ns = (std::max)(sample.maximum_ns, elapsed);
		}

		// The original H2 call is always outside this scope. Only the one-shot
		// observer's own work and mutex acquisition are measured, without logging,
		// allocations or runtime calls in the hook.
		class observation_scope
		{
		public:
			explicit observation_scope(const observer_stage stage) noexcept
				: started_(now_ns()), lock_(state_mutex), stage_(stage), eye_(evidence.active_eye)
			{
				record_timing(evidence.observer_lock_wait, now_ns() - started_);
			}
			~observation_scope()
			{
				const auto elapsed = now_ns() - started_;
				record_timing(evidence.observer_timings[static_cast<std::size_t>(stage_)], elapsed);
				if (eye_ < 2) evidence.eye_observer_ns[eye_] += elapsed;
			}
		private:
			std::uint64_t started_;
			std::lock_guard<std::mutex> lock_;
			observer_stage stage_;
			std::uint32_t eye_;
		};

		[[nodiscard]] bool add_address(const std::uintptr_t base,
			const std::uint64_t offset, std::uintptr_t& output) noexcept
		{
			if (offset > std::numeric_limits<std::uintptr_t>::max() - base) return false;
			output = base + static_cast<std::uintptr_t>(offset);
			return true;
		}

		[[nodiscard]] bool readable_protection(const DWORD protection) noexcept
		{
			if ((protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
			switch (protection & 0xFFu)
			{
			case PAGE_READONLY:
			case PAGE_READWRITE:
			case PAGE_WRITECOPY:
			case PAGE_EXECUTE_READ:
			case PAGE_EXECUTE_READWRITE:
			case PAGE_EXECUTE_WRITECOPY:
				return true;
			default:
				return false;
			}
		}

		[[nodiscard]] bool readable_range(const void* const source,
			const std::size_t bytes) noexcept
		{
			if (bytes == 0) return true;
			if (source == nullptr) return false;
			const auto begin = reinterpret_cast<std::uintptr_t>(source);
			if (bytes > std::numeric_limits<std::uintptr_t>::max() - begin)
				return false;
			const auto end = begin + bytes;
			auto cursor = begin;
			while (cursor < end)
			{
				MEMORY_BASIC_INFORMATION information{};
				const auto query_started = now_ns();
				const auto queried = VirtualQuery(reinterpret_cast<const void*>(cursor),
					&information, sizeof(information));
				record_timing(evidence.virtual_query_timing, now_ns() - query_started);
				if (queried == 0 || information.State != MEM_COMMIT ||
					!readable_protection(information.Protect))
				{
					return false;
				}
				const auto region_base = reinterpret_cast<std::uintptr_t>(
					information.BaseAddress);
				if (information.RegionSize >
					std::numeric_limits<std::uintptr_t>::max() - region_base)
				{
					return false;
				}
				const auto region_end = region_base + information.RegionSize;
				if (region_end <= cursor) return false;
				cursor = (std::min)(end, region_end);
			}
			return true;
		}

		[[nodiscard]] bool guarded_copy(void* const destination,
			const void* const source, const std::size_t bytes) noexcept
		{
			const auto started = now_ns();
			if (!readable_range(source, bytes))
			{
				record_timing(evidence.guarded_read_timing, now_ns() - started);
				return false;
			}
			__try
			{
				std::memcpy(destination, source, bytes);
				record_timing(evidence.guarded_read_timing, now_ns() - started);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				record_timing(evidence.guarded_read_timing, now_ns() - started);
				return false;
			}
		}

		template <typename Value>
		[[nodiscard]] bool guarded_read(const std::uintptr_t address,
			Value& output) noexcept
		{
			return guarded_copy(&output, reinterpret_cast<const void*>(address),
				sizeof(output));
		}

		void hash_byte(std::uint64_t& hash, const std::uint8_t value) noexcept
		{
			hash ^= value;
			hash *= 1099511628211ull;
		}

		void hash_u64(std::uint64_t& hash, const std::uint64_t value) noexcept
		{
			for (std::uint32_t shift{}; shift < 64; shift += 8)
				hash_byte(hash, static_cast<std::uint8_t>(value >> shift));
		}

		[[nodiscard]] std::uint64_t hash_bytes(const std::uint8_t* const bytes,
			const std::size_t size) noexcept
		{
			std::uint64_t hash = 1469598103934665603ull;
			for (std::size_t index{}; index < size; ++index) hash_byte(hash, bytes[index]);
			return hash;
		}

		[[nodiscard]] bool hash_payload(const std::uintptr_t pointer,
			const std::uint64_t byte_count, std::uint64_t& output_hash,
			std::uint32_t& sampled_bytes, std::uint8_t& sampled_windows) noexcept
		{
			output_hash = 1469598103934665603ull;
			sampled_bytes = 0;
			sampled_windows = 0;
			hash_u64(output_hash, byte_count);
			if (byte_count == 0) return true;
			if (pointer == 0) return false;

			constexpr std::size_t window_bytes =
				maximum_hashed_bytes_per_stream / 4;
			std::array<std::uint8_t, maximum_hashed_bytes_per_stream> buffer{};
			if (byte_count <= maximum_hashed_bytes_per_stream)
			{
				const auto bytes = static_cast<std::size_t>(byte_count);
				if (!guarded_copy(buffer.data(), reinterpret_cast<const void*>(pointer),
					bytes))
				{
					return false;
				}
				hash_u64(output_hash, 0);
				for (std::size_t index{}; index < bytes; ++index)
					hash_byte(output_hash, buffer[index]);
				sampled_bytes = static_cast<std::uint32_t>(bytes);
				sampled_windows = 1;
				return true;
			}

			const auto last_offset = byte_count - window_bytes;
			const std::array<std::uint64_t, 4> offsets{
				0,
				last_offset / 3,
				(last_offset / 3) * 2,
				last_offset,
			};
			for (const auto offset : offsets)
			{
				std::uintptr_t address{};
				if (!add_address(pointer, offset, address) ||
					!guarded_copy(buffer.data(), reinterpret_cast<const void*>(address),
						window_bytes))
				{
					return false;
				}
				hash_u64(output_hash, offset);
				for (std::size_t index{}; index < window_bytes; ++index)
					hash_byte(output_hash, buffer[index]);
				sampled_bytes += static_cast<std::uint32_t>(window_bytes);
				++sampled_windows;
			}
			return true;
		}

		void add_sample(const mismatch_kind kind, const std::uint32_t ordinal,
			const std::uint32_t descriptor_index, const std::uint32_t stream_index,
			const std::array<std::uintptr_t, 2>& addresses,
			const std::array<std::uint64_t, 2>& values,
			const std::array<std::uint64_t, 2>& byte_counts = {}) noexcept
		{
			if (evidence.sample_count >= evidence.samples.size())
			{
				++evidence.dropped_samples;
				return;
			}
			auto& sample = evidence.samples[evidence.sample_count++];
			sample.kind = kind;
			sample.boundary_ordinal = ordinal;
			sample.descriptor_index = descriptor_index;
			sample.stream_index = stream_index;
			sample.addresses = addresses;
			sample.values = values;
			sample.byte_counts = byte_counts;
		}

		void note_unreadable(const std::uint32_t ordinal,
			const std::uint32_t descriptor_index, const std::uint32_t stream_index,
			const std::uintptr_t address, const std::uint64_t byte_count) noexcept
		{
			++evidence.unreadable_sources;
			add_sample(mismatch_kind::unreadable, ordinal, descriptor_index,
				stream_index, {address, 0}, {0, 0}, {byte_count, 0});
		}

		void snapshot_stream(const std::array<std::uint8_t, descriptor_size>& bytes,
			const stream_layout& layout, const std::uint32_t ordinal,
			const std::uint32_t descriptor_index, const std::uint32_t stream_index,
			stream_snapshot& output) noexcept
		{
			std::memcpy(&output.count, bytes.data() + layout.count_offset,
				sizeof(output.count));
			std::memcpy(&output.pointer, bytes.data() + layout.pointer_offset,
				sizeof(output.pointer));
			output.stride = layout.stride;
			output.byte_count = static_cast<std::uint64_t>(output.count) * layout.stride;
			output.readable = hash_payload(output.pointer, output.byte_count,
				output.content_hash, output.sampled_bytes, output.sampled_windows);
			if (output.readable)
				evidence.sampled_payload_bytes += output.sampled_bytes;
			else
				note_unreadable(ordinal, descriptor_index, stream_index, output.pointer,
					output.byte_count);
		}

		void snapshot_boundary(const std::uint32_t ordinal,
			const void* const owner_record, const void* const backend_state,
			boundary_snapshot& output) noexcept
		{
			output.present = true;
			output.owner_record = reinterpret_cast<std::uintptr_t>(owner_record);
			output.backend_state = reinterpret_cast<std::uintptr_t>(backend_state);
			std::uintptr_t data_field{};
			if (!add_address(output.backend_state, backend_data_offset, data_field) ||
				!guarded_read(data_field, output.backend_data) || output.backend_data == 0)
			{
				note_unreadable(ordinal, 0xFFFFFFFFu, 0xFFFFFFFFu, data_field,
					sizeof(output.backend_data));
				return;
			}
			output.backend_data_readable = true;

			std::uintptr_t count_address{};
			if (!add_address(output.backend_data, descriptor_count_offset,
					count_address) || !guarded_read(count_address, output.descriptor_count))
			{
				note_unreadable(ordinal, 0xFFFFFFFFu, 0xFFFFFFFFu, count_address,
					sizeof(output.descriptor_count));
				return;
			}
			output.descriptor_count_readable = true;
			const auto capture_count = (std::min)(output.descriptor_count,
				static_cast<std::uint32_t>(maximum_descriptors));
			for (std::uint32_t descriptor_index{}; descriptor_index < capture_count;
				++descriptor_index)
			{
				auto& descriptor = output.descriptors[descriptor_index];
				const auto descriptor_offset = descriptor_array_offset +
					static_cast<std::uint64_t>(descriptor_index) * descriptor_size;
				if (!add_address(output.backend_data, descriptor_offset,
					descriptor.address))
				{
					note_unreadable(ordinal, descriptor_index, 0xFFFFFFFFu, 0,
						descriptor_size);
					continue;
				}
				std::array<std::uint8_t, descriptor_size> bytes{};
				if (!guarded_copy(bytes.data(),
					reinterpret_cast<const void*>(descriptor.address), bytes.size()))
				{
					note_unreadable(ordinal, descriptor_index, 0xFFFFFFFFu,
						descriptor.address, descriptor_size);
					continue;
				}
				descriptor.readable = true;
				descriptor.content_hash = hash_bytes(bytes.data(), bytes.size());
				for (std::uint32_t stream_index{}; stream_index < stream_count;
					++stream_index)
				{
					snapshot_stream(bytes, stream_layouts[stream_index], ordinal,
						descriptor_index, stream_index,
						descriptor.streams[stream_index]);
				}
			}
		}

		void compare_streams(const std::uint32_t ordinal,
			const std::uint32_t descriptor_index,
			const descriptor_snapshot& left, const descriptor_snapshot& right) noexcept
		{
			for (std::uint32_t stream_index{}; stream_index < stream_count;
				++stream_index)
			{
				const auto& output0_stream = left.streams[stream_index];
				const auto& output1_stream = right.streams[stream_index];
				++evidence.compared_streams;
				if (output0_stream.pointer != output1_stream.pointer)
				{
					++evidence.stream_pointer_mismatches;
					add_sample(mismatch_kind::stream_pointer, ordinal,
						descriptor_index, stream_index,
						{output0_stream.pointer, output1_stream.pointer},
						{output0_stream.count, output1_stream.count},
						{output0_stream.byte_count, output1_stream.byte_count});
				}
				if (output0_stream.count != output1_stream.count)
				{
					++evidence.stream_count_mismatches;
					add_sample(mismatch_kind::stream_count, ordinal,
						descriptor_index, stream_index,
						{output0_stream.pointer, output1_stream.pointer},
						{output0_stream.count, output1_stream.count},
						{output0_stream.byte_count, output1_stream.byte_count});
				}
				if (output0_stream.readable && output1_stream.readable &&
					output0_stream.count == output1_stream.count &&
					output0_stream.content_hash != output1_stream.content_hash)
				{
					++evidence.stream_content_mismatches;
					add_sample(mismatch_kind::stream_content, ordinal,
						descriptor_index, stream_index,
						{output0_stream.pointer, output1_stream.pointer},
						{output0_stream.content_hash, output1_stream.content_hash},
						{output0_stream.byte_count, output1_stream.byte_count});
				}
			}
		}

		void compare_boundary(const std::uint32_t ordinal,
			const boundary_snapshot& left, const boundary_snapshot& right) noexcept
		{
			++evidence.compared_boundaries;
			if (!left.backend_data_readable || !right.backend_data_readable ||
				!left.descriptor_count_readable || !right.descriptor_count_readable)
			{
				return;
			}
			if (left.backend_data != right.backend_data)
			{
				++evidence.backend_data_mismatches;
				add_sample(mismatch_kind::backend_data, ordinal, 0xFFFFFFFFu,
					0xFFFFFFFFu, {left.backend_data, right.backend_data}, {}, {});
			}
			if (left.descriptor_count != right.descriptor_count)
			{
				++evidence.descriptor_count_mismatches;
				add_sample(mismatch_kind::descriptor_count, ordinal, 0xFFFFFFFFu,
					0xFFFFFFFFu, {}, {left.descriptor_count, right.descriptor_count}, {});
			}

			const auto left_count = (std::min)(left.descriptor_count,
				static_cast<std::uint32_t>(maximum_descriptors));
			const auto right_count = (std::min)(right.descriptor_count,
				static_cast<std::uint32_t>(maximum_descriptors));
			const auto common_count = (std::min)(left_count, right_count);
			for (std::uint32_t descriptor_index{}; descriptor_index < common_count;
				++descriptor_index)
			{
				const auto& output0_descriptor = left.descriptors[descriptor_index];
				const auto& output1_descriptor = right.descriptors[descriptor_index];
				++evidence.compared_descriptors;
				if (!output0_descriptor.readable || !output1_descriptor.readable)
					continue;
				if (output0_descriptor.address != output1_descriptor.address ||
					output0_descriptor.content_hash != output1_descriptor.content_hash)
				{
					++evidence.descriptor_mismatches;
					add_sample(mismatch_kind::descriptor, ordinal, descriptor_index,
						0xFFFFFFFFu,
						{output0_descriptor.address, output1_descriptor.address},
						{output0_descriptor.content_hash,
							output1_descriptor.content_hash},
						{descriptor_size, descriptor_size});
				}
				compare_streams(ordinal, descriptor_index, output0_descriptor,
					output1_descriptor);
			}

			for (auto descriptor_index = common_count;
				descriptor_index < (std::max)(left_count, right_count);
				++descriptor_index)
			{
				++evidence.descriptor_mismatches;
				const auto left_address = descriptor_index < left_count ?
					left.descriptors[descriptor_index].address : 0;
				const auto right_address = descriptor_index < right_count ?
					right.descriptors[descriptor_index].address : 0;
				add_sample(mismatch_kind::descriptor, ordinal, descriptor_index,
					0xFFFFFFFFu, {left_address, right_address}, {},
					{descriptor_index < left_count ? descriptor_size : 0,
						descriptor_index < right_count ? descriptor_size : 0});
			}
		}

		template <typename Value>
		[[nodiscard]] bool read_buffer_value(const std::uint8_t* const bytes,
			const std::size_t size, const std::size_t offset, Value& output) noexcept
		{
			if (bytes == nullptr || offset > size || sizeof(output) > size - offset)
				return false;
			std::memcpy(&output, bytes + offset, sizeof(output));
			return true;
		}

		[[nodiscard]] const local_consumer_layout* classify_consumer(
			const std::uintptr_t function) noexcept
		{
			for (const auto& layout : local_consumer_layouts)
			{
				if (layout.function == function) return &layout;
			}
			return nullptr;
		}

		[[nodiscard]] std::size_t consumer_index(
			const local_consumer_kind kind) noexcept
		{
			return static_cast<std::size_t>(kind);
		}

		[[nodiscard]] local_hook_kind gate_hook_kind(
			const local_consumer_kind kind) noexcept
		{
			switch (kind)
			{
			case local_consumer_kind::ssr: return local_hook_kind::ssr_gate;
			case local_consumer_kind::code_trans:
				return local_hook_kind::code_trans_gate;
			case local_consumer_kind::glass: return local_hook_kind::glass_gate;
			case local_consumer_kind::spark: return local_hook_kind::spark_gate;
			default: return local_hook_kind::ssr_gate;
			}
		}

		[[nodiscard]] local_hook_kind second_pass_hook_kind(
			const local_consumer_kind kind) noexcept
		{
			switch (kind)
			{
			case local_consumer_kind::ssr: return local_hook_kind::ssr_second_pass;
			case local_consumer_kind::code_trans:
				return local_hook_kind::code_trans_second_pass;
			case local_consumer_kind::glass:
				return local_hook_kind::glass_second_pass;
			case local_consumer_kind::spark:
				return local_hook_kind::spark_second_pass;
			default: return local_hook_kind::ssr_second_pass;
			}
		}

		void initialize_local_report_constants() noexcept
		{
			for (std::size_t index{}; index < local_consumer_layouts.size(); ++index)
				evidence.local_consumers[index].consumer =
					local_consumer_layouts[index].function;
		}

		[[nodiscard]] bool read_cursor(const std::uint8_t* const bytes,
			const std::size_t size, const std::size_t offset,
			cursor_snapshot& output) noexcept
		{
			return read_buffer_value(bytes, size, offset, output.begin) &&
				read_buffer_value(bytes, size, offset + sizeof(std::uintptr_t),
					output.end) &&
				read_buffer_value(bytes, size, offset + 2 * sizeof(std::uintptr_t),
					output.saved);
		}

		[[nodiscard]] bool read_source_shape(const std::uintptr_t descriptor,
			const local_consumer_layout& layout, std::uint32_t& count,
			std::uintptr_t& pointer) noexcept
		{
			if (descriptor == 0 || layout.source_stream >= stream_layouts.size())
				return false;
			std::array<std::uint8_t, descriptor_size> bytes{};
			if (!guarded_copy(bytes.data(),
				reinterpret_cast<const void*>(descriptor), bytes.size()))
			{
				return false;
			}
			const auto& source = stream_layouts[layout.source_stream];
			return read_buffer_value(bytes.data(), bytes.size(), source.count_offset,
				count) && read_buffer_value(bytes.data(), bytes.size(),
				source.pointer_offset, pointer);
		}

		[[nodiscard]] bool snapshot_consumer_context(const void* const context,
			consumer_context_snapshot& output) noexcept
		{
			output = {};
			output.address = reinterpret_cast<std::uintptr_t>(context);
			if (context == nullptr || !guarded_copy(output.words.data(), context,
				sizeof(output.words)))
			{
				return false;
			}
			output.secondary = output.words[1];
			std::uintptr_t technique_field{};
			std::uintptr_t pass_count_field{};
			if (output.secondary == 0 ||
				!add_address(output.secondary, 0x1948, technique_field) ||
				!guarded_read(technique_field, output.technique) ||
				output.technique == 0 ||
				!add_address(output.technique, 0xA, pass_count_field) ||
				!guarded_read(pass_count_field, output.pass_count))
			{
				return false;
			}
			output.readable = true;
			return true;
		}

		void snapshot_selection(const consumer_context_snapshot& context,
			material_selection_snapshot& output) noexcept
		{
			// 0x14072E760 installs material/type/technique at +1938/+1940/+1948.
			// 0x1407834E0 installs pass = technique+0x10+index*0x48 at +1950.
			std::array<std::uint8_t, 0x28> selection{};
			std::array<std::uint8_t, 0x158> material_info{};
			std::uintptr_t address{};
			std::uintptr_t technique{};
			if (!context.readable || !add_address(context.secondary, 0x1938, address) ||
				!guarded_copy(selection.data(), reinterpret_cast<const void*>(address), selection.size())) return;
			std::memcpy(&output.material, selection.data(), sizeof(output.material));
			std::memcpy(&output.technique_type, selection.data() + 8, sizeof(output.technique_type));
			std::memcpy(&technique, selection.data() + 0x10, sizeof(technique));
			std::memcpy(&output.pass, selection.data() + 0x18, sizeof(output.pass));
			std::memcpy(&output.pass_index, selection.data() + 0x20, sizeof(output.pass_index));
			if (technique != context.technique || output.pass_index >= context.pass_count ||
				!add_address(technique, 0x10ull + output.pass_index * 0x48ull, address)) return;
			output.pass_address_matches = output.pass == address;
			if (!output.pass_address_matches ||
				!guarded_copy(material_info.data(), reinterpret_cast<const void*>(output.material), material_info.size()) ||
				!guarded_copy(output.pass_bytes.data(), reinterpret_cast<const void*>(output.pass), output.pass_bytes.size())) return;
			std::memcpy(output.atlas.data(), material_info.data() + 0xA, output.atlas.size());
			output.readable = true;
			// H2 selects GfxStateBits from Material::stateBitsEntry[type] +
			// pass_index (0x140783537..0x140783571), independently of shaders.
			if (output.technique_type < 252)
			{
				output.state_bits_index = material_info[0x30 + output.technique_type] + output.pass_index;
				std::uintptr_t table{};
				std::memcpy(&table, material_info.data() + 0x150, sizeof(table));
				if (table != 0 && output.state_bits_index < material_info[0x12E] &&
					add_address(table, output.state_bits_index * 0x28ull, address))
					output.state_bits_readable = guarded_copy(output.state_bits.data(),
						reinterpret_cast<const void*>(address), output.state_bits.size());
			}

			// Names are bounded optional labels. Their absence is not a selection
			// mismatch; pointer/pass bytes remain separate evidence.
			std::uintptr_t name{};
			std::memcpy(&name, material_info.data(), sizeof(name));
			output.material_name_readable = guarded_copy(output.material_name.data(),
				reinterpret_cast<const void*>(name), output.material_name.size());
			if (!output.material_name_readable) output.material_name = {};
			output.material_name.back() = '\0';
			if (guarded_read(technique, name))
				output.technique_name_readable = guarded_copy(output.technique_name.data(),
					reinterpret_cast<const void*>(name), output.technique_name.size());
			if (!output.technique_name_readable) output.technique_name = {};
			output.technique_name.back() = '\0';
		}

		[[nodiscard]] bool source_matches_cursor(
			const local_consumer_layout& layout, const std::uint32_t count,
			const std::uintptr_t pointer, const cursor_snapshot& cursor) noexcept
		{
			if (layout.inclusive_end && count <= 1)
				return cursor.begin == 0 && cursor.end == 0;
			const auto bytes = static_cast<std::uint64_t>(count) * layout.stride;
			const auto adjustment = layout.inclusive_end ? layout.stride : 0u;
			if (bytes < adjustment || bytes - adjustment >
				std::numeric_limits<std::uintptr_t>::max() - pointer)
			{
				return false;
			}
			return cursor.begin == pointer &&
				cursor.end == pointer + static_cast<std::uintptr_t>(bytes - adjustment);
		}

		[[nodiscard]] std::uintptr_t active_primary_source(
			const std::uint64_t pair_id, const std::uint32_t eye,
			const std::uint32_t thread) noexcept
		{
			std::uintptr_t result{};
			std::uint64_t newest_cookie{};
			for (const auto& scope : primary_scopes)
			{
				if (scope.active && scope.pair_id == pair_id && scope.eye == eye &&
					scope.thread == thread && scope.cookie >= newest_cookie)
				{
					newest_cookie = scope.cookie;
					result = scope.source_descriptor;
				}
			}
			return result;
		}

		void add_local_sample(const executor_scope& scope,
			const local_entry_observation& observation,
			const cursor_snapshot& after) noexcept
		{
			auto& count = evidence.local_sample_counts[scope.eye];
			if (count >= maximum_local_samples_per_eye)
			{
				++evidence.dropped_local_samples[scope.eye];
				return;
			}
			auto& sample = evidence.local_samples[scope.eye][count++];
			sample.executor = scope.kind;
			sample.consumer = observation.consumer;
			sample.eye = scope.eye;
			sample.call_ordinal = observation.call_ordinal;
			sample.entry_index = observation.entry_index;
			sample.group_index = observation.group_index;
			sample.return_address = scope.return_address;
			sample.source_descriptor = observation.source_descriptor;
			sample.source_count = observation.source_count;
			sample.source_pointer = observation.source_pointer;
			sample.source_byte_count = observation.source_byte_count;
			sample.source_content_hash = observation.source_content_hash;
			sample.source_sampled_bytes = observation.source_sampled_bytes;
			sample.source_sampled_windows = observation.source_sampled_windows;
			sample.source_content_readable = observation.source_content_readable;
			sample.cursor_pre = observation.cursor_before.begin;
			sample.cursor_end = observation.cursor_before.end;
			sample.cursor_post = after.begin;
		}

		[[nodiscard]] bool snapshot_executor(executor_scope& scope) noexcept
		{
			std::array<std::uint8_t, local_complex_state_size> bytes{};
			const auto state_size = scope.kind == local_executor_kind::complex ?
				local_complex_state_size : local_simple_state_size;
			if (!guarded_copy(bytes.data(),
				reinterpret_cast<const void*>(scope.local_state), state_size))
			{
				++evidence.local_parse_failures;
				return false;
			}

			if (!read_buffer_value(bytes.data(), state_size, 0, scope.entry_cursor) ||
				!read_buffer_value(bytes.data(), state_size, 4, scope.entry_count))
			{
				++evidence.local_parse_failures;
				return false;
			}
			if (scope.kind == local_executor_kind::complex)
			{
				if (!read_buffer_value(bytes.data(), state_size, 8,
					scope.group_count) ||
					scope.group_count > local_complex_maximum_groups ||
					scope.entry_cursor > local_complex_maximum_entries ||
					scope.entry_count > local_complex_maximum_entries -
						scope.entry_cursor)
				{
					++evidence.local_parse_failures;
					return false;
				}
				++evidence.complex_executor_calls[scope.eye];
				evidence.complex_group_counts[scope.eye] += scope.group_count;
				evidence.complex_entry_counts[scope.eye] += scope.entry_count;
			}
			else
			{
				scope.group_count = 1;
				if (scope.entry_cursor > local_simple_maximum_entries ||
					scope.entry_count > local_simple_maximum_entries -
						scope.entry_cursor)
				{
					++evidence.local_parse_failures;
					return false;
				}
				++evidence.simple_executor_calls[scope.eye];
				evidence.simple_entry_counts[scope.eye] += scope.entry_count;
			}

			const auto entry_base = scope.kind == local_executor_kind::complex ?
				complex_entry_base : simple_entry_base;
			const auto descriptor_base = scope.kind == local_executor_kind::complex ?
				complex_descriptor_base : simple_descriptor_base;
			const auto maximum_entries = scope.kind == local_executor_kind::complex ?
				local_complex_maximum_entries : local_simple_maximum_entries;
			if (scope.entry_cursor > maximum_entries ||
				scope.entry_count > maximum_entries - scope.entry_cursor)
			{
				++evidence.local_parse_failures;
				return false;
			}

			for (std::uint32_t relative_index{};
				relative_index < scope.entry_count; ++relative_index)
			{
				const auto entry_index = scope.entry_cursor + relative_index;
				const auto entry_offset = entry_base +
					static_cast<std::size_t>(entry_index) * local_entry_stride;
				std::uintptr_t consumer{};
				if (!read_buffer_value(bytes.data(), state_size,
					entry_offset + local_entry_consumer_offset, consumer))
				{
					++evidence.local_parse_failures;
					return false;
				}
				const auto* const layout = classify_consumer(consumer);
				if (layout == nullptr) continue;

				std::uint32_t group_index{};
				if (scope.kind == local_executor_kind::complex &&
					(!read_buffer_value(bytes.data(), state_size, entry_offset + 4,
						group_index) || group_index >= scope.group_count))
				{
					++evidence.local_parse_failures;
					return false;
				}
				const auto cursor_offset = descriptor_base +
					static_cast<std::size_t>(group_index) * 0x148 +
					layout->cursor_offset;
				cursor_snapshot cursor{};
				if (!read_cursor(bytes.data(), state_size, cursor_offset, cursor))
				{
					++evidence.local_parse_failures;
					return false;
				}

				std::uintptr_t source_descriptor{};
				if (scope.kind == local_executor_kind::complex)
				{
					std::uintptr_t marker{};
					if (!read_buffer_value(bytes.data(), state_size,
						complex_source_marker_base +
							static_cast<std::size_t>(group_index) *
								sizeof(std::uintptr_t), marker))
					{
						++evidence.local_parse_failures;
						return false;
					}
					if (marker >= 0x100) source_descriptor = marker - 0x100;
				}
				else
				{
					std::uintptr_t marker{};
					std::uintptr_t marker_address{};
					if (add_address(scope.header, 0x10, marker_address) &&
						guarded_read(marker_address, marker) && marker >= 0x100)
					{
						source_descriptor = marker - 0x100;
					}
				}
				if (source_descriptor == 0)
					source_descriptor = active_primary_source(scope.pair_id, scope.eye,
						scope.thread);

				const auto report_index = consumer_index(layout->kind);
				auto& aggregate = evidence.local_consumers[report_index].eyes[scope.eye];
				local_entry_observation observation{};
				observation.present = true;
				observation.consumer = layout->kind;
				observation.entry_index = entry_index;
				observation.group_index = group_index;
				observation.call_ordinal = static_cast<std::uint32_t>(
					(std::min)(aggregate.calls,
						static_cast<std::uint64_t>((std::numeric_limits<std::uint32_t>::max)())));
				observation.cursor_address_offset = cursor_offset;
				observation.source_descriptor = source_descriptor;
				observation.cursor_before = cursor;
				++aggregate.calls;
				aggregate.latest_source_descriptor = source_descriptor;
				aggregate.latest_cursor_pre = cursor.begin;
				aggregate.latest_cursor_end = cursor.end;
				if (!read_source_shape(source_descriptor, *layout,
					observation.source_count, observation.source_pointer))
				{
					++aggregate.unresolved_sources;
				}
				else
				{
					aggregate.entries += observation.source_count;
					aggregate.latest_source_count = observation.source_count;
					aggregate.latest_source_pointer = observation.source_pointer;
					observation.source_byte_count =
						static_cast<std::uint64_t>(observation.source_count) *
						layout->stride;
					observation.source_content_readable = hash_payload(
						observation.source_pointer, observation.source_byte_count,
						observation.source_content_hash,
						observation.source_sampled_bytes,
						observation.source_sampled_windows);
					aggregate.latest_source_byte_count =
						observation.source_byte_count;
					aggregate.latest_source_content_hash =
						observation.source_content_hash;
					aggregate.latest_source_sampled_bytes =
						observation.source_sampled_bytes;
					aggregate.latest_source_sampled_windows =
						observation.source_sampled_windows;
					if (observation.source_content_readable)
					{
						++aggregate.payload_hashes;
						aggregate.payload_sampled_bytes +=
							observation.source_sampled_bytes;
					}
					else
					{
						++aggregate.payload_hash_failures;
					}
					if (!source_matches_cursor(*layout, observation.source_count,
						observation.source_pointer, cursor))
					{
						++aggregate.source_shape_mismatches;
					}
				}
				if (scope.observation_count >= scope.observations.size())
				{
					++evidence.local_capacity_overflows;
					return false;
				}
				scope.observations[scope.observation_count++] = observation;
			}
			return true;
		}

		[[nodiscard]] bool finish_executor(executor_scope& scope) noexcept
		{
			std::array<std::uint8_t, local_complex_state_size> bytes{};
			const auto state_size = scope.kind == local_executor_kind::complex ?
				local_complex_state_size : local_simple_state_size;
			if (!guarded_copy(bytes.data(),
				reinterpret_cast<const void*>(scope.local_state), state_size))
			{
				++evidence.local_parse_failures;
				return false;
			}
			for (std::uint32_t index{}; index < scope.observation_count; ++index)
			{
				const auto& observation = scope.observations[index];
				cursor_snapshot after{};
				if (!read_cursor(bytes.data(), state_size,
					observation.cursor_address_offset, after))
				{
					++evidence.local_parse_failures;
					return false;
				}
				auto& aggregate = evidence.local_consumers[
					consumer_index(observation.consumer)].eyes[scope.eye];
				aggregate.latest_cursor_post = after.begin;
				if (after.begin > observation.cursor_before.begin)
				{
					++aggregate.cursor_advances;
					aggregate.cursor_advance_bytes +=
						after.begin - observation.cursor_before.begin;
				}
				else if (after.begin == observation.cursor_before.begin)
				{
					++aggregate.cursor_unchanged;
				}
				else
				{
					++aggregate.cursor_regressions;
				}
				add_local_sample(scope, observation, after);
			}
			return true;
		}

		void compare_local_payload_samples() noexcept
		{
			std::array<bool, maximum_local_samples_per_eye> right_matched{};
			for (std::size_t left_index{};
				left_index < evidence.local_sample_counts[0]; ++left_index)
			{
				const auto& left = evidence.local_samples[0][left_index];
				std::size_t right_index{};
				for (; right_index < evidence.local_sample_counts[1]; ++right_index)
				{
					if (right_matched[right_index]) continue;
					const auto& candidate = evidence.local_samples[1][right_index];
					if (candidate.consumer == left.consumer &&
						candidate.executor == left.executor &&
						candidate.call_ordinal == left.call_ordinal)
					{
						break;
					}
				}
				if (right_index == evidence.local_sample_counts[1])
				{
					++evidence.local_payload_pair_misses;
					continue;
				}
				right_matched[right_index] = true;
				const auto& right = evidence.local_samples[1][right_index];
				if (left.source_pointer != right.source_pointer)
					++evidence.local_source_pointer_mismatches;
				if (left.source_count != right.source_count)
					++evidence.local_source_count_mismatches;
				if (!left.source_content_readable || !right.source_content_readable)
				{
					++evidence.local_payload_unreadable;
					continue;
				}
				++evidence.local_payload_comparisons;
				if (left.source_byte_count != right.source_byte_count ||
					left.source_content_hash != right.source_content_hash)
				{
					++evidence.local_payload_mismatches;
				}
			}
			for (std::size_t right_index{};
				right_index < evidence.local_sample_counts[1]; ++right_index)
			{
				if (!right_matched[right_index])
					++evidence.local_payload_pair_misses;
			}
		}

		void compare_local_gate_samples() noexcept
		{
			std::array<bool, maximum_local_gate_samples_per_eye> right_matched{};
			for (std::size_t left_index{};
				left_index < evidence.local_gate_sample_counts[0]; ++left_index)
			{
				const auto& left = evidence.local_gate_samples[0][left_index];
				std::size_t right_index{};
				for (; right_index < evidence.local_gate_sample_counts[1]; ++right_index)
				{
					if (right_matched[right_index]) continue;
					const auto& candidate = evidence.local_gate_samples[1][right_index];
					if (candidate.consumer == left.consumer &&
						candidate.call_ordinal == left.call_ordinal)
					{
						break;
					}
				}
				if (right_index == evidence.local_gate_sample_counts[1])
				{
					++evidence.local_gate_pair_misses;
					continue;
				}
				right_matched[right_index] = true;
				const auto& right = evidence.local_gate_samples[1][right_index];
				++evidence.local_gate_comparisons;
				if (left.accepted != right.accepted)
					++evidence.local_gate_result_mismatches;
				if (left.key != right.key)
					++evidence.local_gate_key_mismatches;
				if (!left.context_readable || !right.context_readable)
				{
					++evidence.local_gate_context_unreadable;
				}
				else if (left.pass_count != right.pass_count)
				{
					++evidence.local_gate_pass_count_mismatches;
				}
				if (!left.accepted || !right.accepted) continue;
				const auto& lhs = left.selection;
				const auto& rhs = right.selection;
				if (!lhs.readable || !rhs.readable)
				{
					++evidence.selection_unreadable;
					continue;
				}
				++evidence.selection_comparisons;
				if (lhs.material != rhs.material || lhs.technique_type != rhs.technique_type ||
					left.technique != right.technique || lhs.pass_index != rhs.pass_index)
					++evidence.selection_identity_mismatches;
				if (lhs.pass_bytes != rhs.pass_bytes) ++evidence.selection_pass_mismatches;
				if (lhs.atlas != rhs.atlas) ++evidence.selection_atlas_mismatches;
				if (lhs.state_bits_readable && rhs.state_bits_readable)
				{
					++evidence.selection_state_comparisons;
					if (lhs.state_bits != rhs.state_bits) ++evidence.selection_state_mismatches;
				}
				else ++evidence.selection_state_unreadable;
			}
			for (std::size_t right_index{};
				right_index < evidence.local_gate_sample_counts[1]; ++right_index)
			{
				if (!right_matched[right_index])
					++evidence.local_gate_pair_misses;
			}
		}

		void publish(const state value) noexcept
		{
			evidence.current = value;
			published_state.store(value, std::memory_order_release);
		}
	}

	bool arm() noexcept
	{
		const auto started = now_ns();
		const std::lock_guard lock(state_mutex);
		if (published_state.load(std::memory_order_acquire) == state::recording)
		{
			++evidence.arm_attempts;
			++evidence.lifecycle_mismatches;
			return false;
		}
		evidence = {};
		evidence.armed_utc_ms = utc_ms();
		pair_started_ns = 0;
		eye_started_ns = {};
		evidence.active_eye = 2;
		evidence.arm_attempts = 1;
		evidence.hooks_installed = hooks.installed;
		evidence.hook_install_attempts = hooks.attempts;
		evidence.hook_install_failures = hooks.failures;
		evidence.hook_targets = hooks.targets;
		initialize_local_report_constants();
		output0 = {};
		output1_present = {};
		primary_scopes = {};
		executor_scopes = {};
		evidence.arm_ns = now_ns() - started;
		publish(state::armed);
		return true;
	}

	bool begin_pair(const std::uint64_t pair_id,
		const std::uint32_t owner_thread) noexcept
	{
		if (published_state.load(std::memory_order_acquire) != state::armed)
			return false;
		const std::lock_guard lock(state_mutex);
		if (evidence.current != state::armed) return false;
		++evidence.pair_attempts;
		if (pair_id == 0 || owner_thread == 0 || owner_thread != GetCurrentThreadId())
		{
			++evidence.lifecycle_mismatches;
			return false;
		}
		evidence.pair_id = pair_id;
		evidence.owner_thread = owner_thread;
		evidence.active_eye = 2;
		pair_started_ns = now_ns();
		evidence.started_utc_ms = utc_ms();
		publish(state::recording);
		return true;
	}

	bool begin_eye(const std::uint64_t pair_id, const std::uint32_t eye) noexcept
	{
		if (published_state.load(std::memory_order_acquire) != state::recording)
			return false;
		const std::lock_guard lock(state_mutex);
		const auto expected_eye = evidence.completed_eye_mask == 0 ? 0u : 1u;
		if (evidence.current != state::recording || evidence.pair_id != pair_id ||
			eye >= 2 || eye != expected_eye || evidence.active_eye != 2 ||
			evidence.owner_thread != GetCurrentThreadId())
		{
			++evidence.lifecycle_mismatches;
			return false;
		}
		evidence.active_eye = eye;
		eye_started_ns[eye] = now_ns();
		return true;
	}

	void observe_backend_view_copy(const std::uint64_t pair_id,
		const std::uint32_t eye, const std::uint32_t ordinal,
		const void* const owner_record, const void* const backend_state) noexcept
	{
		if (published_state.load(std::memory_order_acquire) != state::recording)
			return;
		const observation_scope observation(observer_stage::boundary);
		if (evidence.current != state::recording || evidence.pair_id != pair_id ||
			eye >= 2 || evidence.active_eye != eye ||
			evidence.owner_thread != GetCurrentThreadId())
		{
			++evidence.lifecycle_mismatches;
			return;
		}
		++evidence.observations[eye];
		if (ordinal >= maximum_boundaries_per_eye)
		{
			++evidence.boundary_overflows;
			return;
		}
		if ((eye == 0 && output0[ordinal].present) ||
			(eye == 1 && output1_present[ordinal]))
		{
			++evidence.boundary_duplicates;
			return;
		}

		boundary_snapshot current{};
		snapshot_boundary(ordinal, owner_record, backend_state, current);
		++evidence.unique_boundaries[eye];
		evidence.owner_records[eye] = current.owner_record;
		evidence.backend_states[eye] = current.backend_state;
		evidence.latest_backend_data[eye] = current.backend_data;
		evidence.latest_descriptor_counts[eye] = current.descriptor_count;
		if (eye == 0)
		{
			output0[ordinal] = current;
		}
		else
		{
			output1_present[ordinal] = true;
			if (output0[ordinal].present)
				compare_boundary(ordinal, output0[ordinal], current);
		}
	}

	bool begin_primary(const std::uint64_t pair_id, const std::uint32_t eye,
		const void* const scene_context, const void* const pair16,
		const void* const source_descriptor, const std::uint32_t mode,
		const std::uintptr_t return_address, primary_token& output) noexcept
	{
		output = {};
		if (published_state.load(std::memory_order_acquire) != state::recording)
			return false;
		const observation_scope observation(observer_stage::primary);
		if (evidence.current != state::recording || evidence.pair_id != pair_id ||
			eye >= 2 || evidence.active_eye != eye ||
			evidence.owner_thread != GetCurrentThreadId() || scene_context == nullptr ||
			pair16 == nullptr || source_descriptor == nullptr)
		{
			++evidence.local_lifecycle_mismatches;
			return false;
		}
		primary_scope* selected{};
		std::uint32_t selected_index{};
		for (std::uint32_t index{}; index < primary_scopes.size(); ++index)
		{
			if (!primary_scopes[index].active)
			{
				selected = &primary_scopes[index];
				selected_index = index;
				break;
			}
		}
		if (selected == nullptr)
		{
			++evidence.local_capacity_overflows;
			return false;
		}
		primary_scope candidate{};
		candidate.active = true;
		candidate.cookie = next_scope_cookie++;
		candidate.pair_id = pair_id;
		candidate.eye = eye;
		candidate.thread = GetCurrentThreadId();
		candidate.scene_context = reinterpret_cast<std::uintptr_t>(scene_context);
		candidate.pair16 = reinterpret_cast<std::uintptr_t>(pair16);
		candidate.source_descriptor =
			reinterpret_cast<std::uintptr_t>(source_descriptor);
		candidate.mode = mode;
		candidate.return_address = return_address;
		if (!guarded_copy(candidate.pair16_before.data(), pair16,
			candidate.pair16_before.size()) ||
			!guarded_copy(candidate.source_before.data(), source_descriptor,
				candidate.source_before.size()))
		{
			++evidence.local_parse_failures;
			return false;
		}
		*selected = candidate;
		output.cookie = candidate.cookie;
		output.slot = selected_index;
		output.active = true;
		++evidence.primary_calls[eye];
		evidence.hook_callsites_observed[static_cast<std::size_t>(
			local_hook_kind::primary)] = true;
		return true;
	}

	bool end_primary(primary_token& token, const void* const scene_context,
		const void* const pair16, const void* const source_descriptor) noexcept
	{
		if (!token.active) return false;
		const observation_scope observation(observer_stage::primary);
		if (token.slot >= primary_scopes.size())
		{
			token = {};
			++evidence.local_lifecycle_mismatches;
			return false;
		}
		auto& scope = primary_scopes[token.slot];
		if (!scope.active || scope.cookie != token.cookie ||
			scope.thread != GetCurrentThreadId() ||
			scope.scene_context != reinterpret_cast<std::uintptr_t>(scene_context) ||
			scope.pair16 != reinterpret_cast<std::uintptr_t>(pair16) ||
			scope.source_descriptor !=
				reinterpret_cast<std::uintptr_t>(source_descriptor))
		{
			token = {};
			++evidence.local_lifecycle_mismatches;
			return false;
		}
		std::array<std::uint8_t, 16> pair_after{};
		std::array<std::uint8_t, descriptor_size> source_after{};
		const auto readable = guarded_copy(pair_after.data(), pair16,
			pair_after.size()) && guarded_copy(source_after.data(), source_descriptor,
			source_after.size());
		if (!readable)
		{
			++evidence.local_parse_failures;
		}
		else
		{
			if (pair_after != scope.pair16_before)
				++evidence.primary_pair16_mutations[scope.eye];
			if (source_after != scope.source_before)
				++evidence.primary_source_mutations[scope.eye];
		}
		scope = {};
		token = {};
		return readable;
	}

	bool begin_executor(const std::uint64_t pair_id, const std::uint32_t eye,
		const local_executor_kind kind, const void* const header,
		const void* const local_state, const std::uintptr_t return_address,
		executor_token& output) noexcept
	{
		output = {};
		if (published_state.load(std::memory_order_acquire) != state::recording)
			return false;
		const observation_scope observation(observer_stage::executor_begin);
		if (evidence.current != state::recording || evidence.pair_id != pair_id ||
			eye >= 2 || evidence.active_eye != eye ||
			evidence.owner_thread != GetCurrentThreadId() || header == nullptr ||
			local_state == nullptr)
		{
			++evidence.local_lifecycle_mismatches;
			return false;
		}
		executor_scope* selected{};
		std::uint32_t selected_index{};
		for (std::uint32_t index{}; index < executor_scopes.size(); ++index)
		{
			if (!executor_scopes[index].active)
			{
				selected = &executor_scopes[index];
				selected_index = index;
				break;
			}
		}
		if (selected == nullptr)
		{
			++evidence.local_capacity_overflows;
			return false;
		}
		executor_scope candidate{};
		candidate.active = true;
		candidate.cookie = next_scope_cookie++;
		candidate.pair_id = pair_id;
		candidate.eye = eye;
		candidate.thread = GetCurrentThreadId();
		candidate.kind = kind;
		candidate.header = reinterpret_cast<std::uintptr_t>(header);
		candidate.local_state = reinterpret_cast<std::uintptr_t>(local_state);
		candidate.return_address = return_address;
		if (!snapshot_executor(candidate)) return false;
		*selected = candidate;
		output.cookie = candidate.cookie;
		output.slot = selected_index;
		output.kind = kind;
		output.active = true;
		evidence.hook_callsites_observed[static_cast<std::size_t>(
			kind == local_executor_kind::simple ? local_hook_kind::simple_executor :
				local_hook_kind::complex_executor)] = true;
		return true;
	}

	bool end_executor(executor_token& token, const void* const header,
		const void* const local_state) noexcept
	{
		if (!token.active) return false;
		const observation_scope observation(observer_stage::executor_end);
		if (token.slot >= executor_scopes.size())
		{
			token = {};
			++evidence.local_lifecycle_mismatches;
			return false;
		}
		auto& scope = executor_scopes[token.slot];
		if (!scope.active || scope.cookie != token.cookie ||
			scope.kind != token.kind || scope.thread != GetCurrentThreadId() ||
			scope.header != reinterpret_cast<std::uintptr_t>(header) ||
			scope.local_state != reinterpret_cast<std::uintptr_t>(local_state))
		{
			token = {};
			++evidence.local_lifecycle_mismatches;
			return false;
		}
		const auto result = finish_executor(scope);
		scope = {};
		token = {};
		return result;
	}

	void observe_consumer_gate(const std::uint64_t pair_id,
		const std::uint32_t eye, const local_consumer_kind consumer,
		const void* const context, const std::uint64_t key, const bool accepted,
		const std::uintptr_t return_address) noexcept
	{
		if (published_state.load(std::memory_order_acquire) != state::recording)
			return;
		const observation_scope observation(observer_stage::gate);
		const auto index = consumer_index(consumer);
		if (evidence.current != state::recording || evidence.pair_id != pair_id ||
			eye >= 2 || index >= evidence.local_consumers.size() ||
			evidence.active_eye != eye ||
			evidence.owner_thread != GetCurrentThreadId())
		{
			++evidence.local_lifecycle_mismatches;
			return;
		}

		auto& aggregate = evidence.local_consumers[index].eyes[eye];
		const auto ordinal = static_cast<std::uint32_t>((std::min)(
			aggregate.gate_calls,
			static_cast<std::uint64_t>((std::numeric_limits<std::uint32_t>::max)())));
		++aggregate.gate_calls;
		if (accepted) ++aggregate.gate_true;
		else ++aggregate.gate_false;
		evidence.hook_callsites_observed[static_cast<std::size_t>(
			gate_hook_kind(consumer))] = true;
		// Reserve a fixed quota for every family. A large first consumer must not
		// starve GLASS/CODE_TRANS/SPARK, and dropped detail must not perform any
		// context memory reads. Aggregate admission counts still cover every call.
		auto& count = evidence.local_gate_sample_counts[eye];
		if (aggregate.gate_samples >= maximum_gate_samples_per_consumer ||
			count >= maximum_local_gate_samples_per_eye)
		{
			++aggregate.gate_samples_dropped;
			++evidence.dropped_local_gate_samples[eye];
			return;
		}
		++aggregate.gate_samples;
		aggregate.latest_gate_context = reinterpret_cast<std::uintptr_t>(context);
		aggregate.latest_gate_key = key;

		consumer_context_snapshot context_snapshot{};
		const auto context_readable = snapshot_consumer_context(context,
			context_snapshot);
		if (!context_readable) ++aggregate.gate_context_failures;
		aggregate.latest_gate_secondary = context_snapshot.secondary;
		aggregate.latest_gate_technique = context_snapshot.technique;
		aggregate.latest_gate_pass_count = context_snapshot.pass_count;

		auto& sample = evidence.local_gate_samples[eye][count++];
		sample.consumer = consumer;
		sample.eye = eye;
		sample.call_ordinal = ordinal;
		sample.accepted = accepted;
		sample.context_readable = context_readable;
		sample.return_address = return_address;
		sample.context = context_snapshot.address;
		sample.key = key;
		sample.context_words = context_snapshot.words;
		sample.secondary = context_snapshot.secondary;
		sample.technique = context_snapshot.technique;
		sample.pass_count = context_snapshot.pass_count;
		if (accepted) snapshot_selection(context_snapshot, sample.selection);
	}

	void observe_consumer_second_pass(const std::uint64_t pair_id,
		const std::uint32_t eye, const local_consumer_kind consumer,
		const void* const context, const std::uintptr_t return_address) noexcept
	{
		if (published_state.load(std::memory_order_acquire) != state::recording)
			return;
		const observation_scope observation(observer_stage::second_pass);
		const auto index = consumer_index(consumer);
		if (evidence.current != state::recording || evidence.pair_id != pair_id ||
			eye >= 2 || index >= evidence.local_consumers.size() ||
			evidence.active_eye != eye ||
			evidence.owner_thread != GetCurrentThreadId())
		{
			++evidence.local_lifecycle_mismatches;
			return;
		}
		auto& aggregate = evidence.local_consumers[index].eyes[eye];
		++aggregate.second_pass_calls;
		aggregate.latest_second_pass_context =
			reinterpret_cast<std::uintptr_t>(context);
		aggregate.latest_second_pass_return = return_address;
		evidence.hook_callsites_observed[static_cast<std::size_t>(
			second_pass_hook_kind(consumer))] = true;
	}

	void note_hooks(const bool installed, const std::uint64_t install_attempts,
		const std::uint64_t install_failures,
		const std::array<std::uintptr_t, local_hook_count>& targets) noexcept
	{
		const std::lock_guard lock(state_mutex);
		hooks.installed = installed;
		hooks.attempts = install_attempts;
		hooks.failures = install_failures;
		hooks.targets = targets;
		evidence.hooks_installed = installed;
		evidence.hook_install_attempts = install_attempts;
		evidence.hook_install_failures = install_failures;
		evidence.hook_targets = targets;
		initialize_local_report_constants();
	}

	bool end_eye(const std::uint64_t pair_id, const std::uint32_t eye) noexcept
	{
		if (published_state.load(std::memory_order_acquire) != state::recording)
			return false;
		const std::lock_guard lock(state_mutex);
		if (evidence.current != state::recording || evidence.pair_id != pair_id ||
			eye >= 2 || evidence.active_eye != eye ||
			evidence.owner_thread != GetCurrentThreadId())
		{
			++evidence.lifecycle_mismatches;
			return false;
		}
		evidence.completed_eye_mask |= static_cast<std::uint8_t>(1u << eye);
		evidence.eye_wall_ns[eye] = now_ns() - eye_started_ns[eye];
		evidence.active_eye = 2;
		return true;
	}

	void end_pair(const std::uint64_t pair_id, const bool owner_complete) noexcept
	{
		if (published_state.load(std::memory_order_acquire) != state::recording)
			return;
		const observation_scope observation(observer_stage::finish);
		evidence.completed_utc_ms = utc_ms();
		evidence.pair_wall_ns = pair_started_ns != 0 ? now_ns() - pair_started_ns : 0;
		if (evidence.current != state::recording || evidence.pair_id != pair_id ||
			evidence.owner_thread != GetCurrentThreadId())
		{
			++evidence.lifecycle_mismatches;
			// A diagnostic teardown mismatch must not strand the global one-shot in
			// recording forever. Terminate only the observer; no H2/native state is
			// owned here and no production action is taken.
			evidence.owner_complete = false;
			++evidence.pair_completions;
			publish(state::complete);
			return;
		}
		evidence.owner_complete = owner_complete;
		if (evidence.completed_eye_mask != 0x3 || evidence.active_eye != 2 ||
			!owner_complete)
		{
			++evidence.lifecycle_mismatches;
		}
		for (auto& scope : primary_scopes)
		{
			if (!scope.active) continue;
			++evidence.local_lifecycle_mismatches;
			scope = {};
		}
		for (auto& scope : executor_scopes)
		{
			if (!scope.active) continue;
			++evidence.local_lifecycle_mismatches;
			scope = {};
		}
		for (std::uint32_t ordinal{}; ordinal < maximum_boundaries_per_eye; ++ordinal)
		{
			if (output0[ordinal].present == output1_present[ordinal]) continue;
			++evidence.boundary_mismatches;
			add_sample(mismatch_kind::boundary, ordinal, 0xFFFFFFFFu, 0xFFFFFFFFu,
				{output0[ordinal].backend_state, 0},
				{output0[ordinal].present ? 1ull : 0ull,
					output1_present[ordinal] ? 1ull : 0ull}, {});
		}
		compare_local_payload_samples();
		compare_local_gate_samples();
		++evidence.pair_completions;
		publish(state::complete);
	}

	void get_report(report& output) noexcept
	{
		const std::lock_guard lock(state_mutex);
		output = evidence;
		output.current = published_state.load(std::memory_order_acquire);
	}

	const char* to_string(const observer_stage value) noexcept
	{
		switch (value)
		{
		case observer_stage::boundary: return "boundary";
		case observer_stage::primary: return "primary";
		case observer_stage::executor_begin: return "executor_begin";
		case observer_stage::executor_end: return "executor_end";
		case observer_stage::gate: return "gate";
		case observer_stage::second_pass: return "second_pass";
		case observer_stage::finish: return "finish";
		default: return "unknown";
		}
	}

	const char* to_string(const state value) noexcept
	{
		switch (value)
		{
		case state::idle: return "idle";
		case state::armed: return "armed";
		case state::recording: return "recording";
		case state::complete: return "complete";
		default: return "unknown";
		}
	}

	const char* to_string(const mismatch_kind value) noexcept
	{
		switch (value)
		{
		case mismatch_kind::boundary: return "boundary";
		case mismatch_kind::descriptor_count: return "descriptor_count";
		case mismatch_kind::backend_data: return "backend_data";
		case mismatch_kind::descriptor: return "descriptor";
		case mismatch_kind::stream_pointer: return "stream_pointer";
		case mismatch_kind::stream_count: return "stream_count";
		case mismatch_kind::stream_content: return "stream_content";
		case mismatch_kind::unreadable: return "unreadable";
		default: return "unknown";
		}
	}

	const char* to_string(const local_executor_kind value) noexcept
	{
		switch (value)
		{
		case local_executor_kind::complex: return "complex";
		case local_executor_kind::simple: return "simple";
		default: return "unknown";
		}
	}

	const char* to_string(const local_consumer_kind value) noexcept
	{
		switch (value)
		{
		case local_consumer_kind::ssr: return "ssr";
		case local_consumer_kind::code_trans: return "code_trans";
		case local_consumer_kind::glass: return "glass";
		case local_consumer_kind::spark: return "spark";
		default: return "unknown";
		}
	}
}
