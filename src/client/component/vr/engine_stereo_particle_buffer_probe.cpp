#include <std_include.hpp>

#include "engine_stereo_particle_buffer_probe.hpp"

#include "engine_stereo_resource_ops.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <mutex>
#include <wrl/client.h>

namespace vr::engine_stereo_particle_buffer_probe
{
	namespace
	{
		struct family_resources
		{
			std::array<Microsoft::WRL::ComPtr<ID3D11Buffer>, 2> index_staging;
			std::array<Microsoft::WRL::ComPtr<ID3D11Buffer>, 2> vertex_staging;
			std::uint32_t index_source_bytes{};
			std::uint32_t index_source_offset{};
			std::uint32_t index_copy_bytes{};
			std::uint32_t vertex_source_bytes{};

			void reset() noexcept
			{
				for (auto& resource : index_staging) resource.Reset();
				for (auto& resource : vertex_staging) resource.Reset();
				index_source_bytes = 0;
				index_source_offset = 0;
				index_copy_bytes = 0;
				vertex_source_bytes = 0;
			}
		};

		std::mutex state_mutex;
		std::atomic<state> published_state{state::idle};
		report evidence{};
		Microsoft::WRL::ComPtr<ID3D11Device> device;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> expected_context;
		Microsoft::WRL::ComPtr<ID3D11Query> completion_query;
		std::array<family_resources, maximum_family_samples> resources;
		cpu_reference_reader read_cpu_reference{};

		void publish(const state value) noexcept
		{
			evidence.current = value;
			published_state.store(value, std::memory_order_release);
		}

		void release_resources() noexcept
		{
			for (auto& family : resources) family.reset();
			completion_query.Reset();
			expected_context.Reset();
			device.Reset();
		}

		void fail(const failure reason) noexcept
		{
			evidence.error = reason;
			publish(state::failed);
			release_resources();
		}

		std::uint64_t hash_bytes(const std::uint8_t* const bytes,
			const std::size_t size) noexcept
		{
			std::uint64_t value = 1469598103934665603ull;
			for (std::size_t index{}; index < size; ++index)
			{
				value ^= bytes[index];
				value *= 1099511628211ull;
			}
			return value;
		}

		void compare_bytes(buffer_comparison& output,
			const std::uint8_t* const left, const std::uint8_t* const right,
			const std::uint32_t source_bytes, const std::uint32_t offset,
			const std::uint32_t bytes) noexcept
		{
			output.available = true;
			output.exact_range = true;
			output.source_bytes = source_bytes;
			output.compared_offset = offset;
			output.compared_bytes = bytes;
			output.hashes = {hash_bytes(left, bytes), hash_bytes(right, bytes)};
			for (std::uint32_t index{}; index < bytes; ++index)
			{
				if (left[index] == right[index]) continue;
				const auto absolute_offset = offset + index;
				if (output.differing_bytes == 0)
					output.first_difference = absolute_offset;
				output.last_difference = absolute_offset;
				++output.differing_bytes;
				if (output.difference_offset_count < output.difference_offsets.size())
				{
					const auto destination = output.difference_offset_count++;
					output.difference_offsets[destination] = absolute_offset;
					output.output0_values[destination] = left[index];
					output.output1_values[destination] = right[index];
				}
			}
			output.identical = output.differing_bytes == 0;
		}

		bool draw_contract_matches(const draw& left, const draw& right) noexcept
		{
			return right.family == left.family && right.caller == left.caller &&
				right.vertex_shader == left.vertex_shader &&
				right.pixel_shader == left.pixel_shader &&
				right.output_target_id == left.output_target_id &&
				right.index_count == left.index_count &&
				right.start_index == left.start_index &&
				right.base_vertex == left.base_vertex &&
				right.index_format == left.index_format &&
				right.index_offset == left.index_offset &&
				right.vertex_stride == left.vertex_stride &&
				right.vertex_offset == left.vertex_offset;
		}

		bool create_staging_buffer(const std::uint32_t bytes,
			Microsoft::WRL::ComPtr<ID3D11Buffer>& output,
			HRESULT& result) noexcept
		{
			D3D11_BUFFER_DESC description{};
			description.ByteWidth = bytes;
			description.Usage = D3D11_USAGE_STAGING;
			description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			result = device->CreateBuffer(&description, nullptr,
				output.ReleaseAndGetAddressOf());
			return SUCCEEDED(result) && output != nullptr;
		}

