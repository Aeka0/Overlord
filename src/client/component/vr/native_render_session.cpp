#include <std_include.hpp>

#include "native_render_session.hpp"
#include "diagnostics.hpp"
#include "engine_stereo_gpu_timing.hpp"
#include "native_conversion_command_list.hpp"
#include "native_display_contract.hpp"
#include "stabilization.hpp"

#include <d3dcompiler.h>
#include <algorithm>
#include <chrono>
#include <format>
#include <sstream>
#include <windows.h>

#pragma comment(lib, "d3dcompiler.lib")

namespace vr::native_render_session
{
	namespace
	{
		constexpr std::uint32_t min_dimension = 256;
		constexpr std::uint32_t max_dimension = 16384;

		[[nodiscard]] std::uint64_t elapsed_us(
			const std::chrono::steady_clock::time_point started) noexcept
		{
			const auto count = std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - started).count();
			return count > 0 ? static_cast<std::uint64_t>(count) : 0;
		}

		void record_timing(std::array<std::uint64_t, 2>& samples,
			std::array<std::uint64_t, 2>& last_us,
			std::array<std::uint64_t, 2>& maximum_us,
			std::array<std::uint64_t, 2>& total_us, const std::uint32_t eye,
			const std::uint64_t value) noexcept
		{
			if (eye >= 2) return;
			++samples[eye];
			last_us[eye] = value;
			maximum_us[eye] = (std::max)(maximum_us[eye], value);
			total_us[eye] += value;
		}
		constexpr char conversion_vertex_shader_source[] = R"(
float4 main(uint vertex_id : SV_VertexID) : SV_Position
{
	const float2 positions[3] = {
		float2(-1.0, -1.0),
		float2(-1.0,  3.0),
		float2( 3.0, -1.0)
	};
	return float4(positions[vertex_id], 0.0, 1.0);
}
)";

		constexpr char conversion_pixel_shader_source[] = R"(
Texture2D<float3> source_texture : register(t0);

