#pragma once
#include "native_hud_quad.hpp"
#include "spatial_panel.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <string>
#include <span>
#include <vector>
#include <memory>
#include <optional>

namespace vr::narrative_ui
{
	enum class channel { story, progress, announcement, count };
	struct backdrop
	{
		spatial_panel::vec4 pixels{}; // Native text-space rectangle before the blur path's extra scaling.
		float alpha{};
	};
	inline bool valid_backdrop(const backdrop& value) noexcept
	{
		for (float x:value.pixels) if (!std::isfinite(x) || std::abs(x)>32768) return false;
		return value.pixels[2]>value.pixels[0] && value.pixels[3]>value.pixels[1] &&
			std::isfinite(value.alpha) && value.alpha>0 && value.alpha<=1;
	}
	inline spatial_panel::blur_region normalize_backdrop(const backdrop& value,unsigned width,unsigned height) noexcept
	{
		if (!width || !height || !valid_backdrop(value)) return {};
		return {{value.pixels[0]/width,value.pixels[1]/height,value.pixels[2]/width,value.pixels[3]/height},value.alpha};
	}
	inline bool hint_blur_material(std::string_view name) noexcept
	{return name=="h1_hud_tutorial_blur" || name=="h2_hud_ssdd_results_blur";}
	inline bool hint_border_material(std::string_view name) noexcept
	{return name=="h1_hud_tutorial_border" || name=="h1_hud_fng_results_border" || name=="h2_hud_ssdd_results_line";}
	inline bool subtractive_script_ink(std::string_view name,bool owned) noexcept
	{return owned && hint_border_material(name);}
	inline bool script_ink_material(std::string_view name) noexcept
	{return hint_border_material(name);}
	// Trainer script HUD is a table: pulsing values, timers and ordinary labels
	// retain one canvas. Dialogue and LUI have separate native producers.
	inline bool script_panel(std::string_view map) noexcept {return map=="trainer";}
	struct placement {float distance_meters,lower_fraction;};
	inline constexpr placement layout(channel value) noexcept
	{
		return value==channel::progress ? placement{1.2f,.20f} :
			value==channel::announcement ? placement{1.6f,0.f} : placement{2.f,0.f};
	}
	// Match complete native labels (or numeric prefix/suffix), not translated
	// keyword fragments. Dialogue mentioning a download must stay on its plane.
	inline std::string plain_label(std::string_view text)
	{
		std::string result;result.reserve(text.size());
		for(size_t i=0;i<text.size();++i)
			if(text[i]=='^' && i+1<text.size() && text[i+1]>='0' && text[i+1]<='9')++i;
			else result+=text[i];
		return result;
	}
	inline bool progress_text(std::string_view text,std::span<const std::string> labels)
	{
		if(text.empty() || text.size()>1024)return false;
		const auto plain=plain_label(text);
		const auto numeric=[](std::string_view value){return std::all_of(value.begin(),value.end(),[](char c){return (c>='0' && c<='9') || c==' ' || c=='\t' || c=='.' || c==',' || c==':' || c=='/' || c=='%';});};
		for(const auto& label:labels)
		{
			if(label.empty())continue;
			if(const auto slot=label.find("&&1");slot!=std::string::npos && label.find("&&1",slot+3)==std::string::npos)
			{
				const auto prefix=std::string_view(label).substr(0,slot),suffix=std::string_view(label).substr(slot+3);
				if(plain.size()>prefix.size()+suffix.size() && plain.starts_with(prefix) && plain.ends_with(suffix))
				{
					const auto value=std::string_view(plain).substr(prefix.size(),plain.size()-prefix.size()-suffix.size());
					if(numeric(value) && std::any_of(value.begin(),value.end(),[](char c){return c>='0' && c<='9';}))return true;
				}
			}
			if(plain==label || (plain.starts_with(label) && numeric(std::string_view(plain).substr(label.size()))) ||
				(plain.ends_with(label) && numeric(std::string_view(plain).substr(0,plain.size()-label.size()))))return true;
		}
		return false;
	}
	std::shared_ptr<const std::vector<std::string>> progress_labels() noexcept;
	// H2 R_AddCmdDrawConsoleTextSubtitle (+0x38 |= 0x100 at 0x14076AE56)
	// and native pulse/typewriter text (+0x38 |= 0xc0 at 0x14076A137).
	// Frontend font/style numbers are NOT backend render flags.
	inline bool text_command(const void* data, std::size_t size, bool owned_narrative = false) noexcept
	{
		// Only the fixed 60-byte prefix is read; UTF-8 content remains native.
		if (!data || size < 233 || size > 65535) return false;
		const auto* bytes = static_cast<const std::uint8_t*>(data);
		std::uint32_t flags{};
		std::memcpy(&flags, bytes + 56, sizeof(flags));
		if (bytes[2] != 20 || bytes[3] != 0 ||
			(!owned_narrative && !(flags & 0x100) && (flags & 0xc0) != 0xc0)) return false;
		float x{}, y{};
		std::memcpy(&x, bytes + 4, sizeof(x)); std::memcpy(&y, bytes + 8, sizeof(y));
		return std::isfinite(x) && std::isfinite(y) && std::abs(x) <= 32768 && std::abs(y) <= 32768;
	}
	inline bool announcement_command(const void* data,std::size_t size) noexcept
	{
		if (!text_command(data,size)) return false;
		const auto flags=native_hud_quad::field<std::uint32_t>(data,56);
		// Keep dialogue on its own plane even if it also has pulse styling.
		return !(flags&0x100) && (flags&0xc0)==0xc0;
	}
	inline bool black_material(std::string_view name, std::uint32_t rgba) noexcept
	{
		// Solid native materials only. Letterbox bars, vignettes and HUD backing
		// are not full-field fades; the caller must also prove viewport coverage.
		return (name == "black" || (name == "white" && (rgba & 0xffffff) == 0)) && (rgba >> 24) != 0;
	}
	inline bool black_quad_command(const void* data, std::size_t size, std::string_view material,
		native_hud_quad::quad& out) noexcept
	{
		return native_hud_quad::decode(data, size, out) &&
			native_hud_quad::field<std::uint8_t>(data, 3) == 0 && black_material(material, out.color);
	}
	inline bool fade_quad_command(const void* data,std::size_t size,std::string_view material,
		native_hud_quad::quad& out) noexcept
	{
		// Only uniform native textures can extend beyond the narrative canvas.
		// Coverage and scissors are checked against the actual draw viewport.
		return native_hud_quad::decode(data,size,out) &&
			native_hud_quad::field<std::uint8_t>(data,3)==0 &&
			(material=="black" || material=="white") && (out.color>>24)!=0;
	}
	inline spatial_panel::vec4 fade_color(std::string_view material,std::uint32_t rgba) noexcept
	{
		const float alpha=(rgba>>24)/255.f;
		spatial_panel::vec4 result{0,0,0,alpha};
		if(material=="white")for(unsigned i=0;i<3;++i)result[i]=((rgba>>(i*8))&255)/255.f*alpha;
		return result; // Native encoded-space premultiplied RGBA.
	}
	inline void append_fade(spatial_panel::vec4& result,const spatial_panel::vec4& source) noexcept
	{
		for(unsigned i=0;i<4;++i)result[i]=source[i]+result[i]*(1-source[3]);
	}
	inline bool covers_viewport(const std::array<std::array<float, 2>, 4>& vertices,
		float x, float y, float width, float height) noexcept
	{
		if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height) ||
			width < 16 || height < 16 || width > 8192 || height > 8192) return false;
		// Verify all four corners, not just an AABB (a diamond is not coverage).
		constexpr float tolerance = .5f;
		float left = 32768, top = 32768, right = -32768, bottom = -32768;
		for (const auto& v : vertices)
		{
			if (!std::isfinite(v[0]) || !std::isfinite(v[1]) || std::abs(v[0]) > 32768 || std::abs(v[1]) > 32768) return false;
			left = std::min(left, v[0]); top = std::min(top, v[1]);
			right = std::max(right, v[0]); bottom = std::max(bottom, v[1]);
		}
		if (left > x+tolerance || top > y+tolerance || right < x+width-tolerance || bottom < y+height-tolerance) return false;
		const std::array<std::array<float, 2>, 4> corners{{{left,top},{right,top},{right,bottom},{left,bottom}}};
		unsigned occupied{};
		for (const auto& v : vertices)
		{
			if (!std::isfinite(v[0]) || !std::isfinite(v[1])) return false;
			unsigned matched{};
			for (unsigned c = 0; c < 4; ++c)
				if (std::abs(v[0]-corners[c][0]) <= tolerance && std::abs(v[1]-corners[c][1]) <= tolerance)
					matched = 1u << c;
			if (!matched || (occupied & matched)) return false;
			occupied |= matched;
		}
		return occupied == 15;
	}
	inline bool current(std::uint64_t timestamp, std::uint64_t now) noexcept
	{
		// Transient narrative UI must never inherit the weapon HUD's retention.
		return timestamp && now >= timestamp && now - timestamp <= 250;
	}
}
