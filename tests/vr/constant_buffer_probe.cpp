#include <std_include.hpp>

#include "component/d3d11.hpp"
#include "component/vr/engine_stereo_constant_buffer_probe.hpp"
#include "component/vr/engine_stereo_dynamic_upload.hpp"

#include <d3dcompiler.h>

#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <limits>
#include <string_view>
#include <thread>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

namespace
{
	using namespace vr::engine_stereo_constant_buffer_probe;

	int fail(const char* const reason)
	{
		std::cerr << "vr-d3d11-constant-buffer-probe: FAIL; " << reason << '\n';
		return 1;
	}

	int fail_hresult(const char* const reason, const HRESULT result)
	{
		std::cerr << "vr-d3d11-constant-buffer-probe: FAIL; " << reason
			<< " result=0x" << std::hex << static_cast<std::uint32_t>(result)
			<< std::dec << '\n';
		return 1;
	}

	Microsoft::WRL::ComPtr<ID3DBlob> compile_vertex_shader(
		const std::string_view source)
	{
		Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
		Microsoft::WRL::ComPtr<ID3DBlob> errors;
		if (FAILED(D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr,
			"main", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &bytecode,
			&errors)))
		{
			return {};
		}
		return bytecode;
	}

	Microsoft::WRL::ComPtr<ID3D11VertexShader> create_shader(
		ID3D11Device* const device, ID3DBlob* const bytecode)
	{
		Microsoft::WRL::ComPtr<ID3D11VertexShader> shader;
		if (bytecode == nullptr || FAILED(device->CreateVertexShader(
			bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &shader)))
		{
			return {};
		}
		if (FAILED(shader->SetPrivateData(d3d11::guid_shader_bytecode,
			static_cast<UINT>(bytecode->GetBufferSize()),
			bytecode->GetBufferPointer())))
		{
			return {};
		}
		return shader;
	}

	Microsoft::WRL::ComPtr<ID3D11Buffer> create_constant_buffer(
		ID3D11Device* const device, const D3D11_USAGE usage,
		const UINT byte_width = 16)
	{
		D3D11_BUFFER_DESC description{};
		description.ByteWidth = byte_width;
		description.Usage = usage;
		description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		if (usage == D3D11_USAGE_DYNAMIC)
			description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		Microsoft::WRL::ComPtr<ID3D11Buffer> output;
		if (FAILED(device->CreateBuffer(&description, nullptr, &output))) return {};
		return output;
	}

	Microsoft::WRL::ComPtr<ID3D11Buffer> create_dynamic_vertex_buffer(
		ID3D11Device* const device, const UINT byte_width)
	{
		D3D11_BUFFER_DESC description{};
		description.ByteWidth = byte_width;
		description.Usage = D3D11_USAGE_DYNAMIC;
		description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		Microsoft::WRL::ComPtr<ID3D11Buffer> output;
		if (FAILED(device->CreateBuffer(&description, nullptr, &output))) return {};
		return output;
	}

	bool read_buffer(ID3D11Device* const device, ID3D11DeviceContext* const context,
		ID3D11Buffer* const source, void* const destination, const UINT byte_width)
	{
		D3D11_BUFFER_DESC staging_description{};
		staging_description.ByteWidth = byte_width;
		staging_description.Usage = D3D11_USAGE_STAGING;
		staging_description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		Microsoft::WRL::ComPtr<ID3D11Buffer> staging;
		if (FAILED(device->CreateBuffer(&staging_description, nullptr, &staging)))
			return false;
		context->CopyResource(staging.Get(), source);
		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)) ||
			mapped.pData == nullptr) return false;
		std::memcpy(destination, mapped.pData, byte_width);
		context->Unmap(staging.Get(), 0);
		return true;
	}

	bool write_dynamic_buffer(ID3D11DeviceContext* const context,
		ID3D11Buffer* const buffer, const std::array<std::uint32_t, 4>& values)
	{
		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)) ||
			mapped.pData == nullptr)
		{
			return false;
		}
		std::memcpy(mapped.pData, values.data(), sizeof(values));
		context->Unmap(buffer, 0);
		return true;
	}

	void bind_vs_slot3(ID3D11DeviceContext* const context,
		ID3D11Buffer* const buffer)
	{
		auto* value = buffer;
		context->VSSetConstantBuffers(3, 1, &value);
	}

	std::atomic_uint64_t bind_site_a_invocations{};
	std::atomic_uint64_t bind_site_b_invocations{};

	__declspec(noinline) void bind_vs_slot3_site_a(
		ID3D11DeviceContext* const context, ID3D11Buffer* const buffer)
	{
		bind_site_a_invocations.fetch_add(1, std::memory_order_relaxed);
		auto* value = buffer;
		context->VSSetConstantBuffers(3, 1, &value);
	}

	__declspec(noinline) void bind_vs_slot3_site_b(
		ID3D11DeviceContext* const context, ID3D11Buffer* const buffer)
	{
		bind_site_b_invocations.fetch_add(1, std::memory_order_relaxed);
		auto* value = buffer;
		context->VSSetConstantBuffers(3, 1, &value);
	}

	std::array<std::uint64_t, 2> hash_bytes(const void* const source,
		const std::size_t size)
	{
		const auto* const bytes = static_cast<const std::uint8_t*>(source);
		std::array<std::uint64_t, 2> output{
			1469598103934665603ull,
			1099511628211ull ^ static_cast<std::uint64_t>(size),
		};
		for (std::size_t index{}; index < size; ++index)
		{
			output[0] ^= bytes[index];
			output[0] *= 1099511628211ull;
			output[1] ^= static_cast<std::uint64_t>(bytes[index]) +
				0x9E3779B97F4A7C15ull + (output[1] << 6) + (output[1] >> 2);
		}
		return output;
	}

	bool matches_update_snapshot(const content_snapshot& snapshot,
		const std::array<std::uint64_t, 2>& first_hash,
		const std::array<std::uint64_t, 2>& second_hash,
		const std::uintptr_t first_caller, const std::uintptr_t second_caller)
	{
		if (!snapshot.known) return true;
		if (snapshot.byte_width != 16 ||
			snapshot.source != upload_source::update_subresource)
		{
			return false;
		}
		const auto matches_first = snapshot.hash_low == first_hash[0] &&
			snapshot.hash_high == first_hash[1] &&
			snapshot.upload_caller == first_caller;
		const auto matches_second = snapshot.hash_low == second_hash[0] &&
			snapshot.hash_high == second_hash[1] &&
			snapshot.upload_caller == second_caller;
		return matches_first || matches_second;
	}
}

