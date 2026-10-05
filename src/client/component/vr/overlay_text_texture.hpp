#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <string>
#include <string_view>
#include <array>
#include <cstdint>
#include "native_caption_font.hpp"

namespace vr
{
	// Owner-thread, bounded caption rasterization. Rebuild only when the
	// device, text, face or pixel size changes. Native faces reuse loaded TTF bytes.
	class overlay_text_texture
	{
	public:
		using color=std::array<std::uint8_t,3>;
		// Layout padding around the measured text; exclude it when aligning ink.
		static constexpr unsigned padding_pixels=3;
		// Strict UTF-8 boundary for the shared in-game text catalog.
		bool ensure_utf8(ID3D11Device*, std::string_view, unsigned font_pixels, bool outline=false,std::shared_ptr<const native_caption_font::face> font={},color tint={255,255,255}) noexcept;
		bool ensure(ID3D11Device*, std::wstring_view, unsigned font_pixels, bool outline=false,std::shared_ptr<const native_caption_font::face> font={},color tint={255,255,255}) noexcept;
		ID3D11ShaderResourceView* view() const noexcept {return view_.Get();}
		unsigned width() const noexcept {return width_;}
		unsigned height() const noexcept {return height_;}
	private:
		static constexpr unsigned max_text_units=128;
		Microsoft::WRL::ComPtr<ID3D11Device> device_;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view_;
		std::wstring text_;
		unsigned font_pixels_{},width_{},height_{};
		bool outline_{};
		color tint_{255,255,255};
		std::shared_ptr<const native_caption_font::face> face_;
	};
}
