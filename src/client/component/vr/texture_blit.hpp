#pragma once
#include "component/d3d11.hpp"
#include <array>
#include <string>

namespace vr::texture_blit
{
	enum class encoding : unsigned
	{
		linear,
		encoded_premultiplied,
		encoded_opaque
	};
	// Short owner-thread draw, with all immediate-context state restored. Sources
	// remain on the same device; callers own their immutable publication leases.
	class renderer
	{
		struct cached_source
		{
			Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
			encoding format{};
		};
		Microsoft::WRL::ComPtr<ID3D11Device> device_;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> deferred_;
		Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_;
		Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_;
		Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer_;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_;
		Microsoft::WRL::ComPtr<ID3D11BlendState> blend_;
		Microsoft::WRL::ComPtr<ID3D11Buffer> parameters_;
		std::array<cached_source, 16> sources_;
		unsigned cursor_{};
		bool ensure(ID3D11Device*, std::string&);

	  public:
		struct draw_request
		{
			const d3d11::device_snapshot& graphics;
			ID3D11Texture2D* source;
			ID3D11RenderTargetView* destination;
			unsigned width, height;
			encoding source_encoding;
			float dim{};
		};
		bool draw(const draw_request&, std::string& error);
		void reset() noexcept;
	};
}