float4 main(float4 position : SV_Position) : SV_Target
{
	// H2 final PostFX targets a plain UNORM desktop RTV: its result is already
	// display encoded. Decode once for our ColorSpace_Linear submission contract.
	float3 encoded = source_texture.Load(int3(uint2(position.xy), 0));
	float3 high = pow(max((encoded + 0.055) / 1.055, 0.0), 2.4);
	float3 linear_rgb = float3(encoded.r <= 0.04045 ? encoded.r / 12.92 : high.r,
	                          encoded.g <= 0.04045 ? encoded.g / 12.92 : high.g,
	                          encoded.b <= 0.04045 ? encoded.b / 12.92 : high.b);
	return float4(linear_rgb, 1.0);
}
)";

		std::string hresult_error(const char* operation, const HRESULT result)
		{
			return std::format("{} failed (HRESULT=0x{:08X})", operation,
				static_cast<std::uint32_t>(result));
		}

		std::string run_capability_probe(ID3D11Device* const device,
			const std::uint32_t width, const std::uint32_t height)
		{
			std::ostringstream output;
			output << "size=" << width << 'x' << height;
			const auto removed_reason = device->GetDeviceRemovedReason();
			output << " removed_reason=" << std::format("0x{:08X}",
				static_cast<std::uint32_t>(removed_reason));

			UINT format_support{};
			const auto format_result = device->CheckFormatSupport(DXGI_FORMAT_R8G8B8A8_UNORM,
				&format_support);
			output << " rgba_support=" << std::format("0x{:08X}", format_support);
			output << " check_format=" << std::format("0x{:08X}", static_cast<std::uint32_t>(format_result));
			return output.str();
		}

		bool valid_copy_source(const D3D11_TEXTURE2D_DESC& value) noexcept
		{
			return value.Width >= min_dimension && value.Width <= max_dimension &&
				value.Height >= min_dimension && value.Height <= max_dimension &&
				value.MipLevels == 1 && value.ArraySize == 1 &&
				value.Format == DXGI_FORMAT_R11G11B10_FLOAT &&
				value.SampleDesc.Count == 1 && value.SampleDesc.Quality == 0 &&
				value.Usage == D3D11_USAGE_DEFAULT && value.BindFlags ==
					(D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET |
						D3D11_BIND_UNORDERED_ACCESS) &&
				value.CPUAccessFlags == 0 && value.MiscFlags == 0;
		}

		bool valid_destination_format(const DXGI_FORMAT format) noexcept
		{
			switch (format)
			{
			case DXGI_FORMAT_R11G11B10_FLOAT:
			case DXGI_FORMAT_R16G16B16A16_FLOAT:
			case DXGI_FORMAT_R10G10B10A2_UNORM:
			case DXGI_FORMAT_R8G8B8A8_UNORM:
				return true;
			default:
				return false;
			}
		}

		HRESULT create_conversion_pipeline(ID3D11Device* const device,
			Microsoft::WRL::ComPtr<ID3D11VertexShader>& vertex_shader,
			Microsoft::WRL::ComPtr<ID3D11PixelShader>& pixel_shader,
			Microsoft::WRL::ComPtr<ID3D11RasterizerState>& rasterizer_state,
			Microsoft::WRL::ComPtr<ID3D11DepthStencilState>& depth_state,
			Microsoft::WRL::ComPtr<ID3D11BlendState>& blend_state) noexcept
		{
			if (device == nullptr) return E_POINTER;
			Microsoft::WRL::ComPtr<ID3DBlob> vertex_bytecode;
			Microsoft::WRL::ComPtr<ID3DBlob> compile_errors;
			auto result = D3DCompile(conversion_vertex_shader_source,
				sizeof(conversion_vertex_shader_source) - 1, "h2v-native-format-vs", nullptr,
				nullptr, "main", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
				&vertex_bytecode, &compile_errors);
			if (FAILED(result)) return result;
			result = device->CreateVertexShader(vertex_bytecode->GetBufferPointer(),
				vertex_bytecode->GetBufferSize(), nullptr, &vertex_shader);
			if (FAILED(result)) return result;

			Microsoft::WRL::ComPtr<ID3DBlob> pixel_bytecode;
			compile_errors.Reset();
			result = D3DCompile(conversion_pixel_shader_source,
				sizeof(conversion_pixel_shader_source) - 1, "h2v-native-format-ps", nullptr,
				nullptr, "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
				&pixel_bytecode, &compile_errors);
			if (FAILED(result)) return result;
			result = device->CreatePixelShader(pixel_bytecode->GetBufferPointer(),
				pixel_bytecode->GetBufferSize(), nullptr, &pixel_shader);
			if (FAILED(result)) return result;

			D3D11_RASTERIZER_DESC rasterizer{};
			rasterizer.FillMode = D3D11_FILL_SOLID;
			rasterizer.CullMode = D3D11_CULL_NONE;
			rasterizer.DepthClipEnable = TRUE;
			result = device->CreateRasterizerState(&rasterizer, &rasterizer_state);
			if (FAILED(result)) return result;

			D3D11_DEPTH_STENCIL_DESC depth{};
			depth.DepthEnable = FALSE;
			depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
			depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
			result = device->CreateDepthStencilState(&depth, &depth_state);
			if (FAILED(result)) return result;

			D3D11_BLEND_DESC blend{};
			blend.RenderTarget[0].BlendEnable = FALSE;
			blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			return device->CreateBlendState(&blend, &blend_state);
		}

		bool texture_uses_device(ID3D11Texture2D* const texture,
			ID3D11Device* const expected) noexcept
		{
			if (texture == nullptr || expected == nullptr) return false;
			Microsoft::WRL::ComPtr<ID3D11Device> owner;
			texture->GetDevice(&owner);
			Microsoft::WRL::ComPtr<IUnknown> owner_identity;
			Microsoft::WRL::ComPtr<IUnknown> expected_identity;
			return owner && SUCCEEDED(owner.As(&owner_identity)) &&
				SUCCEEDED(expected->QueryInterface(IID_PPV_ARGS(&expected_identity))) &&
				owner_identity.Get() == expected_identity.Get();
		}
	}

	bool session::ensure(const d3d11::device_snapshot& graphics,
		const std::uint32_t requested_width, const std::uint32_t requested_height,
		std::string& error) noexcept
	{
		diagnostics::record_trace(diagnostics::trace_event::native_session_ensure,
			(static_cast<std::uint64_t>(requested_width) << 32) | requested_height,
			graphics.generation);
		const std::lock_guard lock(mutex_);
		error.clear();
		if (!graphics || graphics.device == nullptr || graphics.context == nullptr)
		{
			error = "native render session has no game D3D11 device";
			diagnostics::record_trace(diagnostics::trace_event::native_session_ensure_result, 0, 0);
			return false;
		}
		if (requested_width < min_dimension || requested_width > max_dimension ||
			requested_height < min_dimension || requested_height > max_dimension)
		{
			error = "native render session received an unsupported eye size";
			diagnostics::record_trace(diagnostics::trace_event::native_session_ensure_result, 0, 0);
			return false;
		}
		const auto width = requested_width;
		const auto height = requested_height;
		auto record_failure = [](const std::uint64_t stage, const HRESULT result) noexcept
		{
			diagnostics::record_trace(diagnostics::trace_event::native_session_ensure_result,
				stage, static_cast<std::uint32_t>(result));
		};
		if (available(graphics) && !copy_ring_ && width_ == width && height_ == height &&
			format_ == DXGI_FORMAT_R8G8B8A8_UNORM)
		{
			diagnostics::record_trace(diagnostics::trace_event::native_session_ensure_result, 1,
				device_generation_);
			return true;
		}

		invalidate_locked();
		D3D11_TEXTURE2D_DESC color_description{};
		color_description.Width = width;
		color_description.Height = height;
		color_description.MipLevels = 1;
		color_description.ArraySize = 1;
		color_description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		color_description.SampleDesc.Count = 1;
		color_description.Usage = D3D11_USAGE_DEFAULT;
		color_description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		// These are ordinary H2-device render targets. OpenVR receives the exact
		// ID3D11Texture2D objects; no cross-device sharing contract exists here.
		color_description.MiscFlags = 0;

		D3D11_TEXTURE2D_DESC depth_description{};
		depth_description.Width = width;
		depth_description.Height = height;
		depth_description.MipLevels = 1;
		depth_description.ArraySize = 1;
		depth_description.Format = DXGI_FORMAT_R24G8_TYPELESS;
		depth_description.SampleDesc.Count = 1;
		depth_description.Usage = D3D11_USAGE_DEFAULT;
		depth_description.BindFlags = D3D11_BIND_DEPTH_STENCIL;

		D3D11_DEPTH_STENCIL_VIEW_DESC depth_view_description{};
		depth_view_description.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		depth_view_description.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;

		for (auto& pair : pairs_)
		{
			for (auto& eye : pair.eyes)
			{
				const auto color_result = graphics.device->CreateTexture2D(&color_description,
					nullptr, &eye.color);
				if (FAILED(color_result))
				{
					error = hresult_error("CreateTexture2D(native color)", color_result);
					invalidate_locked();
					status_.capability_probe = run_capability_probe(graphics.device.Get(), width, height);
					record_failure(1, color_result);
					return false;
				}
				const auto color_view_result = graphics.device->CreateRenderTargetView(
					eye.color.Get(), nullptr, &eye.color_view);
				if (FAILED(color_view_result))
				{
					error = hresult_error("CreateRenderTargetView(native color)", color_view_result);
					invalidate_locked();
					record_failure(2, color_view_result);
					return false;
				}
				const auto depth_result = graphics.device->CreateTexture2D(&depth_description,
					nullptr, &eye.depth);
				if (FAILED(depth_result))
				{
					error = hresult_error("CreateTexture2D(native depth)", depth_result);
					invalidate_locked();
					record_failure(3, depth_result);
					return false;
				}
				const auto depth_view_result = graphics.device->CreateDepthStencilView(
					eye.depth.Get(), &depth_view_description, &eye.depth_view);
				if (FAILED(depth_view_result))
				{
					error = hresult_error("CreateDepthStencilView(native depth)", depth_view_result);
					invalidate_locked();
					record_failure(4, depth_view_result);
					return false;
				}
				eye.width = width;
				eye.height = height;
			}
		}
		device_generation_ = graphics.generation;
		width_ = width;
		height_ = height;
		format_ = color_description.Format;
		source_format_ = color_description.Format;
		copy_ring_ = false;
		accepting_pairs_ = true;
		expected_pair_id_ = 0;
		device_ = graphics.device;
		context_ = graphics.context;
		status_.available = true;
		status_.device_generation = graphics.generation;
		status_.width = width;
		status_.height = height;
		status_.format = static_cast<std::uint32_t>(format_);
		status_.source_format = static_cast<std::uint32_t>(source_format_);
		status_.copy_ring = false;
		status_.accepting_pairs = true;
		status_.expected_pair_id = 0;
		status_.deferred_conversion = false;
		++status_.rebuilds;
		diagnostics::record_trace(diagnostics::trace_event::native_session_ensure_result, 1,
			device_generation_);
		return true;
	}

	bool session::ensure_copy_ring(const d3d11::device_snapshot& graphics,
		const D3D11_TEXTURE2D_DESC& source, const DXGI_FORMAT destination_format,
		std::string& error) noexcept
	{
		diagnostics::record_trace(diagnostics::trace_event::native_session_ensure,
			(static_cast<std::uint64_t>(source.Width) << 32) | source.Height,
			graphics.generation);
		const std::lock_guard lock(mutex_);
		error.clear();
		if (!graphics || !graphics.device || !graphics.context)
		{
			error = "native copy ring has no game D3D11 device";
			return false;
		}
		if (!valid_copy_source(source))
		{
			error = "native copy ring rejected the exact H2 scene descriptor";
			return false;
		}
		if (!valid_destination_format(destination_format))
		{
			error = "native copy ring rejected an unprobed destination format";
			return false;
		}
		if (accepting_pairs_)
		{
			error = "native copy ring rebuild requires suspended pair acquisition";
			return false;
		}
		if (!std::ranges::all_of(pairs_, [](const target_pair& pair)
		{
			return pair.state == pair_state::free && pair.pair_id == 0 &&
				pair.completed_eye_mask == 0 && pair.active_eye_mask == 0;
		}))
		{
			error = "native copy ring rebuild rejected a live ownership slot";
			return false;
		}
		if (available(graphics) && copy_ring_ && width_ == source.Width &&
			height_ == source.Height && source_format_ == source.Format &&
			format_ == destination_format)
		{
			return true;
		}

		UINT destination_support{};
		const auto support_result = graphics.device->CheckFormatSupport(
			destination_format, &destination_support);
		constexpr UINT required_support = D3D11_FORMAT_SUPPORT_TEXTURE2D |
			D3D11_FORMAT_SUPPORT_SHADER_SAMPLE | D3D11_FORMAT_SUPPORT_RENDER_TARGET;
		if (FAILED(support_result) ||
			(destination_support & required_support) != required_support)
		{
			status_.destination_format_support = destination_support;
			error = FAILED(support_result)
				? hresult_error("CheckFormatSupport(native destination)", support_result)
				: "native destination lacks texture, shader-resource, or render-target support";
			return false;
		}

		Microsoft::WRL::ComPtr<ID3D11DeviceContext> conversion_deferred_context;
		// Encoding conversion is mandatory even when storage formats match.
		const auto deferred_context_result = graphics.device->CreateDeferredContext(0,
			&conversion_deferred_context);
		if (FAILED(deferred_context_result) || !conversion_deferred_context)
		{
			status_.deferred_context_create_result =
				static_cast<std::uint32_t>(deferred_context_result);
			error = hresult_error("CreateDeferredContext(native conversion)",
				deferred_context_result);
			return false;
		}

		Microsoft::WRL::ComPtr<ID3D11VertexShader> conversion_vertex_shader;
		Microsoft::WRL::ComPtr<ID3D11PixelShader> conversion_pixel_shader;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> conversion_rasterizer_state;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> conversion_depth_state;
		Microsoft::WRL::ComPtr<ID3D11BlendState> conversion_blend_state;
		const auto pipeline_result = create_conversion_pipeline(graphics.device.Get(),
			conversion_vertex_shader, conversion_pixel_shader,
			conversion_rasterizer_state, conversion_depth_state,
			conversion_blend_state);
		status_.conversion_pipeline_result = static_cast<std::uint32_t>(pipeline_result);
		if (FAILED(pipeline_result))
		{
			error = hresult_error("create native format-conversion pipeline", pipeline_result);
			return false;
		}

		auto destination = source;
		destination.Format = destination_format;
		destination.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		destination.CPUAccessFlags = 0;
		destination.MiscFlags = 0;
		std::array<target_pair, pair_count> replacement_pairs{};
		for (auto& pair : replacement_pairs)
		{
			for (auto& eye : pair.eyes)
			{
				const auto texture_result = graphics.device->CreateTexture2D(
					&destination, nullptr, &eye.color);
				if (FAILED(texture_result))
				{
					error = hresult_error("CreateTexture2D(native format ring)", texture_result);
					return false;
				}
				const auto view_result = graphics.device->CreateRenderTargetView(
					eye.color.Get(), nullptr, &eye.color_view);
				if (FAILED(view_result))
				{
					error = hresult_error("CreateRenderTargetView(native format ring)", view_result);
					return false;
				}
				eye.width = destination.Width;
				eye.height = destination.Height;
			}
		}

		// Acquisition is suspended and every old slot is free, so this is the only
		// point at which a format-probe ring may replace the previous resources.
		invalidate_locked();
		pairs_ = std::move(replacement_pairs);
		device_generation_ = graphics.generation;
		width_ = destination.Width;
		height_ = destination.Height;
		format_ = destination.Format;
		source_format_ = source.Format;
		copy_ring_ = true;
		accepting_pairs_ = false;
		expected_pair_id_ = 0;
		device_ = graphics.device;
		context_ = graphics.context;
		conversion_deferred_context_ = std::move(conversion_deferred_context);
		conversion_vertex_shader_ = std::move(conversion_vertex_shader);
		conversion_pixel_shader_ = std::move(conversion_pixel_shader);
		conversion_rasterizer_state_ = std::move(conversion_rasterizer_state);
		conversion_depth_state_ = std::move(conversion_depth_state);
		conversion_blend_state_ = std::move(conversion_blend_state);
		status_.available = true;
		status_.device_generation = graphics.generation;
		status_.width = width_;
		status_.height = height_;
		status_.format = static_cast<std::uint32_t>(format_);
		status_.source_format = static_cast<std::uint32_t>(source_format_);
		status_.copy_ring = true;
		status_.accepting_pairs = false;
		status_.expected_pair_id = 0;
		status_.deferred_conversion = true;
		status_.destination_format_support = destination_support;
		status_.deferred_context_create_result =
			static_cast<std::uint32_t>(deferred_context_result);
		status_.last_command_list_result = static_cast<std::uint32_t>(S_OK);
		status_.conversion_pipeline_result = static_cast<std::uint32_t>(pipeline_result);
		++status_.rebuilds;
		// Query allocation belongs to device/ring setup, never to a sampled owner
		// pass. The diagnostic is deliberately non-authoritative: an unsupported
		// timestamp query cannot disable otherwise valid native stereo rendering.
		if constexpr (engine_stereo_gpu_timing::instrumentation_enabled)
		{
			(void)engine_stereo_gpu_timing::prepare_device(graphics.device.Get(),
				graphics.context.Get(), graphics.generation);
		}
		diagnostics::record_trace(diagnostics::trace_event::native_session_ensure_result,
			1, device_generation_);
		return true;
	}

	void session::invalidate(const std::uint64_t device_generation) noexcept
	{
		const std::lock_guard lock(mutex_);
		if (device_generation != 0 && device_generation_ != 0 &&
			device_generation_ != device_generation)
		{
			return;
		}
		engine_stereo_gpu_timing::invalidate_device(context_.Get(), device_generation_);
		invalidate_locked();
	}

	void session::invalidate_locked() noexcept
	{
		stabilization::invalidate();
		for (auto& pair : pairs_)
		{
			pair = {};
		}
		next_pair_index_ = 0;
		device_generation_ = 0;
		width_ = 0;
		height_ = 0;
		format_ = DXGI_FORMAT_UNKNOWN;
		source_format_ = DXGI_FORMAT_UNKNOWN;
		copy_ring_ = false;
		accepting_pairs_ = false;
		expected_pair_id_ = 0;
		device_.Reset();
		context_.Reset();
		conversion_deferred_context_.Reset();
		conversion_vertex_shader_.Reset();
		conversion_pixel_shader_.Reset();
		conversion_rasterizer_state_.Reset();
		conversion_depth_state_.Reset();
		conversion_blend_state_.Reset();
		conversion_source_views_ = {};
		conversion_sources_ = {};
		failed_pair_id_ = 0;
		deferred_pair_id_ = 0;
		status_.available = false;
		status_.device_generation = 0;
		status_.width = 0;
		status_.height = 0;
		status_.format = 0;
		status_.source_format = 0;
		status_.copy_ring = false;
		status_.accepting_pairs = false;
		status_.expected_pair_id = 0;
		status_.deferred_conversion = false;
		status_.source_view_cached = false;
		++status_.invalidations;
	}

	bool session::available(const d3d11::device_snapshot& graphics) const noexcept
	{
		const std::lock_guard lock(mutex_);
		return status_.available && graphics && device_generation_ != 0 &&
			device_generation_ == graphics.generation;
	}

	bool session::suspend_acquisition() noexcept
	{
		const std::lock_guard lock(mutex_);
		if (!status_.available) return false;
		expected_pair_id_ = 0;
		status_.expected_pair_id = 0;
		if (accepting_pairs_)
		{
			accepting_pairs_ = false;
			status_.accepting_pairs = false;
			++status_.acquisition_suspends;
		}
		return true;
	}

	bool session::admit_pair(const std::uint64_t pair_id) noexcept
	{
		const std::lock_guard lock(mutex_);
		if (!status_.available || !copy_ring_ || pair_id == 0) return false;
		if (accepting_pairs_)
		{
			return expected_pair_id_ == pair_id;
		}
		if (!std::ranges::all_of(pairs_, [](const target_pair& pair)
			{
				return pair.state == pair_state::free && pair.pair_id == 0 &&
					pair.completed_eye_mask == 0 && pair.active_eye_mask == 0;
			}))
		{
			return false;
		}
		accepting_pairs_ = true;
		expected_pair_id_ = pair_id;
		status_.accepting_pairs = true;
		status_.expected_pair_id = pair_id;
		++status_.acquisition_resumes;
		return true;
	}

	bool session::accepts_pair(const std::uint64_t pair_id) const noexcept
	{
		const std::lock_guard lock(mutex_);
		return status_.available && copy_ring_ && accepting_pairs_ && pair_id != 0 &&
			expected_pair_id_ == pair_id;
	}

	bool session::acquire_target(const std::uint64_t pair_id, const std::uint32_t eye,
		eye_target& output) noexcept
	{
		const std::lock_guard lock(mutex_);
		output = {};
		if (pair_id == 0 || eye >= 2 || !status_.available)
		{
			if (pair_id != 0) failed_pair_id_ = pair_id;
			return false;
		}
		for (auto& pair : pairs_)
		{
			if (pair.pair_id != pair_id) continue;
			const auto eye_bit = 1u << eye;
			if (pair.state != pair_state::rendering ||
				(pair.completed_eye_mask & eye_bit) != 0 || pair.active_eye_mask != 0)
			{
				++status_.pair_rejections;
				failed_pair_id_ = pair_id;
				return false;
			}
			pair.active_eye_mask = eye_bit;
			output = pair.eyes[eye];
			if (!output.color || !output.color_view || !output.depth_view)
			{
				pair.active_eye_mask = 0;
				++status_.pair_rejections;
				failed_pair_id_ = pair_id;
				return false;
			}
			return true;
		}
		if (!accepting_pairs_ || (copy_ring_ && pair_id != expected_pair_id_))
		{
			++status_.acquisition_rejections;
			++status_.pair_rejections;
			failed_pair_id_ = pair_id;
			return false;
		}
		for (std::size_t offset{}; offset < pairs_.size(); ++offset)
		{
			auto& pair = pairs_[(next_pair_index_ + offset) % pairs_.size()];
			if (pair.state != pair_state::free) continue;
			pair.pair_id = pair_id;
			pair.preview_pair_id = 0;
			pair.preview_projection = {};
			pair.preview_recording_crop = {};
			pair.preview_camera = {};
			pair.completed_eye_mask = 0;
			pair.active_eye_mask = 1u << eye;
			pair.state = pair_state::rendering;
			output = pair.eyes[eye];
			next_pair_index_ = (&pair - pairs_.data() + 1) % pairs_.size();
			++status_.pair_acquires;
			if (!output.color || !output.color_view || !output.depth_view)
			{
				pair.pair_id = 0;
				pair.completed_eye_mask = 0;
				pair.active_eye_mask = 0;
				pair.state = pair_state::free;
				++status_.pair_rejections;
				failed_pair_id_ = pair_id;
				return false;
			}
			return true;
		}
		++status_.pair_exhaustions;
		failed_pair_id_ = pair_id;
		return false;
	}

	bool session::complete_rendered_eye(const std::uint64_t pair_id,
		const std::uint32_t eye, ID3D11Texture2D* const texture) noexcept
	{
		const std::lock_guard lock(mutex_);
		if (pair_id == 0 || eye >= 2 || texture == nullptr)
		{
			if (pair_id != 0) failed_pair_id_ = pair_id;
			++status_.pair_rejections;
			return false;
		}
		for (auto& pair : pairs_)
		{
			const auto eye_bit = 1u << eye;
			if (pair.pair_id != pair_id) continue;
			const bool valid = pair.state == pair_state::rendering &&
				(pair.active_eye_mask & eye_bit) != 0 &&
				(pair.completed_eye_mask & eye_bit) == 0 &&
				pair.eyes[eye].color.Get() == texture;
			if (!valid)
			{
				failed_pair_id_ = pair_id;
				++status_.pair_rejections;
				return false;
			}
			pair.active_eye_mask &= ~eye_bit;
			pair.completed_eye_mask |= eye_bit;
			if (pair.completed_eye_mask == 0x3)
			{
				pair.state = pair_state::published;
				pair.preview_pair_id = pair_id;
				pair.preview_owner_thread = GetCurrentThreadId();
			}
			return true;
		}
		failed_pair_id_ = pair_id;
		++status_.pair_rejections;
		return false;
	}

	const char* to_string(const copy_failure value) noexcept
	{
		switch (value)
		{
		case copy_failure::none: return "none";
		case copy_failure::admission: return "admission";
		case copy_failure::session: return "session";
		case copy_failure::arguments: return "arguments";
		case copy_failure::context: return "context";
		case copy_failure::source_device: return "source_device";
		case copy_failure::deferred_context: return "deferred_context";
		case copy_failure::source_descriptor: return "source_descriptor";
		case copy_failure::source_extent: return "source_extent";
		case copy_failure::ring_exhausted: return "ring_exhausted";
		case copy_failure::ring_state: return "ring_state";
		case copy_failure::conversion_pipeline: return "conversion_pipeline";
		case copy_failure::source_identity: return "source_identity";
		case copy_failure::source_view: return "source_view";
		case copy_failure::command_list: return "command_list";
		case copy_failure::device_removed: return "device_removed";
		default: return "unknown";
		}
	}

	bool session::copy_eye(const std::uint64_t pair_id, const std::uint32_t eye,
		ID3D11Texture2D* const source, const std::uint32_t source_target, ID3D11DeviceContext* const context,
		const eye_composition::event* const composition) noexcept
	{
		diagnostics::record_trace(diagnostics::trace_event::native_conversion_begin,
			pair_id, (static_cast<std::uint64_t>(eye) << 56) |
				(reinterpret_cast<std::uintptr_t>(source) & 0x00FFFFFFFFFFFFFFull));
		const auto lock_started = std::chrono::steady_clock::now();
		const std::lock_guard lock(mutex_);
		record_timing(status_.copy_lock_wait_samples, status_.copy_lock_wait_last_us,
			status_.copy_lock_wait_max_us, status_.copy_lock_wait_total_us, eye,
			elapsed_us(lock_started));
		++status_.capture_requests;
		const auto source_index = native_display_contract::target_index(source_target);
		D3D11_TEXTURE2D_DESC source_description{};
		bool source_descriptor_read{};
		const auto retain_failure = [&](const copy_failure stage,
			const HRESULT result) noexcept
		{
			auto& saved = status_.last_copy_failure;
			if (saved.stage != copy_failure::none && saved.pair_id == pair_id &&
				saved.device_generation == device_generation_ &&
				saved.rebuilds == status_.rebuilds) return;
			saved = {};
			saved.stage = stage;
			saved.result = result;
			saved.pair_id = pair_id;
			saved.expected_pair_id = expected_pair_id_;
			saved.device_generation = device_generation_;
			saved.rebuilds = status_.rebuilds;
			saved.tick_ms = GetTickCount64();
			saved.eye = eye;
			saved.thread_id = GetCurrentThreadId();
			saved.source = reinterpret_cast<std::uintptr_t>(source);
			saved.cached_source = source_index < conversion_sources_.size() ?
				reinterpret_cast<std::uintptr_t>(conversion_sources_[source_index].Get()) : 0;
			saved.context = reinterpret_cast<std::uintptr_t>(context);
			saved.expected_context = reinterpret_cast<std::uintptr_t>(context_.Get());
			saved.expected_device = reinterpret_cast<std::uintptr_t>(device_.Get());
			saved.available = status_.available;
			saved.accepting_pairs = accepting_pairs_;
			saved.copy_ring = copy_ring_;
			saved.source_descriptor_read = source_descriptor_read;
			saved.source_descriptor = source_description;
			saved.expected_width = width_;
			saved.expected_height = height_;
			saved.expected_source_format = source_format_;
		};
		const auto fail = [&](const copy_failure stage, const HRESULT result = E_FAIL,
			const engine_stereo_gpu_timing::failure timing_failure =
				engine_stereo_gpu_timing::failure::native_conversion) noexcept
		{
			retain_failure(stage, result);
			engine_stereo_gpu_timing::fail_pair(pair_id, context,
				device_generation_, timing_failure);
			failed_pair_id_ = pair_id;
			status_.last_conversion_result = static_cast<std::uint32_t>(result);
			++status_.capture_failures;
			++status_.conversion_failures;
			++status_.pair_rejections;
			for (auto& pair : pairs_)
			{
				if (pair.pair_id != pair_id) continue;
				pair.active_eye_mask = 0;
				pair.state = pair_state::quarantined;
				++status_.pair_quarantines;
				break;
			}
			return false;
		};
		if (!accepting_pairs_ || pair_id != expected_pair_id_)
		{
			retain_failure(copy_failure::admission, E_ACCESSDENIED);
			failed_pair_id_ = pair_id;
			status_.last_conversion_result = static_cast<std::uint32_t>(E_ACCESSDENIED);
			++status_.capture_failures;
			++status_.pair_rejections;
			++status_.acquisition_rejections;
			return false;
		}
		if (!copy_ring_ || !status_.available) return fail(copy_failure::session);
		if (pair_id == 0 || eye >= 2 || source == nullptr || context == nullptr ||
			source_index >= conversion_sources_.size())
			return fail(copy_failure::arguments);
		auto& conversion_source = conversion_sources_[source_index];
		auto& conversion_source_view = conversion_source_views_[source_index];
		if (context != context_.Get()) return fail(copy_failure::context);
		if (!texture_uses_device(source, device_.Get()))
			return fail(copy_failure::source_device);
		if (!conversion_deferred_context_)
			return fail(copy_failure::deferred_context);
		source->GetDesc(&source_description);
		source_descriptor_read = true;
		// Ring allocation is based on the proven scene extent; production copies
		// must instead come from the completed H2 display pass (the selected spare target).
		if (!native_display_contract::accepts(source_description))
			return fail(copy_failure::source_descriptor);
		if (source_description.Width != width_ || source_description.Height != height_ ||
			source_description.Format != source_format_)
		{
			return fail(copy_failure::source_extent);
		}

		target_pair* selected{};
		for (auto& pair : pairs_)
		{
			if (pair.pair_id == pair_id)
			{
				selected = &pair;
				break;
			}
		}
		if (selected == nullptr)
		{
			for (std::size_t offset{}; offset < pairs_.size(); ++offset)
			{
				auto& pair = pairs_[(next_pair_index_ + offset) % pairs_.size()];
				if (pair.state != pair_state::free) continue;
				pair.pair_id = pair_id;
				pair.preview_pair_id = 0;
				pair.preview_projection = {};
				pair.preview_recording_crop = {};
				pair.preview_camera = {};
				pair.completed_eye_mask = 0;
				pair.active_eye_mask = 0;
				pair.state = pair_state::rendering;
				selected = &pair;
				next_pair_index_ = (&pair - pairs_.data() + 1) % pairs_.size();
				++status_.pair_acquires;
				break;
			}
		}
		if (selected == nullptr)
		{
			++status_.pair_exhaustions;
			return fail(copy_failure::ring_exhausted, E_FAIL,
				engine_stereo_gpu_timing::failure::native_ring);
		}
		const auto eye_bit = 1u << eye;
		if (selected->state != pair_state::rendering || selected->active_eye_mask != 0 ||
			(selected->completed_eye_mask & eye_bit) != 0 || !selected->eyes[eye].color)
		{
			return fail(copy_failure::ring_state);
		}
		selected->active_eye_mask = eye_bit;
		++status_.conversion_attempts;

		{
			if (!conversion_vertex_shader_ || !conversion_pixel_shader_ ||
				!conversion_rasterizer_state_ || !conversion_depth_state_ ||
				!conversion_blend_state_)
			{
				return fail(copy_failure::conversion_pipeline, E_UNEXPECTED);
			}
			if (conversion_source)
			{
				if (conversion_source.Get() != source || !conversion_source_view)
				{
					++status_.source_identity_mismatches;
					return fail(copy_failure::source_identity, DXGI_ERROR_INVALID_CALL);
				}
				++status_.source_identity_matches;
			}
			else
			{
				Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> source_view;
				const auto view_result = device_->CreateShaderResourceView(
					source, nullptr, &source_view);
				status_.last_source_view_create_result =
					static_cast<std::uint32_t>(view_result);
				++status_.source_view_creations;
				if (FAILED(view_result) || !source_view)
					return fail(copy_failure::source_view, view_result);
				conversion_source = source;
				conversion_source_view = std::move(source_view);
				status_.source_view_cached = true;
			}
		}

		status_.last_source_texture = reinterpret_cast<std::uintptr_t>(source);
		status_.last_source_view = reinterpret_cast<std::uintptr_t>(conversion_source_view.Get());
		(void)engine_stereo_gpu_timing::begin_native_conversion(pair_id, eye,
			context, device_generation_);
		{
			auto& commands = selected->conversion_commands[eye][source_index];
			if (!commands)
			{
				ID3D11RenderTargetView* target_view =
					selected->eyes[eye].color_view.Get();
				ID3D11ShaderResourceView* source_view_pointer =
					conversion_source_view.Get();
				const float blend_factor[4]{};
				conversion_deferred_context_->OMSetBlendState(
					conversion_blend_state_.Get(), blend_factor, D3D11_DEFAULT_SAMPLE_MASK);
				conversion_deferred_context_->OMSetDepthStencilState(
					conversion_depth_state_.Get(), 0);
				conversion_deferred_context_->OMSetRenderTargets(1, &target_view, nullptr);
				conversion_deferred_context_->RSSetState(
					conversion_rasterizer_state_.Get());
				const D3D11_VIEWPORT viewport{0.0f, 0.0f,
					static_cast<float>(width_), static_cast<float>(height_), 0.0f, 1.0f};
				conversion_deferred_context_->RSSetViewports(1, &viewport);
				conversion_deferred_context_->IASetInputLayout(nullptr);
				conversion_deferred_context_->IASetPrimitiveTopology(
					D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				conversion_deferred_context_->VSSetShader(
					conversion_vertex_shader_.Get(), nullptr, 0);
				conversion_deferred_context_->PSSetShader(
					conversion_pixel_shader_.Get(), nullptr, 0);
				conversion_deferred_context_->PSSetShaderResources(
					0, 1, &source_view_pointer);
				conversion_deferred_context_->Draw(3, 0);
				source_view_pointer = nullptr;
				conversion_deferred_context_->PSSetShaderResources(
					0, 1, &source_view_pointer);
				conversion_deferred_context_->OMSetRenderTargets(0, nullptr, nullptr);

				const auto command_result = conversion_deferred_context_->FinishCommandList(
					FALSE, &commands);
				status_.last_command_list_result =
					static_cast<std::uint32_t>(command_result);
				if (FAILED(command_result) || !commands)
				{
					++status_.command_list_build_failures;
					return fail(copy_failure::command_list, command_result);
				}
				if (!native_conversion_command_list::mark(commands.Get()))
				{
					// Rendering remains valid, but diagnostics must continue to treat an
					// unmarked list as opaque rather than infer its ownership.
					++status_.command_list_tag_failures;
				}
				++status_.command_list_builds;
			}

			// RestoreContextState=TRUE preserves every H2 immediate-context binding
			// without activating an ID3D11DeviceContextState. The owner pass holds
			// H2's own GPU mutex while this command is queued.
			const auto execute_started = std::chrono::steady_clock::now();
			diagnostics::record_trace(diagnostics::trace_event::native_command_list_begin,
				pair_id, (static_cast<std::uint64_t>(eye) << 56) |
					(reinterpret_cast<std::uintptr_t>(commands.Get()) & 0x00FFFFFFFFFFFFFFull));
			context->ExecuteCommandList(commands.Get(), TRUE);
			diagnostics::record_trace(diagnostics::trace_event::native_command_list_end,
				pair_id, eye);
			record_timing(status_.command_list_execute_samples,
				status_.command_list_execute_last_us,
				status_.command_list_execute_max_us,
				status_.command_list_execute_total_us, eye, elapsed_us(execute_started));
			++status_.command_list_executions;
		}
		(void)engine_stereo_gpu_timing::end_native_conversion(pair_id, eye,
			context, device_generation_);
		if (composition && composition->pair_id == pair_id && composition->eye == eye &&
			composition->device_generation == device_generation_ && composition->width == width_ &&
			composition->height == height_)
		{
			// Freeze metadata with this ring image before publication. An invalid
			// preview projection disables only the desktop crop, never HMD output.
			const auto& slot = composition->views.eyes[eye];
			const bool preview_valid=eye==1 && slot.pair_id==pair_id && slot.output_eye==eye &&
				engine_stereo_view::read_projection(slot, selected->preview_projection);
			if(preview_valid)
			{
				auto& camera=selected->preview_camera;
				std::memcpy(camera.axes.data(),slot.bytes.data()+0x10C,sizeof(camera.axes));
				camera.at=composition->views.camera_sampled_at;camera.epoch=composition->views.stabilization_epoch;
				camera.valid=pose_filter::valid({{},camera.axes});
			}
			// Destination is still rendering-owned. This seam cannot modify a texture
			// already held by SteamVR or contaminate H2's natural display source.
			auto event=*composition;
			desktop_mirror::crop recording_crop;
			event.recording_crop=preview_valid ? &recording_crop : nullptr;
			eye_composition::compose(event, context, conversion_source_view.Get(),
				selected->eyes[eye].color_view.Get());
			if (eye==1) selected->preview_recording_crop=recording_crop;
		}
		const auto removed_started = std::chrono::steady_clock::now();
		const auto removed_reason = device_->GetDeviceRemovedReason();
		record_timing(status_.removed_reason_samples, status_.removed_reason_last_us,
			status_.removed_reason_max_us, status_.removed_reason_total_us, eye,
			elapsed_us(removed_started));
		if (FAILED(removed_reason)) return fail(copy_failure::device_removed, removed_reason);
		status_.last_conversion_result = static_cast<std::uint32_t>(S_OK);
		++status_.conversion_completions;
		diagnostics::record_trace(diagnostics::trace_event::native_conversion_end,
			pair_id, eye);
		selected->active_eye_mask = 0;
		selected->completed_eye_mask |= eye_bit;
		if (selected->completed_eye_mask == 0x3)
		{
			selected->state = pair_state::published;
			selected->preview_pair_id = pair_id;
			selected->preview_owner_thread = GetCurrentThreadId();
			// One OpenVR pose family admits exactly one H2 stereo producer. Close the
			// gate before another frontend publication can occupy the second ring slot.
			accepting_pairs_ = false;
			expected_pair_id_ = 0;
			status_.accepting_pairs = false;
			status_.expected_pair_id = 0;
		}
		return true;
	}

	bool session::acquire_published_pair(const std::uint64_t pair_id,
		std::array<eye_target, 2>& output) noexcept
	{
		const std::lock_guard lock(mutex_);
		output = {};
		for (const auto& pair : pairs_)
		{
			if (pair.pair_id != pair_id) continue;
			const bool valid = pair.state == pair_state::published &&
				pair.completed_eye_mask == 0x3 && pair.active_eye_mask == 0 &&
				pair.eyes[0].color != nullptr && pair.eyes[1].color != nullptr;
			if (!valid) ++status_.pair_rejections;
			if (valid) output = pair.eyes;
			return valid;
		}
		++status_.pair_rejections;
		return false;
	}

	desktop_eye session::right_eye_for_desktop() const noexcept
	{
		const std::lock_guard lock(mutex_);
		if (!status_.available) return {};
		const auto thread = GetCurrentThreadId();
		const target_pair* latest{};
		for (const auto& pair : pairs_)
		{
			// A retired image remains readable until the next lease starts writing
			// that slot. Never sample a rendering, incomplete or quarantined image.
			if ((pair.state != pair_state::published && pair.state != pair_state::free) ||
				pair.preview_pair_id == 0 || pair.preview_owner_thread != thread) continue;
			if (!latest || pair.preview_pair_id > latest->preview_pair_id) latest = &pair;
		}
		if (!latest) return {};
		return {latest->eyes[1].color, latest->preview_pair_id, device_generation_, width_, height_,
			latest->preview_projection,latest->preview_recording_crop,latest->preview_camera};
	}

	bool session::pair_failed(const std::uint64_t pair_id) const noexcept
	{
		const std::lock_guard lock(mutex_);
		return pair_id != 0 && failed_pair_id_ == pair_id;
	}

	bool session::pair_deferred(const std::uint64_t pair_id) const noexcept
	{
		const std::lock_guard lock(mutex_);
		return pair_id != 0 && deferred_pair_id_ == pair_id;
	}

	bool session::pair_published(const std::uint64_t pair_id) const noexcept
	{
		const std::lock_guard lock(mutex_);
		for (const auto& pair : pairs_)
		{
			if (pair.pair_id == pair_id)
			{
				return pair.state == pair_state::published &&
					pair.completed_eye_mask == 0x3 && pair.active_eye_mask == 0;
			}
		}
		return false;
	}

	bool session::release_pair(const std::uint64_t pair_id) noexcept
	{
		const std::lock_guard lock(mutex_);
		for (auto& pair : pairs_)
		{
			if (pair.state == pair_state::published && pair.pair_id == pair_id &&
				pair.active_eye_mask == 0)
			{
				pair.pair_id = 0;
				pair.completed_eye_mask = 0;
				pair.active_eye_mask = 0;
				pair.state = pair_state::free;
				++status_.pair_releases;
				diagnostics::record_trace(diagnostics::trace_event::native_pair_release,
					pair_id, status_.pair_releases);
				engine_stereo_gpu_timing::poll_retired_pair(pair_id, context_.Get(),
					device_generation_);
				return true;
			}
		}
		return false;
	}

	bool session::discard_unpublished_pair(const std::uint64_t pair_id) noexcept
	{
		const std::lock_guard lock(mutex_);
		return terminate_unpublished_pair_locked(pair_id, true);
	}

	bool session::defer_unpublished_pair(const std::uint64_t pair_id) noexcept
	{
		const std::lock_guard lock(mutex_);
		return terminate_unpublished_pair_locked(pair_id, false);
	}

	bool session::terminate_unpublished_pair_locked(const std::uint64_t pair_id,
		const bool failed) noexcept
	{
		if (pair_id == 0) return false;

		target_pair* selected{};
		for (auto& pair : pairs_)
		{
			if (pair.pair_id != pair_id) continue;
			if (pair.state != pair_state::rendering || pair.active_eye_mask != 0)
			{
				return false;
			}
			selected = &pair;
			break;
		}

		const auto predicted = accepting_pairs_ && expected_pair_id_ == pair_id;
		if (selected == nullptr && !predicted) return false;
		if (selected != nullptr)
		{
			selected->pair_id = 0;
			selected->completed_eye_mask = 0;
			selected->active_eye_mask = 0;
			selected->state = pair_state::free;
		}
		if (predicted)
		{
			accepting_pairs_ = false;
			expected_pair_id_ = 0;
			status_.accepting_pairs = false;
			status_.expected_pair_id = 0;
		}
		if (failed)
		{
			// OpenVR already owns this predicted frame id. Preserve the terminal marker
			// so its existing Present-owner path can retire the pending family explicitly.
			failed_pair_id_ = pair_id;
		}
		else
		{
			deferred_pair_id_ = pair_id;
			++status_.pair_deferrals;
		}
		return true;
	}

	void session::quarantine_pair(const std::uint64_t pair_id) noexcept
	{
		const std::lock_guard lock(mutex_);
		for (auto& pair : pairs_)
		{
			if ((pair.state == pair_state::rendering || pair.state == pair_state::published) &&
				pair.pair_id == pair_id)
			{
				// A failed capture or partial Submit may still reference this resource.
				// Never lease either eye to H2 again in this device session.
				pair.active_eye_mask = 0;
				pair.state = pair_state::quarantined;
				failed_pair_id_ = pair_id;
				++status_.pair_quarantines;
				return;
			}
		}
	}

	void session::cancel_pair(const std::uint64_t pair_id) noexcept
	{
		const std::lock_guard lock(mutex_);
		for (auto& pair : pairs_)
		{
			if (pair.state == pair_state::rendering && pair.pair_id == pair_id)
			{
				// A renderer-side cancellation makes this predicted family terminal.
				// Releasing it for a silent retry would leave OpenVR waiting forever for
				// a pair that can no longer be completed coherently.
				pair.active_eye_mask = 0;
				pair.state = pair_state::quarantined;
				failed_pair_id_ = pair_id;
				++status_.pair_quarantines;
				return;
			}
		}
	}

	void session::record_capture(const bool success) noexcept
	{
		const std::lock_guard lock(mutex_);
		diagnostics::record_trace(diagnostics::trace_event::native_capture_result,
			success ? 1 : 0, status_.capture_requests);
		++status_.capture_requests;
		if (!success) ++status_.capture_failures;
	}

	status session::get_status() const noexcept
	{
		const std::lock_guard lock(mutex_);
		return status_;
	}
	bool session::try_get_status(status& output) const noexcept
	{
		const std::unique_lock lock(mutex_,std::try_to_lock);
		if(!lock.owns_lock())return false;
		output=status_;return true;
	}

	session& active() noexcept
	{
		static session instance;
		return instance;
	}
}
