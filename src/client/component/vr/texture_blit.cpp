#include <std_include.hpp>
#include "texture_blit.hpp"
#include "native_conversion_command_list.hpp"
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")

namespace vr::texture_blit
{
	namespace
	{
		constexpr char vertex_source[] = R"(
struct Output
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};
Output main(uint vertex_id : SV_VertexID)
{
    // One fullscreen triangle avoids a diagonal seam between two primitives.
    const float2 positions[3] = {float2(-1,-1), float2(-1,3), float2(3,-1)};
    const float2 uvs[3] = {float2(0,1), float2(0,-1), float2(2,1)};
    Output output;
    output.position = float4(positions[vertex_id], 0, 1);
    output.uv = uvs[vertex_id];
    return output;
})";
		constexpr char pixel_source[] = R"(
Texture2D<float4> source : register(t0);
SamplerState sampling : register(s0);
cbuffer Parameters : register(b0)
{
    uint mode;
    float dim;
    uint2 padding;
};
// Values match texture_blit::encoding sent through Parameters.
static const uint SOURCE_LINEAR = 0;
static const uint SOURCE_ENCODED_PREMULTIPLIED = 1;
static const uint SOURCE_ENCODED_OPAQUE = 2;
float3 decode_srgb(float3 encoded)
{
    float3 low = encoded / 12.92;
    float3 high = pow(max((encoded + 0.055) / 1.055, 0), 2.4);
    return float3(encoded.r <= 0.04045 ? low.r : high.r,
                  encoded.g <= 0.04045 ? low.g : high.g,
                  encoded.b <= 0.04045 ? low.b : high.b);
}
float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    float4 color = source.Sample(sampling, uv);
    if (mode == SOURCE_LINEAR)
        return float4(color.rgb * (1 - dim), color.a);

    float alpha = mode == SOURCE_ENCODED_OPAQUE ? 1 : color.a;
    float3 encoded = color.rgb;
    // Native UI was premultiplied in encoded space. Undo it before decoding,
    // then premultiply in linear space for the compositor's blend operation.
    if (mode == SOURCE_ENCODED_PREMULTIPLIED)
        encoded = color.a > 0 ? color.rgb / color.a : 0;
    return float4(decode_srgb(encoded) * alpha, alpha);
})";
	}
	void renderer::reset() noexcept
	{
		sources_ = {};
		device_.Reset();
		deferred_.Reset();
		vertex_.Reset();
		pixel_.Reset();
		sampler_.Reset();
		rasterizer_.Reset();
		depth_.Reset();
		blend_.Reset();
		parameters_.Reset();
		cursor_ = 0;
	}
	bool renderer::ensure(ID3D11Device* device, std::string& error)
	{
		if (device_.Get() == device && deferred_)
			return true;
		reset();
		Microsoft::WRL::ComPtr<ID3DBlob> vertex, pixel, errors;
		if (FAILED(D3DCompile(vertex_source,
		                      sizeof(vertex_source) - 1,
		                      "texture-blit",
		                      nullptr,
		                      nullptr,
		                      "main",
		                      "vs_5_0",
		                      D3DCOMPILE_OPTIMIZATION_LEVEL3,
		                      0,
		                      &vertex,
		                      &errors)) ||
		    FAILED(D3DCompile(pixel_source,
		                      sizeof(pixel_source) - 1,
		                      "texture-blit",
		                      nullptr,
		                      nullptr,
		                      "main",
		                      "ps_5_0",
		                      D3DCOMPILE_OPTIMIZATION_LEVEL3,
		                      0,
		                      &pixel,
		                      &errors)))
		{
			error = "texture blit shader compilation failed";
			if (errors)
				error += ": " + std::string(static_cast<const char*>(errors->GetBufferPointer()),
				                            (std::min)(std::size_t(4096), errors->GetBufferSize()));
			return false;
		}
		D3D11_SAMPLER_DESC sampler{};
		sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampler.MaxLOD = D3D11_FLOAT32_MAX;
		D3D11_RASTERIZER_DESC raster{};
		raster.FillMode = D3D11_FILL_SOLID;
		raster.CullMode = D3D11_CULL_NONE;
		raster.DepthClipEnable = TRUE;
		D3D11_DEPTH_STENCIL_DESC depth{};
		depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		D3D11_BLEND_DESC blend{};
		blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		D3D11_BUFFER_DESC buffer{};
		buffer.ByteWidth = 16;
		buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		buffer.Usage = D3D11_USAGE_DEFAULT;
		if (FAILED(device->CreateDeferredContext(0, &deferred_)) ||
		    !native_conversion_command_list::mark_recording_context(deferred_.Get()) ||
		    FAILED(device->CreateVertexShader(
		        vertex->GetBufferPointer(), vertex->GetBufferSize(), nullptr, &vertex_)) ||
		    FAILED(device->CreatePixelShader(
		        pixel->GetBufferPointer(), pixel->GetBufferSize(), nullptr, &pixel_)) ||
		    FAILED(device->CreateSamplerState(&sampler, &sampler_)) ||
		    FAILED(device->CreateRasterizerState(&raster, &rasterizer_)) ||
		    FAILED(device->CreateDepthStencilState(&depth, &depth_)) ||
		    FAILED(device->CreateBlendState(&blend, &blend_)) ||
		    FAILED(device->CreateBuffer(&buffer, nullptr, &parameters_)))
		{
			reset();
			error = "texture blit D3D11 pipeline creation failed";
			return false;
		}
		device_ = device;
		return true;
	}
	bool renderer::draw(const draw_request& request, std::string& error)
	{
		const auto& [graphics, source, destination, width, height, format, dim] = request;
		if (!graphics || !source || !destination || !width || !height || width > 8192 || height > 8192)
		{
			error = "invalid texture blit arguments";
			return false;
		}
		Microsoft::WRL::ComPtr<ID3D11Device> source_device, target_device;
		source->GetDevice(&source_device);
		destination->GetDevice(&target_device);
		Microsoft::WRL::ComPtr<ID3D11Resource> target;
		destination->GetResource(&target);
		if (source_device != graphics.device || target_device != graphics.device || target.Get() == source ||
		    graphics.context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
		{
			error = "texture blit requires separate source/output on the same immediate-context device";
			return false;
		}
		if (!ensure(graphics.device.Get(), error))
			return false;
		cached_source* cached = nullptr;
		for (auto& entry : sources_)
			if (entry.texture.Get() == source && entry.format == format)
			{
				cached = &entry;
				break;
			}
		if (!cached)
		{
			auto& entry = sources_[cursor_++ % sources_.size()];
			entry = {};
			D3D11_TEXTURE2D_DESC description{};
			source->GetDesc(&description);
			if (description.MipLevels != 1 || description.ArraySize != 1 || description.SampleDesc.Count != 1)
			{
				error = "texture blit source is not a single sampled 2D image";
				return false;
			}
			D3D11_SHADER_RESOURCE_VIEW_DESC view{};
			view.Format = description.Format;
			if (format != encoding::linear)
			{
				if (view.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
					view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
				if (view.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
					view.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			}
			view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			view.Texture2D.MipLevels = 1;
			if (FAILED(graphics.device->CreateShaderResourceView(source, &view, &entry.view)))
			{
				error = "texture blit source view creation failed";
				return false;
			}
			entry.texture = source;
			entry.format = format;
			cached = &entry;
		}
		const struct
		{
			unsigned mode;
			float dim;
			unsigned padding[2];
		} parameters{
		    static_cast<unsigned>(format), std::isfinite(dim) ? std::clamp(dim, 0.f, 1.f) : 0.f, {0, 0}};
		deferred_->ClearState();
		deferred_->UpdateSubresource(parameters_.Get(), 0, nullptr, &parameters, 0, 0);
		const float blend[4]{};
		deferred_->OMSetBlendState(blend_.Get(), blend, D3D11_DEFAULT_SAMPLE_MASK);
		deferred_->OMSetDepthStencilState(depth_.Get(), 0);
		deferred_->OMSetRenderTargets(1, &destination, nullptr);
		deferred_->RSSetState(rasterizer_.Get());
		const D3D11_VIEWPORT viewport{0, 0, float(width), float(height), 0, 1};
		deferred_->RSSetViewports(1, &viewport);
		deferred_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		deferred_->VSSetShader(vertex_.Get(), nullptr, 0);
		deferred_->PSSetShader(pixel_.Get(), nullptr, 0);
		deferred_->PSSetShaderResources(0, 1, cached->view.GetAddressOf());
		deferred_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
		deferred_->PSSetConstantBuffers(0, 1, parameters_.GetAddressOf());
		deferred_->Draw(3, 0);
		Microsoft::WRL::ComPtr<ID3D11CommandList> commands;
		if (FAILED(deferred_->FinishCommandList(FALSE, &commands)))
		{
			error = "texture blit recording failed";
			return false;
		}
		if (!native_conversion_command_list::mark(commands.Get()))
		{
			error = "texture blit command ownership marker failed";
			return false;
		}
		const auto queue = d3d11::acquire_gpu_queue_interop();
		graphics.context->ExecuteCommandList(commands.Get(), TRUE);
		if (FAILED(graphics.device->GetDeviceRemovedReason()))
		{
			error = "texture blit device was removed";
			return false;
		}
		return true;
	}
}
