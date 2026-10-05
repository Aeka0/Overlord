#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <span>
#include <vector>
#include <cstddef>
#include <cstring>
#include <utility>

namespace scene_models
{
	// Immutable subset allocation, main asset-owner only. No immediate-context
	// calls/readback/fences on rendering workers. Retain owners with the descriptor.
	inline bool create_surface_indices(ID3D11Device* device,ID3D11ShaderResourceView* source_view,
		std::span<const std::byte> bytes,Microsoft::WRL::ComPtr<ID3D11Buffer>& buffer,
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& view)
	{
		if (!device || bytes.empty() || bytes.size()>65535*6) return false;
		std::vector<std::byte> upload((bytes.size()+3)&~size_t(3));
		std::memcpy(upload.data(),bytes.data(),bytes.size());
		D3D11_BUFFER_DESC desc{}; desc.ByteWidth=static_cast<UINT>(upload.size());
		desc.Usage=D3D11_USAGE_IMMUTABLE;
		desc.BindFlags=D3D11_BIND_INDEX_BUFFER | (source_view ? D3D11_BIND_SHADER_RESOURCE : 0);
		D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
		if (source_view)
		{
			source_view->GetDesc(&srv);
			if (srv.ViewDimension==D3D11_SRV_DIMENSION_BUFFEREX && srv.Format==DXGI_FORMAT_R32_TYPELESS && srv.BufferEx.Flags==D3D11_BUFFEREX_SRV_FLAG_RAW)
			{ desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS; srv.BufferEx.FirstElement=0; srv.BufferEx.NumElements=desc.ByteWidth/4; }
			else if (srv.ViewDimension==D3D11_SRV_DIMENSION_BUFFER && (srv.Format==DXGI_FORMAT_R16_UINT || srv.Format==DXGI_FORMAT_R32_UINT))
			{ srv.Buffer.FirstElement=0; srv.Buffer.NumElements=desc.ByteWidth/(srv.Format==DXGI_FORMAT_R16_UINT ? 2 : 4); }
			else return false;
		}
		Microsoft::WRL::ComPtr<ID3D11Buffer> created;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> created_view;
		const D3D11_SUBRESOURCE_DATA data{upload.data(),0,0};
		if (FAILED(device->CreateBuffer(&desc,&data,&created)) ||
			(source_view && FAILED(device->CreateShaderResourceView(created.Get(),&srv,&created_view)))) return false;
		buffer=std::move(created); view=std::move(created_view); return true;
	}
}
