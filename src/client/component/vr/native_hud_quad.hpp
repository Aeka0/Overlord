#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace vr::native_hud_quad
{
	// H2 backend formats: StretchPic (10), rotated XYWH (12), XY (16),
	// and LUI XYUV (17). Sizes include the four-byte command header.
	template<class T> inline T field(const void* data, unsigned offset) noexcept
	{
		T value{};
		std::memcpy(&value, static_cast<const std::byte*>(data)+offset, sizeof(value));
		return value;
	}
	inline bool is_command(const void* data, std::size_t size) noexcept
	{
		if (!data || size < 4 || field<std::uint16_t>(data,0) != size) return false;
		switch (field<std::uint8_t>(data,2))
		{
		case 10: case 12: return size == 56;
		case 16: return size == 72;
		case 17: return size == 104;
		default: return false;
		}
	}
	struct quad
	{
		std::array<std::array<float,2>,4> vertices{};
		std::uintptr_t material{};
		std::uint32_t color{};
	};
	inline bool decode(const void* data, std::size_t size, quad& out) noexcept
	{
		if (!is_command(data,size)) return false;
		quad result{};
		const auto op=field<std::uint8_t>(data,2);
		result.material=field<std::uintptr_t>(data,8);
		result.color=field<std::uint32_t>(data,(op==10 || op==12) ? 48 : op==16 ? 64 : 96);
		if (op==10 || op==12)
		{
			const float x=field<float>(data,16), y=field<float>(data,20);
			const float w=field<float>(data,24), h=field<float>(data,28);
			// StretchPic's +52 is padding, not an angle.
			const float angle=op==12 ? field<float>(data,52)*.0174532925199433f : 0.f;
			if (!std::isfinite(angle) || std::abs(angle)>1000) return false;
			const float c=std::cos(angle), s=std::sin(angle);
			constexpr std::array<std::array<float,2>,4> corners{{{-.5f,-.5f},{.5f,-.5f},{.5f,.5f},{-.5f,.5f}}};
			for (unsigned i=0;i<4;++i)
			{
				const float dx=corners[i][0]*w, dy=corners[i][1]*h;
				result.vertices[i]={x+w*.5f+dx*c-dy*s,y+h*.5f+dx*s+dy*c};
			}
		}
		else for (unsigned i=0;i<4;++i)
			result.vertices[i]={field<float>(data,16+i*(op==16 ? 8 : 16)),field<float>(data,20+i*(op==16 ? 8 : 16))};
		for (const auto& vertex:result.vertices)
			for (float value:vertex)
				if (!std::isfinite(value) || std::abs(value)>32768) return false;
		out=result;
		return true;
	}
}
