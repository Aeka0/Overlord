#include <std_include.hpp>
#include "auxiliary_scene.hpp"

namespace vr::auxiliary_scene
{
	bool image_copy::capture(ID3D11DeviceContext* context,ID3D11Texture2D* source,
		ID3D11DepthStencilView* source_depth,std::uint64_t generation) noexcept
	{
		if(!context || !source || !generation || context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE) return false;
		Microsoft::WRL::ComPtr<ID3D11Device> device,owner;
		context->GetDevice(&device);source->GetDevice(&owner);if(device!=owner) return false;
		D3D11_TEXTURE2D_DESC cd{};source->GetDesc(&cd);
		if(!cd.Width || !cd.Height || cd.Width>16384 || cd.Height>16384 || cd.MipLevels!=1 ||
			cd.ArraySize!=1 || cd.SampleDesc.Count!=1 || cd.Usage!=D3D11_USAGE_DEFAULT) return false;
		if(device_!=device || generation_!=generation) {reset();device_=device;generation_=generation;}
		if(!color || std::memcmp(&cd,&color_desc_,sizeof(cd)))
		{
			auto desc=cd;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.CPUAccessFlags=0;desc.MiscFlags=0;
			Microsoft::WRL::ComPtr<ID3D11Texture2D> replacement;
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
			if(FAILED(device->CreateTexture2D(&desc,nullptr,&replacement)) ||
				FAILED(device->CreateShaderResourceView(replacement.Get(),nullptr,&srv))) return false;
			color=std::move(replacement);view=std::move(srv);color_desc_=cd;
		}
		Microsoft::WRL::ComPtr<ID3D11Texture2D> native_depth;
		if(source_depth)
		{
			source_depth->GetDevice(&owner);if(owner!=device) return false;
			Microsoft::WRL::ComPtr<ID3D11Resource> resource;source_depth->GetResource(&resource);
			if(FAILED(resource.As(&native_depth))) return false;
			D3D11_TEXTURE2D_DESC dd{};native_depth->GetDesc(&dd);
			D3D11_DEPTH_STENCIL_VIEW_DESC vd{};source_depth->GetDesc(&vd);
			if(dd.Width!=cd.Width || dd.Height!=cd.Height || dd.ArraySize!=1 || dd.SampleDesc.Count!=1 ||
				dd.MipLevels!=1 || vd.ViewDimension!=D3D11_DSV_DIMENSION_TEXTURE2D) return false;
			if(!depth || std::memcmp(&dd,&depth_desc_,sizeof(dd)) || std::memcmp(&vd,&depth_view_desc_,sizeof(vd)))
			{
				auto desc=dd;desc.BindFlags=D3D11_BIND_DEPTH_STENCIL;desc.CPUAccessFlags=0;desc.MiscFlags=0;
				Microsoft::WRL::ComPtr<ID3D11Texture2D> replacement;
				Microsoft::WRL::ComPtr<ID3D11DepthStencilView> dsv;
				if(FAILED(device->CreateTexture2D(&desc,nullptr,&replacement)) ||
					FAILED(device->CreateDepthStencilView(replacement.Get(),&vd,&dsv))) return false;
				depth=std::move(replacement);depth_view=std::move(dsv);depth_desc_=dd;depth_view_desc_=vd;
			}
		}
		if(color.Get()==source || (native_depth && depth==native_depth)) return false;
		context->CopyResource(color.Get(),source);
		if(native_depth) context->CopyResource(depth.Get(),native_depth.Get());
		return true;
	}
}
