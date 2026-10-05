#include <std_include.hpp>
#include "overlay_text_texture.hpp"
#include "native_caption_font.hpp"
#include <gsl/gsl>
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

namespace vr
{
	bool overlay_text_texture::ensure_utf8(ID3D11Device* device, std::string_view text, unsigned font_pixels, bool outline,std::shared_ptr<const native_caption_font::face> face,color tint) noexcept
	{
		if (!device || text.empty() || text.size()>max_text_units*4) return false;
		// Bounded stack conversion; texture comparison/allocation stays in ensure.
		// Invalid or overlong UTF-8 fails instead of substituting '?' or truncating.
		wchar_t wide[max_text_units]{};
		const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),
			static_cast<int>(text.size()),wide,max_text_units);
		return count>0 && ensure(device,{wide,static_cast<std::size_t>(count)},font_pixels,outline,face,tint);
	}
	bool overlay_text_texture::ensure(ID3D11Device* device, std::wstring_view text, unsigned font_pixels, bool outline,std::shared_ptr<const native_caption_font::face> face,color tint) noexcept
	try
	{
		if (!device || text.empty() || text.size()>max_text_units || font_pixels<8 || font_pixels>128) return false;
		const auto& native=face;
		if (device_.Get()==device && text_==text && font_pixels_==font_pixels && outline_==outline && face_==face && tint_==tint) return view_!=nullptr;
		view_.Reset();width_=height_=0;
		text_=text;device_=device;font_pixels_=font_pixels;outline_=outline;face_=face;tint_=tint; // Cache failures, too.
		// Use the first strong Unicode direction for this single-line caption.
		// DrawText otherwise defaults to LTR even for an Arabic game locale.
		WORD directions[max_text_units]{};bool rtl{};
		if (GetStringTypeW(CT_CTYPE2,text.data(),static_cast<int>(text.size()),directions))
			for (std::size_t i=0;i<text.size();++i)
			{
				if (directions[i]==C2_RIGHTTOLEFT) {rtl=true;break;}
				if (directions[i]==C2_LEFTTORIGHT) break;
			}
		const auto dc=CreateCompatibleDC(nullptr);
		if (!dc) return false;
		const auto release_dc=gsl::finally([&]{DeleteDC(dc);});
		const auto font=CreateFontW(-static_cast<int>(font_pixels),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
			DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,
			DEFAULT_PITCH,native?native->family():(rtl ? L"Segoe UI" : L"Microsoft YaHei UI"));
		if (!font) return false;
		const auto release_font=gsl::finally([&]{DeleteObject(font);});
		const auto old_font=SelectObject(dc,font);
		if (!old_font || old_font==HGDI_ERROR) return false;
		const auto restore_font=gsl::finally([&]{SelectObject(dc,old_font);});
		if(native)
		{
			wchar_t selected[128]{};
			if(!GetTextFaceW(dc,128,selected) || std::wstring_view(selected)!=native->family())return false;
		}
		const auto flags=DT_SINGLELINE|DT_NOPREFIX|(rtl ? DT_RTLREADING : 0);
		RECT bounds{};
		if (!DrawTextW(dc,text.data(),static_cast<int>(text.size()),&bounds,flags|DT_CALCRECT)) return false;
		constexpr unsigned padding=padding_pixels;
		const auto width=static_cast<unsigned>(bounds.right-bounds.left)+padding*2;
		const auto height=static_cast<unsigned>(bounds.bottom-bounds.top)+padding*2;
		if (width<=padding*2 || height<=padding*2 || width>4096 || height>256) return false;
		BITMAPINFO info{};
		info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=static_cast<LONG>(width);
		info.bmiHeader.biHeight=-static_cast<LONG>(height);info.bmiHeader.biPlanes=1;
		info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
		void* bits{};
		const auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
		if (!bitmap || !bits) return false;
		const auto release_bitmap=gsl::finally([&]{DeleteObject(bitmap);});
		const auto old_bitmap=SelectObject(dc,bitmap);
		if (!old_bitmap || old_bitmap==HGDI_ERROR) return false;
		const auto restore_bitmap=gsl::finally([&]{SelectObject(dc,old_bitmap);});
		std::memset(bits,0,width*height*4);
		SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(255,255,255));
		bounds={padding,padding,static_cast<LONG>(width-padding),static_cast<LONG>(height-padding)};
		if (!DrawTextW(dc,text.data(),static_cast<int>(text.size()),&bounds,flags)) return false;
		if (!GdiFlush()) return false; // Complete GDI writes before reading DIB memory.
		auto* pixels=static_cast<std::uint32_t*>(bits);
		for (unsigned i=0;i<width*height;++i)
		{
			const auto coverage=(std::max)({pixels[i]&255u,(pixels[i]>>8)&255u,(pixels[i]>>16)&255u});
			pixels[i]=coverage*0x01010101u; // White premultiplied coverage, no ClearType color fringes.
		}
		if (outline)
		{
			// Dilate coverage only. RGB remains the original white glyph, yielding
			// a black two-pixel outline on both bright and dark scene backgrounds.
			// Read the unchanged RGB channel so traversal cannot grow the outline.
			for (unsigned y=0;y<height;++y) for (unsigned x=0;x<width;++x)
			{
				unsigned alpha=pixels[y*width+x]&255u;
				for (int dy=-2;dy<=2;++dy) for (int dx=-2;dx<=2;++dx)
				{
					const int sx=int(x)+dx,sy=int(y)+dy;
					if (sx>=0 && sy>=0 && sx<int(width) && sy<int(height))
						alpha=(std::max)(alpha,pixels[sy*width+sx]&255u);
				}
				pixels[y*width+x]=(pixels[y*width+x]&0x00ffffffu)|(alpha<<24);
			}
		}
		// Tint only after outline dilation. Keep the premultiplied glyph coverage
		// and black outline alpha; yellow must not erase its zero-blue coverage.
		for(unsigned i=0;i<width*height;++i)
		{
			const auto coverage=pixels[i]&255u;
			pixels[i]=(pixels[i]&0xff000000u)|((coverage*tint[0]+127)/255)|
				(((coverage*tint[1]+127)/255)<<8)|(((coverage*tint[2]+127)/255)<<16);
		}
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width=width;desc.Height=height;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
		desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
		const D3D11_SUBRESOURCE_DATA data{bits,width*4,0};
		Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
		if (FAILED(device->CreateTexture2D(&desc,&data,&texture)) ||
			FAILED(device->CreateShaderResourceView(texture.Get(),nullptr,&view_))) return false;
		width_=width;height_=height;return true;
	}
	catch (...) {view_.Reset();width_=height_=0;return false;}
}
