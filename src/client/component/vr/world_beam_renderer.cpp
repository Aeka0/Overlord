#include <std_include.hpp>
#include "world_beam_renderer.hpp"
#include "native_conversion_command_list.hpp"
#include <d3dcompiler.h>
namespace vr::world_beam
{
	namespace
	{
		constexpr char shader[]=R"(
cbuffer Beam : register(b0) { float4 points[8]; float4 color; };
struct V { float4 position:SV_Position; float2 uv:TEXCOORD0; nointerpolation uint spot:TEXCOORD1; };
V vs(uint index:SV_VertexID,uint instance:SV_InstanceID) {
 V o; o.position=points[instance*4+index]; o.uv=float2((index&1)?1:-1,(index&2)?1:-1);o.spot=instance;return o;
}
float4 ps(V v):SV_Target {
 float a=v.spot ? saturate((1-dot(v.uv,v.uv))*2) : saturate(1-abs(v.uv.x));
 return float4(color.rgb,color.a*a*(v.spot?0.95:0.55));
})";
	}
	bool renderer::ensure(ID3D11Device* device)
	{
		if(device_.Get()==device)return ready_;*this={};device_=device;
		Microsoft::WRL::ComPtr<ID3DBlob> vs,ps,error;
		if(FAILED(D3DCompile(shader,sizeof(shader)-1,"world-beam",nullptr,nullptr,"vs","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vs,&error)) ||
			FAILED(D3DCompile(shader,sizeof(shader)-1,"world-beam",nullptr,nullptr,"ps","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&ps,&error)) ||
			FAILED(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vs_)) || FAILED(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&ps_)) ||
			FAILED(device->CreateDeferredContext(0,&deferred_)))return false;
		D3D11_BUFFER_DESC cb{};cb.ByteWidth=9*16;cb.Usage=D3D11_USAGE_DYNAMIC;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;cb.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
		D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
		D3D11_DEPTH_STENCIL_DESC depth{};depth.DepthEnable=TRUE;depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;depth.DepthFunc=D3D11_COMPARISON_GREATER_EQUAL;
		D3D11_BLEND_DESC blend{};auto& b=blend.RenderTarget[0];b.BlendEnable=TRUE;b.SrcBlend=D3D11_BLEND_SRC_ALPHA;b.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;b.BlendOp=D3D11_BLEND_OP_ADD;
		b.SrcBlendAlpha=D3D11_BLEND_ONE;b.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;b.BlendOpAlpha=D3D11_BLEND_OP_ADD;b.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
		ready_=SUCCEEDED(device->CreateBuffer(&cb,nullptr,&constants_)) && SUCCEEDED(device->CreateRasterizerState(&raster,&raster_)) &&
			SUCCEEDED(device->CreateDepthStencilState(&depth,&depth_)) && SUCCEEDED(device->CreateBlendState(&blend,&blend_));return ready_;
	}
	bool renderer::draw(ID3D11DeviceContext* context,ID3D11RenderTargetView* color,ID3D11DepthStencilView* depth,const projected& value,unsigned width,unsigned height)noexcept
	{
		if(!context || !color || !depth || !width || !height || width>16384 || height>16384 || context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return false;
		for(const auto& point:value.points)for(float x:point)if(!std::isfinite(x))return false;
		for(float x:value.color)if(!std::isfinite(x) || x<0 || x>64)return false;
		Microsoft::WRL::ComPtr<ID3D11Device> device,owner;context->GetDevice(&device);depth->GetDevice(&owner);if(owner!=device)return false;
		color->GetDevice(&owner);if(owner!=device || !ensure(device.Get()))return false;
		Microsoft::WRL::ComPtr<ID3D11Resource> resource;Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;D3D11_TEXTURE2D_DESC d{};
		depth->GetResource(&resource);if(FAILED(resource.As(&texture)))return false;texture->GetDesc(&d);
		if(d.Width!=width || d.Height!=height || d.SampleDesc.Count!=1)return false;
		D3D11_MAPPED_SUBRESOURCE mapped{};if(FAILED(deferred_->Map(constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return false;
		std::memcpy(mapped.pData,value.points.data(),sizeof(value.points));
		std::memcpy(static_cast<std::byte*>(mapped.pData)+sizeof(value.points),value.color.data(),sizeof(value.color));deferred_->Unmap(constants_.Get(),0);
		D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1};deferred_->RSSetViewports(1,&viewport);deferred_->RSSetState(raster_.Get());
		deferred_->OMSetRenderTargets(1,&color,depth);deferred_->OMSetDepthStencilState(depth_.Get(),0);deferred_->OMSetBlendState(blend_.Get(),nullptr,UINT_MAX);
		deferred_->IASetInputLayout(nullptr);deferred_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		deferred_->VSSetShader(vs_.Get(),nullptr,0);deferred_->PSSetShader(ps_.Get(),nullptr,0);auto* cb=constants_.Get();deferred_->VSSetConstantBuffers(0,1,&cb);
		deferred_->PSSetConstantBuffers(0,1,&cb);
		deferred_->DrawInstanced(4,value.spot?2:1,0,0);Microsoft::WRL::ComPtr<ID3D11CommandList> commands;
		if(FAILED(deferred_->FinishCommandList(FALSE,&commands)))return false;if(!native_conversion_command_list::mark(commands.Get()))return false;
		context->ExecuteCommandList(commands.Get(),TRUE);return true;
	}
}
