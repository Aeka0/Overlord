#include <std_include.hpp>

#include "scene_compositor.hpp"
#include "diagnostics.hpp"

#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <cmath>
#include <format>
#include <utility>
#include <windows.h>

#pragma comment(lib, "d3dcompiler.lib")

namespace vr
{
	namespace
	{
		constexpr char fullscreen_vertex_shader[] = R"(
struct VSOut
{
	float4 position : SV_Position;
	float2 uv : TEXCOORD0;
};

VSOut main(uint vertex_id : SV_VertexID)
{
	const float2 positions[3] = {
		float2(-1.0, -1.0),
		float2(-1.0,  3.0),
		float2( 3.0, -1.0)
	};
	const float2 texcoords[3] = {
		float2(0.0, 1.0),
		float2(0.0, -1.0),
		float2(2.0, 1.0)
	};

	VSOut output;
	output.position = float4(positions[vertex_id], 0.0, 1.0);
	output.uv = texcoords[vertex_id];
	return output;
}
)";

		constexpr char fullscreen_pixel_shader[] = R"(
Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);
cbuffer BlitParameters : register(b0)
{
	float2 uv_scale;
	float2 uv_offset;
	float2 uv_min;
	float2 uv_max;
	float color_transform;
	float3 padding;
};

float3 srgb_to_linear(float3 value)
{
	return lerp(value / 12.92,
		pow((value + 0.055) / 1.055, 2.4),
		step(0.04045, value));
}

float3 linear_to_srgb(float3 value)
{
	return lerp(value * 12.92,
		1.055 * pow(max(value, 0.0), 1.0 / 2.4) - 0.055,
		step(0.0031308, value));
}

