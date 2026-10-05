#include <std_include.hpp>
#include "opaque_mesh_renderer.hpp"
#include "native_conversion_command_list.hpp"
#include "game/assets.hpp"
#include <d3dcompiler.h>

namespace vr::opaque_mesh
{
	std::shared_ptr<const mesh> mesh::create(const game::XModel* model)
	{
		// Only the already validated rigid_part output is accepted here.
		if(!model || model->numBones!=1 || model->numLods!=1 || !model->numsurfs ||
			model->lodInfo[0].numsurfs!=model->numsurfs || !model->lodInfo[0].surfs)return {};
		std::vector<spatial_math::vec> positions;std::vector<unsigned> triangles;
		Microsoft::WRL::ComPtr<ID3D11Device> device;
		for(unsigned n=0;n<model->numsurfs;++n)
		{
			const auto& s=model->lodInfo[0].surfs[n];
			if((s.flags&4) || !s.vb0 || !s.verts0.packedVerts0 || !s.triIndices || !s.vertCount || !s.triCount ||
				s.rigidVertListCount!=1 || !s.rigidVertLists || s.rigidVertLists[0].boneOffset ||
				triangles.size()+size_t(s.triCount)*3>786432)return {};
			Microsoft::WRL::ComPtr<ID3D11Device> owner;s.vb0->GetDevice(&owner);
			if(device && device!=owner)return {};device=owner;
			std::vector<unsigned> remap(s.vertCount,UINT_MAX);
			for(unsigned f=0;f<s.triCount;++f)for(const auto index:{s.triIndices[f].v1,s.triIndices[f].v2,s.triIndices[f].v3})
			{
				if(index>=s.vertCount)return {};
				if(remap[index]==UINT_MAX)
				{
					const auto& p=s.verts0.packedVerts0[index].xyz;
					for(float v:p)if(!std::isfinite(v) || std::abs(v)>10000)return {};
					if(positions.size()>=262144)return {};
					remap[index]=static_cast<unsigned>(positions.size());positions.push_back({p[0],p[1],p[2]});
				}
				triangles.push_back(remap[index]);
			}
		}
		if(!device || triangles.empty())return {};
		auto result=std::make_shared<mesh>();
		D3D11_BUFFER_DESC desc{};desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
		desc.ByteWidth=static_cast<UINT>(positions.size()*sizeof(positions[0]));D3D11_SUBRESOURCE_DATA data{positions.data(),0,0};
		if(FAILED(device->CreateBuffer(&desc,&data,&result->vertices)))return {};
		desc.BindFlags=D3D11_BIND_INDEX_BUFFER;desc.ByteWidth=static_cast<UINT>(triangles.size()*sizeof(triangles[0]));data.pSysMem=triangles.data();
		if(FAILED(device->CreateBuffer(&desc,&data,&result->indices)))return {};
		result->index_count=static_cast<unsigned>(triangles.size());return result;
	}
	namespace
	{
		constexpr char shader[]=R"(
cbuffer Transform : register(b0) { row_major float4x4 clipFromModel; float4 modulation; };
Texture2D<float4> original : register(t0);
float4 vs(float3 position:POSITION):SV_Position { return mul(float4(position,1),clipFromModel); }
float4 ps(float4 position:SV_Position):SV_Target {
    const float3 yellow=float3(1,0.843137,0);
    float3 native=original.Load(int3(int2(position.xy),0)).rgb;
    return float4(native*lerp(float3(1,1,1),yellow,modulation.x)+yellow*modulation.y,1);
}
)";
		struct constants {matrix transform;std::array<float,4> modulation;};
		static_assert(sizeof(constants)==80);
	}
	bool renderer::ensure(ID3D11Device* device)
	{
		if(device_.Get()==device)return ready_;*this={};device_=device;
		Microsoft::WRL::ComPtr<ID3DBlob> vs,ps,error;
		if(FAILED(D3DCompile(shader,sizeof(shader)-1,"opaque-mesh",nullptr,nullptr,"vs","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vs,&error)) ||
			FAILED(D3DCompile(shader,sizeof(shader)-1,"opaque-mesh",nullptr,nullptr,"ps","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&ps,&error)) ||
			FAILED(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vs_)) ||
			FAILED(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&ps_)) ||
			FAILED(device->CreateDeferredContext(0,&deferred_)))return false;
		const D3D11_INPUT_ELEMENT_DESC element{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
		if(FAILED(device->CreateInputLayout(&element,1,vs->GetBufferPointer(),vs->GetBufferSize(),&layout_)))return false;
		D3D11_BUFFER_DESC cb{};cb.ByteWidth=sizeof(constants);cb.Usage=D3D11_USAGE_DYNAMIC;
		cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;cb.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
		D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
		// One depth quantum avoids roundoff against the same native surface.
		// Preserve reverse-Z and scene occlusion; never force a foreground depth.
		raster.DepthBias=1;
		D3D11_DEPTH_STENCIL_DESC depth{};depth.DepthEnable=TRUE;depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
		depth.DepthFunc=D3D11_COMPARISON_GREATER_EQUAL;
		D3D11_BLEND_DESC blend{};blend.RenderTarget[0].BlendEnable=FALSE;
		blend.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
		ready_=SUCCEEDED(device->CreateBuffer(&cb,nullptr,&constants_)) && SUCCEEDED(device->CreateRasterizerState(&raster,&raster_)) &&
			SUCCEEDED(device->CreateDepthStencilState(&depth,&depth_)) && SUCCEEDED(device->CreateBlendState(&blend,&blend_));
		return ready_;
	}
	bool renderer::prepare_original(ID3D11Texture2D* source,DXGI_FORMAT view_format)
	{
		D3D11_TEXTURE2D_DESC desc{},previous{};source->GetDesc(&desc);
		D3D11_SHADER_RESOURCE_VIEW_DESC old_view{};
		if(original_)original_->GetDesc(&previous);if(original_view_)original_view_->GetDesc(&old_view);
		if(original_view_ && desc.Width==previous.Width && desc.Height==previous.Height &&
			desc.Format==previous.Format && old_view.Format==view_format)return true;
		original_.Reset();original_view_.Reset();
		desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.CPUAccessFlags=desc.MiscFlags=0;
		D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=view_format;
		view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;view.Texture2D.MipLevels=1;
		return SUCCEEDED(device_->CreateTexture2D(&desc,nullptr,&original_)) &&
			SUCCEEDED(device_->CreateShaderResourceView(original_.Get(),&view,&original_view_));
	}
	bool renderer::render(ID3D11DeviceContext* context,ID3D11RenderTargetView* color,ID3D11DepthStencilView* depth,
		std::span<const draw> draws,unsigned width,unsigned height,float tint,float emission) noexcept
	{
		if(!context || !color || !depth || !width || !height || width>16384 || height>16384 || draws.size()>128 ||
			!std::isfinite(tint) || tint<0 || tint>1 || !std::isfinite(emission) || emission<0 || emission>.25f ||
			context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return false;
		if(draws.empty())return true;
		Microsoft::WRL::ComPtr<ID3D11Device> device,owner;context->GetDevice(&device);color->GetDevice(&owner);
		if(owner!=device)return false;depth->GetDevice(&owner);if(owner!=device)return false;
		Microsoft::WRL::ComPtr<ID3D11Resource> resource;Microsoft::WRL::ComPtr<ID3D11Texture2D> texture,original;
		D3D11_RENDER_TARGET_VIEW_DESC color_view{};color->GetDesc(&color_view);
		if(color_view.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D || color_view.Texture2D.MipSlice)return false;
		for(bool checking_depth:{false,true})
		{
			resource.Reset();texture.Reset();if(checking_depth)depth->GetResource(&resource);else color->GetResource(&resource);
			if(FAILED(resource.As(&texture)))return false;D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
			if(desc.Width!=width || desc.Height!=height || desc.SampleDesc.Count!=1)return false;
			if(!checking_depth)
			{
				if(desc.ArraySize!=1 || desc.MipLevels!=1)return false;
				original=texture;
			}
		}
		for(const auto& d:draws)
		{
			if(!d.geometry || !d.geometry->vertices || !d.geometry->indices || !d.geometry->index_count)return false;
			d.geometry->vertices->GetDevice(&owner);if(owner!=device)return false;
			d.geometry->indices->GetDevice(&owner);if(owner!=device)return false;
			for(float v:d.clip_from_model)if(!std::isfinite(v))return false;
		}
		if(!ensure(device.Get()) || !prepare_original(original.Get(),color_view.Format))return false;
		// One immutable color snapshot per active eye, shared by every part.
		// Overlapping triangles cannot compound the tint or emission, and no
		// CPU readback or source-native material change is involved.
		deferred_->CopyResource(original_.Get(),original.Get());
		const D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1};
		deferred_->RSSetViewports(1,&viewport);deferred_->RSSetState(raster_.Get());
		deferred_->OMSetRenderTargets(1,&color,depth);deferred_->OMSetDepthStencilState(depth_.Get(),0);
		deferred_->OMSetBlendState(blend_.Get(),nullptr,UINT_MAX);
		deferred_->IASetInputLayout(layout_.Get());deferred_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		deferred_->VSSetShader(vs_.Get(),nullptr,0);deferred_->PSSetShader(ps_.Get(),nullptr,0);
		auto* cb=constants_.Get();deferred_->VSSetConstantBuffers(0,1,&cb);deferred_->PSSetConstantBuffers(0,1,&cb);
		auto* view=original_view_.Get();deferred_->PSSetShaderResources(0,1,&view);
		for(const auto& d:draws)
		{
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if(FAILED(deferred_->Map(constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped))){deferred_->ClearState();return false;}
			const constants data{d.clip_from_model,{tint,emission,0,0}};
			std::memcpy(mapped.pData,&data,sizeof(data));deferred_->Unmap(constants_.Get(),0);
			auto* vb=d.geometry->vertices.Get();const UINT stride=sizeof(spatial_math::vec),offset=0;
			deferred_->IASetVertexBuffers(0,1,&vb,&stride,&offset);deferred_->IASetIndexBuffer(d.geometry->indices.Get(),DXGI_FORMAT_R32_UINT,0);
			deferred_->DrawIndexed(d.geometry->index_count,0,0);
		}
		Microsoft::WRL::ComPtr<ID3D11CommandList> commands;
		if(FAILED(deferred_->FinishCommandList(FALSE,&commands))){deferred_->ClearState();return false;}
		if(!native_conversion_command_list::mark(commands.Get()))return false;
		context->ExecuteCommandList(commands.Get(),TRUE);return true;
	}
}
