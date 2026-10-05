#pragma once
#include "desktop_mirror_layout.hpp"
#include "spatial_panel.hpp"
#include "settings.hpp"

namespace vr::recording_frame
{
	inline constexpr float outside_dim=settings::recording_dim.default_value/100.f;
	// Transfer angular boundaries, rather than copying UVs between asymmetric
	// eye frusta. This guide approximates the capture at infinity; nearby objects
	// still have the normal binocular parallax. Clip only to the other eye's FOV.
	inline desktop_mirror::crop other_eye_crop(const desktop_mirror::crop& captured,
		const engine_stereo_bridge::eye_projection& source,
		const engine_stereo_bridge::eye_projection& target) noexcept
	{
		const auto valid_projection=[](const auto& p)
		{
			return std::isfinite(p.tan_left) && std::isfinite(p.tan_right) &&
				std::isfinite(p.tan_down) && std::isfinite(p.tan_up) &&
				p.tan_left<0 && p.tan_right>0 && p.tan_down<0 && p.tan_up>0;
		};
		if (!captured || !valid_projection(source) || !valid_projection(target) ||
			!std::isfinite(captured.u0) || !std::isfinite(captured.v0) ||
			!std::isfinite(captured.u1) || !std::isfinite(captured.v1) ||
			captured.u0<0 || captured.v0<0 || captured.u1>1 || captured.v1>1) return {};
		const double source_x=double(source.tan_right)-source.tan_left;
		const double source_y=double(source.tan_up)-source.tan_down;
		const double target_x=double(target.tan_right)-target.tan_left;
		const double target_y=double(target.tan_up)-target.tan_down;
		const auto u=[&](float x) {return (source.tan_left+x*source_x-target.tan_left)/target_x;};
		const auto v=[&](float y) {return (target.tan_up-source.tan_up+y*source_y)/target_y;};
		return {float(std::clamp(u(captured.u0),0.,1.)),float(std::clamp(v(captured.v0),0.,1.)),
			float(std::clamp(u(captured.u1),0.,1.)),float(std::clamp(v(captured.v1),0.,1.)),
			captured.horizontal_fov,captured.limited};
	}
	struct layout
	{
		spatial_panel::vec4 protected_pixels{}; // Full bilinear footprint of the desktop crop.
		float line_pixels{};
		bool valid{};
	};
	inline layout make_layout(const desktop_mirror::crop& crop, unsigned width, unsigned height) noexcept
	{
		if (!crop || width<2 || height<2 || width>16384 || height>16384 ||
			!std::isfinite(crop.u0) || !std::isfinite(crop.v0) || !std::isfinite(crop.u1) || !std::isfinite(crop.v1) ||
			crop.u0<0 || crop.v0<0 || crop.u1>1 || crop.v1>1) return {};
		// Cover every texel that linear sampling can touch, including fractional
		// crop boundaries. Neither dimming nor the border may enter this region.
		return {{std::floor(crop.u0*width)-1,std::floor(crop.v0*height)-1,
			std::ceil(crop.u1*width)+1,std::ceil(crop.v1*height)+1},
			(std::max)(1.f,height/1200.f),true};
	}
	inline unsigned caption_font_pixels(unsigned height) noexcept
	{
		return std::clamp(height/72u,12u,128u);
	}
	// Pixel origin/extent, centered below the frame. Fit the whole caption into
	// available exterior space; a crop touching the bottom has no caption room.
	inline spatial_panel::vec4 caption_bounds(const layout& frame,unsigned width,unsigned height,
		unsigned text_width,unsigned text_height) noexcept
	{
		if (!frame.valid || !text_width || !text_height || width<2 || height<2) return {};
		const float margin=frame.line_pixels*6;
		const float top=frame.protected_pixels[3]+margin;
		const float available_width=width-margin*2,available_height=height-margin-top;
		if (available_width<=0 || available_height<=0) return {};
		const float scale=(std::min)({1.f,available_width/text_width,available_height/text_height});
		const float w=(std::min)(text_width*scale,available_width),h=(std::min)(text_height*scale,available_height);
		if (h<8) return {}; // Never move unreadable text inside the live picture.
		const float center=(frame.protected_pixels[0]+frame.protected_pixels[2])*.5f;
		return {std::clamp(center-w*.5f,margin,(std::max)(margin,width-margin-w)),top,w,h};
	}
}
