#include <std_include.hpp>

#include "engine_stereo_material_buffer_probe.hpp"

#include "engine_stereo_constant_buffer_probe.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <wrl/client.h>

namespace vr::engine_stereo_material_buffer_probe
{
	namespace
	{
		struct capture_record
		{
			std::uint64_t upload_generation{};
			std::uintptr_t source{};
			std::uintptr_t destination{};
			std::uintptr_t caller{};
			std::uint64_t output_ordinal{};
			std::uint32_t ordinal{};
		};

		struct reference_record
		{
			std::array<std::uint64_t, 2> output_ordinals{};
			std::uint64_t output0_family_ordinal{};
			std::uint64_t output1_family_ordinal{};
			std::array<std::uint64_t, 2> upload_generations{};
			std::array<std::uintptr_t, 2> buffer_identities{};
			std::uint8_t family{};
			std::uint8_t stage{};
			std::uint8_t slot{};
		};

		std::mutex state_mutex;
		std::atomic<state> published_state{state::idle};
		report evidence{};
		Microsoft::WRL::ComPtr<ID3D11Device> device;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> expected_context;
		Microsoft::WRL::ComPtr<ID3D11Buffer> staging;
		Microsoft::WRL::ComPtr<ID3D11Query> completion_query;
		std::array<std::array<capture_record, maximum_captures_per_eye>, 2>
			capture_records{};
		std::array<reference_record, maximum_references> references{};
		std::size_t reference_count{};

		void publish(const state value) noexcept
		{
			evidence.current = value;
			published_state.store(value, std::memory_order_release);
		}

		void release_resources() noexcept
		{
			completion_query.Reset();
			staging.Reset();
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

		const capture_record* find_capture(const std::uint32_t eye,
			const std::uint64_t upload_generation,
			const std::uint64_t output_ordinal,
			const std::uintptr_t buffer_identity) noexcept
		{
			if (eye >= 2 || buffer_identity == 0) return nullptr;
			for (std::uint32_t index{}; index < evidence.captures[eye]; ++index)
			{
				const auto& candidate = capture_records[eye][index];
				if (candidate.destination != buffer_identity) continue;
				if ((upload_generation != 0 &&
					candidate.upload_generation == upload_generation) ||
					(upload_generation == 0 && output_ordinal != 0 &&
						candidate.output_ordinal == output_ordinal))
				{
					return &candidate;
				}
			}
			return nullptr;
		}

		void finalize(const std::uint8_t* const bytes) noexcept
		{
			std::array<std::array<bool, maximum_captures_per_eye>, 2> referenced{};
			for (std::size_t index{}; index < reference_count; ++index)
			{
				const auto& source = references[index];
				auto& output = evidence.samples[evidence.sample_count++];
				output.family = source.family;
				output.output_ordinal = source.output_ordinals[1];
				output.output_ordinals = source.output_ordinals;
				output.output0_family_ordinal = source.output0_family_ordinal;
				output.output1_family_ordinal = source.output1_family_ordinal;
				output.upload_generations = source.upload_generations;
				output.buffer_identities = source.buffer_identities;
				output.stage = source.stage;
				output.slot = source.slot;
				const auto* const left = find_capture(0, source.upload_generations[0],
					source.output_ordinals[0], source.buffer_identities[0]);
				const auto* const right = find_capture(1, source.upload_generations[1],
					source.output_ordinals[1], source.buffer_identities[1]);
				output.output0_resolved = left != nullptr;
				output.output1_resolved = right != nullptr;
				if (left == nullptr) ++evidence.unresolved_output0;
				if (right == nullptr) ++evidence.unresolved_output1;
				if (left == nullptr || right == nullptr) continue;
				referenced[0][left->ordinal] = true;
				referenced[1][right->ordinal] = true;
				output.capture_ordinals = {left->ordinal, right->ordinal};
				const auto* const left_bytes = bytes +
					left->ordinal * material_buffer_bytes;
				const auto* const right_bytes = bytes +
					(maximum_captures_per_eye + right->ordinal) * material_buffer_bytes;
				output.content_hashes = {hash_bytes(left_bytes, material_buffer_bytes),
					hash_bytes(right_bytes, material_buffer_bytes)};
				output.compared_bytes = material_buffer_bytes;
				for (std::uint32_t offset{}; offset < material_buffer_bytes; ++offset)
				{
					if (left_bytes[offset] == right_bytes[offset]) continue;
					if (output.differing_bytes == 0) output.first_difference = offset;
					output.last_difference = offset;
					++output.differing_bytes;
					if (output.difference_offset_count <
						output.difference_offsets.size())
					{
						const auto destination = output.difference_offset_count++;
						output.difference_offsets[destination] =
							static_cast<std::uint16_t>(offset);
						output.output0_values[destination] = left_bytes[offset];
						output.output1_values[destination] = right_bytes[offset];
					}
				}
				++evidence.comparisons;
				if (output.differing_bytes == 0) ++evidence.identical;
				else ++evidence.different;
			}
			for (std::size_t eye{}; eye < referenced.size(); ++eye)
				for (std::size_t index{}; index < evidence.captures[eye]; ++index)
					if (!referenced[eye][index]) ++evidence.unreferenced_captures;
		}
	}