		bool finalize_sample(family_sample& sample, const family_resources& source,
			const std::array<const std::uint8_t*, 4>& mapped) noexcept
		{
			sample.index.identities = {sample.draws[0].index_buffer,
				sample.draws[1].index_buffer};
			sample.vertex.identities = {sample.draws[0].vertex_buffer,
				sample.draws[1].vertex_buffer};
			compare_bytes(sample.index, mapped[0], mapped[1],
				source.index_source_bytes, source.index_source_offset,
				source.index_copy_bytes);
			sample.index_values_equal = sample.index.identical;

			const auto index_size = sample.draws[0].index_format ==
				DXGI_FORMAT_R16_UINT ? 2u : 4u;
			std::uint32_t minimum = std::numeric_limits<std::uint32_t>::max();
			std::uint32_t maximum{};
			for (std::size_t eye{}; eye < 2; ++eye)
			{
				for (std::uint32_t index{}; index < sample.draws[eye].index_count;
					++index)
				{
					std::uint32_t value{};
					if (index_size == 2)
					{
						std::uint16_t short_value{};
						std::memcpy(&short_value, mapped[eye] + index * index_size,
							sizeof(short_value));
						value = short_value;
					}
					else
					{
						std::memcpy(&value, mapped[eye] + index * index_size,
							sizeof(value));
					}
					minimum = std::min(minimum, value);
					maximum = std::max(maximum, value);
				}
			}
			if (minimum == std::numeric_limits<std::uint32_t>::max())
			{
				evidence.error = failure::index_range;
				return false;
			}
			sample.minimum_index = minimum;
			sample.maximum_index = maximum;

			const auto first_vertex = static_cast<std::int64_t>(
				sample.draws[0].base_vertex) + minimum;
			const auto last_vertex = static_cast<std::int64_t>(
				sample.draws[0].base_vertex) + maximum;
			if (first_vertex < 0 || last_vertex < first_vertex)
			{
				evidence.error = failure::vertex_range;
				return false;
			}
			const auto start = static_cast<std::uint64_t>(
				sample.draws[0].vertex_offset) +
				static_cast<std::uint64_t>(first_vertex) *
					sample.draws[0].vertex_stride;
			const auto end = static_cast<std::uint64_t>(
				sample.draws[0].vertex_offset) +
				(static_cast<std::uint64_t>(last_vertex) + 1) *
					sample.draws[0].vertex_stride;
			if (start >= end || end > source.vertex_source_bytes ||
				end > std::numeric_limits<std::uint32_t>::max())
			{
				evidence.error = failure::vertex_range;
				return false;
			}
			sample.vertex_range_resolved = true;
			compare_bytes(sample.vertex, mapped[2] + start, mapped[3] + start,
				source.vertex_source_bytes, static_cast<std::uint32_t>(start),
				static_cast<std::uint32_t>(end - start));
			// The aggregate above is a bounding interval and may contain holes.
			// Keep four actually indexed vertices, not arbitrary first bytes. No
			// extra GPU copies/queries are issued; these mappings already exist.
			if (sample.index_values_equal)
			{
				for (std::uint32_t ordinal{}; ordinal < sample.draws[0].index_count &&
					sample.vertex_sample_count < sample.vertices.size(); ++ordinal)
				{
					std::uint32_t index{};
					std::memcpy(&index, mapped[0] + ordinal * index_size, index_size);
					bool duplicate{};
					for (std::size_t previous{}; previous < sample.vertex_sample_count; ++previous)
						duplicate |= sample.vertices[previous].index == index;
					if (duplicate) continue;
					auto& vertex = sample.vertices[sample.vertex_sample_count++];
					vertex.index = index;
					vertex.byte_offset = static_cast<std::uint32_t>(
						static_cast<std::uint64_t>(sample.draws[0].vertex_offset) +
						(static_cast<std::int64_t>(sample.draws[0].base_vertex) + index) *
						sample.draws[0].vertex_stride);
					vertex.captured_bytes = (std::min)(sample.draws[0].vertex_stride,
						static_cast<std::uint32_t>(maximum_vertex_sample_bytes));
					vertex.stride_complete = vertex.captured_bytes == sample.draws[0].vertex_stride;
					for (std::size_t eye{}; eye < 2; ++eye)
						std::memcpy(vertex.gpu[eye].data(), mapped[2 + eye] +
							vertex.byte_offset, vertex.captured_bytes);
					if (read_cpu_reference && sample.draws[0].vertex_buffer == sample.draws[1].vertex_buffer)
					{
						vertex.cpu_reference_available = read_cpu_reference(evidence.pair_id,
							evidence.device_generation, sample.draws[0].vertex_buffer,
							vertex.byte_offset, vertex.captured_bytes,
							vertex.cpu_pre_restore.data(), vertex.cpu_eyes_equal);
						if (vertex.cpu_reference_available)
							for (std::size_t eye{}; eye < 2; ++eye)
								vertex.gpu_matches_cpu_reference[eye] = std::memcmp(
									vertex.gpu[eye].data(), vertex.cpu_pre_restore.data(),
									vertex.captured_bytes) == 0;
					}
				}
			}
			return true;
		}
	}

