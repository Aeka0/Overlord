#pragma once
#include "../native_hud_quad.hpp"
#include <algorithm>
#include <span>
#include <string_view>

namespace vr::gameplay::weapon_hud::native
{
	inline bool border(std::string_view name) noexcept
	{return name=="h1_hud_weapwidget_border" || name=="h1_hud_weapwidget_border_nightvision";}
	inline bool upright_image(std::string_view name) noexcept
	{return name.starts_with("h1_hud_weapwidget_firearms_labels_") || name.starts_with("h1_hud_weapwidget_nullnum");}
	template<class T> inline void write(std::span<std::byte> bytes,unsigned offset,T value) noexcept
	{std::memcpy(bytes.data()+offset,&value,sizeof(value));}
	// Transform a private command copy only. Labels include text baked into
	// native images; those move to the opposite side without reversing glyphs.
	inline bool mirror_quad(std::span<std::byte> bytes,float axis_sum,bool upright) noexcept
	{
		native_hud_quad::quad quad{};
		if(bytes.size()!=104 || !native_hud_quad::decode(bytes.data(),bytes.size(),quad) ||
			native_hud_quad::field<std::uint8_t>(bytes.data(),2)!=17 || !std::isfinite(axis_sum))return false;
		float left=32768,right=-32768;
		for(const auto& v:quad.vertices){left=std::min(left,v[0]);right=std::max(right,v[0]);}
		if(upright)
		{
			const float shift=axis_sum-left-right;
			for(unsigned i=0;i<4;++i)write(bytes,16+i*16,quad.vertices[i][0]+shift);
		}
		else
		{
			// Reverse winding as well as x; retain each vertex's native z/w.
			std::array<std::byte,64> positions{};std::memcpy(positions.data(),bytes.data()+16,64);
			constexpr unsigned order[]{1,0,3,2};
			for(unsigned i=0;i<4;++i)
			{
				std::memcpy(bytes.data()+16+i*16,positions.data()+order[i]*16,16);
				write(bytes,16+i*16,axis_sum-quad.vertices[order[i]][0]);
			}
			const float u0=native_hud_quad::field<float>(bytes.data(),80),u1=native_hud_quad::field<float>(bytes.data(),88);
			if(!std::isfinite(u0) || !std::isfinite(u1))return false;
			write(bytes,80,u1);write(bytes,88,u0);
		}
		return true;
	}
	inline bool mirror_text(std::span<std::byte> bytes,float axis_sum,float measured_width) noexcept
	{
		if(bytes.size()<233 || native_hud_quad::field<std::uint8_t>(bytes.data(),2)!=20 ||
			(native_hud_quad::field<std::uint32_t>(bytes.data(),56)&0x4000))return false;
		const float x=native_hud_quad::field<float>(bytes.data(),4),scale=native_hud_quad::field<float>(bytes.data(),28);
		const float mirrored=axis_sum-x-measured_width*scale;
		if(!std::isfinite(measured_width) || measured_width<0 || measured_width>32768 ||
			!std::isfinite(scale) || scale<=0 || scale>8 || !std::isfinite(mirrored) || std::abs(mirrored)>32768)return false;
		write(bytes,4,mirrored);return true;
	}
}
