#include <std_include.hpp>
#include "texture_extent.hpp"
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
    float sharpness;
    float padding0;
    float2 rcp_source_size;
    float2 padding1;
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
    {
        if (sharpness > 0.001)
        {
            float3 ec = max(color.rgb, 0.0);
            float3 b = max(source.SampleLevel(sampling, uv + float2(0.0, -rcp_source_size.y), 0.0).rgb, 0.0);
            float3 d = max(source.SampleLevel(sampling, uv + float2(-rcp_source_size.x, 0.0), 0.0).rgb, 0.0);
            float3 f = max(source.SampleLevel(sampling, uv + float2(rcp_source_size.x, 0.0), 0.0).rgb, 0.0);
            float3 h = max(source.SampleLevel(sampling, uv + float2(0.0, rcp_source_size.y), 0.0).rgb, 0.0);
            float3 mn = min(min(min(b, d), min(f, h)), ec);
            float3 mx = max(max(max(b, d), max(f, h)), ec);
            float3 amp = sqrt(saturate(min(mn, max(1.0 - mx, 0.0)) / max(mx, 1e-5)));
            float peak = -0.2 * saturate(sharpness);
            float3 w = amp * peak;
            float3 out_rgb = max(((b + d + f + h) * w + ec) / max(1.0 + 4.0 * w, 1e-4), 0.0);
            return float4(out_rgb * (1.0 - dim), color.a);
        }
        return float4(color.rgb * (1.0 - dim), color.a);
    }

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
		validated_device_.Reset();
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
		if ((device_ && device_.Get() != device) ||
		    (validated_device_ && validated_device_.Get() != device))
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
		buffer.ByteWidth = 32;
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
		validated_device_ = device;
		return true;
	}
	bool renderer::draw(const draw_request& request, std::string& error)
	{
		const auto& [graphics, source, destination, width, height, format, dim, sharpness] = request;
		if (!graphics || !source || !destination || !texture_extent::valid(width, height))
		{
			error = "invalid texture blit arguments";
			return false;
		}
		if (graphics.context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
		{
			error = "texture blit requires separate source/output on the same immediate-context device";
			return false;
		}
		if ((device_ && device_.Get() != graphics.device.Get()) ||
		    (validated_device_ && validated_device_.Get() != graphics.device.Get()))
		{
			reset();
		}
		Microsoft::WRL::ComPtr<ID3D11Device> target_device;
		destination->GetDevice(&target_device);
		Microsoft::WRL::ComPtr<ID3D11Resource> target;
		destination->GetResource(&target);
		if (target_device != graphics.device || !target || target.Get() == source)
		{
			error = "texture blit requires separate source/output on the same immediate-context device";
			return false;
		}
		cached_source* any_source_entry = nullptr;
		cached_source* exact_entry = nullptr;
		if (validated_device_.Get() == graphics.device.Get())
		{
			for (auto& entry : sources_)
			{
				if (entry.texture.Get() == source)
				{
					if (!any_source_entry)
						any_source_entry = &entry;
					if (entry.format == format)
					{
						exact_entry = &entry;
						break;
					}
				}
			}
		}
		D3D11_TEXTURE2D_DESC src_desc{};
		if (any_source_entry)
		{
			src_desc = any_source_entry->description;
		}
		else
		{
			Microsoft::WRL::ComPtr<ID3D11Device> source_device;
			source->GetDevice(&source_device);
			if (source_device != graphics.device)
			{
				error = "texture blit requires separate source/output on the same immediate-context device";
				return false;
			}
			source->GetDesc(&src_desc);
			validated_device_ = graphics.device;
		}
		const float clamped_dim = std::isfinite(dim) ? std::clamp(dim, 0.f, 1.f) : 0.f;
		const float clamped_sharpness = std::isfinite(sharpness) ? std::clamp(sharpness, 0.f, 1.f) : 0.f;
		if (format == encoding::linear && clamped_dim == 0.f && clamped_sharpness == 0.f)
		{
			D3D11_RENDER_TARGET_VIEW_DESC rtv_desc{};
			destination->GetDesc(&rtv_desc);
			Microsoft::WRL::ComPtr<ID3D11Texture2D> target_tex;
			if (rtv_desc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D &&
			    rtv_desc.Texture2D.MipSlice == 0 &&
			    SUCCEEDED(target.As(&target_tex)) && target_tex)
			{
				D3D11_TEXTURE2D_DESC dst_desc{};
				target_tex->GetDesc(&dst_desc);
				if (src_desc.Width == width && src_desc.Height == height &&
				    dst_desc.Width == width && dst_desc.Height == height &&
				    src_desc.Format == dst_desc.Format &&
				    src_desc.MipLevels == 1 && dst_desc.MipLevels == 1 &&
				    src_desc.ArraySize == 1 && dst_desc.ArraySize == 1 &&
				    src_desc.SampleDesc.Count == 1 && dst_desc.SampleDesc.Count == 1)
				{
					const auto queue = d3d11::acquire_gpu_queue_interop();
					graphics.context->CopyResource(target.Get(), source);
					if (FAILED(graphics.device->GetDeviceRemovedReason()))
					{
						error = "texture blit device was removed";
						return false;
					}
					if (!any_source_entry)
					{
						auto& entry = sources_[cursor_++ % sources_.size()];
						entry = {};
						entry.texture = source;
						entry.description = src_desc;
						entry.format = format;
					}
					return true;
				}
			}
		}
		if (!ensure(graphics.device.Get(), error))
			return false;
		cached_source* cached = exact_entry;
		if (!cached || !cached->view)
		{
			if (src_desc.MipLevels != 1 || src_desc.ArraySize != 1 || src_desc.SampleDesc.Count != 1)
			{
				error = "texture blit source is not a single sampled 2D image";
				return false;
			}
			D3D11_SHADER_RESOURCE_VIEW_DESC view{};
			view.Format = src_desc.Format;
			if (format != encoding::linear)
			{
				if (view.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
					view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
				if (view.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
					view.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			}
			view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			view.Texture2D.MipLevels = 1;
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shader_view;
			if (FAILED(graphics.device->CreateShaderResourceView(source, &view, &shader_view)))
			{
				error = "texture blit source view creation failed";
				return false;
			}
			if (!cached)
			{
				cached = &sources_[cursor_++ % sources_.size()];
				*cached = {};
				cached->texture = source;
				cached->description = src_desc;
				cached->format = format;
			}
			cached->view = std::move(shader_view);
		}
		const struct
		{
			unsigned mode;
			float dim;
			float sharpness;
			float padding0;
			float rcp_source_size[2];
			float padding1[2];
		} parameters{
		    static_cast<unsigned>(format),
		    clamped_dim,
		    clamped_sharpness,
		    0.f,
		    {1.0f / static_cast<float>((std::max)(1u, src_desc.Width)),
		     1.0f / static_cast<float>((std::max)(1u, src_desc.Height))},
		    {0.f, 0.f}};
		static_assert(sizeof(parameters) == 32);
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
			deferred_->ClearState();
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