	bool begin_pair(const std::uint64_t pair_id,
		ID3D11DeviceContext* const context, const std::uint64_t device_generation,
		const std::uint32_t owner_thread, const cpu_reference_reader read_cpu) noexcept
	{
		const std::lock_guard lock(state_mutex);
		const auto current = published_state.load(std::memory_order_acquire);
		if (pair_id == 0 || context == nullptr || device_generation == 0 ||
			owner_thread == 0 || owner_thread != GetCurrentThreadId() ||
			current == state::recording || current == state::pending)
		{
			return false;
		}
		release_resources();
		read_cpu_reference = read_cpu;
		evidence = {};
		evidence.pair_id = pair_id;
		evidence.device_generation = device_generation;
		evidence.context = reinterpret_cast<std::uintptr_t>(context);
		evidence.owner_thread = owner_thread;
		evidence.active_eye = 2;
		for (std::size_t family{}; family < evidence.samples.size(); ++family)
			evidence.samples[family].family = static_cast<std::uint8_t>(family);
		context->GetDevice(device.GetAddressOf());
		if (!device)
		{
			fail(failure::context);
			return false;
		}
		expected_context = context;
		const D3D11_QUERY_DESC query_description{D3D11_QUERY_EVENT, 0};
		evidence.query_create_result = device->CreateQuery(&query_description,
			completion_query.GetAddressOf());
		if (FAILED(evidence.query_create_result) || !completion_query)
		{
			fail(failure::resource_creation);
			return false;
		}
		evidence.error = failure::none;
		publish(state::recording);
		return true;
	}

	bool begin_eye(const std::uint64_t pair_id, const std::uint32_t eye) noexcept
	{
		const std::lock_guard lock(state_mutex);
		const auto expected_eye = evidence.completed_eye_mask == 0 ? 0u : 1u;
		if (evidence.current != state::recording || evidence.pair_id != pair_id ||
			eye >= 2 || eye != expected_eye || evidence.active_eye != 2 ||
			evidence.owner_thread != GetCurrentThreadId())
		{
			fail(failure::lifecycle);
			return false;
		}
		evidence.active_eye = eye;
		return true;
	}