float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
	const float2 source_uv = clamp(uv * uv_scale + uv_offset, uv_min, uv_max);
	float4 color = source_texture.Sample(source_sampler, source_uv);
	if (color_transform > 0.5 && color_transform < 1.5)
	{
		color.rgb = srgb_to_linear(color.rgb);
	}
	else if (color_transform > 1.5)
	{
		color.rgb = linear_to_srgb(color.rgb);
	}
	return color;
}
)";

		struct alignas(16) blit_parameters
		{
			float scale_u{};
			float scale_v{};
			float offset_u{};
			float offset_v{};
			float min_u{};
			float min_v{};
			float max_u{1.0f};
			float max_v{1.0f};
			float color_transform{};
			float padding[3]{};
		};

		bool is_srgb_format(const DXGI_FORMAT format) noexcept
		{
			switch (format)
			{
			case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
			case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
			case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
				return true;
			default:
				return false;
			}
		}

		std::string shader_error(ID3DBlob* blob, const char* stage)
		{
			if (blob == nullptr || blob->GetBufferPointer() == nullptr ||
				blob->GetBufferSize() == 0)
			{
				return std::format("{} shader compilation failed", stage);
			}

			return std::format("{} shader compilation failed: {}", stage,
				static_cast<const char*>(blob->GetBufferPointer()));
		}

		class scoped_d3d_state final
		{
		public:
			explicit scoped_d3d_state(ID3D11DeviceContext* context) : context_(context)
			{
				context_->OMGetRenderTargets(static_cast<UINT>(render_targets_.size()),
					render_targets_.data(), depth_stencil_view_.GetAddressOf());
				context_->OMGetBlendState(blend_state_.GetAddressOf(), blend_factor_, &sample_mask_);
				context_->OMGetDepthStencilState(depth_stencil_state_.GetAddressOf(), &stencil_ref_);
				context_->RSGetState(rasterizer_state_.GetAddressOf());

				UINT viewport_count = static_cast<UINT>(viewports_.size());
				context_->RSGetViewports(&viewport_count, viewports_.data());
				viewport_count_ = std::min(viewport_count,
					static_cast<UINT>(viewports_.size()));
				UINT scissor_count = static_cast<UINT>(scissors_.size());
				context_->RSGetScissorRects(&scissor_count, scissors_.data());
				scissor_count_ = std::min(scissor_count,
					static_cast<UINT>(scissors_.size()));

				context_->IAGetInputLayout(input_layout_.GetAddressOf());
				context_->IAGetPrimitiveTopology(&primitive_topology_);
				context_->IAGetVertexBuffers(0, static_cast<UINT>(vertex_buffers_.size()),
					vertex_buffers_.data(), vertex_strides_.data(), vertex_offsets_.data());
				context_->IAGetIndexBuffer(index_buffer_.GetAddressOf(), &index_format_, &index_offset_);

				UINT instance_count = static_cast<UINT>(vs_instances_.size());
				context_->VSGetShader(vertex_shader_.GetAddressOf(), vs_instances_.data(),
					&instance_count);
				vs_instance_count_ = std::min(instance_count,
					static_cast<UINT>(vs_instances_.size()));
				instance_count = static_cast<UINT>(ps_instances_.size());
				context_->PSGetShader(pixel_shader_.GetAddressOf(), ps_instances_.data(),
					&instance_count);
				ps_instance_count_ = std::min(instance_count,
					static_cast<UINT>(ps_instances_.size()));
				context_->PSGetShaderResources(0, 1, &pixel_shader_resource_);
				context_->PSGetSamplers(0, 1, &pixel_sampler_);
				context_->PSGetConstantBuffers(0, 1, pixel_constant_buffer_.GetAddressOf());
			}

			~scoped_d3d_state()
			{
				context_->OMSetRenderTargets(static_cast<UINT>(render_targets_.size()),
					render_targets_.data(), depth_stencil_view_.Get());
				context_->OMSetBlendState(blend_state_.Get(), blend_factor_, sample_mask_);
				context_->OMSetDepthStencilState(depth_stencil_state_.Get(), stencil_ref_);
				context_->RSSetState(rasterizer_state_.Get());
				context_->RSSetViewports(viewport_count_, viewports_.data());
				context_->RSSetScissorRects(scissor_count_, scissors_.data());
				context_->IASetInputLayout(input_layout_.Get());
				context_->IASetPrimitiveTopology(primitive_topology_);
				context_->IASetVertexBuffers(0, static_cast<UINT>(vertex_buffers_.size()),
					vertex_buffers_.data(), vertex_strides_.data(), vertex_offsets_.data());
				context_->IASetIndexBuffer(index_buffer_.Get(), index_format_, index_offset_);
				context_->VSSetShader(vertex_shader_.Get(), vs_instances_.data(), vs_instance_count_);
				context_->PSSetShader(pixel_shader_.Get(), ps_instances_.data(), ps_instance_count_);
				context_->PSSetShaderResources(0, 1, &pixel_shader_resource_);
				context_->PSSetSamplers(0, 1, &pixel_sampler_);
				context_->PSSetConstantBuffers(0, 1, pixel_constant_buffer_.GetAddressOf());
				release_all(render_targets_);
				release_all(vertex_buffers_);
				release_all(vs_instances_);
				release_all(ps_instances_);
				release_one(pixel_shader_resource_);
				release_one(pixel_sampler_);
			}

		private:
			template <typename T, std::size_t N>
			static void release_all(std::array<T*, N>& values) noexcept
			{
				for (auto*& value : values)
				{
					release_one(value);
				}
			}

			template <typename T>
			static void release_one(T*& value) noexcept
			{
				if (value != nullptr)
				{
					value->Release();
					value = nullptr;
				}
			}

			ID3D11DeviceContext* context_{};
			std::array<ID3D11RenderTargetView*, 8> render_targets_{};
			Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth_stencil_view_;
			Microsoft::WRL::ComPtr<ID3D11BlendState> blend_state_;
			Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_stencil_state_;
			Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer_state_;
			FLOAT blend_factor_[4]{};
			UINT sample_mask_{};
			UINT stencil_ref_{};
			std::array<D3D11_VIEWPORT, 16> viewports_{};
			UINT viewport_count_{};
			std::array<D3D11_RECT, 16> scissors_{};
			UINT scissor_count_{};
			Microsoft::WRL::ComPtr<ID3D11InputLayout> input_layout_;
			D3D11_PRIMITIVE_TOPOLOGY primitive_topology_{};
			std::array<ID3D11Buffer*, 32> vertex_buffers_{};
			std::array<UINT, 32> vertex_strides_{};
			std::array<UINT, 32> vertex_offsets_{};
			Microsoft::WRL::ComPtr<ID3D11Buffer> index_buffer_;
			DXGI_FORMAT index_format_{DXGI_FORMAT_UNKNOWN};
			UINT index_offset_{};
			Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader_;
			std::array<ID3D11ClassInstance*, 256> vs_instances_{};
			UINT vs_instance_count_{};
			Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader_;
			std::array<ID3D11ClassInstance*, 256> ps_instances_{};
			UINT ps_instance_count_{};
			ID3D11ShaderResourceView* pixel_shader_resource_{};
			ID3D11SamplerState* pixel_sampler_{};
			Microsoft::WRL::ComPtr<ID3D11Buffer> pixel_constant_buffer_;
		};
	}

	bool calculate_projection_uv_mapping(
		const engine_stereo_bridge::eye_projection& projection,
		scene_uv_mapping& output) noexcept
	{
		output = {};
		const auto half_x = projection.symmetric_tan_half_x();
		const auto half_y = projection.symmetric_tan_half_y();
		if (!std::isfinite(projection.tan_left) ||
			!std::isfinite(projection.tan_right) ||
			!std::isfinite(projection.tan_down) ||
			!std::isfinite(projection.tan_up) ||
			projection.tan_left >= projection.tan_right ||
			projection.tan_down >= projection.tan_up ||
			half_x <= 0.0f || half_y <= 0.0f)
		{
			return false;
		}

		// The game renders a centered symmetric frustum. For every destination
		// texel, map the runtime's asymmetric tangent-space ray back into that
		// source frustum. Texture pixel aspect never participates in this math.
		output.scale_u = (projection.tan_right - projection.tan_left) / (2.0f * half_x);
		output.offset_u = (projection.tan_left + half_x) / (2.0f * half_x);
		output.scale_v = (projection.tan_up - projection.tan_down) / (2.0f * half_y);
		output.offset_v = (half_y - projection.tan_up) / (2.0f * half_y);
		return true;
	}

	bool scene_compositor::ensure_pipeline(ID3D11Device* const device,
		const std::uint64_t generation, std::string& error)
	{
		if (generation_ == generation && vertex_shader_ && pixel_shader_ && blit_parameters_ && sampler_ &&
			rasterizer_state_ && depth_state_ && blend_state_ && copy_fence_)
		{
			return true;
		}

		vertex_shader_.Reset();
		pixel_shader_.Reset();
		blit_parameters_.Reset();
		sampler_.Reset();
		rasterizer_state_.Reset();
		depth_state_.Reset();
		blend_state_.Reset();
		copy_fence_.Reset();
		invalidate();

		Microsoft::WRL::ComPtr<ID3DBlob> vertex_bytecode;
		Microsoft::WRL::ComPtr<ID3DBlob> compile_errors;
		auto result = D3DCompile(fullscreen_vertex_shader,
			sizeof(fullscreen_vertex_shader) - 1, "h2-mod-vr-blit-vs", nullptr,
			nullptr, "main", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
			&vertex_bytecode, &compile_errors);
		if (FAILED(result))
		{
			error = shader_error(compile_errors.Get(), "Vertex");
			return false;
		}
		result = device->CreateVertexShader(vertex_bytecode->GetBufferPointer(),
			vertex_bytecode->GetBufferSize(), nullptr, &vertex_shader_);
		if (FAILED(result))
		{
			error = std::format("CreateVertexShader failed (HRESULT=0x{:08x})",
				static_cast<std::uint32_t>(result));
			return false;
		}

		compile_errors.Reset();
		Microsoft::WRL::ComPtr<ID3DBlob> pixel_bytecode;
		result = D3DCompile(fullscreen_pixel_shader,
			sizeof(fullscreen_pixel_shader) - 1, "h2-mod-vr-blit-ps", nullptr,
			nullptr, "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
			&pixel_bytecode, &compile_errors);
		if (FAILED(result))
		{
			error = shader_error(compile_errors.Get(), "Pixel");
			return false;
		}
		result = device->CreatePixelShader(pixel_bytecode->GetBufferPointer(),
			pixel_bytecode->GetBufferSize(), nullptr, &pixel_shader_);
		if (FAILED(result))
		{
			error = std::format("CreatePixelShader failed (HRESULT=0x{:08x})",
				static_cast<std::uint32_t>(result));
			return false;
		}

		D3D11_BUFFER_DESC parameter_description{};
		parameter_description.ByteWidth = sizeof(blit_parameters);
		parameter_description.Usage = D3D11_USAGE_DEFAULT;
		parameter_description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		result = device->CreateBuffer(&parameter_description, nullptr, &blit_parameters_);
		if (FAILED(result))
		{
			error = std::format("Create compositor parameter buffer failed (HRESULT=0x{:08x})",
				static_cast<std::uint32_t>(result));
			return false;
		}

		const D3D11_SAMPLER_DESC sampler_description{
			D3D11_FILTER_MIN_MAG_MIP_LINEAR,
			D3D11_TEXTURE_ADDRESS_CLAMP,
			D3D11_TEXTURE_ADDRESS_CLAMP,
			D3D11_TEXTURE_ADDRESS_CLAMP,
			0.0f, 1, D3D11_COMPARISON_ALWAYS,
			{1.0f, 1.0f, 1.0f, 1.0f}, -FLT_MAX, FLT_MAX};
		result = device->CreateSamplerState(&sampler_description, &sampler_);
		if (FAILED(result))
		{
			error = std::format("CreateSamplerState failed (HRESULT=0x{:08x})",
				static_cast<std::uint32_t>(result));
			return false;
		}

		const D3D11_RASTERIZER_DESC rasterizer_description{
			D3D11_FILL_SOLID, D3D11_CULL_NONE, FALSE, 0, 0.0f, 0.0f,
			TRUE, FALSE, FALSE, FALSE};
		result = device->CreateRasterizerState(&rasterizer_description,
			&rasterizer_state_);
		if (FAILED(result))
		{
			error = std::format("CreateRasterizerState failed (HRESULT=0x{:08x})",
				static_cast<std::uint32_t>(result));
			return false;
		}

		const D3D11_DEPTH_STENCIL_DESC depth_description{
			FALSE,
			D3D11_DEPTH_WRITE_MASK_ZERO,
			D3D11_COMPARISON_ALWAYS,
			FALSE,
			D3D11_DEFAULT_STENCIL_READ_MASK,
			D3D11_DEFAULT_STENCIL_WRITE_MASK,
			{},
			{}};
		result = device->CreateDepthStencilState(&depth_description, &depth_state_);
		if (FAILED(result))
		{
			error = std::format("CreateDepthStencilState failed (HRESULT=0x{:08x})",
				static_cast<std::uint32_t>(result));
			return false;
		}

		const D3D11_BLEND_DESC blend_description{
			FALSE, FALSE, {{FALSE, D3D11_BLEND_ONE, D3D11_BLEND_ZERO,
			D3D11_BLEND_OP_ADD, D3D11_BLEND_ONE, D3D11_BLEND_ZERO,
			D3D11_BLEND_OP_ADD, D3D11_COLOR_WRITE_ENABLE_ALL}}};
		result = device->CreateBlendState(&blend_description, &blend_state_);
		if (FAILED(result))
		{
			error = std::format("CreateBlendState failed (HRESULT=0x{:08x})",
				static_cast<std::uint32_t>(result));
			return false;
		}

		const D3D11_QUERY_DESC fence_description{D3D11_QUERY_EVENT, 0};
		result = device->CreateQuery(&fence_description, &copy_fence_);
		if (FAILED(result))
		{
			error = std::format("Create compositor copy fence failed (HRESULT=0x{:08x})",
				static_cast<std::uint32_t>(result));
			return false;
		}

		generation_ = generation;
		return true;
	}

	bool scene_compositor::ensure_source(ID3D11Device* const device,
		const D3D11_TEXTURE2D_DESC& source_description,
		const std::uint64_t generation, std::string& error)
	{
		if (source_description.Width == 0 || source_description.Height == 0 ||
			source_description.Format == DXGI_FORMAT_UNKNOWN ||
			source_description.ArraySize != 1 || source_description.MipLevels != 1)
		{
			error = "Game backbuffer has unsupported dimensions, format, array size, or mip count";
			return false;
		}
		if (source_description.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS ||
			source_description.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS)
		{
			error = "Typeless game backbuffers are not supported by the first compositor implementation";
			return false;
		}

		const bool needs_rebuild = std::ranges::any_of(source_textures_, [](const auto& source)
		{
			return !source;
		}) ||
			source_description_.Width != source_description.Width ||
			source_description_.Height != source_description.Height ||
			source_description_.Format != source_description.Format ||
			source_description_.SampleDesc.Count != source_description.SampleDesc.Count ||
			source_description_.SampleDesc.Quality != source_description.SampleDesc.Quality ||
			generation_ != generation;
		if (!needs_rebuild)
		{
			return true;
		}

		for (auto& source : source_textures_)
		{
			source.Reset();
		}
		for (auto& view : source_views_)
		{
			view.Reset();
		}
		for (auto& metadata : source_metadata_)
		{
			metadata = {};
		}

		auto intermediate_description = source_description;
		intermediate_description.Usage = D3D11_USAGE_DEFAULT;
		intermediate_description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		intermediate_description.CPUAccessFlags = 0;
		intermediate_description.MiscFlags = 0;
		intermediate_description.SampleDesc.Count = 1;
		intermediate_description.SampleDesc.Quality = 0;
		D3D11_SHADER_RESOURCE_VIEW_DESC view_description{};
		view_description.Format = intermediate_description.Format;
		view_description.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		view_description.Texture2D.MipLevels = 1;
		for (std::size_t source_index = 0; source_index < source_textures_.size(); ++source_index)
		{
			auto result = device->CreateTexture2D(&intermediate_description, nullptr,
				&source_textures_[source_index]);
			if (FAILED(result))
			{
				error = std::format("Create compositor intermediate texture {} failed (HRESULT=0x{:08x})",
					source_index, static_cast<std::uint32_t>(result));
				return false;
			}
			result = device->CreateShaderResourceView(source_textures_[source_index].Get(),
				&view_description, &source_views_[source_index]);
			if (FAILED(result))
			{
				error = std::format("Create compositor source view {} failed (HRESULT=0x{:08x})",
					source_index, static_cast<std::uint32_t>(result));
				return false;
			}
		}
		source_description_ = source_description;
		generation_ = generation;
		return true;
	}

	bool scene_compositor::copy_source_synchronized(ID3D11DeviceContext* const context,
		ID3D11Texture2D* const source, ID3D11Texture2D* const destination,
		std::string& error)
	{
		if (context == nullptr || source == nullptr || destination == nullptr || !copy_fence_)
		{
			error = "Compositor copy fence is not initialized";
			return false;
		}
		diagnostics::gpu_interop_entered(reinterpret_cast<std::uintptr_t>(source),
			reinterpret_cast<std::uintptr_t>(destination));
		const auto interop_exit = gsl::finally([]()
		{
			diagnostics::gpu_interop_exited();
		});

		Microsoft::WRL::ComPtr<IDXGIKeyedMutex> keyed_mutex;
		const auto keyed_query = source->QueryInterface(IID_PPV_ARGS(&keyed_mutex));
		if (SUCCEEDED(keyed_query) && keyed_mutex != nullptr)
		{
			const auto acquire = keyed_mutex->AcquireSync(1, 500);
		if (FAILED(acquire))
			{
				error = std::format("AcquireSync(native source) failed (HRESULT=0x{:08x})",
					static_cast<std::uint32_t>(acquire));
				return false;
			}
		}

		diagnostics::record_trace(diagnostics::trace_event::scene_gpu_copy_begin,
			reinterpret_cast<std::uintptr_t>(source),
			reinterpret_cast<std::uintptr_t>(destination));
		context->CopyResource(destination, source);
		diagnostics::record_trace(diagnostics::trace_event::scene_gpu_copy_end,
			reinterpret_cast<std::uintptr_t>(source),
			reinterpret_cast<std::uintptr_t>(destination));
		diagnostics::record_trace(diagnostics::trace_event::scene_gpu_fence,
			reinterpret_cast<std::uintptr_t>(source),
			reinterpret_cast<std::uintptr_t>(destination));
		context->End(copy_fence_.Get());
		context->Flush();
		const auto deadline = GetTickCount64() + 500;
		for (;;)
		{
			const auto result = context->GetData(copy_fence_.Get(), nullptr, 0, 0);
			if (result == S_OK)
			{
				if (keyed_mutex != nullptr)
				{
					const auto release = keyed_mutex->ReleaseSync(0);
					if (FAILED(release))
					{
						error = std::format("ReleaseSync(native source) failed (HRESULT=0x{:08x})",
							static_cast<std::uint32_t>(release));
						return false;
					}
				}
				diagnostics::record_trace(diagnostics::trace_event::scene_gpu_fence_result, 1, 0);
				return true;
			}
		if (FAILED(result) || GetTickCount64() >= deadline)
			{
				diagnostics::record_trace(diagnostics::trace_event::scene_gpu_fence_result,
					0, static_cast<std::uint32_t>(result));
				// The keyed mutex is still owned by this consumer until the GPU
				// copy fence completes. Releasing key 0 on timeout lets H2 begin
				// writing while CopyResource is in flight, which is an actual
				// cross-device race and was observed as an NVIDIA access violation.
				// Leave it acquired so the producer cannot reuse the resource.
				error = std::format("Compositor copy fence did not complete (HRESULT=0x{:08x})",
					static_cast<std::uint32_t>(result));
				return false;
			}
			SwitchToThread();
		}
	}

	bool scene_compositor::copy_source(ID3D11DeviceContext* const context,
		ID3D11Texture2D* const backbuffer,
		const D3D11_TEXTURE2D_DESC& description,
		const std::uint32_t source_index, std::string& error)
	{
		if (context == nullptr || backbuffer == nullptr ||
			source_index >= source_textures_.size() || !source_textures_[source_index])
		{
			error = "Backbuffer compositor received an invalid source slot";
			return false;
		}

		scoped_d3d_state state(context);
		context->OMSetRenderTargets(0, nullptr, nullptr);
		if (description.SampleDesc.Count > 1)
		{
			context->ResolveSubresource(source_textures_[source_index].Get(), 0,
				backbuffer, 0, description.Format);
		}
		else
		{
			context->CopyResource(source_textures_[source_index].Get(), backbuffer);
		}
		return true;
	}

	bool scene_compositor::prepare_capture(const d3d11::device_snapshot& graphics,
		ID3D11Texture2D* const source, const D3D11_TEXTURE2D_DESC& description,
		const std::uint64_t frame_id, const std::uint64_t game_device_generation,
		std::string& error)
	{
		if (!graphics || source == nullptr || frame_id == 0 || game_device_generation == 0)
		{
			error = "Capture compositor received invalid source metadata";
			return false;
		}
		if (!ensure_pipeline(graphics.device.Get(), graphics.generation, error) ||
			!ensure_source(graphics.device.Get(), description, graphics.generation, error))
		{
			return false;
		}
		if (description.SampleDesc.Count > 1)
		{
			error = "VR capture source must be single-sampled";
			return false;
		}
		if (!copy_source_synchronized(graphics.context.Get(), source,
			source_textures_[0].Get(), error))
		{
			return false;
		}
		// A captured game backbuffer is intentionally monoscopic at this stage.
		// Keep its real frame id for diagnostics, but do not subject it to the
		// temporal left/right-pair validation used by engine stereo sources.
		source_metadata_[0] = {true, false, frame_id, 0, graphics.generation, 0, 0};
		return true;
	}

	bool scene_compositor::prepare_cpu_capture(const d3d11::device_snapshot& graphics,
		const std::vector<std::byte>& pixels, const std::uint32_t row_pitch,
		const D3D11_TEXTURE2D_DESC& description, const std::uint64_t frame_id,
		const std::uint64_t game_device_generation, std::string& error)
	{
		if (!graphics || pixels.empty() || row_pitch == 0 || frame_id == 0 ||
			game_device_generation == 0)
		{
			error = "CPU capture compositor received invalid source metadata";
			return false;
		}
		if (description.SampleDesc.Count != 1 || description.Width == 0 ||
			description.Height == 0 || description.Format == DXGI_FORMAT_UNKNOWN)
		{
			error = "CPU capture source has an unsupported description";
			return false;
		}
		const auto required_size = static_cast<std::size_t>(row_pitch) * description.Height;
		if (pixels.size() < required_size)
		{
			error = "CPU capture source is smaller than its declared row pitch and height";
			return false;
		}
		if (!ensure_pipeline(graphics.device.Get(), graphics.generation, error) ||
			!ensure_source(graphics.device.Get(), description, graphics.generation, error))
		{
			return false;
		}

		D3D11_BOX destination{};
		destination.right = description.Width;
		destination.bottom = description.Height;
		destination.back = 1;
		graphics.context->UpdateSubresource(source_textures_[0].Get(), 0, &destination,
			pixels.data(), row_pitch, 0);
		source_metadata_[0] = {true, false, frame_id, 0, graphics.generation, 0, 0};
		return true;
	}

	bool scene_compositor::activate_stereo_pair(
		const d3d11::device_snapshot& graphics) noexcept
	{
		const auto& left = source_metadata_[0];
		const auto& right = source_metadata_[1];
		if (!graphics || !left.available || !right.available || !left.stereo || !right.stereo ||
			left.device_generation != graphics.generation ||
			right.device_generation != graphics.generation || left.eye_index != 0 ||
			right.eye_index != 1 || left.pair_id == 0 || right.pair_id == 0 ||
			left.pair_id != right.pair_id || left.frame_id == 0 ||
			left.frame_id != right.frame_id)
		{
			return false;
		}

		graphics.context->CopyResource(source_textures_[2].Get(), source_textures_[0].Get());
		graphics.context->CopyResource(source_textures_[3].Get(), source_textures_[1].Get());
		source_metadata_[2] = left;
		source_metadata_[3] = right;
		return true;
	}

	bool scene_compositor::prepare_stereo_pair(const d3d11::device_snapshot& graphics,
		const std::array<stereo_capture_source, 2>& sources, std::string& error)
	{
		const auto& left = sources[0];
		const auto& right = sources[1];
		if (!graphics || left.texture == nullptr || right.texture == nullptr ||
			left.frame_id == 0 || right.frame_id != left.frame_id ||
			left.game_device_generation == 0 ||
			right.game_device_generation != left.game_device_generation ||
			left.pair_id == 0 || right.pair_id != left.pair_id ||
			left.eye_index != 0 || right.eye_index != 1)
		{
			error = "Stereo compositor received a non-atomic eye pair";
			return false;
		}
		const auto same_description = [](const D3D11_TEXTURE2D_DESC& a,
			const D3D11_TEXTURE2D_DESC& b) noexcept
		{
			return a.Width == b.Width && a.Height == b.Height && a.Format == b.Format &&
				a.ArraySize == b.ArraySize && a.MipLevels == b.MipLevels &&
				a.SampleDesc.Count == b.SampleDesc.Count &&
				a.SampleDesc.Quality == b.SampleDesc.Quality;
		};
		if (!same_description(left.description, right.description) ||
			left.description.SampleDesc.Count != 1)
		{
			error = "Stereo compositor eye descriptions are incompatible";
			return false;
		}
		diagnostics::record_trace(diagnostics::trace_event::scene_source_ensure_begin,
			left.description.Width, left.description.Height);
		const auto pipeline_ready = ensure_pipeline(
			graphics.device.Get(), graphics.generation, error);
		const auto source_ready = pipeline_ready && ensure_source(graphics.device.Get(),
			left.description, graphics.generation, error);
		diagnostics::record_trace(diagnostics::trace_event::scene_source_ensure_result,
			pipeline_ready ? 1 : 0, source_ready ? 1 : 0);
		if (!source_ready)
		{
			return false;
		}

		// Slots 2 and 3 are the active immutable pair consumed by render_eye().
		// Copy directly into them. The former per-eye path copied into slots 0/1
		// and then copied both textures a second time during activation, doubling
		// full-resolution GPU traffic at the exact point where TDRs were observed.
		for (std::size_t eye{}; eye < sources.size(); ++eye)
		{
			if (!copy_source_synchronized(graphics.context.Get(), sources[eye].texture,
				source_textures_[eye + 2].Get(), error))
			{
				source_metadata_[2] = {};
				source_metadata_[3] = {};
				return false;
			}
		}
		source_metadata_[2] = {true, true, left.frame_id, 0, graphics.generation,
			left.pair_id, 0};
		source_metadata_[3] = {true, true, right.frame_id, 0, graphics.generation,
			right.pair_id, 1};
		error.clear();
		return true;
	}

	bool scene_compositor::prepare_cpu_stereo_capture(const d3d11::device_snapshot& graphics,
		const std::vector<std::byte>& pixels, const std::uint32_t row_pitch,
		const D3D11_TEXTURE2D_DESC& description, const std::uint64_t frame_id,
		const std::uint64_t game_device_generation, const std::uint64_t pair_id,
		const std::uint32_t eye_index, std::string& error)
	{
		if (!graphics || pixels.empty() || row_pitch == 0 || frame_id == 0 ||
			game_device_generation == 0 || pair_id == 0 || eye_index >= 2)
		{
			error = "CPU stereo capture compositor received invalid eye metadata";
			return false;
		}
		const auto required_size = static_cast<std::size_t>(row_pitch) * description.Height;
		if (description.SampleDesc.Count != 1 || description.Width == 0 ||
			description.Height == 0 || description.Format == DXGI_FORMAT_UNKNOWN ||
			pixels.size() < required_size)
		{
			error = "CPU stereo capture source has an unsupported description";
			return false;
		}
		if (!ensure_pipeline(graphics.device.Get(), graphics.generation, error) ||
			!ensure_source(graphics.device.Get(), description, graphics.generation, error))
		{
			return false;
		}
		D3D11_BOX destination{};
		destination.right = description.Width;
		destination.bottom = description.Height;
		destination.back = 1;
		graphics.context->UpdateSubresource(source_textures_[eye_index].Get(), 0, &destination,
			pixels.data(), row_pitch, 0);
		source_metadata_[eye_index] = {
			true, true, frame_id, 0, graphics.generation, pair_id, eye_index};
		const auto active = activate_stereo_pair(graphics);
		if (!active)
		{
			error = "waiting for the opposite stereo eye capture";
		}
		return active;
	}

	bool scene_compositor::prepare(const d3d11::device_snapshot& graphics,
		IDXGISwapChain* const swap_chain, std::string& error)
	{
		const source_metadata metadata{
			true,
			false,
			0,
			0,
			graphics.generation,
			0,
			0,
		};
		return prepare_source(graphics, swap_chain, 0, metadata, error);
	}

	bool scene_compositor::prepare_source(const d3d11::device_snapshot& graphics,
		IDXGISwapChain* const swap_chain, const std::uint32_t source_index,
		const source_metadata& metadata, std::string& error)
	{
		if (!graphics || swap_chain == nullptr)
		{
			error = "Backbuffer compositor received no graphics device or swap chain";
			return false;
		}
		if (source_index >= source_textures_.size() || metadata.eye_index >= source_textures_.size())
		{
			error = "Backbuffer compositor source eye index is out of range";
			return false;
		}
		if (!ensure_pipeline(graphics.device.Get(), graphics.generation, error))
		{
			return false;
		}

		Microsoft::WRL::ComPtr<IDXGISwapChain3> swap_chain3;
		UINT buffer_index = 0;
		if (SUCCEEDED(swap_chain->QueryInterface(IID_PPV_ARGS(&swap_chain3))))
		{
			buffer_index = swap_chain3->GetCurrentBackBufferIndex();
		}
		Microsoft::WRL::ComPtr<ID3D11Texture2D> backbuffer;
		const auto result = swap_chain->GetBuffer(buffer_index, IID_PPV_ARGS(&backbuffer));
		if (FAILED(result) || !backbuffer)
		{
			error = std::format("GetBuffer({}) failed (HRESULT=0x{:08x})", buffer_index,
				static_cast<std::uint32_t>(result));
			return false;
		}
		D3D11_TEXTURE2D_DESC description{};
		backbuffer->GetDesc(&description);
		if (!ensure_source(graphics.device.Get(), description, graphics.generation, error) ||
			!copy_source(graphics.context.Get(), backbuffer.Get(), description, source_index, error))
		{
			return false;
		}

		source_metadata_[source_index] = metadata;
		return true;
	}

	bool scene_compositor::monoscopic_source_available(
		const std::uint64_t device_generation) const noexcept
	{
		const auto& source = source_metadata_[0];
		return source.available && !source.stereo && source.device_generation == device_generation;
	}

	bool scene_compositor::stereo_source_available(
		const std::uint64_t device_generation) const noexcept
	{
		const auto& left = source_metadata_[2];
		const auto& right = source_metadata_[3];
		return left.available && right.available && left.stereo && right.stereo &&
			left.device_generation == device_generation &&
			right.device_generation == device_generation && left.eye_index == 0 &&
			right.eye_index == 1 && left.pair_id != 0 && left.pair_id == right.pair_id;
	}

	bool scene_compositor::render_eye(const d3d11::device_snapshot& graphics,
		const scene_compositor_target& target, std::string& error,
		const std::uint32_t source_index)
	{
		if (!graphics || target.render_target == nullptr || target.width == 0 || target.height == 0 ||
			source_index >= source_views_.size() || !source_views_[source_index] ||
			!vertex_shader_ || !pixel_shader_ || !blit_parameters_ || !sampler_ || !rasterizer_state_ ||
			!depth_state_ || !blend_state_)
		{
			error = "Backbuffer compositor is not prepared for this eye";
			return false;
		}

		if (!source_metadata_[source_index].available ||
			source_metadata_[source_index].device_generation != graphics.generation)
		{
			error = "Backbuffer compositor source belongs to a stale D3D11 device generation";
			return false;
		}
		scene_uv_mapping mapping;
		if (target.remap_projection &&
			!calculate_projection_uv_mapping(target.projection, mapping))
		{
			error = "Backbuffer compositor cannot calculate the optical projection remap";
			return false;
		}
		D3D11_RENDER_TARGET_VIEW_DESC target_description{};
		target.render_target->GetDesc(&target_description);
		const bool source_is_srgb = is_srgb_format(source_description_.Format);
		const bool target_is_srgb = is_srgb_format(target_description.Format);
		const float color_transform = source_is_srgb == target_is_srgb
			? 0.0f : target_is_srgb ? 1.0f : 2.0f;
		const blit_parameters parameters{
			mapping.scale_u,
			mapping.scale_v,
			mapping.offset_u,
			mapping.offset_v,
			0.5f / static_cast<float>(source_description_.Width),
			0.5f / static_cast<float>(source_description_.Height),
			(static_cast<float>(source_description_.Width) - 0.5f) /
				static_cast<float>(source_description_.Width),
			(static_cast<float>(source_description_.Height) - 0.5f) /
				static_cast<float>(source_description_.Height),
			color_transform};

		scoped_d3d_state state(graphics.context.Get());
		graphics.context->UpdateSubresource(blit_parameters_.Get(), 0, nullptr, &parameters, 0, 0);
		const float blend_factor[4]{0, 0, 0, 0};
		graphics.context->OMSetBlendState(blend_state_.Get(), blend_factor,
			D3D11_DEFAULT_SAMPLE_MASK);
		graphics.context->OMSetDepthStencilState(depth_state_.Get(), 0);
		graphics.context->OMSetRenderTargets(1, &target.render_target, nullptr);
		graphics.context->RSSetState(rasterizer_state_.Get());
		const D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(target.width),
			static_cast<float>(target.height), 0.0f, 1.0f};
		graphics.context->RSSetViewports(1, &viewport);
		graphics.context->IASetInputLayout(nullptr);
		graphics.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		graphics.context->VSSetShader(vertex_shader_.Get(), nullptr, 0);
		graphics.context->PSSetShader(pixel_shader_.Get(), nullptr, 0);
		graphics.context->PSSetConstantBuffers(0, 1, blit_parameters_.GetAddressOf());
		graphics.context->PSSetShaderResources(0, 1,
			source_views_[source_index].GetAddressOf());
		graphics.context->PSSetSamplers(0, 1, sampler_.GetAddressOf());
		graphics.context->Draw(3, 0);
		return true;
	}

	void scene_compositor::revoke_sources() noexcept
	{
		for (auto& metadata : source_metadata_)
		{
			metadata = {};
		}
	}

	void scene_compositor::invalidate() noexcept
	{
		for (auto& source : source_textures_)
		{
			source.Reset();
		}
		for (auto& view : source_views_)
		{
			view.Reset();
		}
		revoke_sources();
		source_description_ = {};
		generation_ = 0;
	}
}
