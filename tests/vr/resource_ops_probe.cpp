#include <std_include.hpp>

#include "component/vr/engine_stereo_resource_ops.hpp"

#include <array>
#include <iostream>

namespace
{
	std::array<std::uint64_t, vr::engine_stereo_resource_ops::api_count> observed{};
	std::array<vr::engine_stereo_resource_ops::event,
		vr::engine_stereo_resource_ops::api_count> latest{};
	std::array<std::uint64_t, vr::engine_stereo_resource_ops::api_count>
		content_observed{};
	vr::engine_stereo_resource_ops::event latest_update_content{};

	void observe(const vr::engine_stereo_resource_ops::event& value) noexcept
	{
		const auto index = static_cast<std::size_t>(value.operation);
		if (index >= observed.size()) return;
		++observed[index];
		latest[index] = value;
	}

	void observe_content(const vr::engine_stereo_resource_ops::event& value) noexcept
	{
		const auto index = static_cast<std::size_t>(value.operation);
		if (index >= content_observed.size()) return;
		++content_observed[index];
		if (value.operation == vr::engine_stereo_resource_ops::api::update_subresource)
			latest_update_content = value;
	}

	int fail(const char* const reason)
	{
		std::cerr << "vr-d3d11-resource-ops-probe: FAIL; " << reason << '\n';
		return 1;
	}
}

