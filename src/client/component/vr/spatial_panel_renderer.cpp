#include <std_include.hpp>
#include "spatial_panel_renderer.hpp"
#include "presentation_options.hpp"
#include "native_conversion_command_list.hpp"
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")

namespace vr::spatial_panel
{
	namespace
	{
		constexpr char shader[] = R"(
cbuffer Panel : register(b0) {
    float4 corners[4]; float4 settings; float4 canvas; float4 ink_uv; float4 source_window;
    float4 backdrop_count; float4 backdrop_bounds[8]; float4 backdrop_settings[8];
};
struct Vertex { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Vertex vs(uint index : SV_VertexID) {
    Vertex v; v.position = corners[index];
    v.uv = float2(index & 1, index >> 1); return v;
}
Texture2D<float3> scene : register(t0);
Texture2D<float4> ink : register(t1);
Texture2D<float4> scope_shadow : register(t2);
Texture2D<float4> scope_flash : register(t3);
SamplerState linear_clamp : register(s0);
float4 ink_ps(Vertex v) : SV_Target {
    return ink.Sample(linear_clamp, ink_uv.xy + v.uv*ink_uv.zw);
}
float3 to_linear(float3 x) {
    float3 high = pow(max((x + 0.055) / 1.055, 0.0), 2.4);
    return float3(x.r <= 0.04045 ? x.r / 12.92 : high.r,
                  x.g <= 0.04045 ? x.g / 12.92 : high.g,
                  x.b <= 0.04045 ? x.b / 12.92 : high.b);
}
float3 to_encoded(float3 x) {
    float3 high = 1.055 * pow(max(x, 0.0), 1.0/2.4) - 0.055;
    return float3(x.r <= 0.0031308 ? x.r * 12.92 : high.r,
                  x.g <= 0.0031308 ? x.g * 12.92 : high.g,
                  x.b <= 0.0031308 ? x.b * 12.92 : high.b);
}
float4 screen_ps(Vertex v) : SV_Target {
    float2 uv = (v.uv - canvas.xy) / canvas.zw;
    float4 fg = ink_uv; // Full-field native fade outside the projected canvas.
    bool in_canvas = all(uv >= 0) && all(uv <= 1);
    if (in_canvas) fg = ink.Sample(linear_clamp, uv);
    float4 subtractive = 0;
    if (in_canvas && backdrop_count.y > 0) subtractive = scope_shadow.Sample(linear_clamp,uv);
    // Native additive ink has RGB emission with zero alpha: it adds light
    // without attenuating the eye image. Only all-zero ink is empty.
    bool empty_ink = all(fg == 0) && all(subtractive == 0);
    if (empty_ink && (!in_canvas || backdrop_count.x <= 0)) discard;
    float blur_alpha = 0, radius = 0;
    [loop] for (int i=0; in_canvas && i<min(8,(int)backdrop_count.x); ++i) {
        float4 box = backdrop_bounds[i];
        if (all(uv >= box.xy) && all(uv <= box.zw)) {
            float2 edge = min(uv-box.xy, box.zw-uv)*canvas.zw/settings.xy;
            float coverage = smoothstep(0, 2, min(edge.x,edge.y))*backdrop_settings[i].x;
            blur_alpha = coverage + blur_alpha*(1-coverage);
            radius = max(radius, clamp((box.w-box.y)*canvas.w/settings.y*.08,1,8));
        }
    }
    if (empty_ink && blur_alpha <= 0) discard;
    // Copy contains every earlier layer, including the weapon HUD. Encode it
    // before native source-over, then return to the linear eye target once.
    float3 bg = to_encoded(scene.Sample(linear_clamp, v.uv));
    if (blur_alpha > 0) {
        float3 blurred = 0;
        [unroll] for (int y=-1; y<=1; ++y)
            [unroll] for (int x=-1; x<=1; ++x)
                blurred += scene.Sample(linear_clamp,v.uv+float2(x,y)*settings.xy*radius)
                    * (x==0 ? 2 : 1) * (y==0 ? 2 : 1) / 16.0;
        bg = lerp(bg,to_encoded(blurred),blur_alpha);
    }
    // Native tutorial borders precede their text and use reverse subtraction.
    // Keep the positive subtractand separate from transparent source-over ink.
    bg = max(bg*(1-subtractive.a)-subtractive.rgb,0);
    return float4(to_linear(fg.rgb + bg*(1-fg.a)), 1);
}
float4 ps(Vertex v) : SV_Target {
    float4 foreground = ink.Sample(linear_clamp, v.uv); // native premultiplied RGB
    float coverage = foreground.a;
    if (backdrop_count.y > 0) {
        float2 mask_uv = (v.uv-source_window.xy)/source_window.zw;
        coverage = all(mask_uv >= 0) && all(mask_uv <= 1) ? scope_shadow.Sample(linear_clamp,mask_uv).a : 0;
    }
    coverage = settings.z > 0 ? saturate(coverage*settings.w) : 0;
    if (foreground.a <= 0 && coverage <= 0) discard;
    float2 uv = v.position.xy * settings.xy;
    float3 original = scene.Sample(linear_clamp, uv);
    float3 background = original;
    if (coverage > 0) {
        // Bounded 3x3 tent kernel, only over the projected panel's pixels.
        // Sample the current EYE's display image, never a desktop/other-eye copy.
        float3 blurred = 0;
        [unroll] for (int y = -1; y <= 1; ++y)
            [unroll] for (int x = -1; x <= 1; ++x)
                blurred += scene.Sample(linear_clamp, uv + float2(x,y)*settings.xy*settings.z)
                    * (x == 0 ? 2 : 1) * (y == 0 ? 2 : 1) / 16.0;
        // A native mask is independent of glyph/border ink: the clear area
        // behind the ammo counter still receives its authored background blur.
        background = lerp(original, blurred, coverage);
    }
    // Match native encoded-space compositing, then decode once to the linear
    // submission target. Blending premultiplied encoded ink directly in linear
    // space would change the original font edges and black backing appearance.
    return float4(to_linear(foreground.rgb + background * (1-foreground.a)), 1);
}
float4 mask_ps(Vertex v) : SV_Target {
    float coverage = ink_uv.x > 0 ? 1 : saturate(ink.Sample(linear_clamp,v.uv).a*settings.w);
    if (coverage <= 0) discard;
    float2 uv = v.position.xy*settings.xy;
    float3 original = scene.SampleLevel(linear_clamp,uv,0), blurred = 0;
    // Five-tap binomial weights per axis: smoother low-frequency backing
    // without the separated sharp copies produced by a wide 3x3 kernel.
    static const float weights[5] = {1,4,6,4,1};
    // Prefilter each tap's footprint before widening the kernel. Sparse
    // full-resolution taps alone preserve/alias fine detail at even radii.
    float lod = max(0,log2(max(settings.z,1)));
    [unroll] for (int y=-2;y<=2;++y)
        [unroll] for (int x=-2;x<=2;++x)
            blurred += scene.SampleLevel(linear_clamp,uv+float2(x,y)*settings.xy*settings.z,lod)
                * weights[x+2] * weights[y+2] / 256.0;
    return float4(lerp(original,blurred,coverage),1);
}
float4 optic_ps(Vertex v) : SV_Target {
    float radius = length((v.uv-.5)*2);
    if (radius > 1) discard;
    if (canvas.w <= 0) return float4(0,0,0,1);
    float2 ret_uv = v.uv-canvas.xy+.5;
    // A calibrated opaque artwork rim stays in reticle coordinates. Widening
    // the viewing eye box must not reveal scene pixels past its transparent tail.
    float pupil_limit = ink_uv.x < 1 ? ink_uv.x : ink_uv.x*ink_uv.y;
    float pupil_radius = length((ret_uv-.5)*2)/pupil_limit;
    // A shifted texture rectangle is not an optical aperture. Intersect the
    // physical lens with a circular exit pupil BEFORE sampling the reticle;
    // outside it is scope shadow, not an unmasked view of the scene.
    if (pupil_radius >= 1) return float4(0,0,0,1);
    float2 lens_uv = canvas.xy + (v.uv-canvas.xy)/canvas.z;
    // Project the compressed point on the same physical plane. This preserves
    // each eye's asymmetric perspective even when the lens is tilted.
    float4 clip_pos = lerp(lerp(corners[0],corners[1],lens_uv.x),
                          lerp(corners[2],corners[3],lens_uv.x),lens_uv.y);
    float2 uv = float2(clip_pos.x/clip_pos.w*.5+.5, .5-clip_pos.y/clip_pos.w*.5);
    uv = (uv-source_window.xy)/source_window.zw;
    if (clip_pos.w <= 0 || any(uv < settings.xy*.5) || any(uv > 1-settings.xy*.5) ||
        length((lens_uv-.5)*2) > .98*ink_uv.y) return float4(0,0,0,1);
    float3 color = scene.Sample(linear_clamp,uv);
    float4 reticle = ink.Sample(linear_clamp,ret_uv);
    color = lerp(color,reticle.rgb,reticle.a);
    color *= (1-smoothstep(.94,1,radius)) * (1-smoothstep(.94,1,pupil_radius));
    return float4(to_linear(color),1);
}
float4 scope_ps(Vertex v) : SV_Target {
    float2 uv = v.uv; // Perspective-correct coordinates on the anchored 3D plane.
    if (any(uv < 0) || any(uv > 1)) return float4(0,0,0,1);
    float4 fg = backdrop_count.w > 0 ? ink.Sample(linear_clamp,uv) : float4(0,0,0,0);
    float3 bg = scene.Sample(linear_clamp,uv);
    if (backdrop_count.z > 0) {
        float4 flash = scope_flash.Sample(linear_clamp,uv);
        bg = saturate(flash.rgb + bg*(1-flash.a));
    }
    if (backdrop_count.y > 0) {
        float4 shade = scope_shadow.Sample(linear_clamp,uv);
        bg = max(bg*(1-shade.a)-shade.rgb,0);
    }
    return float4(to_linear(fg.rgb + bg*(1-fg.a)),1);
}
float4 recording_ps(Vertex v) : SV_Target {
    float2 pixel = v.position.xy;
    if (all(pixel >= canvas.xy) && all(pixel <= canvas.zw)) discard;
    float distance = max(max(canvas.x-pixel.x,pixel.x-canvas.z),
                         max(canvas.y-pixel.y,pixel.y-canvas.w));
    // Dark outline keeps the white frame readable over both snow and shadows.
    // The entire border lies outside the desktop's bilinear sample footprint.
    if (distance < settings.z) return float4(0,0,0,.7);
    if (distance < settings.z*3) return float4(.9,.9,.9,.9);
    if (distance < settings.z*4) return float4(0,0,0,.7);
    if (all(ink_uv.zw > 0) && all(pixel >= ink_uv.xy) && all(pixel < ink_uv.xy+ink_uv.zw)) {
        float2 uv = (pixel-ink_uv.xy)/ink_uv.zw;
        uint w,h; ink.GetDimensions(w,h);
        float2 texel = 1.0/float2(w,h);
        float glyph = ink.Sample(linear_clamp,uv).a;
        float outline = glyph;
        [unroll] for (int y=-1;y<=1;++y)
            [unroll] for (int x=-1;x<=1;++x)
                outline = max(outline,ink.Sample(linear_clamp,uv+float2(x,y)*texel).a);
        float alpha = glyph + (1-glyph)*outline*.8;
        return float4(glyph.xxx,alpha+(1-alpha)*settings.w);
    }
    return float4(0,0,0,settings.w);
}
)";
		struct constants
		{
			projected_quad corners;vec4 settings,canvas,ink_uv,source_window{0,0,1,1};
			vec4 backdrop_count{};
			std::array<vec4,blur_region_capacity> backdrop_bounds{},backdrop_settings{};
		};
		static_assert(blur_region_capacity==8); // Matches the bounded shader arrays.
		static_assert(sizeof(constants) == 400);
		struct shader_bytecode
		{
			Microsoft::WRL::ComPtr<ID3DBlob> vs,ps,screen_ps,ink_ps,optic_ps,recording_ps,scope_ps,mask_ps;
			bool ready{};
			HRESULT compile_result{};
			const char* failed_entry{"none"};
		};
		const shader_bytecode& compiled_shaders() noexcept
		{
			// Bytecode has no device or context ownership. Thread-safe local-static
			// initialization compiles it once for every panel instance in the process,
			// including renderers first used after a device replacement. Cache failure
			// as well, so another HUD layer cannot start a repeated compile loop.
			static const shader_bytecode compiled=[] {
				shader_bytecode result;
				Microsoft::WRL::ComPtr<ID3DBlob> errors;
				const auto compile=[&](const char* entry,const char* target,Microsoft::WRL::ComPtr<ID3DBlob>& output) {
					const auto hr=D3DCompile(shader,sizeof(shader)-1,"spatial-panel",nullptr,nullptr,
						entry,target,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&output,errors.ReleaseAndGetAddressOf());
					if(FAILED(hr)){result.compile_result=hr;result.failed_entry=entry;}
					return SUCCEEDED(hr);
				};
				result.ready=compile("vs","vs_5_0",result.vs) && compile("ps","ps_5_0",result.ps) &&
					compile("screen_ps","ps_5_0",result.screen_ps) && compile("ink_ps","ps_5_0",result.ink_ps) &&
					compile("optic_ps","ps_5_0",result.optic_ps) && compile("recording_ps","ps_5_0",result.recording_ps) &&
					compile("scope_ps","ps_5_0",result.scope_ps) && compile("mask_ps","ps_5_0",result.mask_ps);
				return result;
			}();
			return compiled;
		}
	}
	bool renderer::ensure(ID3D11Device* device) noexcept
	{
		if (device_.Get() == device) return ready_;
		*this = {}; device_ = device;
		// Device objects and mutable draw state stay local to this renderer.
		// A failed device initialization remains cached until the device changes.
		const auto& code=compiled_shaders();
		if(!code.ready){initialization_status_={"shader_compile",code.compile_result,true,code.failed_entry};return false;}
		const auto checked=[&](HRESULT result,const char* stage)
		{if(FAILED(result))initialization_status_={stage,result,true};return SUCCEEDED(result);};
		if (!checked(device->CreateVertexShader(code.vs->GetBufferPointer(),code.vs->GetBufferSize(),nullptr,&vs_),"create_vertex_shader") ||
			!checked(device->CreatePixelShader(code.scope_ps->GetBufferPointer(),code.scope_ps->GetBufferSize(),nullptr,&scope_ps_),"create_scope_shader") ||
			!checked(device->CreatePixelShader(code.mask_ps->GetBufferPointer(),code.mask_ps->GetBufferSize(),nullptr,&mask_ps_),"create_mask_shader") ||
			!checked(device->CreatePixelShader(code.ps->GetBufferPointer(),code.ps->GetBufferSize(),nullptr,&ps_),"create_panel_shader") ||
			!checked(device->CreatePixelShader(code.screen_ps->GetBufferPointer(),code.screen_ps->GetBufferSize(),nullptr,&screen_ps_),"create_screen_shader") ||
			!checked(device->CreatePixelShader(code.ink_ps->GetBufferPointer(),code.ink_ps->GetBufferSize(),nullptr,&ink_ps_),"create_ink_shader") ||
			!checked(device->CreatePixelShader(code.optic_ps->GetBufferPointer(),code.optic_ps->GetBufferSize(),nullptr,&optic_ps_),"create_optic_shader") ||
			!checked(device->CreatePixelShader(code.recording_ps->GetBufferPointer(),code.recording_ps->GetBufferSize(),nullptr,&recording_ps_),"create_recording_shader") ||
			!checked(device->CreateDeferredContext(0, &deferred_),"create_deferred_context")) return false;
		if(!native_conversion_command_list::mark_recording_context(deferred_.Get()))
		{initialization_status_={"recording_context_tag",0,false};return false;}
		D3D11_BUFFER_DESC buffer{};
		buffer.ByteWidth = sizeof(constants); buffer.Usage = D3D11_USAGE_DYNAMIC;
		buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER; buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		D3D11_SAMPLER_DESC sampler{};
		sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampler.MaxLOD = D3D11_FLOAT32_MAX; sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
		D3D11_RASTERIZER_DESC raster{};
		raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = D3D11_CULL_NONE; raster.DepthClipEnable = TRUE;
		D3D11_DEPTH_STENCIL_DESC depth{};
		depth.DepthEnable = FALSE; depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
		auto scene_depth=depth;scene_depth.DepthEnable=TRUE;scene_depth.DepthFunc=D3D11_COMPARISON_GREATER_EQUAL;
		D3D11_BLEND_DESC blend{};
		blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		auto ink_blend=blend; auto& ib=ink_blend.RenderTarget[0];
		ib.BlendEnable=TRUE; ib.SrcBlend=ib.SrcBlendAlpha=D3D11_BLEND_ONE;
		ib.DestBlend=ib.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA; ib.BlendOp=ib.BlendOpAlpha=D3D11_BLEND_OP_ADD;
		ready_ = checked(device->CreateBuffer(&buffer, nullptr, &constants_),"create_constant_buffer") &&
			checked(device->CreateSamplerState(&sampler, &sampler_),"create_sampler") &&
			checked(device->CreateRasterizerState(&raster, &raster_),"create_rasterizer") &&
			checked(device->CreateDepthStencilState(&depth, &depth_),"create_depth_state") &&
			checked(device->CreateDepthStencilState(&scene_depth, &scene_depth_),"create_scene_depth_state") &&
			checked(device->CreateBlendState(&blend, &blend_),"create_blend_state") &&
			checked(device->CreateBlendState(&ink_blend, &ink_blend_),"create_ink_blend_state");
		return ready_;
	}
	bool renderer::draw_canvas(ID3D11DeviceContext* context,ID3D11ShaderResourceView* source,
		ID3D11RenderTargetView* destination,const ui_canvas::mapping& map) noexcept
	{
		if(!context||context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE||!source||!destination||!map.valid())return false;
		Microsoft::WRL::ComPtr<ID3D11Device> device,owner;context->GetDevice(&device);
		source->GetDevice(&owner);if(owner!=device)return false;destination->GetDevice(&owner);
		if(owner!=device||!ensure(device.Get()))return false;
		Microsoft::WRL::ComPtr<ID3D11Resource> input,output;source->GetResource(&input);destination->GetResource(&output);
		Microsoft::WRL::ComPtr<ID3D11Texture2D> in_texture,out_texture;
		if(input==output||FAILED(input.As(&in_texture))||FAILED(output.As(&out_texture)))return false;
		D3D11_TEXTURE2D_DESC in_desc{},out_desc{};in_texture->GetDesc(&in_desc);out_texture->GetDesc(&out_desc);
		D3D11_SHADER_RESOURCE_VIEW_DESC srv{};source->GetDesc(&srv);
		D3D11_RENDER_TARGET_VIEW_DESC rtv{};destination->GetDesc(&rtv);
		if(in_desc.Width!=map.source_width||in_desc.Height!=map.source_height||out_desc.Width!=map.target_width||out_desc.Height!=map.target_height||
			in_desc.SampleDesc.Count!=1||out_desc.SampleDesc.Count!=1||in_desc.ArraySize!=1||out_desc.ArraySize!=1||
			srv.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D||rtv.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D||
			srv.Format!=DXGI_FORMAT_R8G8B8A8_UNORM||rtv.Format!=DXGI_FORMAT_R8G8B8A8_UNORM||srv.Texture2D.MostDetailedMip||rtv.Texture2D.MipSlice)return false;
		const float left=2*map.x/map.target_width-1,right=2*(map.x+map.width)/map.target_width-1;
		const float top=1-2*map.y/map.target_height,bottom=1-2*(map.y+map.height)/map.target_height;
		const projected_quad quad{{{left,top,.5f,1},{right,top,.5f,1},{left,bottom,.5f,1},{right,bottom,.5f,1}}};
		const constants data{quad,{},{},{0,0,1,1}};D3D11_MAPPED_SUBRESOURCE mapped{};
		if(FAILED(deferred_->Map(constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return false;
		std::memcpy(mapped.pData,&data,sizeof(data));deferred_->Unmap(constants_.Get(),0);
		const float transparent[4]{};deferred_->ClearRenderTargetView(destination,transparent);
		const D3D11_VIEWPORT viewport{0,0,float(map.target_width),float(map.target_height),0,1};
		deferred_->OMSetRenderTargets(1,&destination,nullptr);deferred_->OMSetDepthStencilState(depth_.Get(),0);
		deferred_->OMSetBlendState(blend_.Get(),nullptr,UINT_MAX);
		deferred_->RSSetViewports(1,&viewport);deferred_->RSSetState(raster_.Get());
		deferred_->IASetInputLayout(nullptr);deferred_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		deferred_->VSSetShader(vs_.Get(),nullptr,0);deferred_->PSSetShader(ink_ps_.Get(),nullptr,0);
		auto* cb=constants_.Get();deferred_->VSSetConstantBuffers(0,1,&cb);deferred_->PSSetConstantBuffers(0,1,&cb);
		auto* sampler=sampler_.Get();deferred_->PSSetSamplers(0,1,&sampler);deferred_->PSSetShaderResources(1,1,&source);
		deferred_->Draw(4,0);Microsoft::WRL::ComPtr<ID3D11CommandList> list;
		if(FAILED(deferred_->FinishCommandList(FALSE,&list))){deferred_->ClearState();return false;}
		if(!native_conversion_command_list::mark(list.Get()))return false;
		context->ExecuteCommandList(list.Get(),TRUE);return true;
	}
	bool renderer::draw_layers(ID3D11DeviceContext* context, ID3D11RenderTargetView* destination,
		const image_layer* layers, unsigned count, unsigned width, unsigned height) noexcept
	{
		if (!context || context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE || !destination || !layers ||
			!count || count>64 || !width || !height || width>8192 || height>8192) return false;
		Microsoft::WRL::ComPtr<ID3D11Device> device, owner; context->GetDevice(&device); destination->GetDevice(&owner);
		if (owner.Get()!=device.Get() || !ensure(device.Get())) return false;
		for (unsigned i=0;i<count;++i)
		{
			const auto& layer=layers[i];
			if (!layer.ink) return false;
			layer.ink->GetDevice(&owner); if (owner.Get()!=device.Get()) return false;
			for (const auto& v:layer.corners) for (float x:v) if (!std::isfinite(x) || v[3]<=0) return false;
			for (float x:layer.uv) if (!std::isfinite(x) || x<0 || x>1) return false;
			if (layer.uv[2]<=0 || layer.uv[3]<=0 || layer.uv[0]+layer.uv[2]>1 || layer.uv[1]+layer.uv[3]>1) return false;
		}
		D3D11_TEXTURE2D_DESC desc{}; if (ink_copy_) ink_copy_->GetDesc(&desc);
		if (!ink_view_ || desc.Width!=width || desc.Height!=height)
		{
			ink_copy_.Reset(); ink_target_.Reset(); ink_view_.Reset(); desc={};
			desc.Width=width; desc.Height=height; desc.ArraySize=desc.MipLevels=1;
			desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count=1;
			desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
			if (FAILED(device->CreateTexture2D(&desc,nullptr,&ink_copy_)) ||
				FAILED(device->CreateRenderTargetView(ink_copy_.Get(),nullptr,&ink_target_)) ||
				FAILED(device->CreateShaderResourceView(ink_copy_.Get(),nullptr,&ink_view_))) return false;
		}
		const D3D11_VIEWPORT viewport{0,0,static_cast<float>(width),static_cast<float>(height),0,1};
		const float transparent[4]{}; deferred_->ClearRenderTargetView(ink_target_.Get(),transparent);
		auto* target=ink_target_.Get(); deferred_->OMSetRenderTargets(1,&target,nullptr);
		deferred_->RSSetViewports(1,&viewport); deferred_->RSSetState(raster_.Get());
		deferred_->OMSetDepthStencilState(depth_.Get(),0); deferred_->OMSetBlendState(ink_blend_.Get(),nullptr,UINT_MAX);
		deferred_->IASetInputLayout(nullptr); deferred_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		deferred_->VSSetShader(vs_.Get(),nullptr,0); deferred_->PSSetShader(ink_ps_.Get(),nullptr,0);
		auto* cb=constants_.Get(); deferred_->VSSetConstantBuffers(0,1,&cb); deferred_->PSSetConstantBuffers(0,1,&cb);
		auto* sampler=sampler_.Get(); deferred_->PSSetSamplers(0,1,&sampler);
		for (unsigned i=0;i<count;++i)
		{
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (FAILED(deferred_->Map(constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))
			{ deferred_->ClearState(); return false; }
			const constants data{layers[i].corners,{},{},layers[i].uv};
			std::memcpy(mapped.pData,&data,sizeof(data)); deferred_->Unmap(constants_.Get(),0);
			deferred_->PSSetShaderResources(1,1,&layers[i].ink); deferred_->Draw(4,0);
		}
		Microsoft::WRL::ComPtr<ID3D11CommandList> commands;
		if (FAILED(deferred_->FinishCommandList(FALSE,&commands))) { deferred_->ClearState(); return false; }
		if (!native_conversion_command_list::mark(commands.Get())) return false;
		context->ExecuteCommandList(commands.Get(),TRUE);
		constexpr projected_quad full{{{-1,1,0,1},{1,1,0,1},{-1,-1,0,1},{1,-1,0,1}}};
		return draw_screen_layer(context,ink_view_.Get(),destination,full,width,height,{});
	}
	bool renderer::draw(ID3D11DeviceContext* context, ID3D11ShaderResourceView* background,
		ID3D11ShaderResourceView* ink, ID3D11RenderTargetView* destination,
		const projected_quad& corners, unsigned width, unsigned height,
		float blur_pixels, float blur_strength,ID3D11ShaderResourceView* blur_mask,const vec4& blur_window) noexcept
	{
		if(presentation_options::blur_disabled()){blur_pixels=0;blur_strength=0;}
		if(blur_mask)
		{
			for(float x:blur_window)if(!std::isfinite(x) || std::abs(x)>2)return false;
			if(std::abs(blur_window[2])<.0001f || std::abs(blur_window[3])<.0001f)return false;
		}
		return draw_internal(context, background, ink, destination, corners, width, height,
			blur_pixels, blur_strength, {}, false,false,nullptr,blur_window,{},1,false,blur_mask);
	}
	bool renderer::prepare_background_copy(ID3D11DeviceContext* context,ID3D11RenderTargetView* destination,
		unsigned width,unsigned height,bool mipmapped) noexcept
	{
		if(!context||!destination||!width||!height||width>16384||height>16384)return false;
		Microsoft::WRL::ComPtr<ID3D11Device> device; context->GetDevice(&device);
		if (!ensure(device.Get())) return false;
		Microsoft::WRL::ComPtr<ID3D11Resource> resource; destination->GetResource(&resource);
		Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
		if (FAILED(resource.As(&texture))) return false;
		D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
		D3D11_RENDER_TARGET_VIEW_DESC target_desc{}; destination->GetDesc(&target_desc);
		if (desc.Width != width || desc.Height != height || desc.SampleDesc.Count != 1 || desc.ArraySize != 1 ||
			desc.MipLevels != 1 || target_desc.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D) return false;
		D3D11_TEXTURE2D_DESC previous{};
		if (background_copy_) background_copy_->GetDesc(&previous);
		D3D11_SHADER_RESOURCE_VIEW_DESC previous_view{};
		if (background_view_) background_view_->GetDesc(&previous_view);
		if (!background_view_ || previous.Width != width || previous.Height != height ||
			previous.Format != desc.Format || previous_view.Format != target_desc.Format ||
			bool(previous.MiscFlags&D3D11_RESOURCE_MISC_GENERATE_MIPS)!=mipmapped)
		{
			background_copy_.Reset(); background_view_.Reset();
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE; desc.Usage = D3D11_USAGE_DEFAULT;
			desc.CPUAccessFlags = desc.MiscFlags = 0;
			if(mipmapped)
			{
				UINT support{};
				if(FAILED(device->CheckFormatSupport(target_desc.Format,&support))||!(support&D3D11_FORMAT_SUPPORT_MIP_AUTOGEN))return false;
				desc.BindFlags|=D3D11_BIND_RENDER_TARGET;desc.MiscFlags=D3D11_RESOURCE_MISC_GENERATE_MIPS;desc.MipLevels=0;
			}
			D3D11_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Format = target_desc.Format;
			srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; srv.Texture2D.MipLevels = mipmapped?UINT(-1):1;
			if (FAILED(device->CreateTexture2D(&desc, nullptr, &background_copy_)) ||
				FAILED(device->CreateShaderResourceView(background_copy_.Get(), &srv, &background_view_))) return false;
		}
		return true;
	}
	bool renderer::draw_masked_blur(ID3D11DeviceContext* context,ID3D11ShaderResourceView* mask,ID3D11RenderTargetView* destination,
		const projected_quad& corners,unsigned width,unsigned height,float radius,float strength,ID3D11DepthStencilView* scene_depth) noexcept
	{
		if(presentation_options::blur_disabled())return true;
		if(!mask||!prepare_background_copy(context,destination,width,height,true))return false;
		for(const auto& v:corners)if(v[3]<=.001f)return false;
		return draw_internal(context,background_view_.Get(),mask,destination,corners,width,height,radius,strength,
			{},false,false,scene_depth,{0,0,1,1},{},1,false,nullptr,nullptr,blur_mode::alpha_mask);
	}
	bool renderer::draw_panel_blur(ID3D11DeviceContext* context,ID3D11RenderTargetView* destination,
		const projected_quad& corners,unsigned width,unsigned height,float radius,ID3D11DepthStencilView* scene_depth) noexcept
	{
		if(presentation_options::blur_disabled())return true;
		if(!prepare_background_copy(context,destination,width,height,true))return false;
		if(std::none_of(corners.begin(),corners.end(),[](const auto& v){return v[3]>.001f;}))return false;
		return draw_internal(context,background_view_.Get(),background_view_.Get(),destination,corners,width,height,radius,1,
			{},false,false,scene_depth,{0,0,1,1},{},1,false,nullptr,nullptr,blur_mode::panel);
	}
	bool renderer::draw_screen_layer(ID3D11DeviceContext* context, ID3D11ShaderResourceView* ink,
		ID3D11RenderTargetView* destination, const projected_quad& canvas,
		unsigned width, unsigned height, const vec4& fade,std::span<const blur_region> backdrops,ID3D11ShaderResourceView* subtractive) noexcept
	{
		if(presentation_options::blur_disabled())backdrops={};
		for(float value:fade)if(!std::isfinite(value)||value<0||value>1)return false;
		for(unsigned i=0;i<3;++i)if(fade[i]>fade[3])return false;
		if(!ink||!prepare_background_copy(context,destination,width,height))return false;
		for (const auto& v : canvas)
			for (float x : v) if (!std::isfinite(x) || v[3] <= .001f) return false;
		// Head-relative canvas is parallel to the eye plane. Preserve binocular
		// offsets and asymmetric FOV; never center independently in each eye.
		const vec4 rect{(canvas[0][0]/canvas[0][3]+1)*.5f, (1-canvas[0][1]/canvas[0][3])*.5f,
			(canvas[1][0]/canvas[1][3]-canvas[0][0]/canvas[0][3])*.5f,
			(canvas[0][1]/canvas[0][3]-canvas[2][1]/canvas[2][3])*.5f};
		if (rect[2] <= .001f || rect[3] <= .001f) return false;
		constexpr projected_quad full{{{-1,1,0,1},{1,1,0,1},{-1,-1,0,1},{1,-1,0,1}}};
		return draw_internal(context, background_view_.Get(), ink, destination, full, width, height,
			0, 0, rect, true,false,nullptr,{0,0,1,1},backdrops,1,false,subtractive,nullptr,blur_mode::none,1,fade);
	}
	bool renderer::draw_recording_frame(ID3D11DeviceContext* context, ID3D11RenderTargetView* target,
		unsigned width,unsigned height,const vec4& protected_pixels,float line_pixels,
		float outside_dim,ID3D11ShaderResourceView* caption,const vec4& caption_pixels) noexcept
	{
		if (!context || !target || context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE || width<2 || height<2 ||
			width>16384 || height>16384 || !std::isfinite(line_pixels) || line_pixels<=0 || line_pixels>64 ||
			!std::isfinite(outside_dim) || outside_dim<0 || outside_dim>1) return false;
		for (float x:protected_pixels) if (!std::isfinite(x)) return false;
		if (protected_pixels[0]<-1 || protected_pixels[1]<-1 || protected_pixels[2]>width+1.f || protected_pixels[3]>height+1.f ||
			protected_pixels[2]<=protected_pixels[0] || protected_pixels[3]<=protected_pixels[1]) return false;
		Microsoft::WRL::ComPtr<ID3D11Device> device,owner;
		context->GetDevice(&device);target->GetDevice(&owner);
		if (device.Get()!=owner.Get() || !ensure(device.Get())) return false;
		for (float x:caption_pixels) if (!std::isfinite(x)) return false;
		if (caption)
		{
			caption->GetDevice(&owner);
			if (device.Get()!=owner.Get() || caption_pixels[0]<0 || caption_pixels[1]<protected_pixels[3] ||
				caption_pixels[2]<=0 || caption_pixels[3]<=0 || caption_pixels[0]+caption_pixels[2]>width ||
				caption_pixels[1]+caption_pixels[3]>height) return false;
		}
		Microsoft::WRL::ComPtr<ID3D11Resource> resource;target->GetResource(&resource);
		Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
		if (FAILED(resource.As(&texture))) return false;
		D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
		if (caption)
		{
			Microsoft::WRL::ComPtr<ID3D11Resource> ink_resource;caption->GetResource(&ink_resource);
			D3D11_SHADER_RESOURCE_VIEW_DESC ink_desc{};caption->GetDesc(&ink_desc);
			if (ink_resource.Get()==resource.Get() || ink_desc.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D) return false;
		}
		D3D11_RENDER_TARGET_VIEW_DESC view{};target->GetDesc(&view);
		if (desc.Width!=width || desc.Height!=height || desc.ArraySize!=1 || desc.SampleDesc.Count!=1 ||
			view.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D || view.Texture2D.MipSlice!=0) return false;
		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(deferred_->Map(constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped))) return false;
		constexpr projected_quad full{{{-1,1,.5f,1},{1,1,.5f,1},{-1,-1,.5f,1},{1,-1,.5f,1}}};
		const constants data{full,{1.f/width,1.f/height,line_pixels,outside_dim},protected_pixels,caption?caption_pixels:vec4{}};
		std::memcpy(mapped.pData,&data,sizeof(data));deferred_->Unmap(constants_.Get(),0);
		const D3D11_VIEWPORT viewport{0,0,static_cast<float>(width),static_cast<float>(height),0,1};
		deferred_->RSSetViewports(1,&viewport);deferred_->RSSetState(raster_.Get());
		deferred_->OMSetRenderTargets(1,&target,nullptr);deferred_->OMSetDepthStencilState(depth_.Get(),0);
		deferred_->OMSetBlendState(ink_blend_.Get(),nullptr,UINT_MAX);
		deferred_->IASetInputLayout(nullptr);deferred_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		deferred_->VSSetShader(vs_.Get(),nullptr,0);deferred_->PSSetShader(recording_ps_.Get(),nullptr,0);
		deferred_->PSSetShaderResources(1,1,&caption);
		auto* sampler=sampler_.Get();deferred_->PSSetSamplers(0,1,&sampler);
		auto* cb=constants_.Get();deferred_->VSSetConstantBuffers(0,1,&cb);deferred_->PSSetConstantBuffers(0,1,&cb);
		deferred_->Draw(4,0);
		Microsoft::WRL::ComPtr<ID3D11CommandList> commands;
		if (FAILED(deferred_->FinishCommandList(FALSE,&commands))) {deferred_->ClearState();return false;}
		if (!native_conversion_command_list::mark(commands.Get())) return false;
		context->ExecuteCommandList(commands.Get(),TRUE);return true;
	}
	bool renderer::draw_optic(ID3D11DeviceContext* context, ID3D11ShaderResourceView* background,
		ID3D11ShaderResourceView* reticle, ID3D11RenderTargetView* target, const projected_quad& corners,
		unsigned width, unsigned height, const vec4& optic, ID3D11DepthStencilView* scene_depth,
		const vec4& source_window,float pupil_radius,float eye_box_scale) noexcept
	{
		for(float x:source_window) if(!std::isfinite(x)) return false;
		if(source_window[0]<0 || source_window[1]<0 || source_window[2]<.0001f || source_window[3]<.0001f ||
			source_window[0]+source_window[2]>1 || source_window[1]+source_window[3]>1) return false;
		for (float x:optic) if (!std::isfinite(x)) return false;
		if (optic[2]<1 || optic[2]>12 || optic[3]<0 || optic[3]>1) return false;
		if(!std::isfinite(pupil_radius) || pupil_radius<=0 || pupil_radius>1)return false;
		if(!std::isfinite(eye_box_scale) || eye_box_scale<1 || eye_box_scale>2)return false;
		for (const auto& v:corners) if (v[3]<=.001f) return false;
		if (!background || !target) return false;
		Microsoft::WRL::ComPtr<ID3D11Resource> source_resource,target_resource;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> source_texture,target_texture;
		background->GetResource(&source_resource);target->GetResource(&target_resource);
		if (FAILED(source_resource.As(&source_texture)) || FAILED(target_resource.As(&target_texture))) return false;
		D3D11_SHADER_RESOURCE_VIEW_DESC source_view{};background->GetDesc(&source_view);
		D3D11_RENDER_TARGET_VIEW_DESC target_view{};target->GetDesc(&target_view);
		if (source_view.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D || source_view.Texture2D.MostDetailedMip!=0 ||
			target_view.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D || target_view.Texture2D.MipSlice!=0) return false;
		for (auto* texture:{source_texture.Get(),target_texture.Get()})
		{
			D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
			if (desc.Width!=width || desc.Height!=height || desc.ArraySize!=1 || desc.SampleDesc.Count!=1) return false;
		}
		if(scene_depth)
		{
			Microsoft::WRL::ComPtr<ID3D11Device> owner,device;target->GetDevice(&device);scene_depth->GetDevice(&owner);
			Microsoft::WRL::ComPtr<ID3D11Resource> resource;scene_depth->GetResource(&resource);
			Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
			if(owner!=device || FAILED(resource.As(&texture))) return false;
			D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
			if(desc.Width!=width || desc.Height!=height || desc.SampleDesc.Count!=1 || desc.ArraySize!=1) return false;
		}
		return draw_internal(context,background,reticle,target,corners,width,height,0,0,optic,false,true,scene_depth,source_window,{},pupil_radius,false,nullptr,nullptr,blur_mode::none,eye_box_scale);
	}
	bool renderer::draw_screen_scope(ID3D11DeviceContext* context,ID3D11ShaderResourceView* scene,ID3D11ShaderResourceView* ink,
		ID3D11RenderTargetView* target,const projected_quad& canvas,unsigned width,unsigned height,ID3D11ShaderResourceView* shadow,ID3D11ShaderResourceView* flash) noexcept
	{
		scope_status_={"canvas",0,false};
		for(const auto& corner:canvas)for(float x:corner)if(!std::isfinite(x) || std::abs(x)>100)return false;
		// Nonzero plane edges are required, including when it lies behind the eye.
		if(canvas[0]==canvas[1] || canvas[0]==canvas[2])return false;
		return draw_internal(context,scene,ink,target,canvas,width,height,0,0,{},false,false,nullptr,{0,0,1,1},{},1,true,shadow,flash);
	}
	bool renderer::draw_internal(ID3D11DeviceContext* context, ID3D11ShaderResourceView* background,
		ID3D11ShaderResourceView* ink, ID3D11RenderTargetView* destination,
		const projected_quad& corners, unsigned width, unsigned height,
		float blur_pixels, float blur_strength, const vec4& canvas, bool screen_layer, bool optic_layer,
		ID3D11DepthStencilView* scene_depth, const vec4& source_window,std::span<const blur_region> backdrops,float pupil_radius,bool screen_scope,
		ID3D11ShaderResourceView* shadow,ID3D11ShaderResourceView* flash,blur_mode blur,float eye_box_scale,const vec4& screen_fade) noexcept
	{
		const bool mask_only=blur!=blur_mode::none;
		if(screen_scope)scope_status_={"inputs",0,false};
		if (!context || !background || (!ink && !screen_scope) || !destination || !width || !height ||
			width > 16384 || height > 16384 || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE ||
			!std::isfinite(blur_pixels) || !std::isfinite(blur_strength)) return false;
		for (const auto& v : corners)
			for (float x : v) if (!std::isfinite(x)) return false;
		if (backdrops.size()>blur_region_capacity) return false;
		for (const auto& region:backdrops)
		{
			for (float x:region.bounds) if (!std::isfinite(x) || std::abs(x)>2048) return false;
			if (region.bounds[2]<=region.bounds[0] || region.bounds[3]<=region.bounds[1] ||
				!std::isfinite(region.alpha) || region.alpha<0 || region.alpha>1) return false;
		}
		Microsoft::WRL::ComPtr<ID3D11Device> device;
		context->GetDevice(&device);
		if(screen_scope)scope_status_.stage="resources";
		if (!ensure(device.Get())) {if(screen_scope)scope_status_=initialization_status_;return false;}
		if(screen_scope)scope_status_.stage="device_ownership";
		for (ID3D11DeviceChild* child : {static_cast<ID3D11DeviceChild*>(background),
			static_cast<ID3D11DeviceChild*>(ink), static_cast<ID3D11DeviceChild*>(destination)})
		{
			if(!child)continue;
			Microsoft::WRL::ComPtr<ID3D11Device> owner; child->GetDevice(&owner);
			if (owner.Get() != device.Get()) return false;
		}
		Microsoft::WRL::ComPtr<ID3D11Resource> bg, fg, output;
		if(screen_scope)scope_status_.stage="resource_alias";
		background->GetResource(&bg); if(ink)ink->GetResource(&fg); destination->GetResource(&output);
		if (bg.Get() == output.Get() || fg.Get() == output.Get()) return false;
		for(auto* view:{shadow,flash})if(view)
		{
			Microsoft::WRL::ComPtr<ID3D11Device> owner;view->GetDevice(&owner);
			Microsoft::WRL::ComPtr<ID3D11Resource> resource;view->GetResource(&resource);
			if(owner!=device || resource==output)return false;
		}
		D3D11_MAPPED_SUBRESOURCE mapped{};
		const auto map_result=deferred_->Map(constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		if (FAILED(map_result)){if(screen_scope)scope_status_={"constant_buffer",map_result,true};return false;}
		constants data{corners, {1.f/width, 1.f/height,
			std::clamp(blur_pixels, 0.f, mask_only ? 24.f : 8.f),
			std::clamp(blur_strength, 0.f, mask_only ? 4.f : 1.f)}, canvas, {}, source_window};
		if(optic_layer){data.ink_uv[0]=pupil_radius;data.ink_uv[1]=eye_box_scale;}
		if(screen_layer)data.ink_uv=screen_fade;
		if(blur==blur_mode::panel)data.ink_uv[0]=1;
		data.backdrop_count[0]=float(backdrops.size());
		data.backdrop_count[1]=shadow?1.f:0.f;data.backdrop_count[2]=flash?1.f:0.f;
		data.backdrop_count[3]=ink?1.f:0.f;
		for (std::size_t i=0;i<backdrops.size();++i)
		{data.backdrop_bounds[i]=backdrops[i].bounds;data.backdrop_settings[i][0]=backdrops[i].alpha;}
		std::memcpy(mapped.pData, &data, sizeof(data)); deferred_->Unmap(constants_.Get(), 0);
		const D3D11_VIEWPORT viewport{0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
		if(mask_only)
		{
			deferred_->CopySubresourceRegion(background_copy_.Get(),0,0,0,0,output.Get(),0,nullptr);
			deferred_->GenerateMips(background_view_.Get());
		}
		else if(screen_layer)deferred_->CopyResource(background_copy_.Get(), output.Get());
		if(screen_scope){const float black[]{0,0,0,1};deferred_->ClearRenderTargetView(destination,black);}
		deferred_->RSSetViewports(1, &viewport); deferred_->RSSetState(raster_.Get());
		deferred_->OMSetRenderTargets(1, &destination, scene_depth);
		deferred_->OMSetDepthStencilState(scene_depth ? scene_depth_.Get() : depth_.Get(), 0);
		deferred_->OMSetBlendState(blend_.Get(), nullptr, UINT_MAX);
		deferred_->IASetInputLayout(nullptr); deferred_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		deferred_->VSSetShader(vs_.Get(), nullptr, 0);
		deferred_->PSSetShader(mask_only ? mask_ps_.Get() : screen_scope ? scope_ps_.Get() : optic_layer ? optic_ps_.Get() : screen_layer ? screen_ps_.Get() : ps_.Get(), nullptr, 0);
		ID3D11Buffer* cb = constants_.Get();
		deferred_->VSSetConstantBuffers(0, 1, &cb); deferred_->PSSetConstantBuffers(0, 1, &cb);
		ID3D11ShaderResourceView* textures[]{background, ink,shadow,flash};
		deferred_->PSSetShaderResources(0, 4, textures);
		ID3D11SamplerState* sampler = sampler_.Get(); deferred_->PSSetSamplers(0, 1, &sampler);
		deferred_->Draw(4, 0);
		Microsoft::WRL::ComPtr<ID3D11CommandList> commands;
		const auto result = deferred_->FinishCommandList(FALSE, &commands);
		if (FAILED(result)) { if(screen_scope)scope_status_={"finish_command_list",result,true};deferred_->ClearState(); return false; }
		if(screen_scope)scope_status_={"command_list_tag",0,false};
		if (!native_conversion_command_list::mark(commands.Get())) return false;
		// The established owner holds H2's GPU mutex. Recording has not touched
		// native state; Execute(TRUE) restores all of it, including shader instances.
		context->ExecuteCommandList(commands.Get(), TRUE);
		if(screen_scope)scope_status_={"complete",result,true};
		return true;
	}
}
