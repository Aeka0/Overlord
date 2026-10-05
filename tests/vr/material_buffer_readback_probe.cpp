#include <std_include.hpp>

#include "component/vr/engine_stereo_constant_buffer_probe.hpp"
#include "component/vr/engine_stereo_material_buffer_probe.hpp"
#include "component/vr/engine_stereo_resource_ops.hpp"

#include <array>
#include <iostream>
#include <thread>

namespace
{
	int fail(const char* const reason)
	{
		std::cerr << "vr-d3d11-material-buffer-readback-probe: FAIL; "
			<< reason << '\n';
		return 1;
	}

	Microsoft::WRL::ComPtr<ID3D11Buffer> create_material_source(
		ID3D11Device* const device,
		const std::array<std::uint8_t,
			vr::engine_stereo_material_buffer_probe::material_buffer_bytes>& bytes)
	{
		D3D11_BUFFER_DESC description{};
		description.ByteWidth = static_cast<UINT>(bytes.size());
		description.Usage = D3D11_USAGE_DEFAULT;
		description.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		description.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		description.StructureByteStride = 16;
		const D3D11_SUBRESOURCE_DATA initial{bytes.data(), 0, 0};
		Microsoft::WRL::ComPtr<ID3D11Buffer> output;
		if (FAILED(device->CreateBuffer(&description, &initial,
			output.GetAddressOf()))) return {};
		return output;
	}

	Microsoft::WRL::ComPtr<ID3D11Buffer> create_material_destination(
		ID3D11Device* const device)
	{
		D3D11_BUFFER_DESC description{};
		description.ByteWidth =
			vr::engine_stereo_material_buffer_probe::material_buffer_bytes;
		description.Usage = D3D11_USAGE_DEFAULT;
		description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		Microsoft::WRL::ComPtr<ID3D11Buffer> output;
		if (FAILED(device->CreateBuffer(&description, nullptr,
			output.GetAddressOf()))) return {};
		return output;
	}
}

