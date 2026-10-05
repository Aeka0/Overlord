#include "std_include.hpp"

#include "component/d3d11.hpp"
#include "component/vr/engine_stereo_draw_indexed.hpp"
#include "component/vr/engine_stereo_dynamic_arena.hpp"
#include "component/vr/engine_stereo_execution.hpp"
#include "component/vr/engine_stereo_gpu_census.hpp"
#include "component/vr/engine_stereo_output_merger.hpp"
#include "component/vr/engine_stereo_resource_ops.hpp"

#include <d3dcompiler.h>

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>

namespace
{
	using Microsoft::WRL::ComPtr;

	[[noreturn]] void fail(const char* const message)
	{
		std::cerr << "vr-d3d11-gpu-census-probe: FAIL; " << message << '\n';
		std::exit(1);
	}

	ComPtr<ID3DBlob> compile(const std::string_view source, const char* const profile)
	{
		ComPtr<ID3DBlob> bytecode;
		ComPtr<ID3DBlob> errors;
		const auto result = D3DCompile(source.data(), source.size(), nullptr, nullptr,
			nullptr, "main", profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
			bytecode.GetAddressOf(), errors.GetAddressOf());
		if (FAILED(result))
		{
			if (errors) std::cerr.write(static_cast<const char*>(errors->GetBufferPointer()),
				static_cast<std::streamsize>(errors->GetBufferSize()));
			fail("shader compilation");
		}
		return bytecode;
	}

	struct color_resource
	{
		ComPtr<ID3D11Texture2D> texture;
		ComPtr<ID3D11RenderTargetView> rtv;
		ComPtr<ID3D11ShaderResourceView> srv;
	};

	color_resource make_color(ID3D11Device* const device,
		const std::uint32_t width = 16, const std::uint32_t height = 16)
	{
		D3D11_TEXTURE2D_DESC description{};
		description.Width = width;
		description.Height = height;
		description.MipLevels = 1;
		description.ArraySize = 1;
		description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		description.SampleDesc.Count = 1;
		description.Usage = D3D11_USAGE_DEFAULT;
		description.BindFlags = D3D11_BIND_RENDER_TARGET |
			D3D11_BIND_SHADER_RESOURCE;
		color_resource output{};
		if (FAILED(device->CreateTexture2D(&description, nullptr,
			output.texture.GetAddressOf())) ||
			FAILED(device->CreateRenderTargetView(output.texture.Get(), nullptr,
				output.rtv.GetAddressOf())) ||
			FAILED(device->CreateShaderResourceView(output.texture.Get(), nullptr,
				output.srv.GetAddressOf())))
		{
			fail("color resource creation");
		}
		return output;
	}

	const vr::engine_stereo_gpu_census::resource_access& find_resource(
		const vr::engine_stereo_gpu_census::report& report,
		ID3D11Resource* const identity)
	{
		for (const auto& resource : report.resources)
		{
			if (resource.identity == reinterpret_cast<std::uintptr_t>(identity))
				return resource;
		}
		fail("expected resource identity was not captured");
	}

	void expect_no_right_read(const vr::engine_stereo_gpu_census::report& report,
		ID3D11Resource* const resource, const char* const message)
	{
		const auto& access = find_resource(report, resource);
		if ((access.read_mask & 2u) != 0 || access.first_right_read.call != 0)
			fail(message);
	}

	const vr::engine_stereo_gpu_census::shader_resource_mismatch_sample&
		find_srv_sample(const vr::engine_stereo_gpu_census::ordered_comparison& ordered,
			const std::uint8_t stage, const std::uint8_t slot)
	{
		for (std::size_t index{}; index < ordered.shader_resource_sample_count; ++index)
		{
			const auto& sample = ordered.shader_resource_samples[index];
			if (sample.stage == stage && sample.slot == slot) return sample;
		}
		fail("expected shader-resource mismatch sample");
	}
}

