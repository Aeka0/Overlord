#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace vr::ui_canvas
{
	inline constexpr unsigned menu_width=1920,menu_height=1080;
	struct mapping
	{
		unsigned source_width{},source_height{},target_width{},target_height{};
		float x{},y{},width{},height{},scale{};
		bool valid() const noexcept
		{return source_width&&source_height&&target_width&&target_height&&std::isfinite(scale)&&scale>0&&
			std::isfinite(x)&&std::isfinite(y)&&std::isfinite(width)&&std::isfinite(height)&&width>0&&height>0;}
		bool source_uv(float& u,float& v) const noexcept
		{
			if(!valid()||!std::isfinite(u)||!std::isfinite(v))return false;
			const auto sx=(u*target_width-x)/width,sy=(v*target_height-y)/height;
			if(sx<-.00001f||sx>1.00001f||sy<-.00001f||sy>1.00001f)return false;
			u=std::clamp(sx,0.f,1.f);v=std::clamp(sy,0.f,1.f);return true;
		}
	};
	inline mapping fit(unsigned source_width,unsigned source_height,unsigned target_width=menu_width,unsigned target_height=menu_height) noexcept
	{
		if(!source_width||!source_height||!target_width||!target_height||source_width>16384||source_height>16384||target_width>16384||target_height>16384)return {};
		const auto scale=std::min(float(target_width)/source_width,float(target_height)/source_height);
		const auto w=source_width*scale,h=source_height*scale;
		return {source_width,source_height,target_width,target_height,(target_width-w)*.5f,(target_height-h)*.5f,w,h,scale};
	}
}
