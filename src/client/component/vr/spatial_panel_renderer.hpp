#pragma once
#include "spatial_panel.hpp"
#include <d3d11.h>
#include <wrl/client.h>
#include <span>
#include "ui_canvas.hpp"

namespace vr::spatial_panel
{
	struct image_layer
	{
		ID3D11ShaderResourceView* ink{};
		projected_quad corners{};
		vec4 uv{0,0,1,1}; // atlas origin and extent
	};
	// Owner-thread renderer. No engine hooks, input policy or game assets.
	class renderer
	{
	public:
		// Preserve encoded premultiplied UI pixels while fitting the completed
		// native image into an independent transparent target (no native relayout).
		bool draw_canvas(ID3D11DeviceContext*,ID3D11ShaderResourceView*,ID3D11RenderTargetView*,const ui_canvas::mapping&) noexcept;
		bool draw(ID3D11DeviceContext* context, ID3D11ShaderResourceView* background,
			ID3D11ShaderResourceView* ink, ID3D11RenderTargetView* destination,
			const projected_quad& corners, unsigned width, unsigned height,
			float blur_pixels, float blur_strength,ID3D11ShaderResourceView* blur_mask=nullptr,
			const vec4& blur_window={0,0,1,1}) noexcept;
		// Alpha-only spatial mask over a snapshot of the current linear eye.
		// Mask RGB never tints the result; untouched pixels remain unchanged.
		bool draw_masked_blur(ID3D11DeviceContext*,ID3D11ShaderResourceView* mask,ID3D11RenderTargetView*,
			const projected_quad&,unsigned width,unsigned height,float radius,float strength,ID3D11DepthStencilView* scene_depth=nullptr) noexcept;
		// Uniform blur across the projected plane, independent of all UI alpha.
		bool draw_panel_blur(ID3D11DeviceContext*,ID3D11RenderTargetView*,const projected_quad&,
			unsigned width,unsigned height,float radius,ID3D11DepthStencilView* scene_depth=nullptr) noexcept;
		// Full-field narrative layer over the ALREADY composed linear target.
		// The canvas retains native layout; premultiplied fade RGBA fills its exterior.
		bool draw_screen_layer(ID3D11DeviceContext* context, ID3D11ShaderResourceView* ink,
			ID3D11RenderTargetView* destination, const projected_quad& canvas,
			unsigned width, unsigned height, const vec4& fade,std::span<const blur_region> backdrops={},
			ID3D11ShaderResourceView* subtractive=nullptr) noexcept;
		// Native encoded scope image and premultiplied HUD share one optical canvas.
		// Its exterior is opaque black; ink may be absent during native reload.
		bool draw_screen_scope(ID3D11DeviceContext*,ID3D11ShaderResourceView* scene,ID3D11ShaderResourceView* ink,
			ID3D11RenderTargetView*,const projected_quad& canvas,unsigned width,unsigned height,
			ID3D11ShaderResourceView* shadow=nullptr,ID3D11ShaderResourceView* flash=nullptr) noexcept;
		struct scope_draw_status {const char* stage{"not_attempted"};HRESULT result{};bool result_known{};const char* shader{"none"};};
		scope_draw_status last_scope_draw() const noexcept {return scope_status_;}
		// Batch native encoded ink with source-over, then composite once over the
		// current linear target. Overlapping icons preserve all preceding layers.
		bool draw_layers(ID3D11DeviceContext*, ID3D11RenderTargetView*, const image_layer*,
			unsigned count, unsigned width, unsigned height) noexcept;
		// Physical lens quad; xy is its aiming-axis UV, z magnification, w eye-box visibility.
		bool draw_optic(ID3D11DeviceContext*, ID3D11ShaderResourceView* background,
			ID3D11ShaderResourceView* reticle, ID3D11RenderTargetView*, const projected_quad&,
			unsigned width, unsigned height, const vec4& optic,
			ID3D11DepthStencilView* scene_depth = nullptr, const vec4& source_window = {0,0,1,1},float pupil_radius=1.f,float eye_box_scale=1.f) noexcept;
		// Blend only outside a protected pixel rectangle; no scene texture/copy.
		bool draw_recording_frame(ID3D11DeviceContext*, ID3D11RenderTargetView*, unsigned width,
			unsigned height, const vec4& protected_pixels, float line_pixels,
			float outside_dim, ID3D11ShaderResourceView* caption=nullptr, const vec4& caption_pixels={}) noexcept;
	private:
		enum class blur_mode {none,alpha_mask,panel};
		bool ensure(ID3D11Device* device) noexcept;
		bool prepare_background_copy(ID3D11DeviceContext*,ID3D11RenderTargetView*,unsigned,unsigned,bool mipmapped=false) noexcept;
		bool draw_internal(ID3D11DeviceContext*, ID3D11ShaderResourceView*, ID3D11ShaderResourceView*,
			ID3D11RenderTargetView*, const projected_quad&, unsigned, unsigned, float, float,
			const vec4& canvas, bool screen_layer, bool optic_layer = false,
			ID3D11DepthStencilView* scene_depth = nullptr, const vec4& source_window = {0,0,1,1},
			std::span<const blur_region> backdrops={},float pupil_radius=1.f,bool screen_scope=false,
			ID3D11ShaderResourceView* shadow=nullptr,ID3D11ShaderResourceView* flash=nullptr,blur_mode blur=blur_mode::none,float eye_box_scale=1.f,
			const vec4& screen_fade={}) noexcept;
		Microsoft::WRL::ComPtr<ID3D11Device> device_;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> deferred_;
		Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_;
		Microsoft::WRL::ComPtr<ID3D11PixelShader> ps_;
		Microsoft::WRL::ComPtr<ID3D11PixelShader> screen_ps_;
		Microsoft::WRL::ComPtr<ID3D11PixelShader> ink_ps_;
		Microsoft::WRL::ComPtr<ID3D11PixelShader> optic_ps_;
		Microsoft::WRL::ComPtr<ID3D11PixelShader> recording_ps_;
		Microsoft::WRL::ComPtr<ID3D11PixelShader> scope_ps_;
		Microsoft::WRL::ComPtr<ID3D11PixelShader> mask_ps_;
		Microsoft::WRL::ComPtr<ID3D11BlendState> ink_blend_;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> ink_copy_;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> ink_target_;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ink_view_;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> background_copy_;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> background_view_;
		Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
		Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster_;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> scene_depth_;
		Microsoft::WRL::ComPtr<ID3D11BlendState> blend_;
		bool ready_{};
		scope_draw_status scope_status_;
		scope_draw_status initialization_status_;
	};
}