	void capture_draw(ID3D11DeviceContext* const context, const std::uint32_t eye,
		const draw& value) noexcept
	{
		if (published_state.load(std::memory_order_acquire) != state::recording ||
			context == nullptr || eye >= 2 || value.family >= maximum_family_samples)
		{
			return;
		}
		const std::lock_guard lock(state_mutex);
		if (evidence.current != state::recording || evidence.active_eye != eye ||
			context != expected_context.Get() ||
			evidence.owner_thread != GetCurrentThreadId())
		{
			return;
		}
		auto& sample = evidence.samples[value.family];
		auto& family = resources[value.family];
		++sample.candidate_hits[eye];
		evidence.observed_family_mask |= static_cast<std::uint8_t>(1u << value.family);
		if (sample.captured_eye_mask & (1u << eye)) return;
		if (value.index_count == 0 || value.index_buffer == 0 ||
			value.vertex_buffer == 0 || value.vertex_stride == 0 ||
			(value.index_format != DXGI_FORMAT_R16_UINT &&
				value.index_format != DXGI_FORMAT_R32_UINT))
		{
			return;
		}
		if (eye == 1 && !draw_contract_matches(sample.draws[0], value)) return;

		auto* const index_buffer = reinterpret_cast<ID3D11Buffer*>(value.index_buffer);
		auto* const vertex_buffer = reinterpret_cast<ID3D11Buffer*>(value.vertex_buffer);
		D3D11_BUFFER_DESC index_description{};
		D3D11_BUFFER_DESC vertex_description{};
		index_buffer->GetDesc(&index_description);
		vertex_buffer->GetDesc(&vertex_description);
		const auto index_size = value.index_format == DXGI_FORMAT_R16_UINT ? 2u : 4u;
		const auto copy_offset = static_cast<std::uint64_t>(value.index_offset) +
			static_cast<std::uint64_t>(value.start_index) * index_size;
		const auto copy_bytes = static_cast<std::uint64_t>(value.index_count) * index_size;
		if (index_description.ByteWidth == 0 || vertex_description.ByteWidth == 0 ||
			index_description.ByteWidth > maximum_buffer_bytes ||
			vertex_description.ByteWidth > maximum_buffer_bytes || copy_bytes == 0 ||
			copy_offset + copy_bytes > index_description.ByteWidth ||
			copy_offset > std::numeric_limits<std::uint32_t>::max() ||
			copy_bytes > std::numeric_limits<std::uint32_t>::max())
		{
			return;
		}

		if (eye == 0)
		{
			family.index_source_bytes = index_description.ByteWidth;
			family.index_source_offset = static_cast<std::uint32_t>(copy_offset);
			family.index_copy_bytes = static_cast<std::uint32_t>(copy_bytes);
			family.vertex_source_bytes = vertex_description.ByteWidth;
			if (!create_staging_buffer(family.index_copy_bytes,
					family.index_staging[0], sample.staging_create_results[0]) ||
				!create_staging_buffer(family.index_copy_bytes,
					family.index_staging[1], sample.staging_create_results[1]) ||
				!create_staging_buffer(family.vertex_source_bytes,
					family.vertex_staging[0], sample.staging_create_results[2]) ||
				!create_staging_buffer(family.vertex_source_bytes,
					family.vertex_staging[1], sample.staging_create_results[3]))
			{
				fail(failure::resource_creation);
				return;
			}
		}
		else if (index_description.ByteWidth != family.index_source_bytes ||
			vertex_description.ByteWidth != family.vertex_source_bytes ||
			copy_offset != family.index_source_offset ||
			copy_bytes != family.index_copy_bytes)
		{
			return;
		}

		if (!engine_stereo_resource_ops::copy_buffer_region_unobserved(context,
			family.index_staging[eye].Get(), 0, index_buffer,
			family.index_source_offset, family.index_copy_bytes) ||
			!engine_stereo_resource_ops::copy_buffer_region_unobserved(context,
				family.vertex_staging[eye].Get(), 0, vertex_buffer,
				family.vertex_source_bytes))
		{
			fail(failure::copy);
			return;
		}
		sample.draws[eye] = value;
		sample.captured_eye_mask |= static_cast<std::uint8_t>(1u << eye);
	}

	bool end_eye(const std::uint64_t pair_id, const std::uint32_t eye) noexcept
	{
		const std::lock_guard lock(state_mutex);
		if (evidence.current != state::recording || evidence.pair_id != pair_id ||
			eye >= 2 || evidence.active_eye != eye ||
			evidence.owner_thread != GetCurrentThreadId())
		{
			fail(failure::lifecycle);
			return false;
		}
		evidence.completed_eye_mask |= static_cast<std::uint8_t>(1u << eye);
		evidence.active_eye = 2;
		return true;
	}

	bool end_pair(const std::uint64_t pair_id) noexcept
	{
		const std::lock_guard lock(state_mutex);
		if (evidence.current != state::recording || evidence.pair_id != pair_id ||
			evidence.active_eye != 2 || evidence.completed_eye_mask != 0x3 ||
			evidence.owner_thread != GetCurrentThreadId())
		{
			fail(failure::lifecycle);
			return false;
		}
		for (std::size_t family{}; family < evidence.samples.size(); ++family)
		{
			const auto bit = static_cast<std::uint8_t>(1u << family);
			const auto& sample = evidence.samples[family];
			if (sample.captured_eye_mask == 0x3)
			{
				evidence.complete_family_mask |= bit;
				++evidence.sample_count;
			}
			else if ((evidence.observed_family_mask & bit) != 0)
			{
				evidence.incomplete_family_mask |= bit;
			}
		}
		if (evidence.complete_family_mask == 0)
		{
			fail(failure::missing_draw);
			return false;
		}
		expected_context->End(completion_query.Get());
		publish(state::pending);
		return true;
	}

