#pragma once
#include "spatial_panel.hpp"
#include <cstddef>

namespace vr::spatial_lines
{
	using namespace spatial_panel;
	inline constexpr size_t capacity = 192;
	struct segment { vec4 a{}, b{}, color{}; };
	struct batch
	{
		std::array<segment,capacity> lines{};
		size_t count{};
		bool add(vec3 a, vec3 b, vec4 color) noexcept
		{
			if (count == capacity || !finite(a) || !finite(b)) return false;
			for (float v : color) if (!std::isfinite(v) || v < 0 || v > 1) return false;
			lines[count++] = {{a[0],a[1],a[2],1},{b[0],b[1],b[2],1},color};
			return true;
		}
	};
	// Clip before perspective divide, including near-plane crossings. Diagnostics
	// intentionally ignore scene depth (like an x-ray ruler), not eye disparity.
	inline bool clip(vec4& a, vec4& b, float near_w) noexcept
	{
		if (!std::isfinite(near_w) || near_w <= 0) return false;
		for (const auto& p : {a,b}) for (float v : p) if (!std::isfinite(v)) return false;
		const auto distances = [near_w](vec4 p) {
			return std::array<float,5>{p[3]-near_w,p[3]+p[0],p[3]-p[0],p[3]+p[1],p[3]-p[1]};
		};
		const auto da=distances(a), db=distances(b);
		float start=0, end=1;
		for (size_t i=0;i<da.size();++i)
		{
			if (da[i] < 0 && db[i] < 0) return false;
			if (da[i] < 0) start=std::max(start,da[i]/(da[i]-db[i]));
			else if (db[i] < 0) end=std::min(end,da[i]/(da[i]-db[i]));
		}
		if (start > end) return false;
		const auto original=a;
		for (size_t i=0;i<4;++i) { const auto d=b[i]-original[i]; a[i]=original[i]+start*d; b[i]=original[i]+end*d; }
		a[2]=a[3]*.5f; b[2]=b[3]*.5f;
		return true;
	}
	inline batch project(const batch& world, vec3 eye, const matrix& vp, float near_w) noexcept
	{
		batch result;
		if (!finite(eye) || world.count > capacity) return result;
		for (float v : vp) if (!std::isfinite(v)) return result;
		for (size_t n=0;n<world.count;++n)
		{
			auto line=world.lines[n];
			for (auto* endpoint : {&line.a,&line.b})
			{
				const auto original=*endpoint;
				for (size_t c=0;c<4;++c)
				{
					(*endpoint)[c]=vp[12+c];
					for (size_t axis=0;axis<3;++axis) (*endpoint)[c]+=(original[axis]-eye[axis])*vp[axis*4+c];
				}
			}
			if (clip(line.a,line.b,near_w)) result.lines[result.count++]=line;
		}
		return result;
	}
}
