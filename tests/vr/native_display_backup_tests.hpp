#pragma once
#include "component/vr/native_display_backup.hpp"

template<class Check> void native_display_backup_tests(ID3D11Device* device,ID3D11DeviceContext* context,Check check)
{
	using Microsoft::WRL::ComPtr;
	vr::native_display_contract::source_backup backup;
	for(const auto width:{8u,16u})
	{
		D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=width;
		desc.ArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_R11G11B10_FLOAT;
		desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
		ComPtr<ID3D11Texture2D> raw,display,staging;ComPtr<ID3D11RenderTargetView> raw_view,display_view;
		bool ok=SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&raw)) && SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&display)) &&
			SUCCEEDED(device->CreateRenderTargetView(raw.Get(),nullptr,&raw_view)) && SUCCEEDED(device->CreateRenderTargetView(display.Get(),nullptr,&display_view));
		desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
		ok=ok && SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&staging));
		check(ok,"native display preservation WARP resources");if(!ok)return;
		const auto pixel=[&](ID3D11Texture2D* texture){
			context->CopyResource(staging.Get(),texture);D3D11_MAPPED_SUBRESOURCE mapped{};std::uint32_t value{};
			const bool readable=SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));check(readable,"native display preservation readback");
			if(readable){std::memcpy(&value,mapped.pData,sizeof(value));context->Unmap(staging.Get(),0);}return value;
		};
		const float red[]{1,0,0,1},blue[]{0,0,1,1},green[]{0,1,0,1};
		for(const auto* eye_color:{red,blue})
		{
			context->ClearRenderTargetView(raw_view.Get(),green);const auto transformed=pixel(raw.Get());
			context->ClearRenderTargetView(raw_view.Get(),eye_color);const auto hdr=pixel(raw.Get());
			context->ClearRenderTargetView(display_view.Get(),blue);
			check(backup.compose(context,raw.Get(),display.Get(),[&]{context->ClearRenderTargetView(raw_view.Get(),green);return true;}),
				"per-eye native effect can temporarily reuse the HDR slot");
			check(pixel(raw.Get())==hdr && pixel(display.Get())==transformed,"effect result committed while the exact current eye HDR source survives");
			check(!backup.compose(context,raw.Get(),display.Get(),[&]{context->ClearRenderTargetView(raw_view.Get(),blue);return false;}) &&
				pixel(raw.Get())==hdr && pixel(display.Get())==transformed,"rejected effect restores HDR and does not publish a partial output");
			try{backup.compose(context,raw.Get(),display.Get(),[&]() -> bool {context->ClearRenderTargetView(raw_view.Get(),blue);throw 1;});}catch(int){}
			check(pixel(raw.Get())==hdr,"exception restores native HDR source");
		}
		bool invoked{};
		check(!backup.compose(context,raw.Get(),raw.Get(),[&]{invoked=true;return true;}) && !invoked,"aliasing source/output rejected before native effect execution");
	}
}
