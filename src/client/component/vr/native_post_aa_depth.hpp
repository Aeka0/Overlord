#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <cstring>

namespace vr::native_post_aa
{
	// Native SMAA uses the scene DSV as scratch: 0x140296F00 clears it through
	// 0x140786AF0 with D3D11_CLEAR_DEPTH. VR optics still consume this depth
	// afterwards. Preserve its exact GPU contents without touching native binds.
	class depth_preserver
	{
		Microsoft::WRL::ComPtr<ID3D11Texture2D> saved_;
	public:
		template<class Draw> bool preserve(ID3D11DeviceContext* context,
			ID3D11DepthStencilView* view, Draw&& draw)
		{
			if (!context || !view) return false;
			Microsoft::WRL::ComPtr<ID3D11Resource> resource;
			Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
			Microsoft::WRL::ComPtr<ID3D11Device> device, owner;
			view->GetResource(&resource);
			context->GetDevice(&device);
			if (!resource || FAILED(resource.As(&texture))) return false;
			texture->GetDevice(&owner);
			D3D11_TEXTURE2D_DESC desc{};
			texture->GetDesc(&desc);
			if (device != owner || !desc.Width || !desc.Height || desc.ArraySize != 1 || desc.MipLevels != 1 ||
				desc.SampleDesc.Count != 1 || desc.SampleDesc.Quality || desc.Usage != D3D11_USAGE_DEFAULT ||
				desc.CPUAccessFlags || desc.MiscFlags || !(desc.BindFlags & D3D11_BIND_DEPTH_STENCIL)) return false;
			desc.BindFlags = 0;
			if (saved_)
			{
				D3D11_TEXTURE2D_DESC saved_desc{};
				Microsoft::WRL::ComPtr<ID3D11Device> saved_device;
				saved_->GetDesc(&saved_desc);
				saved_->GetDevice(&saved_device);
				if (device != saved_device || std::memcmp(&desc, &saved_desc, sizeof(desc))) saved_.Reset();
			}
			if (!saved_ && FAILED(device->CreateTexture2D(&desc, nullptr, &saved_))) return false;
			context->CopyResource(saved_.Get(), texture.Get());
			struct restore
			{
				ID3D11DeviceContext* context;
				ID3D11Texture2D* destination;
				ID3D11Texture2D* source;
				~restore() { context->CopyResource(destination, source); }
			} scoped{context, texture.Get(), saved_.Get()};
			return draw();
		}
	};
}