int main()
{
	using namespace vr;
	Microsoft::WRL::ComPtr<ID3D11Device> device;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
	D3D_FEATURE_LEVEL feature_level{};
	if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
		D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
		device.GetAddressOf(), &feature_level, context.GetAddressOf())))
	{
		return fail("D3D11CreateDevice(WARP)");
	}
	engine_stereo_constant_buffer_probe::set_resource_observer_attached(true);
	if (!engine_stereo_constant_buffer_probe::install_early(device.Get(),
		context.Get(), 1) ||
		!engine_stereo_constant_buffer_probe::select_device(device.Get(),
			context.Get(), 1) ||
		!engine_stereo_resource_ops::install(context.Get(), 1))
	{
		return fail("observer installation");
	}
	engine_stereo_resource_ops::set_observer(
		engine_stereo_resource_ops::observer_channel::constant_buffer_history,
		engine_stereo_constant_buffer_probe::observe_resource_operation);
	engine_stereo_resource_ops::set_observer(
		engine_stereo_resource_ops::observer_channel::owner_diagnostic,
		engine_stereo_material_buffer_probe::observe_resource_operation);
	std::array<std::uint8_t,
		engine_stereo_material_buffer_probe::material_buffer_bytes> left_bytes{};
	for (std::size_t index{}; index < left_bytes.size(); ++index)
		left_bytes[index] = static_cast<std::uint8_t>((index * 29u + 7u) & 0xffu);
	auto right_bytes = left_bytes;
	right_bytes[17] ^= 0x55;
	right_bytes[512] ^= 0xa3;
	const auto left_source = create_material_source(device.Get(), left_bytes);
	const auto right_source = create_material_source(device.Get(), right_bytes);
	const auto left_destination = create_material_destination(device.Get());
	const auto right_destination = create_material_destination(device.Get());
	if (!left_source || !right_source || !left_destination || !right_destination)
		return fail("material resources");
	// Reproduce the game case: both material buffers were uploaded before the
	// one-shot pair starts, so no upload-time event can satisfy the readback.
	context->CopyResource(left_destination.Get(), left_source.Get());
	context->CopyResource(right_destination.Get(), right_source.Get());
	engine_stereo_constant_buffer_probe::set_history_tracking_enabled(true);
	std::array<std::uint64_t, 2> upload_generations{};
	for (std::uint32_t eye{}; eye < 2; ++eye)
	{
		engine_stereo_constant_buffer_probe::content_snapshot content{};
		auto* const destination = eye == 0 ? left_destination.Get() :
			right_destination.Get();
		if (!engine_stereo_constant_buffer_probe::query_content_snapshot(
			destination, content) || content.byte_width !=
				engine_stereo_material_buffer_probe::material_buffer_bytes ||
			content.upload_generation != 0 || content.known)
		{
			return fail("pre-pair upload must remain generation-unknown");
		}
		upload_generations[eye] = content.upload_generation;
	}

	constexpr std::uint64_t pair_id = 41;
	const auto owner_thread = GetCurrentThreadId();
	if (!engine_stereo_material_buffer_probe::begin_pair(pair_id, context.Get(), 1,
		owner_thread)) return fail("begin pair");
	for (std::uint32_t eye{}; eye < 2; ++eye)
	{
		if (!engine_stereo_material_buffer_probe::begin_eye(pair_id, eye))
			return fail("begin eye");
		auto* const destination = eye == 0 ? left_destination.Get() :
			right_destination.Get();
		engine_stereo_material_buffer_probe::capture_bound_buffer(context.Get(), eye,
			91 + eye, destination, upload_generations[eye], 0x1407B9FBAull);
		// VS and PS commonly bind the same generation. The second observation must
		// be deduplicated rather than consuming another staging slot.
		engine_stereo_material_buffer_probe::capture_bound_buffer(context.Get(), eye,
			91 + eye, destination, upload_generations[eye], 0x1407B9FBAull);
		if (!engine_stereo_material_buffer_probe::end_eye(pair_id, eye))
			return fail("end eye");
	}
	engine_stereo_material_buffer_probe::note_dynamic_fx_reference(0, 91, 92, 7, 7,
		0, 3, reinterpret_cast<std::uintptr_t>(left_destination.Get()),
		reinterpret_cast<std::uintptr_t>(right_destination.Get()),
		upload_generations[0], upload_generations[1]);
	if (!engine_stereo_material_buffer_probe::end_pair(pair_id))
		return fail("end pair");
	context->Flush();
	for (std::size_t attempt{}; attempt < 10000; ++attempt)
	{
		engine_stereo_material_buffer_probe::poll(context.Get(), 1, owner_thread);
		const auto current = engine_stereo_material_buffer_probe::get_report().current;
		if (current == engine_stereo_material_buffer_probe::state::complete ||
			current == engine_stereo_material_buffer_probe::state::failed) break;
		std::this_thread::yield();
	}
	const auto report = engine_stereo_material_buffer_probe::get_report();
	if (report.current != engine_stereo_material_buffer_probe::state::complete ||
		report.error != engine_stereo_material_buffer_probe::failure::none)
	{
		return fail("asynchronous retirement");
	}
	if (report.captures != std::array<std::uint32_t, 2>{1, 1} ||
		report.capture_completions != 2 || report.capture_overflows != 0 ||
		report.bound_capture_attempts != 4 ||
		report.bound_capture_completions != 2 ||
		report.capture_deduplications != 2 || report.capture_count_mismatch ||
		report.reference_completions != 1 || report.comparisons != 1 ||
		report.identical != 0 || report.different != 1 || report.sample_count != 1)
	{
		return fail("capture summary");
	}
	const auto& sample = report.samples[0];
	if (!sample.output0_resolved || !sample.output1_resolved ||
		sample.upload_generations != upload_generations ||
		sample.compared_bytes != left_bytes.size() || sample.differing_bytes != 2 ||
		sample.first_difference != 17 || sample.last_difference != 512 ||
		sample.difference_offset_count != 2 ||
		sample.difference_offsets[0] != 17 || sample.difference_offsets[1] != 512 ||
		sample.output0_values[0] != left_bytes[17] ||
		sample.output1_values[0] != right_bytes[17] ||
		sample.output0_values[1] != left_bytes[512] ||
		sample.output1_values[1] != right_bytes[512])
	{
		return fail("byte comparison");
	}
	const auto resource_status = engine_stereo_resource_ops::get_status();
	if (resource_status.calls[static_cast<std::size_t>(
		engine_stereo_resource_ops::api::copy_resource)] != 2 ||
		resource_status.calls[static_cast<std::size_t>(
			engine_stereo_resource_ops::api::copy_subresource_region)] != 0)
	{
		return fail("diagnostic copy polluted observer stream");
	}
	engine_stereo_constant_buffer_probe::set_history_tracking_enabled(false);
	std::cout << "vr-d3d11-material-buffer-readback-probe: PASS\n";
	return 0;
}