	bool begin_pair(const std::uint64_t pair_id,
		ID3D11DeviceContext* const context, const std::uint64_t device_generation,
		const std::uint32_t owner_thread) noexcept
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
		evidence = {};
		evidence.pair_id = pair_id;
		evidence.device_generation = device_generation;
		evidence.context = reinterpret_cast<std::uintptr_t>(context);
		evidence.owner_thread = owner_thread;
		evidence.active_eye = 2;
		capture_records = {};
		references = {};
		reference_count = 0;

		context->GetDevice(device.GetAddressOf());
		if (!device)
		{
			fail(failure::context);
			return false;
		}
		expected_context = context;
		D3D11_BUFFER_DESC description{};
		description.ByteWidth = static_cast<UINT>(2 * maximum_captures_per_eye *
			material_buffer_bytes);
		description.Usage = D3D11_USAGE_STAGING;
		description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		evidence.staging_create_result = device->CreateBuffer(&description, nullptr,
			staging.GetAddressOf());
		if (FAILED(evidence.staging_create_result) || !staging)
		{
			fail(failure::resource_creation);
			return false;
		}
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
		if (published_state.load(std::memory_order_acquire) != state::recording ||
			evidence.pair_id != pair_id || eye >= 2 || eye != expected_eye ||
			evidence.active_eye != 2 || evidence.owner_thread != GetCurrentThreadId())
		{
			fail(failure::lifecycle);
			return false;
		}
		evidence.active_eye = eye;
		return true;
	}

	bool end_eye(const std::uint64_t pair_id, const std::uint32_t eye) noexcept
	{
		const std::lock_guard lock(state_mutex);
		if (published_state.load(std::memory_order_acquire) != state::recording ||
			evidence.pair_id != pair_id || eye >= 2 || evidence.active_eye != eye ||
			evidence.owner_thread != GetCurrentThreadId())
		{
			fail(failure::lifecycle);
			return false;
		}
		evidence.completed_eye_mask |= static_cast<std::uint8_t>(1u << eye);
		evidence.active_eye = 2;
		return true;
	}

