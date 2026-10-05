#include <std_include.hpp>
#include "spatial_lines_renderer.hpp"
#include "native_conversion_command_list.hpp"
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")

namespace vr::spatial_lines
{
	namespace
	{
		constexpr char shader[]=R"(
struct Line { float4 a; float4 b; float4 color; };
cbuffer Lines : register(b0) { Line lines[192]; float4 viewport; };
struct Vertex { float4 position : SV_Position; float4 color : COLOR0; };
Vertex vs(uint index : SV_VertexID, uint instance : SV_InstanceID) {
    Line stroke=lines[instance];
    float2 delta=(stroke.b.xy/stroke.b.w-stroke.a.xy/stroke.a.w)*viewport.xy;
    float2 normal=float2(-delta.y,delta.x)/max(length(delta),0.00001);
    float4 position=index < 2 ? stroke.a : stroke.b;
    position.xy+=normal*(index & 1 ? 1 : -1)*viewport.z/viewport.xy*position.w;
    Vertex v; v.position=position; v.color=stroke.color; return v;
}
float4 ps(Vertex v) : SV_Target { return v.color; }
)";
		struct constants { std::array<segment,capacity> lines; vec4 viewport; };
		static_assert(capacity==192 && sizeof(constants)==192*48+16);
	}
	bool renderer::ensure(ID3D11Device* device) noexcept
	{
		if (device_.Get()==device) return ready_;
		*this={}; device_=device;
		Microsoft::WRL::ComPtr<ID3DBlob> vs,ps,errors;
		if (FAILED(D3DCompile(shader,sizeof(shader)-1,"spatial-lines",nullptr,nullptr,"vs","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vs,&errors)) ||
			FAILED(D3DCompile(shader,sizeof(shader)-1,"spatial-lines",nullptr,nullptr,"ps","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&ps,&errors)) ||
			FAILED(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vs_)) ||
			FAILED(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&ps_)) ||
			FAILED(device->CreateDeferredContext(0,&deferred_))) return false;
		D3D11_BUFFER_DESC buffer{};
		buffer.ByteWidth=sizeof(constants); buffer.Usage=D3D11_USAGE_DYNAMIC;
		buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER; buffer.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
		D3D11_RASTERIZER_DESC raster{};
		raster.FillMode=D3D11_FILL_SOLID; raster.CullMode=D3D11_CULL_NONE; raster.DepthClipEnable=TRUE;
		D3D11_DEPTH_STENCIL_DESC depth{};
		depth.DepthEnable=FALSE; depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO; depth.DepthFunc=D3D11_COMPARISON_ALWAYS;
		D3D11_BLEND_DESC blend{};
		auto& rt=blend.RenderTarget[0]; rt.BlendEnable=TRUE;
		rt.SrcBlend=D3D11_BLEND_SRC_ALPHA; rt.DestBlend=D3D11_BLEND_INV_SRC_ALPHA; rt.BlendOp=D3D11_BLEND_OP_ADD;
		rt.SrcBlendAlpha=D3D11_BLEND_ONE; rt.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA; rt.BlendOpAlpha=D3D11_BLEND_OP_ADD;
		rt.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
		ready_=SUCCEEDED(device->CreateBuffer(&buffer,nullptr,&constants_)) &&
			SUCCEEDED(device->CreateRasterizerState(&raster,&raster_)) &&
			SUCCEEDED(device->CreateDepthStencilState(&depth,&depth_)) && SUCCEEDED(device->CreateBlendState(&blend,&blend_));
		return ready_;
	}
	bool renderer::draw(ID3D11DeviceContext* context, ID3D11RenderTargetView* target,
		const batch& projected, unsigned width, unsigned height,float stroke_pixels) noexcept
	{
		if (!context || !target || !width || !height || width>16384 || height>16384 ||
			projected.count>capacity || !std::isfinite(stroke_pixels) || stroke_pixels<.5f || stroke_pixels>12 ||
			context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE) return false;
		if (!projected.count) return true;
		for (size_t n=0;n<projected.count;++n)
		{
			const auto& line=projected.lines[n];
			for (const auto& v : {line.a,line.b,line.color}) for (float x : v) if (!std::isfinite(x)) return false;
			if (line.a[3]<=0 || line.b[3]<=0) return false;
		}
		Microsoft::WRL::ComPtr<ID3D11Device> device,owner;
		context->GetDevice(&device); target->GetDevice(&owner);
		if (device.Get()!=owner.Get() || !ensure(device.Get())) return false;
		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(deferred_->Map(constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped))) return false;
		const constants data{projected.lines,{static_cast<float>(width),static_cast<float>(height),stroke_pixels,0}};
		std::memcpy(mapped.pData,&data,sizeof(data)); deferred_->Unmap(constants_.Get(),0);
		const D3D11_VIEWPORT viewport{0,0,static_cast<float>(width),static_cast<float>(height),0,1};
		deferred_->RSSetViewports(1,&viewport); deferred_->RSSetState(raster_.Get());
		deferred_->OMSetRenderTargets(1,&target,nullptr); deferred_->OMSetDepthStencilState(depth_.Get(),0);
		deferred_->OMSetBlendState(blend_.Get(),nullptr,UINT_MAX);
		deferred_->IASetInputLayout(nullptr); deferred_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		deferred_->VSSetShader(vs_.Get(),nullptr,0); deferred_->PSSetShader(ps_.Get(),nullptr,0);
		ID3D11Buffer* cb=constants_.Get(); deferred_->VSSetConstantBuffers(0,1,&cb);
		deferred_->DrawInstanced(4,static_cast<UINT>(projected.count),0,0);
		Microsoft::WRL::ComPtr<ID3D11CommandList> commands;
		if (FAILED(deferred_->FinishCommandList(FALSE,&commands))) { deferred_->ClearState(); return false; }
		if (!native_conversion_command_list::mark(commands.Get())) return false;
		context->ExecuteCommandList(commands.Get(),TRUE); // restore the complete native pipeline
		return true;
	}
}