	void poll(ID3D11DeviceContext* const context,
		const std::uint64_t device_generation, const std::uint32_t owner_thread) noexcept
	{
		if (published_state.load(std::memory_order_acquire) != state::pending) return;
		const std::lock_guard lock(state_mutex);
		if (evidence.current != state::pending) return;
		if (context == nullptr || context != expected_context.Get() ||
			device_generation != evidence.device_generation)
		{
			fail(failure::context);
			return;
		}
		if (owner_thread == 0 || owner_thread != evidence.owner_thread ||
			owner_thread != GetCurrentThreadId())
		{
			fail(failure::thread);
			return;
		}
		++evidence.retirement_polls;
		BOOL complete{};
		evidence.query_result = context->GetData(completion_query.Get(), &complete,
			sizeof(complete), D3D11_ASYNC_GETDATA_DONOTFLUSH);
		if (evidence.query_result == S_FALSE ||
			(evidence.query_result == S_OK && complete == FALSE))
		{
			++evidence.not_ready_polls;
			return;
		}
		if (FAILED(evidence.query_result))
		{
			fail(failure::query);
			return;
		}

		std::array<std::array<D3D11_MAPPED_SUBRESOURCE, 4>,
			maximum_family_samples> mappings{};
		std::array<std::array<const std::uint8_t*, 4>,
			maximum_family_samples> bytes{};
		std::array<ID3D11Buffer*, maximum_family_samples * 4> mapped_resources{};
		std::size_t mapped_count{};
		for (std::size_t family{}; family < evidence.samples.size(); ++family)
		{
			if ((evidence.complete_family_mask & (1u << family)) == 0) continue;
			auto& sample = evidence.samples[family];
			const std::array<ID3D11Buffer*, 4> sources{
				resources[family].index_staging[0].Get(),
				resources[family].index_staging[1].Get(),
				resources[family].vertex_staging[0].Get(),
				resources[family].vertex_staging[1].Get()};
			for (std::size_t index{}; index < sources.size(); ++index)
			{
				sample.map_results[index] = context->Map(sources[index], 0,
					D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT,
					&mappings[family][index]);
				if (sample.map_results[index] == DXGI_ERROR_WAS_STILL_DRAWING)
				{
					for (std::size_t mapped{}; mapped < mapped_count; ++mapped)
						context->Unmap(mapped_resources[mapped], 0);
					++evidence.not_ready_polls;
					return;
				}
				if (FAILED(sample.map_results[index]) ||
					mappings[family][index].pData == nullptr)
				{
					for (std::size_t mapped{}; mapped < mapped_count; ++mapped)
						context->Unmap(mapped_resources[mapped], 0);
					fail(failure::map);
					return;
				}
				mapped_resources[mapped_count++] = sources[index];
				bytes[family][index] = static_cast<const std::uint8_t*>(
					mappings[family][index].pData);
			}
		}

		bool finalized = true;
		for (std::size_t family{}; family < evidence.samples.size(); ++family)
		{
			if ((evidence.complete_family_mask & (1u << family)) == 0) continue;
			if (!finalize_sample(evidence.samples[family], resources[family],
				bytes[family]))
			{
				finalized = false;
				break;
			}
		}
		for (std::size_t mapped{}; mapped < mapped_count; ++mapped)
			context->Unmap(mapped_resources[mapped], 0);
		if (!finalized)
		{
			fail(evidence.error);
			return;
		}
		publish(state::complete);
		release_resources();
	}

	void cancel(ID3D11DeviceContext* const context,
		const std::uint64_t device_generation) noexcept
	{
		const std::lock_guard lock(state_mutex);
		if ((context != nullptr && expected_context.Get() != context) ||
			(device_generation != 0 && evidence.device_generation != device_generation))
		{
			return;
		}
		if (evidence.current == state::recording || evidence.current == state::pending)
			fail(failure::lifecycle);
		else release_resources();
	}

	report get_report() noexcept
	{
		const std::lock_guard lock(state_mutex);
		auto output = evidence;
		output.current = published_state.load(std::memory_order_acquire);
		return output;
	}

	const char* to_string(const state value) noexcept
	{
		switch (value)
		{
		case state::idle: return "idle";
		case state::recording: return "recording";
		case state::pending: return "pending";
		case state::complete: return "complete";
		case state::failed: return "failed";
		default: return "unknown";
		}
	}

	const char* to_string(const failure value) noexcept
	{
		switch (value)
		{
		case failure::none: return "none";
		case failure::lifecycle: return "lifecycle";
		case failure::context: return "context";
		case failure::thread: return "thread";
		case failure::missing_draw: return "missing_draw";
		case failure::draw_contract: return "draw_contract";
		case failure::resource_contract: return "resource_contract";
		case failure::resource_creation: return "resource_creation";
		case failure::copy: return "copy";
		case failure::query: return "query";
		case failure::map: return "map";
		case failure::index_range: return "index_range";
		case failure::vertex_range: return "vertex_range";
		default: return "unknown";
		}
	}
}
