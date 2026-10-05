#pragma once
#include "native_display_contract.hpp"
#include <wrl/client.h>

namespace vr::native_display_contract
{
	// Reuse the HDR slot as a temporary effect destination without changing
	// native target identities or losing the source needed by the desktop tail.
	class source_backup
	{
		Microsoft::WRL::ComPtr<ID3D11Texture2D> saved_;
	public:
		template<class Draw> bool compose(ID3D11DeviceContext* context,ID3D11Texture2D* raw,
			ID3D11Texture2D* display,Draw&& draw)
		{
			if(!context || !raw || !display || raw==display)return false;
			Microsoft::WRL::ComPtr<ID3D11Device> device,raw_device,display_device;
			context->GetDevice(&device);raw->GetDevice(&raw_device);display->GetDevice(&display_device);
			D3D11_TEXTURE2D_DESC a{},b{};raw->GetDesc(&a);display->GetDesc(&b);
			if(device!=raw_device || device!=display_device || !accepts(a) || !accepts(b) ||
				a.Width!=b.Width || a.Height!=b.Height)return false;
			if(saved_)
			{
				D3D11_TEXTURE2D_DESC saved{};saved_->GetDesc(&saved);
				Microsoft::WRL::ComPtr<ID3D11Device> saved_device;saved_->GetDevice(&saved_device);
				if(saved_device!=device || saved.Width!=a.Width || saved.Height!=a.Height || saved.Format!=a.Format)saved_.Reset();
			}
			if(!saved_)
			{
				a.BindFlags=0;
				if(FAILED(device->CreateTexture2D(&a,nullptr,&saved_)))return false;
			}
			context->CopyResource(saved_.Get(),raw);
			struct restore_source
			{
				ID3D11DeviceContext* context;ID3D11Texture2D* raw;ID3D11Texture2D* saved;
				~restore_source(){context->CopyResource(raw,saved);}
			} restore{context,raw,saved_.Get()};
			if(!draw())return false;
			context->CopyResource(display,raw);return true;
		}
	};
}