	void observe_resource_operation(
		const engine_stereo_resource_ops::event& event) noexcept
	{
		if (published_state.load(std::memory_order_acquire) != state::recording ||
			event.operation != engine_stereo_resource_ops::api::copy_resource ||
			event.context == nullptr || event.destination == nullptr ||
			event.source == nullptr)
		{
			return;
		}
		const std::lock_guard lock(state_mutex);
		if (evidence.current != state::recording || evidence.active_eye >= 2 ||
			event.context != expected_context.Get() ||
			evidence.owner_thread != GetCurrentThreadId())
		{
			return;
		}
		++evidence.capture_attempts;
		D3D11_RESOURCE_DIMENSION destination_dimension{};
		D3D11_RESOURCE_DIMENSION source_dimension{};
		event.destination->GetType(&destination_dimension);
		event.source->GetType(&source_dimension);
		if (destination_dimension != D3D11_RESOURCE_DIMENSION_BUFFER ||
			source_dimension != D3D11_RESOURCE_DIMENSION_BUFFER)
		{
			++evidence.capture_contract_rejections;
			return;
		}
		auto* const destination = static_cast<ID3D11Buffer*>(event.destination);
		auto* const source = static_cast<ID3D11Buffer*>(event.source);
		D3D11_BUFFER_DESC destination_description{};
		D3D11_BUFFER_DESC source_description{};
		destination->GetDesc(&destination_description);
		source->GetDesc(&source_description);
		const auto exact_material_copy =
			destination_description.ByteWidth == material_buffer_bytes &&
			(destination_description.BindFlags & D3D11_BIND_CONSTANT_BUFFER) != 0 &&
			source_description.ByteWidth == material_buffer_bytes &&
			source_description.Usage == D3D11_USAGE_DEFAULT &&
			(source_description.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0 &&
			(source_description.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) != 0 &&
			source_description.StructureByteStride == 16;
		if (!exact_material_copy)
		{
			++evidence.capture_contract_rejections;
			return;
		}
		const auto eye = evidence.active_eye;
		const auto ordinal = evidence.captures[eye];
		if (ordinal >= maximum_captures_per_eye)
		{
			++evidence.capture_overflows;
			fail(failure::capture_capacity);
			return;
		}
		engine_stereo_constant_buffer_probe::content_snapshot content{};
		if (!engine_stereo_constant_buffer_probe::query_content_snapshot(destination,
			content) || content.byte_width != material_buffer_bytes ||
			content.upload_generation == 0 ||
			content.source != engine_stereo_constant_buffer_probe::
				upload_source::copy_resource)
		{
			fail(failure::resource_contract);
			return;
		}
		const auto staging_offset = static_cast<std::uint32_t>(
			(eye * maximum_captures_per_eye + ordinal) * material_buffer_bytes);
		if (!engine_stereo_resource_ops::copy_buffer_region_unobserved(event.context,
			staging.Get(), staging_offset, source, material_buffer_bytes))
		{
			fail(failure::copy);
			return;
		}
		capture_records[eye][ordinal] = {content.upload_generation,
			reinterpret_cast<std::uintptr_t>(source),
			reinterpret_cast<std::uintptr_t>(destination), event.caller, 0, ordinal};
		++evidence.captures[eye];
		++evidence.capture_completions;
	}

	void capture_bound_buffer(ID3D11DeviceContext* const context,
		const std::uint32_t eye, const std::uint64_t output_ordinal,
		ID3D11Buffer* const buffer,
		const std::uint64_t upload_generation,
		const std::uintptr_t draw_caller) noexcept
	{
		if (published_state.load(std::memory_order_acquire) != state::recording ||
			context == nullptr || eye >= 2 || output_ordinal == 0 || buffer == nullptr)
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

		++evidence.capture_attempts;
		++evidence.bound_capture_attempts;
		engine_stereo_constant_buffer_probe::content_snapshot content{};
		D3D11_BUFFER_DESC description{};
		buffer->GetDesc(&description);
		if (description.ByteWidth != material_buffer_bytes ||
			(description.BindFlags & D3D11_BIND_CONSTANT_BUFFER) == 0 ||
			!engine_stereo_constant_buffer_probe::query_content_snapshot(buffer,
				content) || content.byte_width != material_buffer_bytes ||
			content.upload_generation != upload_generation)
		{
			++evidence.capture_contract_rejections;
			return;
		}

		for (std::uint32_t index{}; index < evidence.captures[eye]; ++index)
		{
			const auto& existing = capture_records[eye][index];
			if (existing.destination != reinterpret_cast<std::uintptr_t>(buffer))
				continue;
			if ((upload_generation != 0 &&
				existing.upload_generation == upload_generation) ||
				(upload_generation == 0 &&
					existing.output_ordinal == output_ordinal))
			{
				++evidence.capture_deduplications;
				return;
			}
		}

		const auto ordinal = evidence.captures[eye];
		if (ordinal >= maximum_captures_per_eye)
		{
			++evidence.capture_overflows;
			fail(failure::capture_capacity);
			return;
		}
		const auto staging_offset = static_cast<std::uint32_t>(
			(eye * maximum_captures_per_eye + ordinal) * material_buffer_bytes);
		if (!engine_stereo_resource_ops::copy_buffer_region_unobserved(context,
			staging.Get(), staging_offset, buffer, material_buffer_bytes))
		{
			fail(failure::copy);
			return;
		}
		const auto identity = reinterpret_cast<std::uintptr_t>(buffer);
		capture_records[eye][ordinal] = {upload_generation, identity, identity,
			draw_caller, output_ordinal, ordinal};
		++evidence.captures[eye];
		++evidence.capture_completions;
		++evidence.bound_capture_completions;
	}

	void note_dynamic_fx_reference(const std::uint8_t family,
		const std::uint64_t output0_output_ordinal,
		const std::uint64_t output1_output_ordinal,
		const std::uint64_t output0_family_ordinal,
		const std::uint64_t output1_family_ordinal,
		const std::uint8_t stage, const std::uint8_t slot,
		const std::uintptr_t output0_buffer_identity,
		const std::uintptr_t output1_buffer_identity,
		const std::uint64_t output0_upload_generation,
		const std::uint64_t output1_upload_generation) noexcept
	{
		const std::lock_guard lock(state_mutex);
		if (evidence.current != state::recording) return;
		++evidence.reference_attempts;
		if (output0_output_ordinal == 0 || output1_output_ordinal == 0 ||
			output0_buffer_identity == 0 || output1_buffer_identity == 0)
			return;
		for (std::size_t index{}; index < reference_count; ++index)
		{
			const auto& existing = references[index];
			if (existing.output_ordinals[0] == output0_output_ordinal &&
				existing.output_ordinals[1] == output1_output_ordinal &&
				existing.stage == stage && existing.slot == slot &&
				existing.upload_generations[0] == output0_upload_generation &&
				existing.upload_generations[1] == output1_upload_generation)
			{
				return;
			}
		}
		if (reference_count >= references.size())
		{
			++evidence.reference_overflows;
			evidence.error = failure::reference_capacity;
			return;
		}
		references[reference_count++] = {{output0_output_ordinal,
			output1_output_ordinal}, output0_family_ordinal, output1_family_ordinal,
			{output0_upload_generation, output1_upload_generation},
			{output0_buffer_identity, output1_buffer_identity}, family, stage, slot};
		++evidence.reference_completions;
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
		evidence.capture_count_mismatch =
			evidence.captures[0] != evidence.captures[1];
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
		BOOL completed{};
		evidence.query_result = context->GetData(completion_query.Get(), &completed,
			sizeof(completed), D3D11_ASYNC_GETDATA_DONOTFLUSH);
		if (evidence.query_result == S_FALSE ||
			(evidence.query_result == S_OK && completed == FALSE))
		{
			++evidence.not_ready_polls;
			return;
		}
		if (FAILED(evidence.query_result))
		{
			fail(failure::query);
			return;
		}
		D3D11_MAPPED_SUBRESOURCE mapped{};
		evidence.map_result = context->Map(staging.Get(), 0, D3D11_MAP_READ,
			D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
		if (evidence.map_result == DXGI_ERROR_WAS_STILL_DRAWING)
		{
			++evidence.not_ready_polls;
			return;
		}
		if (FAILED(evidence.map_result) || mapped.pData == nullptr)
		{
			fail(failure::map);
			return;
		}
		finalize(static_cast<const std::uint8_t*>(mapped.pData));
		context->Unmap(staging.Get(), 0);
		if (evidence.error == failure::none &&
			(evidence.unresolved_output0 != 0 || evidence.unresolved_output1 != 0))
		{
			evidence.error = failure::missing_capture;
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
		case failure::resource_creation: return "resource_creation";
		case failure::resource_contract: return "resource_contract";
		case failure::capture_capacity: return "capture_capacity";
		case failure::reference_capacity: return "reference_capacity";
		case failure::copy: return "copy";
		case failure::query: return "query";
		case failure::map: return "map";
		case failure::count_mismatch: return "count_mismatch";
		case failure::missing_capture: return "missing_capture";
		default: return "unknown";
		}
	}
}