int main()
{
	auto observed = std::make_unique<report>();
	Microsoft::WRL::ComPtr<ID3D11Device> device;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
	Microsoft::WRL::ComPtr<ID3D11Device> secondary_device;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> secondary_context;
	D3D_FEATURE_LEVEL feature_level{};
	set_resource_observer_attached(true);
	const auto create_result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
		D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
		&device, &feature_level, &context);
	if (FAILED(create_result))
	{
		return fail_hresult("D3D11CreateDevice(WARP)", create_result);
	}
	if (!install_early(device.Get(), context.Get(), 1)) return fail("early install");
	get_report(*observed);
	if (observed->expected_device != 0 || observed->expected_context != 0 ||
		observed->device_generation != 0 ||
		observed->registered_device_candidates != 1 ||
		observed->history_tracking_enabled)
	{
		return fail("pre-selection candidate report");
	}
	set_history_tracking_enabled(true);
	get_report(*observed);
	if (!observed->history_tracking_enabled)
		return fail("explicit history tracking enable");

	constexpr std::string_view used_source = R"(
		cbuffer EyeValues : register(b3) { float4 eyeOffset; };
		float4 main(float4 position : POSITION) : SV_Position
		{
			return position + eyeOffset;
		}
	)";
	constexpr std::string_view unused_source = R"(
		float4 main(float4 position : POSITION) : SV_Position
		{
			return position;
		}
	)";
	const auto used_bytecode = compile_vertex_shader(used_source);
	const auto unused_bytecode = compile_vertex_shader(unused_source);
	const auto used_shader = create_shader(device.Get(), used_bytecode.Get());
	const auto unused_shader = create_shader(device.Get(), unused_bytecode.Get());
	if (!used_shader || !unused_shader) return fail("shader creation/private bytecode");

	const auto buffer_a = create_constant_buffer(device.Get(), D3D11_USAGE_DYNAMIC);
	const auto buffer_b = create_constant_buffer(device.Get(), D3D11_USAGE_DYNAMIC);
	const auto buffer_c = create_constant_buffer(device.Get(), D3D11_USAGE_DYNAMIC);
	const auto unknown_buffer = create_constant_buffer(device.Get(), D3D11_USAGE_DEFAULT);
	const auto update_source = create_constant_buffer(device.Get(), D3D11_USAGE_DEFAULT);
	const auto copy_destination = create_constant_buffer(device.Get(), D3D11_USAGE_DEFAULT);
	constexpr UINT copied_material_bytes = 1088;
	const auto non_constant_copy_source = create_dynamic_vertex_buffer(device.Get(),
		copied_material_bytes);
	const auto copied_material = create_constant_buffer(device.Get(),
		D3D11_USAGE_DEFAULT, copied_material_bytes);
	if (!buffer_a || !buffer_b || !buffer_c || !unknown_buffer || !update_source ||
		!copy_destination || !non_constant_copy_source || !copied_material)
	{
		return fail("constant buffer creation");
	}
	const std::array<std::uint32_t, 4> values_a{1, 2, 3, 4};
	const std::array<std::uint32_t, 4> values_b{5, 6, 7, 8};
	if (!write_dynamic_buffer(context.Get(), buffer_a.Get(), values_a) ||
		!write_dynamic_buffer(context.Get(), buffer_b.Get(), values_b) ||
		!write_dynamic_buffer(context.Get(), buffer_c.Get(), values_a))
	{
		return fail("Map/Unmap upload");
	}

	context->UpdateSubresource(update_source.Get(), 0, nullptr, values_a.data(), 0, 0);
	vr::engine_stereo_resource_ops::event update_event{};
	update_event.operation = vr::engine_stereo_resource_ops::api::update_subresource;
	update_event.context = context.Get();
	update_event.destination = update_source.Get();
	update_event.source_data = values_a.data();
	update_event.caller = 0x1111;
	observe_resource_operation(update_event);
	context->CopyResource(copy_destination.Get(), update_source.Get());
	vr::engine_stereo_resource_ops::event copy_event{};
	copy_event.operation = vr::engine_stereo_resource_ops::api::copy_resource;
	copy_event.context = context.Get();
	copy_event.destination = copy_destination.Get();
	copy_event.source = update_source.Get();
	copy_event.caller = 0x2222;
	observe_resource_operation(copy_event);

	// H2 feeds its 1088-byte material constant buffer through CopyResource from a
	// non-constant dynamic buffer. The first copy discovers that source; the next
	// observed Map/Unmap must retain exact bytes for the following copy.
	vr::engine_stereo_resource_ops::event material_copy_event{};
	material_copy_event.operation = vr::engine_stereo_resource_ops::api::copy_resource;
	material_copy_event.context = context.Get();
	material_copy_event.destination = copied_material.Get();
	material_copy_event.source = non_constant_copy_source.Get();
	material_copy_event.caller = 0x3333;
	context->CopyResource(copied_material.Get(), non_constant_copy_source.Get());
	observe_resource_operation(material_copy_event);
	std::vector<std::uint8_t> material_values(copied_material_bytes);
	for (std::size_t index{}; index < material_values.size(); ++index)
		material_values[index] = static_cast<std::uint8_t>((index * 37u + 11u) & 0xFFu);
	D3D11_MAPPED_SUBRESOURCE mapped_material{};
	if (FAILED(context->Map(non_constant_copy_source.Get(), 0,
		D3D11_MAP_WRITE_DISCARD, 0, &mapped_material)) ||
		mapped_material.pData == nullptr)
	{
		return fail("non-constant CopyResource source map");
	}
	std::memcpy(mapped_material.pData, material_values.data(), material_values.size());
	context->Unmap(non_constant_copy_source.Get(), 0);
	context->CopyResource(copied_material.Get(), non_constant_copy_source.Get());
	observe_resource_operation(material_copy_event);
	if (!select_device(device.Get(), context.Get(), 1))
		return fail("explicit H2 device selection");
	content_snapshot byte_metadata{};
	content_byte_snapshot byte_content{};
	if (!query_content_snapshot(buffer_a.Get(), byte_metadata) ||
		!byte_metadata.known ||
		!query_content_bytes(buffer_a.Get(), byte_metadata.upload_generation,
			byte_content) || !byte_content.complete ||
		byte_content.byte_width != sizeof(values_a) ||
		byte_content.captured_bytes != sizeof(values_a) ||
		std::memcmp(byte_content.bytes.data(), values_a.data(), sizeof(values_a)) != 0 ||
		query_content_bytes(buffer_a.Get(), byte_metadata.upload_generation + 1,
			byte_content))
	{
		return fail("exact upload-generation byte snapshot");
	}
	content_snapshot copied_material_metadata{};
	content_byte_snapshot copied_material_content{};
	if (!query_content_snapshot(copied_material.Get(), copied_material_metadata) ||
		!copied_material_metadata.known ||
		copied_material_metadata.source != upload_source::copy_resource ||
		copied_material_metadata.byte_width != copied_material_bytes ||
		!query_content_bytes(copied_material.Get(),
			copied_material_metadata.upload_generation, copied_material_content) ||
		!copied_material_content.complete ||
		copied_material_content.captured_bytes != copied_material_bytes ||
		std::memcmp(copied_material_content.bytes.data(), material_values.data(),
			material_values.size()) != 0)
	{
		return fail("non-constant CopyResource source byte propagation");
	}

	if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
		D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
		&secondary_device, &feature_level, &secondary_context)) ||
		!install_early(secondary_device.Get(), secondary_context.Get(), 2))
	{
		return fail("post-selection higher-generation candidate");
	}
	get_report(*observed);
	if (observed->expected_context !=
			reinterpret_cast<std::uintptr_t>(context.Get()) ||
		observed->device_generation != 1 ||
		observed->registered_device_candidates != 2)
	{
		return fail("higher candidate must not auto-promote");
	}
	if (!begin_pair(40, context.Get(), GetCurrentThreadId()) ||
		!begin_eye(40, 0) || abort_pair(41) || !abort_pair(40))
	{
		return fail("exact pair abort lifecycle");
	}
	get_report(*observed);
	if (observed->current_state != state::failed)
		return fail("exact pair abort report");

	// Prove that all six base D3D11 SetConstantBuffers entry points are distinct
	// and pass through. Only VS slot 3 contributes to the focused provenance.
	ID3D11Buffer* null_buffer{};
	context->VSSetConstantBuffers(0, 1, &null_buffer);
	context->PSSetConstantBuffers(0, 1, &null_buffer);
	context->GSSetConstantBuffers(0, 1, &null_buffer);
	context->HSSetConstantBuffers(0, 1, &null_buffer);
	context->DSSetConstantBuffers(0, 1, &null_buffer);
	context->CSSetConstantBuffers(0, 1, &null_buffer);

	bind_vs_slot3(context.Get(), buffer_a.Get());
	if (!begin_pair(41, context.Get(), GetCurrentThreadId()) || !begin_eye(41, 0))
		return fail("left-eye lifecycle");
	observe_vs_draw(context.Get(), 1, 0x1001, used_shader.Get(), buffer_a.Get());
	observe_vs_draw(context.Get(), 2, 0x1002, unused_shader.Get(), buffer_a.Get());
	observe_vs_draw(context.Get(), 3, 0x1003, used_shader.Get(), buffer_a.Get());
	observe_vs_draw(context.Get(), 4, 0x1004, used_shader.Get(), buffer_a.Get());
	bind_vs_slot3(context.Get(), unknown_buffer.Get());
	observe_vs_draw(context.Get(), 5, 0x1005, used_shader.Get(), unknown_buffer.Get());
	bind_vs_slot3(context.Get(), nullptr);
	if (!end_eye(41, 0) || !begin_eye(41, 1))
		return fail("eye handoff lifecycle");

	observe_vs_draw(context.Get(), 1, 0x2001, used_shader.Get(), nullptr);
	observe_vs_draw(context.Get(), 2, 0x2002, unused_shader.Get(), nullptr);
	bind_vs_slot3(context.Get(), buffer_b.Get());
	observe_vs_draw(context.Get(), 3, 0x2003, used_shader.Get(), buffer_b.Get());
	bind_vs_slot3(context.Get(), buffer_c.Get());
	observe_vs_draw(context.Get(), 4, 0x2004, used_shader.Get(), buffer_c.Get());
	bind_vs_slot3(context.Get(), unknown_buffer.Get());
	observe_vs_draw(context.Get(), 5, 0x2005, used_shader.Get(), unknown_buffer.Get());
	context->ClearState();
	observe_clear_state(context.Get(), 0x3001);
	if (!end_eye(41, 1) || !end_pair(41)) return fail("terminal lifecycle");

	get_report(*observed);
	const auto& result = *observed;
	if (result.current_state != state::complete || !result.hooks_installed ||
		result.expected_device != reinterpret_cast<std::uintptr_t>(device.Get()) ||
		result.expected_context != reinterpret_cast<std::uintptr_t>(context.Get()) ||
		result.device_generation != 1 || result.pair_id != 41 ||
		result.completed_eye_mask != 0x3 || result.hook_failures != 0 ||
		result.registered_device_candidates != 2 ||
		!result.selected_resource_history_complete)
	{
		return fail("terminal report identity");
	}
	for (std::size_t stage{}; stage < shader_stage_count; ++stage)
	{
		if (result.set_hook_targets[stage] == 0 || result.set_calls[stage] == 0)
			return fail("six-stage setter hook coverage");
	}
	if (result.map_hook_target == 0 || result.unmap_hook_target == 0 ||
		result.map_calls != 4 || result.unmap_calls != 4 ||
		result.mapped_uploads != 4 || result.pending_map_overflows != 0 ||
		result.unmatched_unmaps != 0)
	{
		return fail("Map/Unmap provenance report");
	}
	if (result.shader_private_data_missing != 0 ||
		result.shader_private_data_oversized != 0 ||
		result.shader_reflection_failures != 0 ||
		result.eyes[0].shader_used_draws != 4 ||
		result.eyes[0].shader_unused_draws != 1)
	{
		return fail("shader b3 reflection report");
	}
	if (result.eyes[0].begin.content.buffer !=
			reinterpret_cast<std::uintptr_t>(buffer_a.Get()) ||
		result.eyes[0].end.content.buffer != 0 ||
		result.eyes[1].begin.content.buffer != 0 ||
		result.eyes[1].end.content.buffer != 0 ||
		result.eyes[0].explicit_nulls != 1 || result.eyes[1].explicit_binds != 3 ||
		result.eyes[1].clear_states != 1)
	{
		return fail("slot3 boundary/set provenance");
	}
	if (result.comparisons != 5 || result.used_left_bound_right_null != 1 ||
		result.unused_left_bound_right_null != 1 ||
		result.unknown_usage_left_bound_right_null != 0 ||
		result.both_bound_different_content != 1 ||
		result.both_bound_same_content != 1 ||
		result.both_bound_content_unknown != 1 || result.ordinal_mismatches != 0 ||
		result.sample_count == 0)
	{
		return fail("ordered slot3 comparison");
	}
	if (result.resource_updates != 1 || result.resource_copies != 3 ||
		result.resource_unknown_writes != 1 || result.copy_source_candidates != 3 ||
		result.copy_source_registered != 3 || result.copy_source_known != 2 ||
		result.copy_source_bytes_propagated != 2)
	{
		return fail("resource_ops content entry");
	}
	if (result.copy_source_sample_count != 1 ||
		result.copy_source_sample_overflows != 0)
	{
		return fail("1088-byte copy-source sample count");
	}
	const auto& material_source_sample = result.copy_source_samples[0];
	if (material_source_sample.destination !=
			reinterpret_cast<std::uintptr_t>(copied_material.Get()) ||
		material_source_sample.source !=
			reinterpret_cast<std::uintptr_t>(non_constant_copy_source.Get()) ||
		material_source_sample.destination_creation_serial == 0 ||
		material_source_sample.source_creation_serial == 0 ||
		material_source_sample.destination_byte_width != copied_material_bytes ||
		material_source_sample.source_dimension != D3D11_RESOURCE_DIMENSION_BUFFER ||
		material_source_sample.source_byte_width != copied_material_bytes ||
		material_source_sample.source_usage != D3D11_USAGE_DYNAMIC ||
		material_source_sample.source_bind_flags != D3D11_BIND_VERTEX_BUFFER ||
		material_source_sample.source_cpu_access_flags != D3D11_CPU_ACCESS_WRITE ||
		material_source_sample.observations != 2 ||
		material_source_sample.registered_observations != 2 ||
		material_source_sample.known_observations != 1 ||
		material_source_sample.byte_snapshot_observations != 1 ||
		material_source_sample.unknown_content_observations != 1 ||
		material_source_sample.null_source_observations != 0 ||
		material_source_sample.non_buffer_observations != 0 ||
		material_source_sample.size_mismatch_observations != 0 ||
		material_source_sample.registration_failure_observations != 0 ||
		material_source_sample.byte_snapshot_failure_observations != 0 ||
		material_source_sample.source_content_origin != upload_source::map_unmap ||
		material_source_sample.copy_caller != 0x3333)
	{
		return fail("1088-byte copy-source diagnostics");
	}

	// Exercise both seqlocks with real overlap: the worker publishes complete
	// content snapshots and VS slot-3 hook metadata while the owner records draws.
	// The worker is the only thread calling the immediate context during the
	// overlap, so the stress does not rely on D3D11 immediate-context concurrency.
	constexpr std::uint64_t stress_pair = 42;
	constexpr std::size_t stress_updates = 20000;
	constexpr std::size_t stress_draws = 4096;
	constexpr std::uintptr_t stress_caller_a = 0xA001;
	constexpr std::uintptr_t stress_caller_b = 0xB002;
	const std::array<std::uint32_t, 4> stress_values_a{
		0x11111111, 0x22222222, 0x33333333, 0x44444444};
	const std::array<std::uint32_t, 4> stress_values_b{
		0xAAAAAAAA, 0xBBBBBBBB, 0xCCCCCCCC, 0xDDDDDDDD};
	const auto stress_hash_a = hash_bytes(stress_values_a.data(),
		sizeof(stress_values_a));
	const auto stress_hash_b = hash_bytes(stress_values_b.data(),
		sizeof(stress_values_b));
	std::atomic_bool start_stress{};
	std::atomic_bool writer_started{};
	std::atomic_uint32_t writer_thread{};
	if (!begin_pair(stress_pair, context.Get(), GetCurrentThreadId()) ||
		!begin_eye(stress_pair, 0))
	{
		return fail("concurrent stress left-eye lifecycle");
	}
	// Record the two hook call sites on the owner thread. During the overlap,
	// site A always publishes buffer A and site B always publishes buffer B, so
	// an A identity combined with B's caller is a torn slot-metadata payload.
	bind_vs_slot3_site_a(context.Get(), buffer_a.Get());
	bind_vs_slot3_site_b(context.Get(), buffer_b.Get());
	bind_vs_slot3_site_a(context.Get(), buffer_a.Get());
	std::thread writer([&]
	{
		writer_thread.store(GetCurrentThreadId(), std::memory_order_relaxed);
		while (!start_stress.load(std::memory_order_acquire)) SwitchToThread();
		for (std::size_t index{}; index < stress_updates; ++index)
		{
			const auto first = (index & 1u) == 0;
			const auto& values = first ? stress_values_a : stress_values_b;
			observe_update_content(context.Get(), buffer_a.Get(), values.data(),
				sizeof(values), true, first ? stress_caller_a : stress_caller_b);
			if (first)
				bind_vs_slot3_site_a(context.Get(), buffer_a.Get());
			else
				bind_vs_slot3_site_b(context.Get(), buffer_b.Get());
			if (index == 0)
				writer_started.store(true, std::memory_order_release);
			if ((index & 63u) == 0) SwitchToThread();
		}
	});
	start_stress.store(true, std::memory_order_release);
	while (!writer_started.load(std::memory_order_acquire)) SwitchToThread();
	for (std::size_t index{}; index < stress_draws; ++index)
	{
		observe_vs_draw(context.Get(), index + 1, 0xC003,
			used_shader.Get(), buffer_a.Get());
	}
	writer.join();
	if (!end_eye(stress_pair, 0))
		return fail("concurrent stress left-eye completion");

	observe_update_content(context.Get(), buffer_a.Get(), stress_values_b.data(),
		sizeof(stress_values_b), true, stress_caller_b);
	bind_vs_slot3_site_a(context.Get(), buffer_a.Get());
	if (!begin_eye(stress_pair, 1))
		return fail("concurrent stress right-eye lifecycle");
	for (std::size_t index{}; index < stress_draws; ++index)
	{
		observe_vs_draw(context.Get(), index + 1, 0xD004,
			used_shader.Get(), buffer_a.Get());
	}
	if (!end_eye(stress_pair, 1) || !end_pair(stress_pair))
		return fail("concurrent stress terminal lifecycle");

	get_report(*observed);
	if (observed->current_state != state::complete ||
		observed->pair_id != stress_pair ||
		observed->eyes[0].draws != stress_draws ||
		observed->eyes[1].draws != stress_draws ||
		observed->comparisons != stress_draws ||
		observed->ordinal_mismatches != 0 || observed->draw_overflows != 0 ||
		observed->sample_count == 0 || observed->foreign_thread_pair_calls == 0)
	{
		return fail("concurrent seqlock stress report");
	}
	std::size_t known_left_samples{};
	std::size_t writer_slot_samples{};
	std::uintptr_t bind_site_a_caller{};
	std::uintptr_t bind_site_b_caller{};
	for (std::size_t index{}; index < observed->eyes[0].set_event_count; ++index)
	{
		const auto& event = observed->eyes[0].set_events[index];
		if (event.buffer == reinterpret_cast<std::uintptr_t>(buffer_a.Get()))
			bind_site_a_caller = event.caller;
		else if (event.buffer == reinterpret_cast<std::uintptr_t>(buffer_b.Get()))
			bind_site_b_caller = event.caller;
	}
	if (bind_site_a_caller == 0 || bind_site_b_caller == 0 ||
		bind_site_a_caller == bind_site_b_caller)
	{
		return fail("distinct VS slot hook call sites");
	}
	for (std::size_t index{}; index < observed->sample_count; ++index)
	{
		const auto& sample = observed->samples[index];
		if (!matches_update_snapshot(sample.left.content, stress_hash_a,
				stress_hash_b, stress_caller_a, stress_caller_b) ||
			!matches_update_snapshot(sample.right.content, stress_hash_a,
				stress_hash_b, stress_caller_a, stress_caller_b))
		{
			return fail("torn constant-buffer content snapshot");
		}
		if (sample.left.content.known) ++known_left_samples;
		if (sample.left.origin == bind_origin::explicit_bind)
		{
			if (sample.left.last_set_sequence == 0 ||
				sample.left.last_set_caller != bind_site_a_caller ||
				sample.left.last_set_thread != writer_thread.load(
					std::memory_order_relaxed))
			{
				return fail("torn VS slot metadata snapshot");
			}
			++writer_slot_samples;
		}
	}
	if (known_left_samples == 0 || writer_slot_samples == 0)
		return fail("concurrent stress lacked stable snapshots");

	// Model the observed H2 order, not the old synthetic Map/write/Unmap inside
	// each eye. The next allocation remains mapped across the owner return;
	// producers fill it only before the NEXT family's Unmap. Readback compares
	// the GPU copies enqueued before WRITE_DISCARD, never a mapped CPU snapshot.
	{
		using namespace vr::engine_stereo_dynamic_upload;
		constexpr UINT bytes = 256;
		const auto arena = create_dynamic_vertex_buffer(device.Get(), bytes);
		if (!arena) return fail("upload-once arena creation");
		std::array<Microsoft::WRL::ComPtr<ID3D11Buffer>, 2> eye_snapshots;
		D3D11_BUFFER_DESC snapshot_desc{};
		snapshot_desc.ByteWidth = bytes;
		snapshot_desc.Usage = D3D11_USAGE_DEFAULT;
		for (auto& snapshot : eye_snapshots)
		{
			if (FAILED(device->CreateBuffer(&snapshot_desc, nullptr, &snapshot)))
				return fail("upload-once GPU snapshot creation");
		}
		D3D11_MAPPED_SUBRESOURCE pending{};
		if (FAILED(context->Map(arena.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &pending)))
			return fail("initial H2 producer map");
		std::array<std::uint8_t, bytes> expected{}, actual{};
		std::uint32_t advances{};
		for (std::uint32_t family = 1; family <= 128; ++family)
		{
			for (UINT i = 0; i < bytes; ++i)
				expected[i] = static_cast<std::uint8_t>(family * 17 + i * 31);
			std::memcpy(pending.pData, expected.data(), bytes);
			context->Unmap(arena.Get(), 0);
			pending = {};
			cycle upload{reinterpret_cast<std::uintptr_t>(arena.Get()), GetCurrentThreadId()};
			context->CopyResource(eye_snapshots[0].Get(), arena.Get());
			if (upload.boundary(0, upload.data, GetCurrentThreadId()) != action::defer ||
				!upload.finish_eye(0))
				return fail("left owner did not defer next allocation");
			// Right owner sees null pData and performs no Unmap. Both consumers
			// still address the uploaded current-frame allocation.
			context->CopyResource(eye_snapshots[1].Get(), arena.Get());
			if (upload.boundary(1, upload.data, GetCurrentThreadId()) != action::advance)
				return fail("right owner did not advance upload");
			if (FAILED(context->Map(arena.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &pending)))
				return fail("next H2 producer map");
			++advances;
			upload.returned();
			if (!upload.finish_eye(1) || upload.take_deferred(GetCurrentThreadId()))
				return fail("normal exit attempted a second Map");
			for (const auto& snapshot : eye_snapshots)
			{
				if (!read_buffer(device.Get(), context.Get(), snapshot.Get(), actual.data(), bytes) ||
					actual != expected)
					return fail("a stereo GPU consumer received the next allocation");
			}
		}
		context->Unmap(arena.Get(), 0);
		if (advances != 128 || device->GetDeviceRemovedReason() != S_OK)
			return fail("upload-once GPU generation contract");
		std::cout << "dynamic-upload: 128 WARP families, both GPU copies match current upload; "
			"one WRITE_DISCARD advance per family\n";
	}

	invalidate_device(device.Get(), context.Get(), 1);
	get_report(*observed);
	if (observed->expected_device != 0 || observed->expected_context != 0 ||
		observed->device_generation != 0 ||
		observed->registered_device_candidates != 1)
	{
		return fail("device invalidation");
	}
	if (!select_device(secondary_device.Get(), secondary_context.Get(), 2))
		return fail("higher-generation explicit promotion");
	invalidate_device(secondary_device.Get(), secondary_context.Get(), 2);
	get_report(*observed);
	if (observed->registered_device_candidates != 0)
		return fail("terminal candidate release");

	std::cout << "vr-d3d11-constant-buffer-probe: PASS\n";
	return 0;
}
