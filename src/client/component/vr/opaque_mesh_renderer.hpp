#pragma once
#include "spatial_panel.hpp"
#include "gameplay/hand_pose_math.hpp"
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>
#include <span>

namespace game {struct XModel;}
namespace vr::opaque_mesh
{
	namespace hands=gameplay::hands;
	using matrix=spatial_panel::matrix;
	// Owns compact, immutable position/index buffers. No borrowed native assets
	// or materials survive preparation; a queued eye pair can retain this safely.
	struct mesh
	{
		Microsoft::WRL::ComPtr<ID3D11Buffer> vertices,indices;
		unsigned index_count{};
		static std::shared_ptr<const mesh> create(const game::XModel* rigid);
	};
	inline matrix transform(hands::anchor model,hands::vec origin,hands::vec eye,const matrix& vp) noexcept
	{
		matrix out{};
		for(unsigned row=0;row<4;++row)
		{
			hands::vec v{};
			if(row<3){v[row]=1;v=hands::rotate(model.rotation,v);}
			else v=hands::sub(hands::add(model.position,origin),eye);
			for(unsigned c=0;c<4;++c)
			{
				out[row*4+c]=row==3?vp[12+c]:0;
				for(unsigned a=0;a<3;++a)out[row*4+c]+=v[a]*vp[a*4+c];
			}
		}
		return out;
	}
	struct draw {const mesh* geometry{};matrix clip_from_model{};};
	class renderer
	{
		Microsoft::WRL::ComPtr<ID3D11Device> device_;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> deferred_;
		Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_;
		Microsoft::WRL::ComPtr<ID3D11PixelShader> ps_;
		Microsoft::WRL::ComPtr<ID3D11InputLayout> layout_;
		Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster_;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_;
		Microsoft::WRL::ComPtr<ID3D11BlendState> blend_;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> original_;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> original_view_;
		bool ready_{};
		bool ensure(ID3D11Device*);
		bool prepare_original(ID3D11Texture2D*,DXGI_FORMAT view_format);
	public:
		// Preserve the completed native material result, including its lighting.
		// An opaque multiply/add recolor, not alpha transparency or a new material.
		bool render(ID3D11DeviceContext*,ID3D11RenderTargetView*,ID3D11DepthStencilView*,
			std::span<const draw>,unsigned width,unsigned height,float tint,float emission) noexcept;
	};
}
