#include <std_include.hpp>

#include "component/vr/engine_stereo_particle_buffer_probe.hpp"
#include "component/vr/engine_stereo_resource_ops.hpp"

#include <array>
#include <cstring>
#include <iostream>
#include <thread>

namespace
{
	std::array<std::uint8_t, 128> cpu_reference{};
	std::uintptr_t cpu_resource{};
	std::uint64_t expected_cpu_pair{};
	bool read_cpu(const std::uint64_t pair, const std::uint64_t generation,
		const std::uintptr_t resource, const std::uint32_t offset,
		const std::uint32_t bytes, std::uint8_t* const output,
		bool& cpu_eyes_equal) noexcept
	{
		cpu_eyes_equal = false;
		if (pair != expected_cpu_pair || generation != 1 || resource != cpu_resource ||
			!output || bytes > 64 || offset > cpu_reference.size() ||
			bytes > cpu_reference.size() - offset) return false;
		std::memcpy(output, cpu_reference.data() + offset, bytes);
		cpu_eyes_equal = true;
		return true;
	}

	int fail(const char* const reason)
	{
		std::cerr << "vr-d3d11-particle-buffer-readback-probe: FAIL; "
			<< reason << '\n';
		return 1;
	}

	template <typename T, std::size_t Size>
	Microsoft::WRL::ComPtr<ID3D11Buffer> create_buffer(ID3D11Device* const device,
		const std::array<T, Size>& data, const std::uint32_t bind_flags)
	{
		D3D11_BUFFER_DESC description{};
		description.ByteWidth = static_cast<UINT>(sizeof(data));
		description.Usage = D3D11_USAGE_DEFAULT;
		description.BindFlags = bind_flags;
		const D3D11_SUBRESOURCE_DATA initial{data.data(), 0, 0};
		Microsoft::WRL::ComPtr<ID3D11Buffer> output;
		if (FAILED(device->CreateBuffer(&description, &initial,
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
	if (!engine_stereo_resource_ops::install(context.Get(), 1))
		return fail("resource operation hook installation");

	constexpr std::array<std::uint16_t, 5> indices{99, 1, 3, 5, 88};
	std::array<std::uint8_t, 128> left_vertices{};
	for (std::size_t index{}; index < left_vertices.size(); ++index)
		left_vertices[index] = static_cast<std::uint8_t>((index * 13u + 5u) & 0xffu);
	auto right_vertices = left_vertices;
	// Vertex 0 lies outside the exact indexed range and must not contaminate the
	// comparison. Vertex 3 is consumed by the draw and must be reported.
	right_vertices[3] ^= 0x71;
	right_vertices[3 * 16 + 5] ^= 0xa5;
	const auto left_indices = create_buffer(device.Get(), indices,
		D3D11_BIND_INDEX_BUFFER);
	const auto right_indices = create_buffer(device.Get(), indices,
		D3D11_BIND_INDEX_BUFFER);
	const auto left_vertex_buffer = create_buffer(device.Get(), left_vertices,
		D3D11_BIND_VERTEX_BUFFER);
	const auto right_vertex_buffer = create_buffer(device.Get(), right_vertices,
		D3D11_BIND_VERTEX_BUFFER);
	if (!left_indices || !right_indices || !left_vertex_buffer ||
		!right_vertex_buffer)
	{
		return fail("source buffers");
	}

	constexpr std::uint64_t pair_id = 73;
	const auto owner_thread = GetCurrentThreadId();
	if (!engine_stereo_particle_buffer_probe::begin_pair(pair_id, context.Get(),
		1, owner_thread)) return fail("begin pair");
	for (std::uint32_t eye{}; eye < 2; ++eye)
	{
		if (!engine_stereo_particle_buffer_probe::begin_eye(pair_id, eye))
			return fail("begin eye");
		for (std::uint8_t family{}; family < 2; ++family)
		{
			engine_stereo_particle_buffer_probe::draw draw{};
			draw.family = family;
			draw.output_ordinal = 500 + family * 10 + eye;
			draw.caller = engine_stereo_particle_buffer_probe::
				particle_cloud_draw_caller;
			draw.vertex_shader = 0x1010 + family;
			draw.pixel_shader = 0x2020 + family;
			draw.index_buffer = reinterpret_cast<std::uintptr_t>(eye == 0 ?
				left_indices.Get() : right_indices.Get());
			draw.vertex_buffer = reinterpret_cast<std::uintptr_t>(eye == 0 ?
				left_vertex_buffer.Get() : right_vertex_buffer.Get());
			draw.output_target_id = 5;
			draw.index_count = 3;
			draw.start_index = 1;
			draw.base_vertex = 0;
			draw.index_format = DXGI_FORMAT_R16_UINT;
			draw.index_offset = 0;
			draw.vertex_stride = 16;
			draw.vertex_offset = 0;
			engine_stereo_particle_buffer_probe::capture_draw(context.Get(), eye,
				draw);
		}
		if (!engine_stereo_particle_buffer_probe::end_eye(pair_id, eye))
			return fail("end eye");
	}
	if (!engine_stereo_particle_buffer_probe::end_pair(pair_id))
		return fail("end pair");
	context->Flush();
	for (std::size_t attempt{}; attempt < 10000; ++attempt)
	{
		engine_stereo_particle_buffer_probe::poll(context.Get(), 1, owner_thread);
		const auto current =
			engine_stereo_particle_buffer_probe::get_report().current;
		if (current == engine_stereo_particle_buffer_probe::state::complete ||
			current == engine_stereo_particle_buffer_probe::state::failed) break;
		std::this_thread::yield();
	}
	const auto report = engine_stereo_particle_buffer_probe::get_report();
	if (report.current != engine_stereo_particle_buffer_probe::state::complete ||
		report.error != engine_stereo_particle_buffer_probe::failure::none)
	{
		return fail("asynchronous retirement");
	}
	if (report.completed_eye_mask != 3 || report.observed_family_mask != 0x3 ||
		report.complete_family_mask != 0x3 ||
		report.incomplete_family_mask != 0 || report.sample_count != 2)
	{
		return fail("capture summary");
	}
	for (std::size_t family{}; family < 2; ++family)
	{
		const auto& sample = report.samples[family];
		if (sample.family != family || sample.captured_eye_mask != 3 ||
			sample.candidate_hits != std::array<std::uint64_t, 2>{1, 1} ||
			!sample.index_values_equal || !sample.vertex_range_resolved)
		{
			return fail("family capture summary");
		}
		if (!sample.index.available || !sample.index.exact_range ||
			!sample.index.identical ||
			sample.index.source_bytes != sizeof(indices) ||
			sample.index.compared_offset != sizeof(std::uint16_t) ||
			sample.index.compared_bytes != 3 * sizeof(std::uint16_t) ||
			sample.minimum_index != 1 || sample.maximum_index != 5)
		{
			return fail("index range comparison");
		}
		if (!sample.vertex.available || !sample.vertex.exact_range ||
			sample.vertex.identical ||
			sample.vertex.source_bytes != left_vertices.size() ||
			sample.vertex.compared_offset != 16 ||
			sample.vertex.compared_bytes != 80 ||
			sample.vertex.differing_bytes != 1 ||
			sample.vertex.first_difference != 53 ||
			sample.vertex.last_difference != 53 ||
			sample.vertex.difference_offset_count != 1 ||
			sample.vertex.difference_offsets[0] != 53)
		{
			return fail("vertex range comparison");
		}
		if (sample.vertex_sample_count != 3) return fail("indexed vertex count");
		for (std::size_t index{}; index < 3; ++index)
		{
			const auto& vertex = sample.vertices[index];
			const auto offset = (1 + index * 2) * 16;
			if (vertex.index != 1 + index * 2 || vertex.byte_offset != offset ||
				vertex.captured_bytes != 16 || !vertex.stride_complete ||
				vertex.cpu_reference_available ||
				std::memcmp(vertex.gpu[0].data(), left_vertices.data() + offset, 16) ||
				std::memcmp(vertex.gpu[1].data(), right_vertices.data() + offset, 16))
				return fail("indexed vertex bytes or unavailable CPU reference");
		}
	}
	// One resource with two GPU content epochs. The CPU reference is the later
	// epoch. Also exercise rearm with an unavailable (wrong-pair) CPU reference.
	for (std::uint64_t pair = 74; pair <= 75; ++pair)
	{
		cpu_reference = right_vertices;
		cpu_resource = reinterpret_cast<std::uintptr_t>(left_vertex_buffer.Get());
		expected_cpu_pair = 74;
		context->UpdateSubresource(left_vertex_buffer.Get(), 0, nullptr, left_vertices.data(), 0, 0);
		if (!engine_stereo_particle_buffer_probe::begin_pair(pair, context.Get(), 1,
			owner_thread, read_cpu)) return fail("reference begin pair");
		for (std::uint32_t eye{}; eye < 2; ++eye)
		{
			if (eye) context->UpdateSubresource(left_vertex_buffer.Get(), 0, nullptr,
				right_vertices.data(), 0, 0);
			if (!engine_stereo_particle_buffer_probe::begin_eye(pair, eye))
				return fail("reference begin eye");
			auto draw = report.samples[0].draws[0];
			draw.vertex_buffer = cpu_resource;
			engine_stereo_particle_buffer_probe::capture_draw(context.Get(), eye, draw);
			if (!engine_stereo_particle_buffer_probe::end_eye(pair, eye))
				return fail("reference end eye");
		}
		if (!engine_stereo_particle_buffer_probe::end_pair(pair)) return fail("reference end pair");
		context->Flush();
		for (std::size_t attempt{}; attempt < 10000; ++attempt)
		{
			engine_stereo_particle_buffer_probe::poll(context.Get(), 1, owner_thread);
			if (engine_stereo_particle_buffer_probe::get_report().current !=
				engine_stereo_particle_buffer_probe::state::pending) break;
			std::this_thread::yield();
		}
		const auto result = engine_stereo_particle_buffer_probe::get_report();
		if (result.current != engine_stereo_particle_buffer_probe::state::complete ||
			result.samples[0].vertex_sample_count != 3) return fail("reference retirement");
		const auto& changed_vertex = result.samples[0].vertices[1];
		if (pair == 74 && (!changed_vertex.cpu_reference_available ||
			!changed_vertex.cpu_eyes_equal || changed_vertex.gpu_matches_cpu_reference[0] ||
			!changed_vertex.gpu_matches_cpu_reference[1])) return fail("CPU/GPU epoch comparison");
		if (pair == 75 && changed_vertex.cpu_reference_available)
			return fail("rearm retained stale CPU reference");
	}
	const auto resource_status = engine_stereo_resource_ops::get_status();
	if (resource_status.calls[static_cast<std::size_t>(
		engine_stereo_resource_ops::api::copy_subresource_region)] != 0)
	{
		return fail("diagnostic copy polluted observer stream");
	}
	std::cout << "vr-d3d11-particle-buffer-readback-probe: PASS\n";
	return 0;
}
