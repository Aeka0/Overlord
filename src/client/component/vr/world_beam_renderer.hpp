#pragma once
#include "spatial_panel.hpp"
#include <d3d11.h>
#include <wrl/client.h>
namespace vr::world_beam
{
	using namespace spatial_panel;
	struct projected {std::array<vec4,8> points{};bool spot{};vec4 color{1,.002f,.001f,1};};
	class renderer
	{
		Microsoft::WRL::ComPtr<ID3D11Device> device_;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> deferred_;
		Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_;
		Microsoft::WRL::ComPtr<ID3D11PixelShader> ps_;
		Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster_;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_;
		Microsoft::WRL::ComPtr<ID3D11BlendState> blend_;
		bool ready_{};bool ensure(ID3D11Device*);
	public:
		bool draw(ID3D11DeviceContext*,ID3D11RenderTargetView*,ID3D11DepthStencilView*,const projected&,unsigned width,unsigned height)noexcept;
	};
}