int main()
{
	using vr::engine_stereo_gpu_census::request_control::launch_eligible;
	using vr::engine_stereo_gpu_census::request_control::make_token;
	if (launch_eligible(0, 0, false) ||
		!launch_eligible(make_token(1, 0), 0, false) ||
		launch_eligible(make_token(1, 1), 1, false) ||
		launch_eligible(make_token(1, 0), 0, true))
	{
		fail("GPU census explicit-request launch contract");
	}

	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	D3D_FEATURE_LEVEL feature_level{};
	if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr,
		0, D3D11_SDK_VERSION, device.GetAddressOf(), &feature_level,
		context.GetAddressOf())))
	{
		fail("D3D11CreateDevice(WARP)");
	}
	D3D11_BUFFER_DESC dynamic_fx_index_buffer_description{};
	dynamic_fx_index_buffer_description.ByteWidth = 256;
	dynamic_fx_index_buffer_description.Usage = D3D11_USAGE_DEFAULT;
	dynamic_fx_index_buffer_description.BindFlags = D3D11_BIND_INDEX_BUFFER;
	std::array<std::uint16_t, 128> dynamic_fx_indices{};
	D3D11_SUBRESOURCE_DATA dynamic_fx_index_data{};
	dynamic_fx_index_data.pSysMem = dynamic_fx_indices.data();
	ComPtr<ID3D11Buffer> dynamic_fx_index_buffer;
	const auto dynamic_fx_index_result = device->CreateBuffer(
		&dynamic_fx_index_buffer_description, &dynamic_fx_index_data,
		dynamic_fx_index_buffer.GetAddressOf());
	if (FAILED(dynamic_fx_index_result))
	{
		fail("dynamic-FX index buffer creation");
	}
	if (!vr::engine_stereo_constant_buffer_probe::install_early(device.Get(),
			context.Get(), 1) ||
		!vr::engine_stereo_constant_buffer_probe::select_device(device.Get(),
			context.Get(), 1) ||
		!vr::engine_stereo_output_merger::install(context.Get(), 1) ||
		!vr::engine_stereo_resource_ops::install(context.Get(), 1) ||
		!vr::engine_stereo_draw_indexed::install(context.Get(), 1) ||
		!vr::engine_stereo_execution::install(context.Get(), 1))
	{
		fail("D3D11 observation hook installation");
	}
	vr::engine_stereo_resource_ops::set_observer(
		vr::engine_stereo_resource_ops::observer_channel::constant_buffer_history,
		vr::engine_stereo_constant_buffer_probe::observe_resource_operation);
	vr::engine_stereo_constant_buffer_probe::set_resource_observer_attached(true);
	vr::engine_stereo_output_merger::set_clear_state_observer(
		vr::engine_stereo_constant_buffer_probe::observe_clear_state);
	vr::engine_stereo_execution::set_opaque_state_observer(
		vr::engine_stereo_constant_buffer_probe::observe_opaque_state_change);

	constexpr std::string_view vertex_source =
		"cbuffer ProbeConstants:register(b3){float4 probeOffset;}"
		"float4 main(uint id : SV_VertexID) : SV_Position {"
		"float2 p=float2((id<<1)&2,id&2); return float4("
		"p*float2(2,-2)+float2(-1,1)+probeOffset.xy,0,1);}";
	constexpr std::string_view pixel_source =
		"Texture2D<float4> sourceTexture:register(t0);"
		"Texture2D<float4> screenSpaceReflection:register(t6);"
		"float4 main(float4 p:SV_Position):SV_Target{return "
		"sourceTexture.Load(int3(0,0,0))+screenSpaceReflection.Load(int3(0,0,0));}";
	constexpr std::string_view compute_source =
		"Texture2D<float4> sourceTexture:register(t0);"
		"[numthreads(1,1,1)] void main(uint3 id:SV_DispatchThreadID){}";
	constexpr std::string_view dynamic_effect_pixel_source =
		"cbuffer EffectConstants:register(b4){float4 atlasOpacity;}"
		"Texture2D<float4> breakableGlassEffect:register(t20);"
		"float4 main(float4 p:SV_Position):SV_Target{return "
		"breakableGlassEffect.Load(int3(0,0,0))*atlasOpacity;}";
	const auto vertex_bytecode = compile(vertex_source, "vs_5_0");
	const auto pixel_bytecode = compile(pixel_source, "ps_5_0");
	const auto compute_bytecode = compile(compute_source, "cs_5_0");
	const auto dynamic_effect_pixel_bytecode = compile(dynamic_effect_pixel_source,
		"ps_5_0");
	ComPtr<ID3D11VertexShader> vertex_shader;
	ComPtr<ID3D11PixelShader> pixel_shader;
	ComPtr<ID3D11PixelShader> dynamic_effect_pixel_shader;
	ComPtr<ID3D11ComputeShader> compute_shader;
	if (FAILED(device->CreateVertexShader(vertex_bytecode->GetBufferPointer(),
		vertex_bytecode->GetBufferSize(), nullptr, vertex_shader.GetAddressOf())) ||
		FAILED(device->CreatePixelShader(pixel_bytecode->GetBufferPointer(),
			pixel_bytecode->GetBufferSize(), nullptr, pixel_shader.GetAddressOf())) ||
		FAILED(device->CreatePixelShader(
			dynamic_effect_pixel_bytecode->GetBufferPointer(),
			dynamic_effect_pixel_bytecode->GetBufferSize(), nullptr,
			dynamic_effect_pixel_shader.GetAddressOf())) ||
		FAILED(device->CreateComputeShader(compute_bytecode->GetBufferPointer(),
			compute_bytecode->GetBufferSize(), nullptr, compute_shader.GetAddressOf())))
	{
		fail("shader creation");
	}
	if (FAILED(vertex_shader->SetPrivateData(d3d11::guid_shader_bytecode,
		static_cast<UINT>(vertex_bytecode->GetBufferSize()),
		vertex_bytecode->GetBufferPointer())) ||
		FAILED(pixel_shader->SetPrivateData(d3d11::guid_shader_bytecode,
			static_cast<UINT>(pixel_bytecode->GetBufferSize()),
			pixel_bytecode->GetBufferPointer())) ||
		FAILED(dynamic_effect_pixel_shader->SetPrivateData(
			d3d11::guid_shader_bytecode,
			static_cast<UINT>(dynamic_effect_pixel_bytecode->GetBufferSize()),
			dynamic_effect_pixel_bytecode->GetBufferPointer())) ||
		FAILED(compute_shader->SetPrivateData(d3d11::guid_shader_bytecode,
			static_cast<UINT>(compute_bytecode->GetBufferSize()),
			compute_bytecode->GetBufferPointer())))
	{
		fail("shader bytecode private data");
	}
	constexpr char vertex_debug_name[] = "gpu_census_probe_vs";
	constexpr char pixel_debug_name[] = "gpu_census_probe_ps_ssr";
	constexpr char dynamic_effect_pixel_debug_name[] =
		"breakable_glass_effect_ps";
	constexpr char compute_debug_name[] = "gpu_census_probe_cs";
	if (FAILED(vertex_shader->SetPrivateData(WKPDID_D3DDebugObjectName,
			static_cast<UINT>(sizeof(vertex_debug_name) - 1), vertex_debug_name)) ||
		FAILED(pixel_shader->SetPrivateData(WKPDID_D3DDebugObjectName,
			static_cast<UINT>(sizeof(pixel_debug_name) - 1), pixel_debug_name)) ||
		FAILED(dynamic_effect_pixel_shader->SetPrivateData(
			WKPDID_D3DDebugObjectName,
			static_cast<UINT>(sizeof(dynamic_effect_pixel_debug_name) - 1),
			dynamic_effect_pixel_debug_name)) ||
		FAILED(compute_shader->SetPrivateData(WKPDID_D3DDebugObjectName,
			static_cast<UINT>(sizeof(compute_debug_name) - 1), compute_debug_name)))
	{
		fail("shader debug names");
	}

	auto dispatch_ps = make_color(device.Get());
	auto dispatch_cs = make_color(device.Get());
	auto dispatch_om = make_color(device.Get());
	auto draw_ps = make_color(device.Get());
	auto draw_cs = make_color(device.Get());
	auto draw_om = make_color(device.Get());
	auto null_gs = make_color(device.Get());
	auto null_hs = make_color(device.Get());
	auto null_ds = make_color(device.Get());
	auto null_ps = make_color(device.Get());
	auto null_cs = make_color(device.Get());
	auto no_color_write = make_color(device.Get());
	D3D11_BLEND_DESC no_color_write_description{};
	no_color_write_description.RenderTarget[0].RenderTargetWriteMask = 0;
	ComPtr<ID3D11BlendState> no_color_write_state;
	if (FAILED(device->CreateBlendState(&no_color_write_description,
			no_color_write_state.GetAddressOf())))
	{
		fail("no-color-write blend state creation");
	}
	D3D11_SAMPLER_DESC dynamic_fx_sampler_description{};
	dynamic_fx_sampler_description.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	dynamic_fx_sampler_description.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	dynamic_fx_sampler_description.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	dynamic_fx_sampler_description.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
	dynamic_fx_sampler_description.MaxLOD = D3D11_FLOAT32_MAX;
	ComPtr<ID3D11SamplerState> dynamic_fx_sampler;
	if (FAILED(device->CreateSamplerState(&dynamic_fx_sampler_description,
			dynamic_fx_sampler.GetAddressOf())))
	{
		fail("dynamic-FX sampler creation");
	}

	D3D11_TEXTURE2D_DESC depth_description{};
	depth_description.Width = 16;
	depth_description.Height = 16;
	depth_description.MipLevels = 1;
	depth_description.ArraySize = 1;
	depth_description.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depth_description.SampleDesc.Count = 1;
	depth_description.Usage = D3D11_USAGE_DEFAULT;
	depth_description.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	ComPtr<ID3D11Texture2D> dispatch_depth;
	ComPtr<ID3D11DepthStencilView> dispatch_dsv;
	if (FAILED(device->CreateTexture2D(&depth_description, nullptr,
		dispatch_depth.GetAddressOf())) ||
		FAILED(device->CreateDepthStencilView(dispatch_depth.Get(), nullptr,
			dispatch_dsv.GetAddressOf())))
	{
		fail("depth resource creation");
	}

	if (!vr::engine_stereo_gpu_census::begin_pair(1, context.Get()) ||
		!vr::engine_stereo_gpu_census::begin_eye(1, 0))
	{
		fail("left census begin");
	}
	const std::array<ID3D11RenderTargetView*, 12> seeded_views{
		dispatch_ps.rtv.Get(), dispatch_cs.rtv.Get(), dispatch_om.rtv.Get(),
		draw_ps.rtv.Get(), draw_cs.rtv.Get(), draw_om.rtv.Get(), null_gs.rtv.Get(),
		null_hs.rtv.Get(), null_ds.rtv.Get(), null_ps.rtv.Get(), null_cs.rtv.Get(),
		no_color_write.rtv.Get()};
	for (auto* const view : seeded_views)
	{
		vr::engine_stereo_gpu_census::observe_clear_render_target(context.Get(), view,
			0x1000, 0xFFFFFFFFu, 0, 0);
	}
	vr::engine_stereo_gpu_census::observe_clear_depth_stencil(context.Get(),
		dispatch_dsv.Get(), D3D11_CLEAR_DEPTH, 0x1001, 0xFFFFFFFFu, 0, 0);
	context->ClearState();
	vr::engine_stereo_gpu_census::observe(context.Get(),
		vr::engine_stereo_gpu_census::api::draw, 0x1002, 0xFFFFFFFFu, 0, 0, {});
	if (!vr::engine_stereo_gpu_census::end_eye(1, 0) ||
		!vr::engine_stereo_gpu_census::begin_eye(1, 1))
	{
		fail("right census begin");
	}

	context->VSSetShader(vertex_shader.Get(), nullptr, 0);
	context->PSSetShader(pixel_shader.Get(), nullptr, 0);
	context->CSSetShader(compute_shader.Get(), nullptr, 0);
	ID3D11ShaderResourceView* dispatch_ps_view = dispatch_ps.srv.Get();
	ID3D11ShaderResourceView* dispatch_cs_view = dispatch_cs.srv.Get();
	context->PSSetShaderResources(0, 1, &dispatch_ps_view);
	context->CSSetShaderResources(0, 1, &dispatch_cs_view);
	ID3D11RenderTargetView* dispatch_output = dispatch_om.rtv.Get();
	context->OMSetRenderTargets(1, &dispatch_output, dispatch_dsv.Get());
	vr::engine_stereo_gpu_census::observe(context.Get(),
		vr::engine_stereo_gpu_census::api::dispatch, 0x2000, 0xFFFFFFFFu, 0, 0, {});

	context->ClearState();
	context->VSSetShader(vertex_shader.Get(), nullptr, 0);
	context->PSSetShader(pixel_shader.Get(), nullptr, 0);
	context->CSSetShader(compute_shader.Get(), nullptr, 0);
	ID3D11ShaderResourceView* draw_ps_view = draw_ps.srv.Get();
	ID3D11ShaderResourceView* draw_cs_view = draw_cs.srv.Get();
	context->PSSetShaderResources(0, 1, &draw_ps_view);
	context->CSSetShaderResources(0, 1, &draw_cs_view);
	ID3D11RenderTargetView* draw_output = draw_om.rtv.Get();
	context->OMSetRenderTargets(1, &draw_output, nullptr);
	vr::engine_stereo_gpu_census::observe(context.Get(),
		vr::engine_stereo_gpu_census::api::draw, 0x2001, 0xFFFFFFFFu, 0, 0, {});

	context->ClearState();
	context->VSSetShader(vertex_shader.Get(), nullptr, 0);
	context->PSSetShader(pixel_shader.Get(), nullptr, 0);
	context->OMSetBlendState(no_color_write_state.Get(), nullptr, 0xFFFFFFFFu);
	ID3D11RenderTargetView* masked_output = no_color_write.rtv.Get();
	context->OMSetRenderTargets(1, &masked_output, nullptr);
	vr::engine_stereo_gpu_census::observe(context.Get(),
		vr::engine_stereo_gpu_census::api::draw, 0x2005, 0xFFFFFFFFu, 0, 0, {});

	context->ClearState();
	context->VSSetShader(vertex_shader.Get(), nullptr, 0);
	context->PSSetShader(pixel_shader.Get(), nullptr, 0);
	ID3D11ShaderResourceView* null_gs_view = null_gs.srv.Get();
	ID3D11ShaderResourceView* null_hs_view = null_hs.srv.Get();
	ID3D11ShaderResourceView* null_ds_view = null_ds.srv.Get();
	context->GSSetShaderResources(0, 1, &null_gs_view);
	context->HSSetShaderResources(0, 1, &null_hs_view);
	context->DSSetShaderResources(0, 1, &null_ds_view);
	vr::engine_stereo_gpu_census::observe(context.Get(),
		vr::engine_stereo_gpu_census::api::draw, 0x2002, 0xFFFFFFFFu, 0, 0, {});

	context->ClearState();
	context->VSSetShader(vertex_shader.Get(), nullptr, 0);
	ID3D11ShaderResourceView* null_ps_view = null_ps.srv.Get();
	context->PSSetShaderResources(0, 1, &null_ps_view);
	vr::engine_stereo_gpu_census::observe(context.Get(),
		vr::engine_stereo_gpu_census::api::draw, 0x2003, 0xFFFFFFFFu, 0, 0, {});

	context->ClearState();
	ID3D11ShaderResourceView* null_cs_view = null_cs.srv.Get();
	context->CSSetShaderResources(0, 1, &null_cs_view);
	vr::engine_stereo_gpu_census::observe(context.Get(),
		vr::engine_stereo_gpu_census::api::dispatch, 0x2004, 0xFFFFFFFFu, 0, 0, {});
	context->ClearState();
	if (!vr::engine_stereo_gpu_census::end_eye(1, 1) ||
		!vr::engine_stereo_gpu_census::end_pair(1))
	{
		fail("census completion");
	}

	auto report_storage =
		std::make_unique<vr::engine_stereo_gpu_census::report>();
	vr::engine_stereo_gpu_census::get_report(*report_storage);
	const auto& report = *report_storage;
	if (report.current_state != vr::engine_stereo_gpu_census::state::complete ||
		report.query_failures != 0 || report.overflows != 0 ||
		report.hazard_candidates != 2)
	{
		fail("aggregate census contract");
	}
	expect_no_right_read(report, dispatch_ps.texture.Get(),
		"Dispatch consumed a PS SRV");
	expect_no_right_read(report, draw_cs.texture.Get(),
		"Draw consumed a CS SRV");
	expect_no_right_read(report, null_gs.texture.Get(),
		"Draw consumed a null-GS SRV");
	expect_no_right_read(report, null_hs.texture.Get(),
		"Draw consumed a null-HS SRV");
	expect_no_right_read(report, null_ds.texture.Get(),
		"Draw consumed a null-DS SRV");
	expect_no_right_read(report, null_ps.texture.Get(),
		"Draw consumed a null-PS SRV");
	expect_no_right_read(report, null_cs.texture.Get(),
		"Dispatch consumed a null-CS SRV");
	const auto& dispatch_om_access = find_resource(report, dispatch_om.texture.Get());
	if ((dispatch_om_access.write_mask & 2u) != 0 ||
		dispatch_om_access.first_right_write.call != 0)
	{
		fail("Dispatch consumed an OM RTV");
	}
	const auto& dispatch_depth_access = find_resource(report, dispatch_depth.Get());
	if ((dispatch_depth_access.read_mask & 2u) != 0 ||
		(dispatch_depth_access.write_mask & 2u) != 0)
	{
		fail("Dispatch consumed an OM DSV");
	}
	const auto& dispatch_cs_access = find_resource(report, dispatch_cs.texture.Get());
	if ((dispatch_cs_access.read_mask & 2u) == 0 ||
		dispatch_cs_access.first_right_read.site !=
			vr::engine_stereo_gpu_census::access_site::cs_srv)
	{
		fail("Dispatch CS positive control");
	}
	const auto& draw_ps_access = find_resource(report, draw_ps.texture.Get());
	if ((draw_ps_access.read_mask & 2u) == 0 ||
		draw_ps_access.first_right_read.site !=
			vr::engine_stereo_gpu_census::access_site::ps_srv)
	{
		fail("Draw PS positive control");
	}
	const auto& draw_om_access = find_resource(report, draw_om.texture.Get());
	if ((draw_om_access.write_mask & 2u) == 0 ||
		draw_om_access.first_right_write.site !=
			vr::engine_stereo_gpu_census::access_site::om_rtv)
	{
		fail("Draw OM positive control");
	}
	const auto& no_color_access = find_resource(report, no_color_write.texture.Get());
	if ((no_color_access.write_mask & 2u) != 0 ||
		no_color_access.first_right_write.call != 0)
	{
		fail("Draw ignored a zero color-write mask");
	}
	if (report.rejected_dispatch_graphics_srv == 0 ||
		report.rejected_dispatch_om == 0 || report.rejected_draw_cs == 0 ||
		report.rejected_null_shader == 0 ||
		report.rejected_no_color_write == 0)
	{
		fail("API-filter rejection telemetry");
	}

	// A second pair validates the high-information ordered diagnostics through
	// the real execution hook: raw DrawIndexedInstanced arguments, a VS constant
	// buffer slot, and a PS SRV slot with different resource/subresource ranges.
	if (!vr::engine_stereo_gpu_census::reset())
		fail("ordered diagnostics reset");
	// Production enables history at the explicit-request pre-roll boundary, before
	// H2 uploads resources that the one-shot pair later compares. Reproduce that
	// ordering here; content written while the gate is closed is intentionally not
	// valid evidence in a later history epoch.
	vr::engine_stereo_constant_buffer_probe::set_history_tracking_enabled(true);
	D3D11_BUFFER_DESC constant_buffer_description{};
	constant_buffer_description.ByteWidth = 16;
	constant_buffer_description.Usage = D3D11_USAGE_DEFAULT;
	constant_buffer_description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	ComPtr<ID3D11Buffer> left_constant_buffer;
	ComPtr<ID3D11Buffer> right_constant_buffer;
	if (FAILED(device->CreateBuffer(&constant_buffer_description, nullptr,
		left_constant_buffer.GetAddressOf())) ||
		FAILED(device->CreateBuffer(&constant_buffer_description, nullptr,
			right_constant_buffer.GetAddressOf())))
	{
		fail("ordered diagnostics constant buffers");
	}
	const std::array<float, 4> left_constant_data{1.0f, 2.0f, 3.0f, 4.0f};
	const std::array<float, 4> right_constant_data{5.0f, 6.0f, 7.0f, 8.0f};
	context->UpdateSubresource(left_constant_buffer.Get(), 0, nullptr,
		left_constant_data.data(), 0, 0);
	context->UpdateSubresource(right_constant_buffer.Get(), 0, nullptr,
		right_constant_data.data(), 0, 0);
	D3D11_TEXTURE2D_DESC ranged_texture_description{};
	ranged_texture_description.Width = 16;
	ranged_texture_description.Height = 16;
	ranged_texture_description.MipLevels = 4;
	ranged_texture_description.ArraySize = 2;
	ranged_texture_description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	ranged_texture_description.SampleDesc.Count = 1;
	ranged_texture_description.Usage = D3D11_USAGE_DEFAULT;
	ranged_texture_description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	ComPtr<ID3D11Texture2D> left_ranged_texture;
	ComPtr<ID3D11Texture2D> right_ranged_texture;
	if (FAILED(device->CreateTexture2D(&ranged_texture_description, nullptr,
		left_ranged_texture.GetAddressOf())) ||
		FAILED(device->CreateTexture2D(&ranged_texture_description, nullptr,
			right_ranged_texture.GetAddressOf())))
	{
		fail("ordered diagnostics ranged textures");
	}
	D3D11_SHADER_RESOURCE_VIEW_DESC left_srv_description{};
	left_srv_description.Format = ranged_texture_description.Format;
	left_srv_description.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	left_srv_description.Texture2DArray.MostDetailedMip = 0;
	left_srv_description.Texture2DArray.MipLevels = 2;
	left_srv_description.Texture2DArray.FirstArraySlice = 0;
	left_srv_description.Texture2DArray.ArraySize = 1;
	auto right_srv_description = left_srv_description;
	right_srv_description.Texture2DArray.MostDetailedMip = 1;
	right_srv_description.Texture2DArray.MipLevels = 3;
	right_srv_description.Texture2DArray.FirstArraySlice = 1;
	ComPtr<ID3D11ShaderResourceView> left_ranged_srv;
	ComPtr<ID3D11ShaderResourceView> right_ranged_srv;
	ComPtr<ID3D11ShaderResourceView> alternate_left_ranged_srv;
	if (FAILED(device->CreateShaderResourceView(left_ranged_texture.Get(),
		&left_srv_description, left_ranged_srv.GetAddressOf())) ||
		FAILED(device->CreateShaderResourceView(right_ranged_texture.Get(),
		&right_srv_description, right_ranged_srv.GetAddressOf())) ||
		FAILED(device->CreateShaderResourceView(left_ranged_texture.Get(),
		&right_srv_description, alternate_left_ranged_srv.GetAddressOf())))
	{
		fail("ordered diagnostics ranged SRVs");
	}
	auto left_unused_srv = make_color(device.Get(), 16, 16);
	auto right_unused_srv = make_color(device.Get(), 8, 8);

	const auto bind_ordered_state = [&](ID3D11Buffer* const constant_buffer,
		ID3D11ShaderResourceView* const used_srv,
		ID3D11ShaderResourceView* const unused_srv)
	{
		context->ClearState();
		context->VSSetShader(vertex_shader.Get(), nullptr, 0);
		context->PSSetShader(pixel_shader.Get(), nullptr, 0);
		context->VSSetConstantBuffers(3, 1, &constant_buffer);
		std::array<ID3D11ShaderResourceView*, 18> srvs{};
		srvs.fill(used_srv);
		srvs[7] = unused_srv;
		context->PSSetShaderResources(0, static_cast<UINT>(srvs.size()), srvs.data());
		ID3D11RenderTargetView* output = draw_om.rtv.Get();
		context->OMSetRenderTargets(1, &output, nullptr);
	};
	const auto bind_dynamic_effect_state = [&](ID3D11Buffer* const constant_buffer,
		ID3D11ShaderResourceView* const srv)
	{
		context->ClearState();
		context->VSSetShader(vertex_shader.Get(), nullptr, 0);
		context->PSSetShader(dynamic_effect_pixel_shader.Get(), nullptr, 0);
		context->VSSetConstantBuffers(3, 1, &constant_buffer);
		context->PSSetShaderResources(20, 1, &srv);
		ID3D11RenderTargetView* output = draw_om.rtv.Get();
		context->OMSetRenderTargets(1, &output, nullptr);
	};
	if (!vr::engine_stereo_gpu_census::begin_pair(2, context.Get()) ||
		!vr::engine_stereo_gpu_census::begin_eye(2, 0))
	{
		fail("ordered diagnostics left begin");
	}
	bind_ordered_state(left_constant_buffer.Get(), left_ranged_srv.Get(),
		left_unused_srv.srv.Get());
	context->DrawIndexedInstanced(12, 2, 3, -4, 5);
	bind_dynamic_effect_state(left_constant_buffer.Get(), left_ranged_srv.Get());
	context->DrawIndexedInstanced(4, 1, 0, 0, 0);
	if (!vr::engine_stereo_gpu_census::end_eye(2, 0) ||
		!vr::engine_stereo_gpu_census::begin_eye(2, 1))
	{
		fail("ordered diagnostics right begin");
	}
	bind_ordered_state(right_constant_buffer.Get(), right_ranged_srv.Get(),
		right_unused_srv.srv.Get());
	context->DrawIndexedInstanced(21, 7, 8, -9, 10);
	bind_dynamic_effect_state(right_constant_buffer.Get(), right_ranged_srv.Get());
	context->DrawIndexedInstanced(4, 1, 0, 0, 0);
	if (!vr::engine_stereo_gpu_census::end_eye(2, 1) ||
		!vr::engine_stereo_gpu_census::end_pair(2))
	{
		fail("ordered diagnostics completion");
	}
	vr::engine_stereo_gpu_census::get_report(*report_storage);
	const auto& ordered = report_storage->ordered;
	const auto& constant_probe = report_storage->constant_buffer_probe;
	if (ordered.comparisons != 2 || ordered.argument_mismatches != 1 ||
		ordered.argument_sample_count != 1 ||
		ordered.constant_buffer_mismatches != 2 ||
		ordered.constant_buffer_sample_count != 2 ||
		ordered.shader_resource_mismatches != 2 ||
		ordered.shader_resource_sample_count !=
			vr::engine_stereo_gpu_census::maximum_shader_resource_mismatch_samples ||
		ordered.constant_buffer_snapshot_truncations != 0 ||
		ordered.shader_resource_snapshot_truncations != 0 ||
		ordered.snapshot_static_storage_bytes == 0)
	{
		fail("ordered category samples");
	}
	if (constant_probe.current_state !=
			vr::engine_stereo_constant_buffer_probe::state::complete ||
		constant_probe.comparisons != 2 ||
		constant_probe.eyes[0].shader_used_draws != 2 ||
		constant_probe.eyes[1].shader_used_draws != 2 ||
		constant_probe.both_bound_different_content != 2 ||
		constant_probe.used_left_bound_right_null != 0 ||
		constant_probe.ordinal_mismatches != 0)
	{
		fail("integrated VS b3 usage/content comparison");
	}
	const auto& argument_sample = ordered.argument_samples[0];
	const std::array<std::uint64_t, 5> expected_left_arguments{12, 2, 3,
		static_cast<std::uint64_t>(static_cast<std::int64_t>(-4)), 5};
	const std::array<std::uint64_t, 5> expected_right_arguments{21, 7, 8,
		static_cast<std::uint64_t>(static_cast<std::int64_t>(-9)), 10};
	if (argument_sample.left.arguments.count != expected_left_arguments.size() ||
		argument_sample.right.arguments.count != expected_right_arguments.size() ||
		!std::equal(expected_left_arguments.begin(), expected_left_arguments.end(),
			argument_sample.left.arguments.values.begin()) ||
		!std::equal(expected_right_arguments.begin(), expected_right_arguments.end(),
			argument_sample.right.arguments.values.begin()))
	{
		fail("raw count/instance/start/base/start-instance arguments");
	}
	const auto& constant_buffer_sample = ordered.constant_buffer_samples[0];
	if (constant_buffer_sample.stage != 0 || constant_buffer_sample.slot != 3 ||
		constant_buffer_sample.left_identity !=
			reinterpret_cast<std::uintptr_t>(left_constant_buffer.Get()) ||
		constant_buffer_sample.right_identity !=
			reinterpret_cast<std::uintptr_t>(right_constant_buffer.Get()))
	{
		fail("constant-buffer stage/slot identities");
	}
	const auto& used_srv_sample = find_srv_sample(ordered, 1, 6);
	if (used_srv_sample.left_binding.view !=
			reinterpret_cast<std::uintptr_t>(left_ranged_srv.Get()) ||
		used_srv_sample.right_binding.view !=
			reinterpret_cast<std::uintptr_t>(right_ranged_srv.Get()) ||
		used_srv_sample.left_binding.resource !=
			reinterpret_cast<std::uintptr_t>(left_ranged_texture.Get()) ||
		used_srv_sample.right_binding.resource !=
			reinterpret_cast<std::uintptr_t>(right_ranged_texture.Get()) ||
		used_srv_sample.left_binding.range.most_detailed_mip != 0 ||
		used_srv_sample.left_binding.range.mip_levels != 2 ||
		used_srv_sample.left_binding.range.first_array_slice != 0 ||
		used_srv_sample.right_binding.range.most_detailed_mip != 1 ||
		used_srv_sample.right_binding.range.mip_levels != 3 ||
		used_srv_sample.right_binding.range.first_array_slice != 1)
	{
		fail("shader-resource stage/slot identity and subresource range");
	}
	const auto& unused_srv_sample = find_srv_sample(ordered, 1, 7);
	const auto& dynamic_effect_sample = find_srv_sample(ordered, 1, 20);
	if (used_srv_sample.left_shader.usage !=
			vr::engine_stereo_gpu_census::srv_shader_usage::used ||
		used_srv_sample.right_shader.usage !=
			vr::engine_stereo_gpu_census::srv_shader_usage::used ||
		used_srv_sample.left_arguments.count != 5 ||
		used_srv_sample.right_arguments.count != 5 ||
		used_srv_sample.left_arguments.values[0] != 12 ||
		used_srv_sample.right_arguments.values[0] != 21 ||
		std::string_view(used_srv_sample.left_shader.shader_debug_name.data()) !=
			"gpu_census_probe_ps_ssr" ||
		std::string_view(used_srv_sample.left_shader.binding_name.data()) !=
			"screenSpaceReflection" ||
		unused_srv_sample.left_shader.usage !=
			vr::engine_stereo_gpu_census::srv_shader_usage::unused ||
		unused_srv_sample.right_shader.usage !=
			vr::engine_stereo_gpu_census::srv_shader_usage::unused ||
		dynamic_effect_sample.left_shader.usage !=
			vr::engine_stereo_gpu_census::srv_shader_usage::used ||
		dynamic_effect_sample.right_shader.usage !=
			vr::engine_stereo_gpu_census::srv_shader_usage::used ||
		dynamic_effect_sample.left_arguments.values[0] != 4 ||
		dynamic_effect_sample.right_arguments.values[0] != 4 ||
		std::string_view(
			dynamic_effect_sample.left_shader.shader_debug_name.data()) !=
			"breakable_glass_effect_ps" ||
		std::string_view(dynamic_effect_sample.left_shader.binding_name.data()) !=
			"breakableGlassEffect")
	{
		fail("shader-declared SRV usage and debug names");
	}
	const auto& usage = ordered.shader_resource_usage;
	const auto& ps6 = usage.slots[1][6];
	const auto& ps7 = usage.slots[1][7];
	if (!usage.classification_complete || usage.slot_mismatches != 19 ||
		usage.used != 3 || usage.unused != 16 || usage.unknown != 0 ||
		usage.descriptor_same != 18 || usage.descriptor_different != 1 ||
		usage.descriptor_unknown != 0 || usage.shader_bytecode_missing != 0 ||
		usage.shader_bytecode_oversized != 0 ||
		usage.shader_reflection_failures != 0 ||
		usage.shader_binding_overflows != 0 || usage.sample_overflows != 3 ||
		usage.shader_cache_overflows != 0 || usage.descriptor_cache_overflows != 0 ||
		ps6.mismatches != 1 || ps6.used != 1 || ps6.descriptor_same != 1 ||
		ps7.mismatches != 1 || ps7.unused != 1 ||
		ps7.descriptor_different != 1 ||
		used_srv_sample.left_resource.width != 16 ||
		used_srv_sample.right_resource.width != 16 ||
		unused_srv_sample.left_resource.width != 16 ||
		unused_srv_sample.right_resource.width != 8)
	{
		fail("stage-slot SRV usage and resource descriptor classification");
	}
	const auto ordered_static_storage_bytes = ordered.snapshot_static_storage_bytes;
	const auto ordered_argument_sample_count = ordered.argument_sample_count;
	const auto ordered_constant_buffer_sample_count = ordered.constant_buffer_sample_count;
	const auto ordered_shader_resource_sample_count = ordered.shader_resource_sample_count;
	const auto ordered_srv_used = usage.used;
	const auto ordered_srv_unused = usage.unused;
	const auto ordered_srv_unknown = usage.unknown;
	if (!report_storage->dynamic_fx.comparison_finalized ||
		report_storage->dynamic_fx.evidence_available ||
		report_storage->dynamic_fx.comparison_complete ||
		report_storage->dynamic_fx.evidence !=
			vr::engine_stereo_gpu_census::dynamic_fx_evidence::not_observed ||
		report_storage->dynamic_fx.range_hits != 0 ||
		report_storage->dynamic_fx.semantic_hits != 0)
	{
		fail("dynamic-FX explicit no-event evidence state");
	}

	using vr::engine_stereo_gpu_census::dynamic_fx_family;
	const auto classify = vr::engine_stereo_gpu_census::classify_dynamic_fx_caller;
	if (classify(0x1407B9780ull) != dynamic_fx_family::code_trans ||
		classify(0x1407B9AFAull) != dynamic_fx_family::code_trans ||
		classify(0x1407B9AFBull) != dynamic_fx_family::unknown ||
		classify(0x1407B9B00ull) != dynamic_fx_family::glass ||
		classify(0x1407B9D75ull) != dynamic_fx_family::glass ||
		classify(0x1407B9D76ull) != dynamic_fx_family::unknown ||
		classify(0x1407B9D80ull) != dynamic_fx_family::mark ||
		classify(0x1407B9FBAull) != dynamic_fx_family::mark ||
		classify(0x1407BA395ull) != dynamic_fx_family::mark ||
		classify(0x1407BA3DAull) != dynamic_fx_family::mark ||
		classify(0x1407BA3DBull) != dynamic_fx_family::unknown ||
		classify(0x1407BA6B0ull) != dynamic_fx_family::spark ||
		classify(0x1407BA9DAull) != dynamic_fx_family::spark ||
		classify(0x1407BA9DBull) != dynamic_fx_family::unknown)
	{
		fail("exact dynamic-FX .pdata caller classification");
	}

	const auto bind_dynamic_fx_pipeline = [&](const DXGI_FORMAT index_format =
		DXGI_FORMAT_R16_UINT, const UINT index_offset = 2)
	{
		context->ClearState();
		context->VSSetShader(vertex_shader.Get(), nullptr, 0);
		context->PSSetShader(pixel_shader.Get(), nullptr, 0);
		ID3D11SamplerState* sampler = dynamic_fx_sampler.Get();
		context->PSSetSamplers(2, 1, &sampler);
		context->IASetIndexBuffer(dynamic_fx_index_buffer.Get(), index_format,
			index_offset);
		ID3D11RenderTargetView* output = draw_om.rtv.Get();
		context->OMSetRenderTargets(1, &output, nullptr);
	};
	const auto bind_dynamic_fx_binding_pipeline = [&](ID3D11Buffer* const buffer,
		ID3D11ShaderResourceView* const srv)
	{
		bind_dynamic_fx_pipeline();
		context->PSSetShader(dynamic_effect_pixel_shader.Get(), nullptr, 0);
		context->VSSetConstantBuffers(3, 1, &buffer);
		context->PSSetConstantBuffers(4, 1, &buffer);
		context->PSSetShaderResources(20, 1, &srv);
	};
	const auto make_draw_indexed_arguments = [](const std::uint32_t index_count,
		const std::uint32_t start_index, const std::int32_t base_vertex)
	{
		vr::engine_stereo_gpu_census::invocation_arguments output{};
		output.count = 3;
		output.values[0] = index_count;
		output.values[1] = start_index;
		output.values[2] = static_cast<std::uint64_t>(
			static_cast<std::int64_t>(base_vertex));
		return output;
	};
	const auto observe_dynamic_fx = [&](const std::uintptr_t caller,
		const vr::engine_stereo_gpu_census::invocation_arguments& arguments,
		const vr::engine_stereo_gpu_census::api operation =
			vr::engine_stereo_gpu_census::api::draw_indexed)
	{
		vr::engine_stereo_gpu_census::observe(context.Get(), operation, caller,
			84, reinterpret_cast<std::uintptr_t>(draw_om.rtv.Get()), 1, arguments);
	};

	// The exact consumer families are compared in family-local order. StartIndex
	// is intentionally shifted while IndexCount/BaseVertex and all bound metadata
	// remain equal, matching the real MARK evidence without performing a draw.
	if (!vr::engine_stereo_gpu_census::reset())
		fail("dynamic-FX exact reset");
	constexpr std::array<std::uintptr_t, 8> dynamic_fx_callers{
		0x1407B9780ull, 0x1407B9AFAull,
		0x1407B9B00ull, 0x1407B9D75ull,
		0x1407B9D80ull, 0x1407B9FBAull,
		0x1407BA6B0ull, 0x1407BA9DAull,
	};
	constexpr std::uint32_t expected_start_index_delta = 0x00A46000u;
	if (!vr::engine_stereo_gpu_census::begin_pair(3, context.Get()) ||
		!vr::engine_stereo_gpu_census::begin_eye(3, 0))
	{
		fail("dynamic-FX exact left begin");
	}
	bind_dynamic_fx_pipeline();
	for (std::size_t index{}; index < dynamic_fx_callers.size(); ++index)
	{
		observe_dynamic_fx(dynamic_fx_callers[index],
			make_draw_indexed_arguments(static_cast<std::uint32_t>(12 + index),
				static_cast<std::uint32_t>(100 + index * 17),
				static_cast<std::int32_t>(index) - 4));
	}
	if (!vr::engine_stereo_gpu_census::end_eye(3, 0) ||
		!vr::engine_stereo_gpu_census::begin_eye(3, 1))
	{
		fail("dynamic-FX exact right begin");
	}
	bind_dynamic_fx_pipeline();
	for (std::size_t index{}; index < dynamic_fx_callers.size(); ++index)
	{
		observe_dynamic_fx(dynamic_fx_callers[index],
			make_draw_indexed_arguments(static_cast<std::uint32_t>(12 + index),
				expected_start_index_delta +
					static_cast<std::uint32_t>(100 + index * 17),
				static_cast<std::int32_t>(index) - 4));
	}
	if (!vr::engine_stereo_gpu_census::end_eye(3, 1) ||
		!vr::engine_stereo_gpu_census::end_pair(3))
	{
		fail("dynamic-FX exact completion");
	}
	vr::engine_stereo_gpu_census::get_report(*report_storage);
	const auto& dynamic_fx = report_storage->dynamic_fx;
	const auto& dynamic_fx_stream = dynamic_fx.stream;
	if (!dynamic_fx.comparison_finalized || !dynamic_fx.evidence_available ||
		!dynamic_fx.comparison_complete ||
		dynamic_fx.evidence !=
			vr::engine_stereo_gpu_census::dynamic_fx_evidence::observed ||
		dynamic_fx.range_hits != 16 ||
		dynamic_fx.semantic_hits != 16 || dynamic_fx.invalid_invocations != 0 ||
		dynamic_fx.observation_overflows != 0 ||
		dynamic_fx.eyes[0].semantic_hits != 8 ||
		dynamic_fx.eyes[1].semantic_hits != 8)
	{
		fail("dynamic-FX aggregate exact capture");
	}
	if (!dynamic_fx_stream.finalized || !dynamic_fx_stream.complete ||
		dynamic_fx_stream.output0_observations != dynamic_fx_callers.size() ||
		dynamic_fx_stream.output1_observations != dynamic_fx_callers.size() ||
		dynamic_fx_stream.compared_ordinals != dynamic_fx_callers.size() ||
		dynamic_fx_stream.dynamic_ordinals != dynamic_fx_callers.size() ||
		dynamic_fx_stream.matched_family_invocations != dynamic_fx_callers.size() ||
		dynamic_fx_stream.family_transitions != 0 ||
		dynamic_fx_stream.semantic_mismatches != 0 ||
		dynamic_fx_stream.missing_output0 != 0 ||
		dynamic_fx_stream.missing_output1 != 0 ||
		dynamic_fx_stream.binding_comparisons != dynamic_fx_callers.size() ||
		dynamic_fx_stream.first_family_transition.captured ||
		dynamic_fx_stream.first_semantic_mismatch.captured ||
		dynamic_fx_stream.first_missing_output0.captured ||
		dynamic_fx_stream.first_missing_output1.captured)
	{
		fail("dynamic-FX global-ordinal exact stream comparison");
	}
	for (std::size_t family{}; family < dynamic_fx.families.size(); ++family)
	{
		const auto& comparison = dynamic_fx.families[family];
		const auto& left_family = dynamic_fx.eyes[0].families[family];
		const auto& right_family = dynamic_fx.eyes[1].families[family];
		if (comparison.ps_sampler_comparisons != 2 ||
			comparison.ps_sampler_identity_mismatches != 0 ||
			comparison.ps_sampler_descriptor_mismatches != 0 ||
			comparison.ps_sampler_sample_count != 0)
		{
			std::cerr << "sampler family=" << family << " compared="
				<< comparison.ps_sampler_comparisons << " identity="
				<< comparison.ps_sampler_identity_mismatches << " descriptor="
				<< comparison.ps_sampler_descriptor_mismatches << " samples="
				<< comparison.ps_sampler_sample_count << '\n';
			fail("dynamic-FX exact PS sampler comparison");
		}
		if (!comparison.comparison_finalized || !comparison.evidence_available ||
			!comparison.comparison_complete || comparison.output0_calls != 2 ||
			comparison.output1_calls != 2 || comparison.paired_calls != 2 ||
			comparison.compared_calls != 2 || comparison.missing_output0 != 0 ||
			comparison.missing_output1 != 0 ||
			comparison.index_count_mismatches != 0 ||
			comparison.base_vertex_mismatches != 0 ||
			comparison.ordinal_mismatches != 0 ||
			comparison.program_mismatches != 0 ||
			comparison.caller_mismatches != 0 ||
			comparison.output_target_mismatches != 0 ||
			comparison.input_layout_mismatches != 0 ||
			comparison.depth_state_mismatches != 0 ||
			comparison.blend_state_mismatches != 0 ||
			comparison.blend_factor_mismatches != 0 ||
			comparison.rasterizer_state_mismatches != 0 ||
			comparison.topology_mismatches != 0 ||
			comparison.viewport_mismatches != 0 ||
			comparison.scissor_mismatches != 0 ||
			comparison.index_buffer_mismatches != 0 ||
			comparison.argument_shape_mismatches != 0 ||
			comparison.start_index_delta_eligible != 2 ||
			comparison.start_index_delta_ineligible != 0 ||
			!comparison.start_index_delta_observed ||
			!comparison.start_index_delta_constant ||
			comparison.start_index_delta_first != expected_start_index_delta ||
			comparison.start_index_delta_min != expected_start_index_delta ||
			comparison.start_index_delta_max != expected_start_index_delta ||
			comparison.nonzero_start_index_deltas != 2 ||
			comparison.mismatch_sample_count != 1 ||
			left_family.observation_count != 2 ||
			right_family.observation_count != 2 ||
			left_family.zero_index_counts != 0 ||
			right_family.zero_index_counts != 0)
		{
			fail("dynamic-FX family-local constant delta comparison");
		}
		const auto& delta_sample = comparison.mismatch_samples[0];
		if (!delta_sample.output0_present || !delta_sample.output1_present ||
			delta_sample.output0.arguments.count != 3 ||
			delta_sample.output1.arguments.count != 3 ||
			static_cast<std::uint32_t>(delta_sample.output1.arguments.values[1]) -
				static_cast<std::uint32_t>(delta_sample.output0.arguments.values[1]) !=
				expected_start_index_delta)
		{
			fail("dynamic-FX first constant nonzero-delta raw sample");
		}
		const auto& left_sample = left_family.observations[0];
		const auto& right_sample = right_family.observations[0];
		if (left_sample.arguments.count != 3 ||
			left_sample.arguments.values[0] != right_sample.arguments.values[0] ||
			left_sample.arguments.values[2] != right_sample.arguments.values[2] ||
			left_sample.output_target_id != 84 ||
			left_sample.index_buffer !=
				reinterpret_cast<std::uintptr_t>(dynamic_fx_index_buffer.Get()) ||
			left_sample.index_format != DXGI_FORMAT_R16_UINT ||
			left_sample.index_offset != 2 ||
			!left_sample.index_range.exact ||
			!left_sample.index_range.hash_eligible ||
			left_sample.index_range.byte_offset !=
				static_cast<std::uint64_t>(left_sample.index_offset) +
				static_cast<std::uint32_t>(left_sample.arguments.values[1]) * 2ull ||
			left_sample.index_range.byte_count !=
				static_cast<std::uint32_t>(left_sample.arguments.values[0]) * 2ull ||
			left_sample.program.vs !=
				reinterpret_cast<std::uintptr_t>(vertex_shader.Get()) ||
			left_sample.program.ps !=
				reinterpret_cast<std::uintptr_t>(pixel_shader.Get()))
		{
			fail("dynamic-FX bounded raw/program/target/index sample");
		}
	}

	// Pipeline state that is not represented by immutable state-object identity
	// must remain observable per invocation. Deliberately vary only the dynamic
	// blend factor, viewport, and scissor between the two outputs.
	if (!vr::engine_stereo_gpu_census::reset() ||
		!vr::engine_stereo_gpu_census::begin_pair(34, context.Get()) ||
		!vr::engine_stereo_gpu_census::begin_eye(34, 0))
	{
		fail("dynamic-FX pipeline-state left begin");
	}
	bind_dynamic_fx_pipeline();
	const FLOAT left_blend_factor[4]{0.125f, 0.25f, 0.5f, 1.0f};
	context->OMSetBlendState(no_color_write_state.Get(), left_blend_factor,
		0xFFFFFFFFu);
	const D3D11_VIEWPORT left_viewport{1.0f, 2.0f, 13.0f, 14.0f, 0.0f, 1.0f};
	const D3D11_RECT left_scissor{1, 2, 14, 16};
	context->RSSetViewports(1, &left_viewport);
	context->RSSetScissorRects(1, &left_scissor);
	observe_dynamic_fx(0x1407B9FBAull, make_draw_indexed_arguments(6, 10, 0));
	if (!vr::engine_stereo_gpu_census::end_eye(34, 0) ||
		!vr::engine_stereo_gpu_census::begin_eye(34, 1))
	{
		fail("dynamic-FX pipeline-state eye transition");
	}
	bind_dynamic_fx_pipeline();
	const FLOAT right_blend_factor[4]{0.25f, 0.25f, 0.5f, 1.0f};
	context->OMSetBlendState(no_color_write_state.Get(), right_blend_factor,
		0xFFFFFFFFu);
	const D3D11_VIEWPORT right_viewport{2.0f, 2.0f, 13.0f, 14.0f, 0.0f, 1.0f};
	const D3D11_RECT right_scissor{2, 2, 14, 16};
	context->RSSetViewports(1, &right_viewport);
	context->RSSetScissorRects(1, &right_scissor);
	observe_dynamic_fx(0x1407B9FBAull, make_draw_indexed_arguments(6, 10, 0));
	if (!vr::engine_stereo_gpu_census::end_eye(34, 1) ||
		!vr::engine_stereo_gpu_census::end_pair(34))
	{
		fail("dynamic-FX pipeline-state completion");
	}
	vr::engine_stereo_gpu_census::get_report(*report_storage);
	const auto pipeline_mark_index = static_cast<std::size_t>(
		dynamic_fx_family::mark);
	const auto& pipeline_mark =
		report_storage->dynamic_fx.families[pipeline_mark_index];
	if (!pipeline_mark.comparison_complete || pipeline_mark.compared_calls != 1 ||
		pipeline_mark.input_layout_mismatches != 0 ||
		pipeline_mark.depth_state_mismatches != 0 ||
		pipeline_mark.blend_state_mismatches != 0 ||
		pipeline_mark.blend_factor_mismatches != 1 ||
		pipeline_mark.rasterizer_state_mismatches != 0 ||
		pipeline_mark.topology_mismatches != 0 ||
		pipeline_mark.viewport_mismatches != 1 ||
		pipeline_mark.scissor_mismatches != 1 ||
		pipeline_mark.mismatch_sample_count != 1 ||
		report_storage->ordered.input_layout_mismatches != 0 ||
		report_storage->ordered.blend_factor_mismatches != 1 ||
		report_storage->ordered.viewport_mismatches != 1 ||
		report_storage->ordered.scissor_mismatches != 1)
	{
		fail("dynamic-FX dynamic pipeline-state comparison");
	}

	// Dynamic-FX binding evidence is paired online by the exact global output
	// ordinal. Reuse one CB identity with different uploaded bytes and
	// one texture with two distinct SRV subresource ranges. The second invocation
	// is an exact control in the same captured pair.
	if (!vr::engine_stereo_gpu_census::reset())
		fail("dynamic-FX binding reset");
	vr::engine_stereo_constant_buffer_probe::set_history_tracking_enabled(true);
	const std::array<float, 4> alternate_constant_data{9.0f, 10.0f, 11.0f, 12.0f};
	context->UpdateSubresource(left_constant_buffer.Get(), 0, nullptr,
		left_constant_data.data(), 0, 0);
	if (!vr::engine_stereo_gpu_census::begin_pair(30, context.Get()) ||
		!vr::engine_stereo_gpu_census::begin_eye(30, 0))
	{
		fail("dynamic-FX binding left begin");
	}
	bind_dynamic_fx_binding_pipeline(left_constant_buffer.Get(),
		left_ranged_srv.Get());
	observe_dynamic_fx(0x1407B9FBAull, make_draw_indexed_arguments(6, 10, 0));
	context->UpdateSubresource(left_constant_buffer.Get(), 0, nullptr,
		right_constant_data.data(), 0, 0);
	bind_dynamic_fx_binding_pipeline(left_constant_buffer.Get(),
		left_ranged_srv.Get());
	observe_dynamic_fx(0x1407B9FBAull, make_draw_indexed_arguments(6, 20, 0));
	if (!vr::engine_stereo_gpu_census::end_eye(30, 0))
		fail("dynamic-FX binding left end");
	context->UpdateSubresource(left_constant_buffer.Get(), 0, nullptr,
		alternate_constant_data.data(), 0, 0);
	if (!vr::engine_stereo_gpu_census::begin_eye(30, 1))
		fail("dynamic-FX binding right begin");
	bind_dynamic_fx_binding_pipeline(left_constant_buffer.Get(),
		alternate_left_ranged_srv.Get());
	observe_dynamic_fx(0x1407B9FBAull, make_draw_indexed_arguments(6, 10, 0));
	context->UpdateSubresource(left_constant_buffer.Get(), 0, nullptr,
		right_constant_data.data(), 0, 0);
	bind_dynamic_fx_binding_pipeline(left_constant_buffer.Get(),
		left_ranged_srv.Get());
	observe_dynamic_fx(0x1407B9FBAull, make_draw_indexed_arguments(6, 20, 0));
	if (!vr::engine_stereo_gpu_census::end_eye(30, 1) ||
		!vr::engine_stereo_gpu_census::end_pair(30))
	{
		fail("dynamic-FX binding completion");
	}
	vr::engine_stereo_gpu_census::get_report(*report_storage);
	const auto binding_mark_index = static_cast<std::size_t>(dynamic_fx_family::mark);
	const auto& binding_dynamic_fx = report_storage->dynamic_fx;
	const auto& binding_mark = binding_dynamic_fx.families[binding_mark_index];
	if (report_storage->current_state !=
			vr::engine_stereo_gpu_census::state::complete ||
		binding_dynamic_fx.evidence !=
			vr::engine_stereo_gpu_census::dynamic_fx_evidence::observed ||
		!binding_dynamic_fx.comparison_complete ||
		!binding_mark.comparison_complete || binding_mark.output0_calls != 2 ||
		binding_mark.output1_calls != 2 ||
		binding_mark.constant_buffer_signature_mismatches != 0 ||
		binding_mark.shader_resource_signature_mismatches != 1 ||
		binding_mark.constant_buffer_slot_comparisons != 4 ||
		binding_mark.constant_buffer_identity_mismatches != 0 ||
		binding_mark.constant_buffer_content_comparisons != 4 ||
		binding_mark.constant_buffer_content_mismatches != 2 ||
		binding_mark.constant_buffer_content_unknown != 0 ||
		binding_mark.ps_shader_resource_comparisons != 2 ||
		binding_mark.ps_shader_resource_mismatches != 1 ||
		binding_mark.binding_detail_drops != 0 ||
		binding_mark.constant_buffer_sample_count != 2 ||
		binding_mark.constant_buffer_sample_overflows != 0 ||
		binding_mark.ps_shader_resource_sample_count != 1 ||
		binding_mark.ps_shader_resource_sample_overflows != 0 ||
		binding_mark.mismatch_sample_count != 0)
	{
		fail("dynamic-FX exact binding/content comparison");
	}
	const auto& dynamic_cb_sample = binding_mark.constant_buffer_samples[0];
	if (dynamic_cb_sample.family_ordinal != 1 || dynamic_cb_sample.stage != 0 ||
		dynamic_cb_sample.slot != 3 || !dynamic_cb_sample.output0_present ||
		!dynamic_cb_sample.output1_present || dynamic_cb_sample.identity_mismatch ||
		dynamic_cb_sample.content_unknown || !dynamic_cb_sample.content_mismatch ||
		dynamic_cb_sample.output0.identity !=
			reinterpret_cast<std::uintptr_t>(left_constant_buffer.Get()) ||
		dynamic_cb_sample.output1.identity !=
			reinterpret_cast<std::uintptr_t>(left_constant_buffer.Get()) ||
		!dynamic_cb_sample.output0.content.known ||
		!dynamic_cb_sample.output1.content.known ||
		(dynamic_cb_sample.output0.content.hash_low ==
			dynamic_cb_sample.output1.content.hash_low &&
		 dynamic_cb_sample.output0.content.hash_high ==
			dynamic_cb_sample.output1.content.hash_high))
	{
		fail("dynamic-FX same-CB identity content mismatch sample");
	}
	const auto& dynamic_ps_cb_sample = binding_mark.constant_buffer_samples[1];
	if (dynamic_ps_cb_sample.family_ordinal != 1 ||
		dynamic_ps_cb_sample.stage != 1 || dynamic_ps_cb_sample.slot != 4 ||
		!dynamic_ps_cb_sample.output0_present ||
		!dynamic_ps_cb_sample.output1_present ||
		dynamic_ps_cb_sample.identity_mismatch ||
		dynamic_ps_cb_sample.content_unknown ||
		!dynamic_ps_cb_sample.content_mismatch)
	{
		fail("dynamic-FX PS constant-buffer content mismatch sample");
	}
	const auto& dynamic_vs_reflection = dynamic_cb_sample.output0_reflection;
	const auto& dynamic_ps_reflection = dynamic_ps_cb_sample.output0_reflection;
	if (!dynamic_vs_reflection.attempted || !dynamic_vs_reflection.available ||
		!dynamic_vs_reflection.binding_declared || !dynamic_vs_reflection.complete ||
		dynamic_vs_reflection.shader !=
			reinterpret_cast<std::uintptr_t>(vertex_shader.Get()) ||
		std::string_view(dynamic_vs_reflection.buffer_name.data()) !=
			"ProbeConstants" || dynamic_vs_reflection.buffer_size != 16 ||
		dynamic_vs_reflection.variable_count != 1 ||
		std::string_view(dynamic_vs_reflection.variables[0].name.data()) !=
			"probeOffset" || dynamic_vs_reflection.variables[0].start_offset != 0 ||
		dynamic_vs_reflection.variables[0].size != 16 ||
		dynamic_vs_reflection.variables[0].differing_bytes == 0 ||
		dynamic_vs_reflection.mapped_differing_bytes !=
			dynamic_cb_sample.byte_difference_count ||
		dynamic_vs_reflection.unmapped_differing_bytes != 0 ||
		dynamic_vs_reflection.variable_overflows != 0)
	{
		fail("dynamic-FX VS constant-buffer variable reflection");
	}
	if (!dynamic_ps_reflection.attempted || !dynamic_ps_reflection.available ||
		!dynamic_ps_reflection.binding_declared || !dynamic_ps_reflection.complete ||
		dynamic_ps_reflection.shader !=
			reinterpret_cast<std::uintptr_t>(dynamic_effect_pixel_shader.Get()) ||
		std::string_view(dynamic_ps_reflection.buffer_name.data()) !=
			"EffectConstants" || dynamic_ps_reflection.buffer_size != 16 ||
		dynamic_ps_reflection.variable_count != 1 ||
		std::string_view(dynamic_ps_reflection.variables[0].name.data()) !=
			"atlasOpacity" || dynamic_ps_reflection.variables[0].start_offset != 0 ||
		dynamic_ps_reflection.variables[0].size != 16 ||
		dynamic_ps_reflection.variables[0].differing_bytes == 0 ||
		dynamic_ps_reflection.mapped_differing_bytes !=
			dynamic_ps_cb_sample.byte_difference_count ||
		dynamic_ps_reflection.unmapped_differing_bytes != 0 ||
		dynamic_ps_reflection.variable_overflows != 0 ||
		binding_dynamic_fx.reflection_attempts < 2 ||
		binding_dynamic_fx.reflection_completions < 2 ||
		binding_dynamic_fx.reflection_bytecode_missing != 0 ||
		binding_dynamic_fx.reflection_bytecode_oversized != 0 ||
		binding_dynamic_fx.reflection_failures != 0 ||
		binding_dynamic_fx.reflection_stage_mismatches != 0 ||
		binding_dynamic_fx.reflection_buffer_overflows != 0 ||
		binding_dynamic_fx.reflection_variable_overflows != 0)
	{
		fail("dynamic-FX PS constant-buffer variable reflection");
	}
	const auto& dynamic_srv_sample = binding_mark.ps_shader_resource_samples[0];
	if (dynamic_srv_sample.family_ordinal != 1 || dynamic_srv_sample.slot != 20 ||
		!dynamic_srv_sample.output0_present || !dynamic_srv_sample.output1_present ||
		dynamic_srv_sample.output0.binding.resource !=
			reinterpret_cast<std::uintptr_t>(left_ranged_texture.Get()) ||
		dynamic_srv_sample.output1.binding.resource !=
			reinterpret_cast<std::uintptr_t>(left_ranged_texture.Get()) ||
		dynamic_srv_sample.output0.binding.view ==
			dynamic_srv_sample.output1.binding.view ||
		dynamic_srv_sample.output0.binding.range.most_detailed_mip != 0 ||
		dynamic_srv_sample.output1.binding.range.most_detailed_mip != 1 ||
		dynamic_srv_sample.output0.binding.range.first_array_slice != 0 ||
		dynamic_srv_sample.output1.binding.range.first_array_slice != 1)
	{
		fail("dynamic-FX same-resource PS SRV range mismatch sample");
	}

	// A bound CB with no complete observed upload must not be guessed equal merely
	// because the COM identity is shared across outputs.
	if (!vr::engine_stereo_gpu_census::reset())
		fail("dynamic-FX unknown-content reset");
	vr::engine_stereo_constant_buffer_probe::set_history_tracking_enabled(true);
	if (!vr::engine_stereo_gpu_census::begin_pair(31, context.Get()) ||
		!vr::engine_stereo_gpu_census::begin_eye(31, 0))
	{
		fail("dynamic-FX unknown-content left begin");
	}
	bind_dynamic_fx_binding_pipeline(right_constant_buffer.Get(),
		left_ranged_srv.Get());
	observe_dynamic_fx(0x1407B9FBAull, make_draw_indexed_arguments(6, 10, 0));
	if (!vr::engine_stereo_gpu_census::end_eye(31, 0) ||
		!vr::engine_stereo_gpu_census::begin_eye(31, 1))
	{
		fail("dynamic-FX unknown-content eye transition");
	}
	bind_dynamic_fx_binding_pipeline(right_constant_buffer.Get(),
		left_ranged_srv.Get());
	observe_dynamic_fx(0x1407B9FBAull, make_draw_indexed_arguments(6, 10, 0));
	if (!vr::engine_stereo_gpu_census::end_eye(31, 1) ||
		!vr::engine_stereo_gpu_census::end_pair(31))
	{
		fail("dynamic-FX unknown-content completion");
	}
	vr::engine_stereo_gpu_census::get_report(*report_storage);
	const auto& unknown_dynamic_fx = report_storage->dynamic_fx;
	const auto& unknown_mark = unknown_dynamic_fx.families[binding_mark_index];
	if (report_storage->current_state !=
			vr::engine_stereo_gpu_census::state::complete ||
		unknown_dynamic_fx.evidence !=
			vr::engine_stereo_gpu_census::dynamic_fx_evidence::incomplete ||
		unknown_dynamic_fx.comparison_complete || unknown_mark.comparison_complete ||
		unknown_mark.constant_buffer_slot_comparisons != 2 ||
		unknown_mark.constant_buffer_identity_mismatches != 0 ||
		unknown_mark.constant_buffer_content_comparisons != 0 ||
		unknown_mark.constant_buffer_content_mismatches != 0 ||
		unknown_mark.constant_buffer_content_unknown != 2 ||
		unknown_mark.constant_buffer_sample_count != 2 ||
		!unknown_mark.constant_buffer_samples[0].content_unknown ||
		unknown_mark.constant_buffer_samples[0].content_mismatch ||
		!unknown_mark.constant_buffer_samples[1].content_unknown ||
		unknown_mark.constant_buffer_samples[1].content_mismatch ||
		unknown_mark.binding_detail_drops != 0)
	{
		fail("dynamic-FX unknown CB content is incomplete evidence");
	}

	// Range hits with a wrong API or raw-argument shape are explicit invalid
	// records. They must not make a zero-semantic result look like no census ran.
	if (!vr::engine_stereo_gpu_census::reset() ||
		!vr::engine_stereo_gpu_census::begin_pair(4, context.Get()) ||
		!vr::engine_stereo_gpu_census::begin_eye(4, 0))
	{
		fail("dynamic-FX invalid left begin");
	}
	bind_dynamic_fx_pipeline();
	observe_dynamic_fx(0x1407B9780ull, {},
		vr::engine_stereo_gpu_census::api::draw);
	if (!vr::engine_stereo_gpu_census::end_eye(4, 0) ||
		!vr::engine_stereo_gpu_census::begin_eye(4, 1))
	{
		fail("dynamic-FX invalid right begin");
	}
	bind_dynamic_fx_pipeline();
	auto invalid_arguments = make_draw_indexed_arguments(4, 8, 0);
	invalid_arguments.count = 2;
	observe_dynamic_fx(0x1407B9780ull, invalid_arguments);
	if (!vr::engine_stereo_gpu_census::end_eye(4, 1) ||
		!vr::engine_stereo_gpu_census::end_pair(4))
	{
		fail("dynamic-FX invalid completion");
	}
	vr::engine_stereo_gpu_census::get_report(*report_storage);
	if (report_storage->dynamic_fx.range_hits != 2 ||
		report_storage->dynamic_fx.semantic_hits != 0 ||
		report_storage->dynamic_fx.invalid_invocations != 2 ||
		!report_storage->dynamic_fx.comparison_finalized ||
		report_storage->dynamic_fx.evidence_available ||
		report_storage->dynamic_fx.comparison_complete ||
		report_storage->dynamic_fx.evidence !=
			vr::engine_stereo_gpu_census::dynamic_fx_evidence::invalid_shape ||
		report_storage->dynamic_fx.eyes[0].families[0].range_hits != 1 ||
		report_storage->dynamic_fx.eyes[1].families[0].range_hits != 1 ||
		report_storage->dynamic_fx.eyes[0].families[0].invalid_invocations != 1 ||
		report_storage->dynamic_fx.eyes[1].families[0].invalid_invocations != 1)
	{
		fail("dynamic-FX explicit semantic_hits=0");
	}

	// A zero-index MARK call on the left with only an unclassified right draw
	// proves zero-count and missing-right accounting without failing the pair.
	if (!vr::engine_stereo_gpu_census::reset() ||
		!vr::engine_stereo_gpu_census::begin_pair(5, context.Get()) ||
		!vr::engine_stereo_gpu_census::begin_eye(5, 0))
	{
		fail("dynamic-FX missing-right left begin");
	}
	bind_dynamic_fx_pipeline();
	observe_dynamic_fx(0x1407B9FBAull, make_draw_indexed_arguments(0, 11, 2));
	if (!vr::engine_stereo_gpu_census::end_eye(5, 0) ||
		!vr::engine_stereo_gpu_census::begin_eye(5, 1))
	{
		fail("dynamic-FX missing-right right begin");
	}
	bind_dynamic_fx_pipeline();
	observe_dynamic_fx(0x1407BA3DBull, make_draw_indexed_arguments(0, 11, 2));
	if (!vr::engine_stereo_gpu_census::end_eye(5, 1) ||
		!vr::engine_stereo_gpu_census::end_pair(5))
	{
		fail("dynamic-FX missing-right completion");
	}
	vr::engine_stereo_gpu_census::get_report(*report_storage);
	const auto& missing_dynamic_fx = report_storage->dynamic_fx;
	const auto mark_index = static_cast<std::size_t>(dynamic_fx_family::mark);
	const auto& missing_mark = missing_dynamic_fx.families[mark_index];
	if (missing_dynamic_fx.semantic_hits != 1 ||
		missing_dynamic_fx.evidence !=
			vr::engine_stereo_gpu_census::dynamic_fx_evidence::incomplete ||
		missing_dynamic_fx.comparison_complete || missing_mark.comparison_complete ||
		missing_dynamic_fx.eyes[0].families[mark_index].zero_index_counts != 1 ||
		missing_mark.output0_calls != 1 || missing_mark.output1_calls != 0 ||
		missing_mark.missing_output1 != 1 || missing_mark.missing_output0 != 0 ||
		missing_mark.start_index_delta_observed ||
		missing_mark.mismatch_sample_count != 1 ||
		!missing_mark.mismatch_samples[0].output0_present ||
		missing_mark.mismatch_samples[0].output1_present ||
		missing_mark.mismatch_samples[0].output0.caller != 0x1407B9FBAull)
	{
		fail("dynamic-FX zero-count/missing-right accounting");
	}

	// Raw mismatch samples are intentionally bounded, but losing them makes the
	// diagnostic evidence incomplete even when all invocation counters fit.
	constexpr auto mismatch_overflow_calls =
		vr::engine_stereo_gpu_census::maximum_dynamic_fx_mismatch_samples + 1;
	if (!vr::engine_stereo_gpu_census::reset() ||
		!vr::engine_stereo_gpu_census::begin_pair(32, context.Get()) ||
		!vr::engine_stereo_gpu_census::begin_eye(32, 0))
	{
		fail("dynamic-FX mismatch-sample overflow left begin");
	}
	bind_dynamic_fx_pipeline();
	for (std::size_t index{}; index < mismatch_overflow_calls; ++index)
	{
		observe_dynamic_fx(0x1407B9FBAull, make_draw_indexed_arguments(3,
			static_cast<std::uint32_t>(index), 0));
	}
	if (!vr::engine_stereo_gpu_census::end_eye(32, 0) ||
		!vr::engine_stereo_gpu_census::begin_eye(32, 1))
	{
		fail("dynamic-FX mismatch-sample overflow right begin");
	}
	bind_dynamic_fx_pipeline();
	for (std::size_t index{}; index < mismatch_overflow_calls; ++index)
	{
		observe_dynamic_fx(0x1407B9FBAull, make_draw_indexed_arguments(4,
			static_cast<std::uint32_t>(index), 0));
	}
	if (!vr::engine_stereo_gpu_census::end_eye(32, 1) ||
		!vr::engine_stereo_gpu_census::end_pair(32))
	{
		fail("dynamic-FX mismatch-sample overflow completion");
	}
	vr::engine_stereo_gpu_census::get_report(*report_storage);
	const auto& mismatch_overflow_dynamic_fx = report_storage->dynamic_fx;
	const auto& mismatch_overflow_mark =
		mismatch_overflow_dynamic_fx.families[mark_index];
	if (mismatch_overflow_dynamic_fx.evidence !=
			vr::engine_stereo_gpu_census::dynamic_fx_evidence::incomplete ||
		mismatch_overflow_dynamic_fx.comparison_complete ||
		mismatch_overflow_dynamic_fx.observation_overflows != 0 ||
		mismatch_overflow_mark.comparison_complete ||
		mismatch_overflow_mark.compared_calls != mismatch_overflow_calls ||
		mismatch_overflow_mark.index_count_mismatches != mismatch_overflow_calls ||
		mismatch_overflow_mark.mismatch_sample_count !=
			vr::engine_stereo_gpu_census::maximum_dynamic_fx_mismatch_samples ||
		mismatch_overflow_mark.mismatch_sample_overflows != 1)
	{
		fail("dynamic-FX mismatch-sample overflow is incomplete evidence");
	}

	// The fixed sample cap is diagnostic-only. Total calls/count mismatches stay
	// valid, while sequence/delta evidence becomes explicitly truncated and must
	// not fail the enclosing GPU census transaction.
	if (!vr::engine_stereo_gpu_census::reset() ||
		!vr::engine_stereo_gpu_census::begin_pair(6, context.Get()) ||
		!vr::engine_stereo_gpu_census::begin_eye(6, 0))
	{
		fail("dynamic-FX bounded left begin");
	}
	bind_dynamic_fx_pipeline();
	for (std::size_t index{};
		index <= vr::engine_stereo_gpu_census::maximum_dynamic_fx_observations_per_family;
		++index)
	{
		observe_dynamic_fx(0x1407B9FBAull, make_draw_indexed_arguments(3,
			static_cast<std::uint32_t>(index), 0));
	}
	if (!vr::engine_stereo_gpu_census::end_eye(6, 0) ||
		!vr::engine_stereo_gpu_census::begin_eye(6, 1))
	{
		fail("dynamic-FX bounded right begin");
	}
	bind_dynamic_fx_pipeline();
	for (std::size_t index{};
		index <= vr::engine_stereo_gpu_census::maximum_dynamic_fx_observations_per_family;
		++index)
	{
		observe_dynamic_fx(0x1407B9FBAull, make_draw_indexed_arguments(3,
			static_cast<std::uint32_t>(index + 5), 0));
	}
	if (!vr::engine_stereo_gpu_census::end_eye(6, 1) ||
		!vr::engine_stereo_gpu_census::end_pair(6))
	{
		fail("dynamic-FX bounded completion");
	}
	vr::engine_stereo_gpu_census::get_report(*report_storage);
	const auto& bounded_dynamic_fx = report_storage->dynamic_fx;
	const auto& bounded_mark = bounded_dynamic_fx.families[mark_index];
	const auto expected_bounded_calls =
		vr::engine_stereo_gpu_census::maximum_dynamic_fx_observations_per_family + 1;
	if (report_storage->current_state !=
			vr::engine_stereo_gpu_census::state::complete ||
		bounded_dynamic_fx.evidence !=
			vr::engine_stereo_gpu_census::dynamic_fx_evidence::truncated ||
		!bounded_dynamic_fx.comparison_finalized ||
		!bounded_dynamic_fx.evidence_available ||
		bounded_dynamic_fx.comparison_complete ||
		bounded_dynamic_fx.observation_overflows != 2 ||
		bounded_mark.output0_calls != expected_bounded_calls ||
		bounded_mark.output1_calls != expected_bounded_calls ||
		bounded_mark.paired_calls != expected_bounded_calls ||
		bounded_mark.compared_calls !=
			vr::engine_stereo_gpu_census::maximum_dynamic_fx_observations_per_family ||
		bounded_mark.comparison_complete ||
		bounded_mark.start_index_delta_eligible !=
			vr::engine_stereo_gpu_census::maximum_dynamic_fx_observations_per_family ||
		bounded_mark.nonzero_start_index_deltas !=
			vr::engine_stereo_gpu_census::maximum_dynamic_fx_observations_per_family ||
		!bounded_mark.start_index_delta_constant ||
		bounded_mark.start_index_delta_first != 5 ||
		bounded_mark.mismatch_sample_count != 1 ||
		bounded_dynamic_fx.eyes[0].families[mark_index].observation_overflows != 1 ||
		bounded_dynamic_fx.eyes[1].families[mark_index].observation_overflows != 1)
	{
		fail("dynamic-FX bounded non-fatal truncation");
	}

	// Family-local raw storage is deliberately bounded, but the left eye's
	// complete global signature stream remains available. Put the first stream
	// divergence after that cap and prove online global-ordinal comparison still
	// identifies a cross-family substitution plus one missing call on each side,
	// with the immediately adjacent ordinals retained for alignment diagnosis.
	constexpr std::size_t stream_alignment_calls =
		vr::engine_stereo_gpu_census::maximum_dynamic_fx_observations_per_family + 5;
	constexpr std::size_t family_sample_cap =
		vr::engine_stereo_gpu_census::maximum_dynamic_fx_observations_per_family;
	const auto stream_caller = [](const std::size_t ordinal, const bool right)
	{
		if (!right)
		{
			if (ordinal == family_sample_cap + 3) return 0x1407BA3DBull;
			if (ordinal == family_sample_cap + 4) return 0x1407B9B00ull;
			return 0x1407B9FBAull;
		}
		if (ordinal == family_sample_cap + 1) return 0x1407B9B00ull;
		if (ordinal == family_sample_cap + 2) return 0x1407BA3DBull;
		if (ordinal == family_sample_cap + 4) return 0x1407B9B00ull;
		return 0x1407B9FBAull;
	};
	if (!vr::engine_stereo_gpu_census::reset() ||
		!vr::engine_stereo_gpu_census::begin_pair(33, context.Get()) ||
		!vr::engine_stereo_gpu_census::begin_eye(33, 0))
	{
		fail("dynamic-FX global stream left begin");
	}
	bind_dynamic_fx_pipeline();
	for (std::size_t ordinal = 1; ordinal <= stream_alignment_calls; ++ordinal)
	{
		if (ordinal == family_sample_cap + 5)
		{
			context->UpdateSubresource(left_constant_buffer.Get(), 0, nullptr,
				left_constant_data.data(), 0, 0);
			bind_dynamic_fx_binding_pipeline(left_constant_buffer.Get(),
				left_ranged_srv.Get());
		}
		observe_dynamic_fx(stream_caller(ordinal, false),
			make_draw_indexed_arguments(3, static_cast<std::uint32_t>(ordinal), 0));
	}
	if (!vr::engine_stereo_gpu_census::end_eye(33, 0) ||
		!vr::engine_stereo_gpu_census::begin_eye(33, 1))
	{
		fail("dynamic-FX global stream eye transition");
	}
	bind_dynamic_fx_pipeline();
	for (std::size_t ordinal = 1; ordinal <= stream_alignment_calls; ++ordinal)
	{
		if (ordinal == family_sample_cap + 5)
		{
			context->UpdateSubresource(left_constant_buffer.Get(), 0, nullptr,
				right_constant_data.data(), 0, 0);
			bind_dynamic_fx_binding_pipeline(left_constant_buffer.Get(),
				left_ranged_srv.Get());
		}
		observe_dynamic_fx(stream_caller(ordinal, true),
			make_draw_indexed_arguments(3, static_cast<std::uint32_t>(ordinal), 0));
	}
	if (!vr::engine_stereo_gpu_census::end_eye(33, 1) ||
		!vr::engine_stereo_gpu_census::end_pair(33))
	{
		fail("dynamic-FX global stream completion");
	}
	vr::engine_stereo_gpu_census::get_report(*report_storage);
	const auto& aligned_dynamic_fx = report_storage->dynamic_fx;
	const auto& aligned_stream = aligned_dynamic_fx.stream;
	const auto glass_index = static_cast<std::size_t>(dynamic_fx_family::glass);
	if (report_storage->current_state !=
			vr::engine_stereo_gpu_census::state::complete ||
		aligned_dynamic_fx.evidence !=
			vr::engine_stereo_gpu_census::dynamic_fx_evidence::truncated ||
		!aligned_stream.finalized || !aligned_stream.complete ||
		aligned_stream.output0_observations != stream_alignment_calls ||
		aligned_stream.output1_observations != stream_alignment_calls ||
		aligned_stream.compared_ordinals != stream_alignment_calls ||
		aligned_stream.dynamic_ordinals != stream_alignment_calls ||
		aligned_stream.matched_family_invocations != family_sample_cap + 2 ||
		aligned_stream.family_transitions != 1 ||
		aligned_stream.semantic_mismatches != 0 ||
		aligned_stream.missing_output0 != 1 ||
		aligned_stream.missing_output1 != 1 ||
		aligned_stream.binding_comparisons != family_sample_cap + 2 ||
		aligned_dynamic_fx.families[mark_index].global_ordinal_matches !=
			family_sample_cap + 1 ||
		aligned_dynamic_fx.families[glass_index].global_ordinal_matches != 1 ||
		aligned_dynamic_fx.families[mark_index].constant_buffer_content_mismatches !=
			2 ||
		aligned_dynamic_fx.families[mark_index].constant_buffer_sample_count != 2 ||
		aligned_dynamic_fx.families[mark_index].constant_buffer_samples[0].
			output_ordinal != family_sample_cap + 5 ||
		aligned_dynamic_fx.families[mark_index].constant_buffer_samples[0].
			output0_family_ordinal != family_sample_cap + 3 ||
		aligned_dynamic_fx.families[mark_index].constant_buffer_samples[0].
			output1_family_ordinal != family_sample_cap + 2)
	{
		fail("dynamic-FX global stream survives family sample cap");
	}
	const auto verify_stream_window = [](const auto& window,
		const std::uint64_t event_ordinal,
		const std::array<dynamic_fx_family, 3>& left_families,
		const std::array<dynamic_fx_family, 3>& right_families)
	{
		if (!window.captured || window.event_output_ordinal != event_ordinal)
			return false;
		for (std::size_t slot{}; slot < window.ordinals.size(); ++slot)
		{
			const auto expected_ordinal = event_ordinal + slot - 1;
			const auto& value = window.ordinals[slot];
			if (value.output_ordinal != expected_ordinal ||
				!value.outputs[0].present || !value.outputs[1].present ||
				value.outputs[0].invocation.output_ordinal != expected_ordinal ||
				value.outputs[1].invocation.output_ordinal != expected_ordinal ||
				value.outputs[0].family != left_families[slot] ||
				value.outputs[1].family != right_families[slot])
			{
				return false;
			}
		}
		return true;
	};
	if (!verify_stream_window(aligned_stream.first_family_transition,
			family_sample_cap + 1,
			{dynamic_fx_family::mark, dynamic_fx_family::mark,
				dynamic_fx_family::mark},
			{dynamic_fx_family::mark, dynamic_fx_family::glass,
				dynamic_fx_family::unknown}) ||
		!verify_stream_window(aligned_stream.first_missing_output1,
			family_sample_cap + 2,
			{dynamic_fx_family::mark, dynamic_fx_family::mark,
				dynamic_fx_family::unknown},
			{dynamic_fx_family::glass, dynamic_fx_family::unknown,
				dynamic_fx_family::mark}) ||
		!verify_stream_window(aligned_stream.first_missing_output0,
			family_sample_cap + 3,
			{dynamic_fx_family::mark, dynamic_fx_family::unknown,
				dynamic_fx_family::glass},
			{dynamic_fx_family::unknown, dynamic_fx_family::mark,
				dynamic_fx_family::glass}) ||
		aligned_stream.first_semantic_mismatch.captured)
	{
		fail("dynamic-FX first divergence three-ordinal windows");
	}

	if (!vr::engine_stereo_gpu_census::reset() ||
		!vr::engine_stereo_gpu_census::begin_pair(7, context.Get()) ||
		!vr::engine_stereo_gpu_census::begin_eye(7, 0))
	{
		fail("dynamic-FX zero-delta left begin");
	}
	bind_dynamic_fx_pipeline();
	observe_dynamic_fx(0x1407B9FBAull, make_draw_indexed_arguments(3, 7, 0));
	if (!vr::engine_stereo_gpu_census::end_eye(7, 0) ||
		!vr::engine_stereo_gpu_census::begin_eye(7, 1))
	{
		fail("dynamic-FX zero-delta right begin");
	}
	bind_dynamic_fx_pipeline();
	observe_dynamic_fx(0x1407B9FBAull, make_draw_indexed_arguments(3, 7, 0));
	if (!vr::engine_stereo_gpu_census::end_eye(7, 1) ||
		!vr::engine_stereo_gpu_census::end_pair(7))
	{
		fail("dynamic-FX zero-delta completion");
	}
	vr::engine_stereo_gpu_census::get_report(*report_storage);
	const auto& zero_delta_mark = report_storage->dynamic_fx.families[mark_index];
	if (!zero_delta_mark.start_index_delta_observed ||
		!zero_delta_mark.start_index_delta_constant ||
		zero_delta_mark.start_index_delta_first != 0 ||
		zero_delta_mark.nonzero_start_index_deltas != 0 ||
		zero_delta_mark.mismatch_sample_count != 0)
	{
		fail("dynamic-FX zero delta is not anomalous");
	}

	// A raw StartIndex difference is not comparable when output target or IA
	// interpretation differs. Cover target mismatch, format+offset mismatch, and
	// offset-only mismatch independently; none may enter nonzero-delta evidence.
	if (!vr::engine_stereo_gpu_census::reset() ||
		!vr::engine_stereo_gpu_census::begin_pair(8, context.Get()) ||
		!vr::engine_stereo_gpu_census::begin_eye(8, 0))
	{
		fail("dynamic-FX delta-ineligible left begin");
	}
	bind_dynamic_fx_pipeline();
	for (std::uint32_t index{}; index < 3; ++index)
	{
		observe_dynamic_fx(0x1407B9FBAull,
			make_draw_indexed_arguments(6, 10 + index * 10, -2));
	}
	if (!vr::engine_stereo_gpu_census::end_eye(8, 0) ||
		!vr::engine_stereo_gpu_census::begin_eye(8, 1))
	{
		fail("dynamic-FX delta-ineligible right begin");
	}
	bind_dynamic_fx_pipeline();
	vr::engine_stereo_gpu_census::observe(context.Get(),
		vr::engine_stereo_gpu_census::api::draw_indexed, 0x1407B9FBAull,
		85, reinterpret_cast<std::uintptr_t>(draw_om.rtv.Get()), 1,
		make_draw_indexed_arguments(6, 110, -2));
	bind_dynamic_fx_pipeline(DXGI_FORMAT_R32_UINT, 4);
	observe_dynamic_fx(0x1407B9FBAull, make_draw_indexed_arguments(6, 120, -2));
	bind_dynamic_fx_pipeline(DXGI_FORMAT_R16_UINT, 4);
	observe_dynamic_fx(0x1407B9FBAull, make_draw_indexed_arguments(6, 130, -2));
	if (!vr::engine_stereo_gpu_census::end_eye(8, 1) ||
		!vr::engine_stereo_gpu_census::end_pair(8))
	{
		fail("dynamic-FX delta-ineligible completion");
	}
	vr::engine_stereo_gpu_census::get_report(*report_storage);
	const auto& ineligible_mark = report_storage->dynamic_fx.families[mark_index];
	if (ineligible_mark.output0_calls != 3 || ineligible_mark.output1_calls != 3 ||
		ineligible_mark.start_index_delta_eligible != 0 ||
		ineligible_mark.start_index_delta_ineligible != 3 ||
		ineligible_mark.nonzero_start_index_deltas != 0 ||
		ineligible_mark.start_index_delta_observed ||
		ineligible_mark.output_target_mismatches != 1 ||
		ineligible_mark.index_buffer_mismatches != 2 ||
		ineligible_mark.mismatch_sample_count != 3)
	{
		fail("dynamic-FX target/IA delta eligibility gate");
	}

	// The arena layout expectations are intentionally independent of the
	// production constants. Each mesh is an opaque 0x38-byte/7-qword record;
	// the test never interprets or follows its contents.
	constexpr std::uintptr_t test_code_trans_offset = 0x5409C0ull;
	constexpr std::uintptr_t test_mesh_stride = 0x58ull;
	constexpr std::uintptr_t test_glass_offset = 0x540B78ull;
	constexpr std::uintptr_t test_mark_offset = 0x540BD0ull;
	constexpr std::uintptr_t test_spark_offset = 0x540C28ull;
	constexpr std::size_t test_mesh_bytes = 0x38;
	constexpr std::array<std::uintptr_t, 8> test_mesh_offsets{
		test_code_trans_offset + 0 * test_mesh_stride,
		test_code_trans_offset + 1 * test_mesh_stride,
		test_code_trans_offset + 2 * test_mesh_stride,
		test_code_trans_offset + 3 * test_mesh_stride,
		test_code_trans_offset + 4 * test_mesh_stride,
		test_glass_offset, test_mark_offset, test_spark_offset,
	};
	static_assert(test_code_trans_offset + 5 * test_mesh_stride ==
		test_glass_offset);
	static_assert(test_glass_offset + test_mesh_stride == test_mark_offset);
	static_assert(test_mark_offset + test_mesh_stride == test_spark_offset);
	static_assert(test_mesh_bytes == 7 * sizeof(std::uint64_t));

	SYSTEM_INFO system_information{};
	GetSystemInfo(&system_information);
	const auto page_size = static_cast<std::size_t>(
		system_information.dwPageSize);
	const auto align_pages = [page_size](const std::size_t size)
	{
		return (size + page_size - 1) & ~(page_size - 1);
	};
	const auto arena_data_bytes = align_pages(static_cast<std::size_t>(
		test_spark_offset + test_mesh_stride + page_size));
	const auto allocate_rw = [](const std::size_t size)
	{
		return static_cast<std::uint8_t*>(VirtualAlloc(nullptr, size,
			MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
	};
	auto* const arena_data_a = allocate_rw(arena_data_bytes);
	auto* const arena_data_b = allocate_rw(arena_data_bytes);
	auto* const backend_state_a = allocate_rw(0x3000);
	auto* const backend_state_b = allocate_rw(0x3000);
	if (!arena_data_a || !arena_data_b || !backend_state_a || !backend_state_b)
		fail("dynamic-FX arena fake committed allocation");

	constexpr std::uint64_t canary_seed = 0xC4A4A00000000000ull;
	const auto seed_arena = [&](std::uint8_t* const data,
		const std::uint64_t seed)
	{
		for (std::size_t mesh{}; mesh < test_mesh_offsets.size(); ++mesh)
		{
			for (std::size_t qword{}; qword < 7; ++qword)
			{
				const auto value = seed | (mesh << 8) | qword;
				std::memcpy(data + test_mesh_offsets[mesh] + qword * sizeof(value),
					&value, sizeof(value));
			}
			const auto canary0 = canary_seed | (mesh << 8) | 0x38;
			const auto canary1 = canary_seed | (mesh << 8) | 0x50;
			std::memcpy(data + test_mesh_offsets[mesh] + 0x38,
				&canary0, sizeof(canary0));
			std::memcpy(data + test_mesh_offsets[mesh] + 0x50,
				&canary1, sizeof(canary1));
		}
	};
	const auto verify_canaries = [&](const std::uint8_t* const data)
	{
		for (std::size_t mesh{}; mesh < test_mesh_offsets.size(); ++mesh)
		{
			std::uint64_t canary0{}, canary1{};
			std::memcpy(&canary0, data + test_mesh_offsets[mesh] + 0x38,
				sizeof(canary0));
			std::memcpy(&canary1, data + test_mesh_offsets[mesh] + 0x50,
				sizeof(canary1));
			if (canary0 != (canary_seed | (mesh << 8) | 0x38) ||
				canary1 != (canary_seed | (mesh << 8) | 0x50))
			{
				return false;
			}
		}
		return true;
	};
	seed_arena(arena_data_a, 0xA100000000000000ull);
	seed_arena(arena_data_b, 0xB200000000000000ull);
	std::uintptr_t global_data_identity = reinterpret_cast<std::uintptr_t>(
		arena_data_a);
	std::memcpy(backend_state_a + 0x2BE8, &global_data_identity,
		sizeof(global_data_identity));
	auto backend_data_identity = reinterpret_cast<std::uintptr_t>(arena_data_b);
	std::memcpy(backend_state_b + 0x2BE8, &backend_data_identity,
		sizeof(backend_data_identity));
	using arena_phase = vr::engine_stereo_gpu_census::dynamic_fx_arena_phase;
	const auto note_arena = [&](const std::uint64_t pair, const std::uint32_t output,
		const arena_phase phase, const std::uintptr_t pointer_slot,
		void* const backend_state = nullptr)
	{
		vr::engine_stereo_gpu_census::note_dynamic_fx_arena_boundary_for_test(
			pair, output, phase, 0x11110000ull + output, pointer_slot,
			backend_state);
	};

	// Capture same and different backend/global identities in one exact pair,
	// retain MARK/SPARK raw churn across the output boundary, and prove each
	// invocation links to the nearest preceding source-matching view copy.
	if (!vr::engine_stereo_gpu_census::reset() ||
		!vr::engine_stereo_gpu_census::begin_pair(9, context.Get()) ||
		!vr::engine_stereo_gpu_census::begin_eye(9, 0))
	{
		fail("dynamic-FX arena identity/churn output0 begin");
	}
	note_arena(9, 0, arena_phase::eye_begin,
		reinterpret_cast<std::uintptr_t>(&global_data_identity));
	note_arena(9, 0, arena_phase::backend_view_copy,
		reinterpret_cast<std::uintptr_t>(&global_data_identity), backend_state_a);
	bind_dynamic_fx_pipeline();
	observe_dynamic_fx(0x1407B9FBAull, make_draw_indexed_arguments(3, 10, 0));
	note_arena(9, 0, arena_phase::backend_view_copy,
		reinterpret_cast<std::uintptr_t>(&global_data_identity), backend_state_a);
	observe_dynamic_fx(0x1407BA6B0ull, make_draw_indexed_arguments(3, 20, 0));
	note_arena(9, 0, arena_phase::eye_end,
		reinterpret_cast<std::uintptr_t>(&global_data_identity));
	if (!vr::engine_stereo_gpu_census::end_eye(9, 0))
		fail("dynamic-FX arena identity/churn output0 end");
	std::uint64_t mark_churn{}, spark_churn{};
	std::memcpy(&mark_churn, arena_data_a + test_mark_offset, sizeof(mark_churn));
	std::memcpy(&spark_churn, arena_data_a + test_spark_offset + 2 * 8,
		sizeof(spark_churn));
	mark_churn ^= 0x1001;
	spark_churn ^= 0x4004;
	std::memcpy(arena_data_a + test_mark_offset, &mark_churn, sizeof(mark_churn));
	std::memcpy(arena_data_a + test_spark_offset + 2 * 8, &spark_churn,
		sizeof(spark_churn));
	if (!vr::engine_stereo_gpu_census::begin_eye(9, 1))
		fail("dynamic-FX arena identity/churn output1 begin");
	note_arena(9, 1, arena_phase::eye_begin,
		reinterpret_cast<std::uintptr_t>(&global_data_identity));
	note_arena(9, 1, arena_phase::backend_view_copy,
		reinterpret_cast<std::uintptr_t>(&global_data_identity), backend_state_b);
	bind_dynamic_fx_pipeline();
	observe_dynamic_fx(0x1407B9FBAull, make_draw_indexed_arguments(3, 10, 0));
	note_arena(9, 1, arena_phase::eye_end,
		reinterpret_cast<std::uintptr_t>(&global_data_identity));
	if (!vr::engine_stereo_gpu_census::end_eye(9, 1) ||
		!vr::engine_stereo_gpu_census::end_pair(9))
	{
		fail("dynamic-FX arena identity/churn completion");
	}
	vr::engine_stereo_gpu_census::get_report(*report_storage);
	const auto& arena_identity = report_storage->dynamic_fx.arena;
	const auto& arena_mark_output0 = report_storage->dynamic_fx.eyes[0].families[
		mark_index].observations[0];
	const auto spark_index = static_cast<std::size_t>(dynamic_fx_family::spark);
	const auto& arena_spark_output0 = report_storage->dynamic_fx.eyes[0].families[
		spark_index].observations[0];
	const auto& arena_mark_output1 = report_storage->dynamic_fx.eyes[1].families[
		mark_index].observations[0];
	if (report_storage->current_state !=
			vr::engine_stereo_gpu_census::state::complete ||
		!arena_identity.finalized || arena_identity.evidence !=
			vr::engine_stereo_gpu_census::dynamic_fx_arena_evidence::complete ||
		arena_identity.snapshot_attempts != 7 ||
		arena_identity.snapshot_completions != 7 || arena_identity.unreadable != 0 ||
		arena_identity.overflows != 0 || arena_identity.order_mismatches != 0 ||
		arena_identity.data_identity_comparisons != 3 ||
		arena_identity.data_identity_mismatches != 1 ||
		arena_identity.cross_output_global_identity_comparisons != 1 ||
		arena_identity.cross_output_global_identity_mismatches != 0 ||
		arena_identity.cross_output_backend_identity_comparisons != 1 ||
		arena_identity.cross_output_backend_identity_mismatches != 1 ||
		arena_identity.linked_invocations != 3 ||
		arena_identity.unlinked_invocations != 0 ||
		arena_identity.outputs[0].snapshot_count != 4 ||
		arena_identity.outputs[1].snapshot_count != 3 ||
		arena_mark_output0.arena_view_copy_snapshot_sequence != 2 ||
		arena_spark_output0.arena_view_copy_snapshot_sequence != 3 ||
		arena_mark_output1.arena_view_copy_snapshot_sequence != 6)
	{
		fail("dynamic-FX arena identity/linkage evidence");
	}
	const auto& cross_boundary = arena_identity.transitions[2];
	const auto& cross_view = arena_identity.transitions[5];
	if (!cross_boundary.observed || !cross_boundary.global_identity_comparable ||
		!cross_boundary.global_identity_equal ||
		!cross_boundary.global_raw_comparable ||
		cross_boundary.backend_identity_comparable ||
		cross_boundary.backend_raw_comparable ||
		cross_boundary.global_changed_qword_masks[6] != 0x01 ||
		cross_boundary.global_changed_qword_masks[7] != 0x04 ||
		!cross_view.observed || !cross_view.global_identity_comparable ||
		!cross_view.global_identity_equal || !cross_view.global_raw_comparable ||
		cross_view.global_changed_qword_masks[6] != 0x01 ||
		cross_view.global_changed_qword_masks[7] != 0x04 ||
		!cross_view.backend_identity_comparable ||
		cross_view.backend_identity_equal || cross_view.backend_raw_comparable ||
		!verify_canaries(arena_data_a) ||
		!verify_canaries(arena_data_b))
	{
		fail("dynamic-FX arena MARK/SPARK raw churn and zero-write canaries");
	}

	// The first view-copy beyond the diagnostic capacity is bounded independently.
	// It must not consume
	// the reserved eye_end slot, must not prevent output1 capture, and must not
	// fail the enclosing census. Calls after a dropped view-copy are explicit
	// unlinked evidence rather than being attached to an older snapshot.
	if (!vr::engine_stereo_gpu_census::reset() ||
		!vr::engine_stereo_gpu_census::begin_pair(10, context.Get()))
	{
		fail("dynamic-FX arena overflow pair begin");
	}
	constexpr auto arena_view_capacity =
		vr::engine_stereo_gpu_census::maximum_dynamic_fx_arena_view_copies_per_output;
	constexpr auto overflow_view_count = arena_view_capacity + 1;
	for (std::uint32_t output_index{}; output_index < 2; ++output_index)
	{
		if (!vr::engine_stereo_gpu_census::begin_eye(10, output_index))
			fail("dynamic-FX arena overflow eye begin");
		note_arena(10, output_index, arena_phase::eye_begin,
			reinterpret_cast<std::uintptr_t>(&global_data_identity));
		for (std::size_t view{}; view < overflow_view_count; ++view)
		{
			note_arena(10, output_index, arena_phase::backend_view_copy,
				reinterpret_cast<std::uintptr_t>(&global_data_identity), backend_state_a);
			if (view + 2 >= overflow_view_count)
			{
				bind_dynamic_fx_pipeline();
				observe_dynamic_fx(0x1407B9FBAull,
					make_draw_indexed_arguments(3,
						static_cast<std::uint32_t>(view), 0));
			}
		}
		note_arena(10, output_index, arena_phase::eye_end,
			reinterpret_cast<std::uintptr_t>(&global_data_identity));
		if (!vr::engine_stereo_gpu_census::end_eye(10, output_index))
			fail("dynamic-FX arena overflow eye end");
	}
	if (!vr::engine_stereo_gpu_census::end_pair(10))
		fail("dynamic-FX arena overflow pair completion");
	vr::engine_stereo_gpu_census::get_report(*report_storage);
	const auto& arena_overflow = report_storage->dynamic_fx.arena;
	if (report_storage->current_state !=
			vr::engine_stereo_gpu_census::state::complete ||
		arena_overflow.evidence !=
			vr::engine_stereo_gpu_census::dynamic_fx_arena_evidence::truncated ||
		arena_overflow.overflows != 2 || arena_overflow.linked_invocations != 2 ||
		arena_overflow.unlinked_invocations != 2)
	{
		fail("dynamic-FX arena bounded overflow remains non-fatal");
	}
	for (std::size_t output_index{}; output_index < 2; ++output_index)
	{
		const auto& arena_output = arena_overflow.outputs[output_index];
		const auto& mark_output = report_storage->dynamic_fx.eyes[output_index].families[
			mark_index];
		if (arena_output.view_copy_attempts != overflow_view_count ||
			arena_output.view_copy_stored != arena_view_capacity ||
			arena_output.overflows != 1 ||
			arena_output.snapshot_count != arena_view_capacity + 2 ||
			arena_output.snapshots[arena_view_capacity + 1].phase !=
				arena_phase::eye_end ||
			mark_output.observation_count != 2 ||
			mark_output.observations[0].arena_view_copy_snapshot_sequence == 0 ||
			mark_output.observations[1].arena_view_copy_snapshot_sequence != 0)
		{
			fail("dynamic-FX arena per-output reservation/link overflow");
		}
	}

	// Exercise every safe-read rejection without changing owner/census outcome:
	// null data, uintptr overflow, reserved-uncommitted pointer slot, guarded slot,
	// and a mesh range that begins readable but crosses into PAGE_NOACCESS.
	auto* const reserved_slot = static_cast<std::uint8_t*>(VirtualAlloc(nullptr,
		page_size, MEM_RESERVE, PAGE_READWRITE));
	auto* const guarded_slot = allocate_rw(page_size);
	constexpr std::size_t partial_shift = 0x410;
	auto* const partial_allocation = allocate_rw(arena_data_bytes + page_size);
	auto* const partial_data = partial_allocation ?
		partial_allocation + partial_shift : nullptr;
	auto* const partial_backend = allocate_rw(0x3000);
	if (!reserved_slot || !guarded_slot || !partial_allocation || !partial_backend)
		fail("dynamic-FX arena invalid-range allocations");
	seed_arena(partial_data, 0xD400000000000000ull);
	auto guarded_identity = reinterpret_cast<std::uintptr_t>(arena_data_a);
	std::memcpy(guarded_slot, &guarded_identity, sizeof(guarded_identity));
	DWORD previous_guard_protection{};
	if (!VirtualProtect(guarded_slot, page_size, PAGE_READWRITE | PAGE_GUARD,
		&previous_guard_protection))
	{
		fail("dynamic-FX arena guard setup");
	}
	const auto partial_identity = reinterpret_cast<std::uintptr_t>(partial_data);
	std::memcpy(partial_backend + 0x2BE8, &partial_identity,
		sizeof(partial_identity));
	const auto partial_mark = reinterpret_cast<std::uintptr_t>(partial_data) +
		test_mark_offset;
	const auto partial_blocked_page = partial_mark + 0x20;
	const auto partial_page = partial_blocked_page &
		~static_cast<std::uintptr_t>(page_size - 1);
	DWORD previous_partial_protection{};
	if (!VirtualProtect(reinterpret_cast<void*>(partial_page), page_size,
		PAGE_NOACCESS, &previous_partial_protection))
	{
		fail("dynamic-FX arena partial-range setup");
	}
	std::uintptr_t null_identity{};
	const auto overflow_slot = (std::numeric_limits<std::uintptr_t>::max)() - 3;
	const auto overflow_backend = reinterpret_cast<void*>(
		(std::numeric_limits<std::uintptr_t>::max)() - 0x1000);
	if (!vr::engine_stereo_gpu_census::reset() ||
		!vr::engine_stereo_gpu_census::begin_pair(11, context.Get()) ||
		!vr::engine_stereo_gpu_census::begin_eye(11, 0))
	{
		fail("dynamic-FX arena unreadable output0 begin");
	}
	note_arena(11, 0, arena_phase::eye_begin,
		reinterpret_cast<std::uintptr_t>(&null_identity));
	note_arena(11, 0, arena_phase::backend_view_copy, overflow_slot,
		overflow_backend);
	bind_dynamic_fx_pipeline();
	observe_dynamic_fx(0x1407B9FBAull, make_draw_indexed_arguments(3, 1, 0));
	note_arena(11, 0, arena_phase::eye_end,
		reinterpret_cast<std::uintptr_t>(reserved_slot));
	if (!vr::engine_stereo_gpu_census::end_eye(11, 0) ||
		!vr::engine_stereo_gpu_census::begin_eye(11, 1))
	{
		fail("dynamic-FX arena unreadable output transition");
	}
	note_arena(11, 1, arena_phase::eye_begin,
		reinterpret_cast<std::uintptr_t>(guarded_slot));
	note_arena(11, 1, arena_phase::backend_view_copy,
		reinterpret_cast<std::uintptr_t>(&partial_identity), partial_backend);
	bind_dynamic_fx_pipeline();
	observe_dynamic_fx(0x1407B9FBAull, make_draw_indexed_arguments(3, 1, 0));
	note_arena(11, 1, arena_phase::eye_end,
		reinterpret_cast<std::uintptr_t>(&global_data_identity));
	if (!vr::engine_stereo_gpu_census::end_eye(11, 1) ||
		!vr::engine_stereo_gpu_census::end_pair(11))
	{
		fail("dynamic-FX arena unreadable completion");
	}
	vr::engine_stereo_gpu_census::get_report(*report_storage);
	const auto& arena_unreadable = report_storage->dynamic_fx.arena;
	if (report_storage->current_state !=
			vr::engine_stereo_gpu_census::state::complete ||
		arena_unreadable.evidence !=
			vr::engine_stereo_gpu_census::dynamic_fx_arena_evidence::unreadable ||
		arena_unreadable.snapshot_attempts != 6 ||
		arena_unreadable.snapshot_completions != 1 ||
		arena_unreadable.unreadable != 5 || arena_unreadable.overflows != 0 ||
		arena_unreadable.order_mismatches != 0 ||
		arena_unreadable.outputs[0].snapshot_count != 3 ||
		arena_unreadable.outputs[1].snapshot_count != 3)
	{
		fail("dynamic-FX arena null/overflow/uncommitted/guard/partial evidence");
	}
	DWORD ignored_protection{};
	(void)VirtualProtect(reinterpret_cast<void*>(partial_page), page_size,
		previous_partial_protection, &ignored_protection);
	(void)VirtualProtect(guarded_slot, page_size, previous_guard_protection,
		&ignored_protection);

	// Reproduce the real owner sequence on one backend data identity: capture the
	// left-compatible origins, advance every natural arena origin, scope the old
	// origins over the replay, and restore the exact post-left values afterwards.
	// Earlier census cases deliberately treat every qword as opaque canary data;
	// establish valid H2 count/byte fields before exercising the typed production
	// snapshot contract added for bounded dynamic-payload preservation.
	for (auto* const arena : {arena_data_a, arena_data_b})
	{
		for (std::size_t mesh{}; mesh < test_mesh_offsets.size(); ++mesh)
		{
			const auto index_count = static_cast<std::uint32_t>(32 + mesh);
			const auto vertex_bytes = static_cast<std::uint32_t>(64 + mesh * 16);
			std::memcpy(arena + test_mesh_offsets[mesh] + 0x04,
				&index_count, sizeof(index_count));
			std::memcpy(arena + test_mesh_offsets[mesh] + 0x24,
				&vertex_bytes, sizeof(vertex_bytes));
		}
	}
	using dynamic_arena = vr::engine_stereo_dynamic_arena::index_base_snapshot;
	{
		using vr::engine_stereo_dynamic_arena::classify_view_copy;
		constexpr std::uintptr_t eye=0x10000,shadow=0x20000,owned=0x30000,foreign=0x40000;
		const auto main=classify_view_copy(eye,eye,owned,owned,true);
		const auto light=classify_view_copy(eye,shadow,owned,owned,true);
		const auto fullscreen=classify_view_copy(eye,shadow,owned,owned,false);
		const auto other_arena=classify_view_copy(eye,shadow,owned,foreign,true);
		if(!main.camera_view || !main.owned_geometry || light.camera_view || !light.owned_geometry ||
			fullscreen.camera_view || fullscreen.owned_geometry || other_arena.camera_view || other_arena.owned_geometry ||
			classify_view_copy(eye,shadow,0,0,true).owned_geometry)
			fail("camera view and owned shadow geometry have independent restoration authority");
	}
	vr::engine_stereo_dynamic_arena::failure dynamic_arena_error{};
	dynamic_arena left_index_origins{};
	if (!vr::engine_stereo_dynamic_arena::capture_backend(backend_state_a,
		left_index_origins, dynamic_arena_error))
	{
		fail("dynamic arena left-origin capture");
	}
	// The live transparent-shadow quad retained this CPU index address while
	// the backend origin advanced; H2 narrowed the resulting delta to a huge
	// DrawIndexed start and the A8 atlas stayed at its white clear value.
	const auto shadow_indices=left_index_origins.index_bases[2]+12;
	for (std::size_t mesh{}; mesh < test_mesh_offsets.size(); ++mesh)
	{
		const auto advanced = left_index_origins.index_bases[mesh] +
			0x2000 * (mesh + 1);
		std::memcpy(arena_data_a + test_mesh_offsets[mesh] + 0x18,
			&advanced, sizeof(advanced));
	}
	dynamic_arena natural_post_left{};
	if (!vr::engine_stereo_dynamic_arena::capture_backend(backend_state_a,
		natural_post_left, dynamic_arena_error))
	{
		fail("dynamic arena post-left capture");
	}
	dynamic_arena displaced{};
	const auto shadow_scope=vr::engine_stereo_dynamic_arena::classify_view_copy(
		0x10000,0x20000,left_index_origins.data_identity,natural_post_left.data_identity,true);
	if (std::uint32_t((shadow_indices-natural_post_left.index_bases[2])/2)<32)
		fail("advanced shadow index origin must reproduce the invalid pre-fix draw range");
	if (!shadow_scope.owned_geometry || shadow_scope.camera_view || !vr::engine_stereo_dynamic_arena::replace_backend(backend_state_a,left_index_origins,
		displaced, dynamic_arena_error) ||
		displaced.data_identity != natural_post_left.data_identity ||
		displaced.index_bases != natural_post_left.index_bases ||
		!vr::engine_stereo_dynamic_arena::validate_backend(backend_state_a,
			left_index_origins, dynamic_arena_error))
	{
		fail("dynamic arena scoped replacement");
	}
	std::uintptr_t installed_shadow_base{};
	std::memcpy(&installed_shadow_base,arena_data_a+test_mesh_offsets[2]+0x18,sizeof(installed_shadow_base));
	if((shadow_indices-installed_shadow_base)/2!=6)
		fail("owned shadow subview restores the left quad index range without borrowing camera projection");
	if (vr::engine_stereo_dynamic_arena::validate_backend(backend_state_b,
		left_index_origins, dynamic_arena_error) ||
		dynamic_arena_error !=
			vr::engine_stereo_dynamic_arena::failure::data_identity)
	{
		fail("dynamic arena backend identity rejection");
	}
	if (!vr::engine_stereo_dynamic_arena::restore(displaced,
		dynamic_arena_error))
	{
		fail("dynamic arena natural-state restore");
	}
	dynamic_arena restored{};
	if (!vr::engine_stereo_dynamic_arena::capture_backend(backend_state_a,
		restored, dynamic_arena_error) ||
		restored.index_bases != natural_post_left.index_bases)
	{
		fail("dynamic arena restore verification");
	}

	// A real H2 eye contains multiple source-matching backend view copies and the
	// dynamic arenas may legally advance between them. Prove ordinal replacement
	// against three distinct left snapshots, followed by one owner-scope restore.
	std::array<dynamic_arena, 3> left_boundaries{};
	for (std::size_t boundary{}; boundary < left_boundaries.size(); ++boundary)
	{
		for (std::size_t mesh{}; mesh < test_mesh_offsets.size(); ++mesh)
		{
			const auto value = natural_post_left.index_bases[mesh] +
				0x10000 * (boundary + 1) + 0x100 * mesh;
			std::memcpy(arena_data_a + test_mesh_offsets[mesh] + 0x18,
				&value, sizeof(value));
		}
		if (!vr::engine_stereo_dynamic_arena::capture_backend(backend_state_a,
			left_boundaries[boundary], dynamic_arena_error))
		{
			fail("dynamic arena per-boundary left capture");
		}
	}
	for (std::size_t mesh{}; mesh < test_mesh_offsets.size(); ++mesh)
	{
		const auto value = natural_post_left.index_bases[mesh] + 0x900000 + mesh;
		std::memcpy(arena_data_a + test_mesh_offsets[mesh] + 0x18,
			&value, sizeof(value));
	}
	dynamic_arena natural_before_right{};
	if (!vr::engine_stereo_dynamic_arena::capture_backend(backend_state_a,
		natural_before_right, dynamic_arena_error))
	{
		fail("dynamic arena natural pre-right capture");
	}
	dynamic_arena owner_restore{};
	for (std::size_t boundary{}; boundary < left_boundaries.size(); ++boundary)
	{
		dynamic_arena boundary_displaced{};
		if (!vr::engine_stereo_dynamic_arena::replace_backend(backend_state_a,
			left_boundaries[boundary], boundary_displaced, dynamic_arena_error) ||
			!vr::engine_stereo_dynamic_arena::validate_backend(backend_state_a,
				left_boundaries[boundary], dynamic_arena_error))
		{
			fail("dynamic arena ordinal right replacement");
		}
		if (boundary == 0) owner_restore = boundary_displaced;
		// Model H2 legitimately changing the arena before the next B5D0 boundary.
		if (boundary + 1 < left_boundaries.size())
		{
			for (std::size_t mesh{}; mesh < test_mesh_offsets.size(); ++mesh)
			{
				const auto value = left_boundaries[boundary].index_bases[mesh] +
					0x8000 + boundary * 0x100 + mesh;
				std::memcpy(arena_data_a + test_mesh_offsets[mesh] + 0x18,
					&value, sizeof(value));
			}
		}
	}
	if (!vr::engine_stereo_dynamic_arena::restore(owner_restore,
		dynamic_arena_error) ||
		!vr::engine_stereo_dynamic_arena::capture_backend(backend_state_a,
			restored, dynamic_arena_error) ||
		restored.index_bases != natural_before_right.index_bases)
	{
		fail("dynamic arena ordinal owner-scope restore");
	}
	dynamic_arena foreign_displaced{};
	if (vr::engine_stereo_dynamic_arena::replace_backend(backend_state_b,
		left_boundaries[0], foreign_displaced, dynamic_arena_error) ||
		dynamic_arena_error !=
			vr::engine_stereo_dynamic_arena::failure::data_identity ||
		foreign_displaced.valid)
	{
		fail("dynamic arena ordinal foreign-backend rejection");
	}
	const auto queries_before = vr::engine_stereo_dynamic_arena::validation_queries_for_tests();
	const auto resident_before = vr::engine_stereo_dynamic_arena::resident_validations_for_tests();
	dynamic_arena operation_displaced{};
	if (!vr::engine_stereo_dynamic_arena::replace_backend(backend_state_a,
		left_boundaries[0], operation_displaced, dynamic_arena_error) ||
		vr::engine_stereo_dynamic_arena::validation_queries_for_tests() - queries_before != 2)
		fail("one query each for backend and arena; writable range reused within replace");
	if (vr::engine_stereo_dynamic_arena::resident_validations_for_tests() - resident_before != 2)
		fail("resident backend and arena use bounded page queries");
	if (!vr::engine_stereo_dynamic_arena::restore(operation_displaced, dynamic_arena_error))
		fail("restore after coalesced range validation");
	const auto descriptor_page = (reinterpret_cast<std::uintptr_t>(arena_data_a) +
		vr::engine_stereo_dynamic_arena::code_trans_offset) & ~(std::uintptr_t{page_size} - 1);
	DWORD old_descriptor_protection{};
	if (!VirtualProtect(reinterpret_cast<void*>(descriptor_page), page_size,
		PAGE_READONLY, &old_descriptor_protection)) fail("read-only arena setup");
	if (!vr::engine_stereo_dynamic_arena::capture_backend(backend_state_a, restored, dynamic_arena_error) ||
		vr::engine_stereo_dynamic_arena::replace_backend(backend_state_a,
			left_boundaries[0], operation_displaced, dynamic_arena_error) ||
		dynamic_arena_error != vr::engine_stereo_dynamic_arena::failure::descriptor_unwritable)
		fail("validation cannot survive an operation or promote read access to write access");
	if (!VirtualProtect(reinterpret_cast<void*>(descriptor_page), page_size,
		PAGE_READWRITE | PAGE_GUARD, &ignored_protection)) fail("guarded arena setup");
	if (vr::engine_stereo_dynamic_arena::capture_backend(backend_state_a, restored, dynamic_arena_error))
		fail("next capture must revalidate guard protection");
	MEMORY_BASIC_INFORMATION guarded_descriptor{};
	if (!VirtualQuery(reinterpret_cast<void*>(descriptor_page), &guarded_descriptor, sizeof(guarded_descriptor)) ||
		(guarded_descriptor.Protect & PAGE_GUARD) == 0)
		fail("guarded arena must be rejected before any read consumes the guard");
	if (!VirtualProtect(reinterpret_cast<void*>(descriptor_page), page_size,
		old_descriptor_protection, &ignored_protection)) fail("arena protection restore");
	if (!verify_canaries(arena_data_a) || !verify_canaries(arena_data_b) ||
		!verify_canaries(partial_data))
	{
		fail("dynamic-FX arena safe-read zero-write canaries");
	}
	VirtualFree(reserved_slot, 0, MEM_RELEASE);
	VirtualFree(guarded_slot, 0, MEM_RELEASE);
	VirtualFree(partial_allocation, 0, MEM_RELEASE);
	VirtualFree(partial_backend, 0, MEM_RELEASE);
	VirtualFree(arena_data_a, 0, MEM_RELEASE);
	VirtualFree(arena_data_b, 0, MEM_RELEASE);
	VirtualFree(backend_state_a, 0, MEM_RELEASE);
	VirtualFree(backend_state_b, 0, MEM_RELEASE);

	std::cout << "static_storage_bytes=" << ordered_static_storage_bytes
		<< " argument_samples=" << ordered_argument_sample_count
		<< " cb_samples=" << ordered_constant_buffer_sample_count
		<< " srv_samples=" << ordered_shader_resource_sample_count
		<< " srv_usage=" << ordered_srv_used << '/' << ordered_srv_unused << '/'
		<< ordered_srv_unknown << '\n';
	std::cout << "vr-d3d11-gpu-census-probe: PASS\n";
	return 0;
}
