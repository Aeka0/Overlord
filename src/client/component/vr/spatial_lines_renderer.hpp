#pragma once
#include "spatial_lines.hpp"
#include <d3d11.h>
#include <wrl/client.h>

namespace vr::spatial_lines
{
	// Fixed-capacity, owner-thread diagnostic renderer. No game state/assets,
	// GPU readbacks, depth queries or work while its caller's overlay is disabled.
	class renderer
	{
	public:
		bool draw(ID3D11DeviceContext* context, ID3D11RenderTargetView* target,
			const batch& projected, unsigned width, unsigned height, float stroke_pixels=2.5f) noexcept;
	private:
		bool ensure(ID3D11Device* device) noexcept;
		Microsoft::WRL::ComPtr<ID3D11Device> device_;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> deferred_;
		Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_;
		Microsoft::WRL::ComPtr<ID3D11PixelShader> ps_;
		Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster_;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_;
		Microsoft::WRL::ComPtr<ID3D11BlendState> blend_;
		bool ready_{};
	};
}