int main()
{
	Microsoft::WRL::ComPtr<ID3D11Device> device;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
	D3D_FEATURE_LEVEL feature_level{};
	if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
		D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
		&device, &feature_level, &context)))
	{
		return fail("D3D11CreateDevice(WARP)");
	}
	if (!vr::engine_stereo_resource_ops::install(context.Get(), 1))
		return fail("hook install");
	vr::engine_stereo_resource_ops::set_observer(
		vr::engine_stereo_resource_ops::observer_channel::gpu_census, observe);
	vr::engine_stereo_resource_ops::set_observer(
		vr::engine_stereo_resource_ops::observer_channel::constant_buffer_history,
		observe_content);

	D3D11_TEXTURE2D_DESC texture_description{};
	texture_description.Width = 16;
	texture_description.Height = 16;
	texture_description.MipLevels = 1;
	texture_description.ArraySize = 1;
	texture_description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	texture_description.SampleDesc.Count = 1;
	texture_description.Usage = D3D11_USAGE_DEFAULT;
	texture_description.BindFlags = D3D11_BIND_SHADER_RESOURCE |
		D3D11_BIND_RENDER_TARGET;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> source;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> destination;
	if (FAILED(device->CreateTexture2D(&texture_description, nullptr, &source)) ||
		FAILED(device->CreateTexture2D(&texture_description, nullptr, &destination)))
	{
		return fail("ordinary texture creation");
	}

	context->CopySubresourceRegion(destination.Get(), 0, 0, 0, 0,
		source.Get(), 0, nullptr);
	context->CopyResource(destination.Get(), source.Get());
	std::array<std::uint32_t, 16 * 16> pixels{};
	context->UpdateSubresource(destination.Get(), 0, nullptr, pixels.data(),
		16 * sizeof(std::uint32_t), 0);

	D3D11_BUFFER_DESC counter_buffer_description{};
	counter_buffer_description.ByteWidth = 16;
	counter_buffer_description.Usage = D3D11_USAGE_DEFAULT;
	counter_buffer_description.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
	counter_buffer_description.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	counter_buffer_description.StructureByteStride = sizeof(std::uint32_t);
	Microsoft::WRL::ComPtr<ID3D11Buffer> counter_buffer;
	if (FAILED(device->CreateBuffer(&counter_buffer_description, nullptr, &counter_buffer)))
		return fail("counter buffer creation");
	D3D11_UNORDERED_ACCESS_VIEW_DESC counter_view_description{};
	counter_view_description.Format = DXGI_FORMAT_UNKNOWN;
	counter_view_description.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	counter_view_description.Buffer.NumElements = 4;
	counter_view_description.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_COUNTER;
	Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> counter_view;
	if (FAILED(device->CreateUnorderedAccessView(counter_buffer.Get(),
		&counter_view_description, &counter_view)))
	{
		return fail("counter UAV creation");
	}
	D3D11_BUFFER_DESC count_destination_description{};
	count_destination_description.ByteWidth = sizeof(std::uint32_t);
	count_destination_description.Usage = D3D11_USAGE_DEFAULT;
	Microsoft::WRL::ComPtr<ID3D11Buffer> count_destination;
	if (FAILED(device->CreateBuffer(&count_destination_description, nullptr,
		&count_destination)))
	{
		return fail("counter destination creation");
	}
	context->CopyStructureCount(count_destination.Get(), 0, counter_view.Get());

	D3D11_TEXTURE2D_DESC uav_description = texture_description;
	uav_description.Format = DXGI_FORMAT_R32_FLOAT;
	uav_description.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> float_uav_texture;
	Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> float_uav;
	if (FAILED(device->CreateTexture2D(&uav_description, nullptr, &float_uav_texture)) ||
		FAILED(device->CreateUnorderedAccessView(float_uav_texture.Get(), nullptr,
			&float_uav)))
	{
		return fail("float UAV creation");
	}
	const FLOAT clear_float[4]{1.0f, 0.0f, 0.0f, 0.0f};
	context->ClearUnorderedAccessViewFloat(float_uav.Get(), clear_float);
	uav_description.Format = DXGI_FORMAT_R32_UINT;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> uint_uav_texture;
	Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uint_uav;
	if (FAILED(device->CreateTexture2D(&uav_description, nullptr, &uint_uav_texture)) ||
		FAILED(device->CreateUnorderedAccessView(uint_uav_texture.Get(), nullptr,
			&uint_uav)))
	{
		return fail("uint UAV creation");
	}
	const UINT clear_uint[4]{1, 0, 0, 0};
	context->ClearUnorderedAccessViewUint(uint_uav.Get(), clear_uint);

	D3D11_TEXTURE2D_DESC mip_description = texture_description;
	mip_description.MipLevels = 0;
	mip_description.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> mip_texture;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> mip_view;
	if (FAILED(device->CreateTexture2D(&mip_description, nullptr, &mip_texture)) ||
		FAILED(device->CreateShaderResourceView(mip_texture.Get(), nullptr, &mip_view)))
	{
		return fail("mip resource creation");
	}
	context->GenerateMips(mip_view.Get());

	UINT quality_levels{};
	if (FAILED(device->CheckMultisampleQualityLevels(texture_description.Format, 2,
		&quality_levels)) || quality_levels == 0)
	{
		return fail("WARP lacks 2x MSAA needed by resolve probe");
	}
	D3D11_TEXTURE2D_DESC multisampled_description = texture_description;
	multisampled_description.SampleDesc.Count = 2;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> multisampled;
	if (FAILED(device->CreateTexture2D(&multisampled_description, nullptr,
		&multisampled)))
	{
		return fail("multisampled source creation");
	}
	context->ResolveSubresource(destination.Get(), 0, multisampled.Get(), 0,
		texture_description.Format);

	const auto status = vr::engine_stereo_resource_ops::get_status();
	if (!status.hooks_installed || status.expected_context !=
		reinterpret_cast<std::uintptr_t>(context.Get()) || status.device_generation != 1 ||
		status.hook_failures != 0 || status.observer_callbacks !=
		2 * vr::engine_stereo_resource_ops::api_count)
	{
		return fail("terminal status contract");
	}
	for (std::size_t operation{}; operation < observed.size(); ++operation)
	{
		if (observed[operation] != 1 || content_observed[operation] != 1 ||
			status.calls[operation] != 1 ||
			status.hook_targets[operation] == 0 || latest[operation].context != context.Get())
		{
			return fail("per-operation observation contract");
		}
	}
	if (latest[static_cast<std::size_t>(vr::engine_stereo_resource_ops::api::
		copy_subresource_region)].source != source.Get() ||
		latest[static_cast<std::size_t>(vr::engine_stereo_resource_ops::api::
		copy_subresource_region)].destination != destination.Get() ||
		latest[static_cast<std::size_t>(vr::engine_stereo_resource_ops::api::
		generate_mips)].view != mip_view.Get())
	{
		return fail("borrowed identity contract");
	}
	if (latest_update_content.source_data != pixels.data() ||
		latest_update_content.update_box != nullptr ||
		latest_update_content.source_row_pitch != 16 * sizeof(std::uint32_t) ||
		latest_update_content.source_depth_pitch != 0)
	{
		return fail("UpdateSubresource content observer contract");
	}

	vr::engine_stereo_resource_ops::invalidate_device(context.Get(), 1);
	const auto invalidated = vr::engine_stereo_resource_ops::get_status();
	if (invalidated.expected_context != 0 || invalidated.device_generation != 0)
		return fail("device invalidation contract");

	std::cout << "vr-d3d11-resource-ops-probe: PASS\n";
	return 0;
}
