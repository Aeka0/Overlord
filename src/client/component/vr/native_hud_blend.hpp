#pragma once
#include <d3d11.h>

namespace vr::native_hud_capture
{
	inline bool additive_coverage_blend(const D3D11_BLEND_DESC& native,D3D11_BLEND_DESC& output) noexcept
	{
		const auto& rgb=native.RenderTarget[0];
		if(native.AlphaToCoverageEnable || !rgb.BlendEnable ||
			(rgb.SrcBlend!=D3D11_BLEND_ONE && rgb.SrcBlend!=D3D11_BLEND_SRC_ALPHA) ||
			rgb.DestBlend!=D3D11_BLEND_ONE || rgb.BlendOp!=D3D11_BLEND_OP_ADD || (rgb.RenderTargetWriteMask&7)!=7)return false;
		output=native;auto& layer=output.RenderTarget[0];
		// RGB is the positive emitted light. Zero coverage keeps the background
		// contribution intact when composited before the lens shadow.
		layer.SrcBlendAlpha=D3D11_BLEND_ZERO;layer.DestBlendAlpha=D3D11_BLEND_ONE;
		layer.BlendOpAlpha=D3D11_BLEND_OP_ADD;layer.RenderTargetWriteMask|=D3D11_COLOR_WRITE_ENABLE_ALPHA;return true;
	}
	// Capture the positive subtractand and its coverage separately. The final
	// compositor applies max(D*(1-A)-S,0) before subsequent source-over ink.
	// Flattening colored reverse subtraction onto transparent UNORM loses S.
	inline bool subtractive_coverage_blend(const D3D11_BLEND_DESC& native,D3D11_BLEND_DESC& output) noexcept
	{
		const auto& rgb=native.RenderTarget[0];
		if(native.AlphaToCoverageEnable || !rgb.BlendEnable || rgb.SrcBlend!=D3D11_BLEND_SRC_ALPHA ||
			rgb.DestBlend!=D3D11_BLEND_INV_SRC_ALPHA || rgb.BlendOp!=D3D11_BLEND_OP_REV_SUBTRACT ||
			(rgb.RenderTargetWriteMask&7)!=7)return false;
		output=native;auto& layer=output.RenderTarget[0];
		layer.BlendOp=layer.BlendOpAlpha=D3D11_BLEND_OP_ADD;
		layer.SrcBlendAlpha=D3D11_BLEND_ONE;layer.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;
		layer.RenderTargetWriteMask|=D3D11_COLOR_WRITE_ENABLE_ALPHA;return true;
	}
	// Flatten only operations representable by a premultiplied transparent layer.
	// For the audited black unlit quads, reverse subtraction is D*(1-a)-0;
	// ordinary source-over of black has precisely the same RGB result. Nonblack
	// reverse subtraction cannot in general be flattened over arbitrary scenery.
	inline bool coverage_blend(const D3D11_BLEND_DESC& native, bool black_unlit_quad,
		D3D11_BLEND_DESC& output) noexcept
	{
		const auto& rgb = native.RenderTarget[0];
		if (native.AlphaToCoverageEnable || !rgb.BlendEnable ||
			rgb.DestBlend != D3D11_BLEND_INV_SRC_ALPHA ||
			(rgb.SrcBlend != D3D11_BLEND_SRC_ALPHA && rgb.SrcBlend != D3D11_BLEND_ONE) ||
			(rgb.BlendOp != D3D11_BLEND_OP_ADD &&
				!(black_unlit_quad && rgb.BlendOp == D3D11_BLEND_OP_REV_SUBTRACT)) ||
			(rgb.RenderTargetWriteMask & 7) != 7) return false;
		output = native; // Preserve RGB operation, factors and channel behavior.
		auto& alpha = output.RenderTarget[0];
		alpha.SrcBlendAlpha = D3D11_BLEND_ONE;
		alpha.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		alpha.BlendOpAlpha = D3D11_BLEND_OP_ADD;
		alpha.RenderTargetWriteMask |= D3D11_COLOR_WRITE_ENABLE_ALPHA;
		return true;
	}
	inline void attenuate_text_blend(D3D11_BLEND_DESC& output) noexcept
	{
		// The shader supplies the fade's premultiplied color C*f. Preserve
		// glyph coverage A and blend E*(1-f)+C*f*A on this separate text plane.
		// Over the already faded scene C*f+B*(1-f), this matches native order
		// for both black and white without applying the fade a second time.
		auto& blend=output.RenderTarget[0];
		blend.SrcBlend=D3D11_BLEND_DEST_ALPHA;blend.SrcBlendAlpha=D3D11_BLEND_ZERO;
		blend.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;
		blend.DestBlendAlpha=D3D11_BLEND_ONE;
		blend.BlendOp=blend.BlendOpAlpha=D3D11_BLEND_OP_ADD;
	}
}
