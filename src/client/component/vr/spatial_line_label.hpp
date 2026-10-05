#pragma once
#include "spatial_lines.hpp"
#include <string_view>
#include <initializer_list>

namespace vr::spatial_lines
{
	// Small fourteen-segment diagnostic lettering, independent of native fonts,
	// UI textures and locale. One bounded line batch; no allocation or GPU readback.
	inline unsigned glyph(char c) noexcept
	{
		const auto bits=[](std::initializer_list<int> indices) { unsigned value{}; for (int i:indices) value|=1u<<i; return value; };
		switch (c)
		{
		case 'A':return bits({0,1,2,4,5,6,7}); case 'B':return bits({0,1,2,3,7,12,13});
		case 'C':return bits({0,3,4,5}); case 'D':return bits({0,1,2,3,12,13});
		case 'E':return bits({0,3,4,5,6,7}); case 'F':return bits({0,4,5,6,7});
		case 'G':return bits({0,2,3,4,5,7}); case 'H':return bits({1,2,4,5,6,7});
		case 'I':return bits({0,3,12,13}); case 'J':return bits({1,2,3,4});
		case 'K':return bits({4,5,6,9,11}); case 'L':return bits({3,4,5});
		case 'M':return bits({1,2,4,5,8,9}); case 'N':return bits({1,2,4,5,8,11});
		case 'O':return bits({0,1,2,3,4,5}); case 'P':return bits({0,1,4,5,6,7});
		case 'Q':return bits({0,1,2,3,4,5,11}); case 'R':return bits({0,1,4,5,6,7,11});
		case 'S':return bits({0,2,3,5,6,7}); case 'T':return bits({0,12,13});
		case 'U':return bits({1,2,3,4,5}); case 'V':return bits({4,5,9,10});
		case 'W':return bits({1,2,4,5,10,11}); case 'X':return bits({8,9,10,11});
		case 'Y':return bits({8,9,13}); case 'Z':return bits({0,3,9,10});
		case '-':return bits({6,7}); default:return 0;
		}
	}
	inline bool label(batch& lines,vec3 origin,vec3 right,vec3 up,float height,std::string_view text,vec4 color) noexcept
	{
		if (lines.count>capacity || text.size()>32 || !finite(origin) || !finite(right) || !finite(up) || !std::isfinite(height) || height<=0) return false;
		constexpr std::array<std::array<float,4>,14> strokes{{
			{0,1,1,1},{1,1,1,.5f},{1,.5f,1,0},{0,0,1,0},{0,.5f,0,0},{0,1,0,.5f},
			{0,.5f,.5f,.5f},{.5f,.5f,1,.5f},{0,1,.5f,.5f},{1,1,.5f,.5f},{0,0,.5f,.5f},{1,0,.5f,.5f},
			{.5f,1,.5f,.5f},{.5f,.5f,.5f,0}}};
		size_t needed{}; for (char c:text) for (unsigned mask=glyph(c);mask;mask>>=1) needed+=mask&1;
		if (needed>capacity-lines.count) return false; // Never truncate a reason mid-word.
		const auto point=[&](size_t n,float x,float y) {
			vec3 result=origin;
			for (int a=0;a<3;++a) result[a]+=height*(right[a]*(n*.72f+x*.5f)+up[a]*y);
			return result;
		};
		for (size_t n=0;n<text.size();++n) for (size_t i=0;i<strokes.size();++i) if (glyph(text[n])&(1u<<i))
		{
			const auto& s=strokes[i]; if (!lines.add(point(n,s[0],s[1]),point(n,s[2],s[3]),color)) return false;
		}
		return true;
	}
}
