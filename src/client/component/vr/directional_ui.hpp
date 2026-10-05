#pragma once
#include "spatial_panel.hpp"
#include "native_hud_quad.hpp"
#include <cstdint>
#include <cstring>
#include <string_view>

namespace vr::directional_ui
{
	using namespace spatial_panel;
	inline constexpr unsigned waypoint_capacity = 32;
	inline constexpr unsigned tile_width = 512, tile_height = 256, atlas_size = 2048;
	struct rectangle { float x{}, y{}, right{}, bottom{}; };
	inline bool marker_crop(rectangle bounds, rectangle& out) noexcept
	{
		if (!std::isfinite(bounds.x) || !std::isfinite(bounds.y) || !std::isfinite(bounds.right) ||
			!std::isfinite(bounds.bottom) || bounds.x < -32768 || bounds.y < -32768 ||
			bounds.right > 32768 || bounds.bottom > 32768 || bounds.right <= bounds.x || bounds.bottom <= bounds.y) return false;
		out={std::floor(bounds.x)-4,std::floor(bounds.y)-4,std::ceil(bounds.right)+4,std::ceil(bounds.bottom)+4};
		return out.right-out.x<=tile_width && out.bottom-out.y<=tile_height;
	}
	struct waypoint
	{
		vec3 world{};
		std::array<float, 2> anchor{}, viewport{}, tangent{};
		rectangle crop{};
		bool clamped{};
		float pixels_per_meter{}; // Positive: fixed world size. Zero: projected pixel size via tangent.
	};
	// Calibrate the existing projected-pixel path to a reference world size.
	// A virtual source tangent keeps the visual size independent of viewport
	// resolution, while world_quad retains the target depth in both eyes.
	inline bool set_fixed_visual_size(waypoint& marker,float pixels_per_meter,float reference_m) noexcept
	{
		if (!std::isfinite(pixels_per_meter) || pixels_per_meter<=0 || pixels_per_meter>8192 ||
			!std::isfinite(reference_m) || reference_m<.12f || reference_m>100) return false;
		std::array<float,2> tangent{};
		for (unsigned i=0;i<2;++i)
		{
			if (!std::isfinite(marker.viewport[i]) || marker.viewport[i]<16 || marker.viewport[i]>8192) return false;
			tangent[i]=marker.viewport[i]/(2*reference_m*pixels_per_meter);
			if (!std::isfinite(tangent[i]) || tangent[i]<=0 || tangent[i]>10) return false;
		}
		marker.tangent=tangent;marker.pixels_per_meter=0;return true;
	}
	template<class T> inline T field(const void* p, unsigned offset) noexcept
	{
		T result{}; std::memcpy(&result, static_cast<const std::byte*>(p)+offset, sizeof(T)); return result;
	}
	inline bool warning_material(std::string_view name) noexcept
	{
		return name == "hit_direction" || name == "hit_direction_overlay" || name == "hit_direction_stun" ||
			name == "hud_grenadeicon" || name == "hud_flashbangicon" || name == "hud_grenadethrowback" ||
			name == "hud_grenadepointer";
	}
	// Native legacy HUD uses stretch pictures (10), rotated rectangles (12) and XY quads (16),
	// whereas LUI uses XYUV quads (17). Never reinterpret one as another.
	inline bool quad_command(const void* p, unsigned size) noexcept
	{
		return native_hud_quad::is_command(p,size);
	}
	inline bool quad_bounds(const void* p, unsigned size, rectangle& out) noexcept
	{
		native_hud_quad::quad quad{};
		if (!native_hud_quad::decode(p,size,quad)) return false;
		out = {32768,32768,-32768,-32768};
		for (const auto& vertex:quad.vertices)
		{
			const float x=vertex[0], y=vertex[1];
			out.x=std::min(out.x,x); out.y=std::min(out.y,y);
			out.right=std::max(out.right,x); out.bottom=std::max(out.bottom,y);
		}
		return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.right) && std::isfinite(out.bottom) &&
			out.x>=-32768 && out.y>=-32768 && out.right<=32768 && out.bottom<=32768 && out.right>out.x && out.bottom>out.y;
	}
	inline bool world_quad(const waypoint& marker, const std::array<float,12>& camera,
		float units, quad& out) noexcept
	{
		if (!finite(marker.world) || !std::isfinite(units) || units<=0) return false;
		for (unsigned i=0;i<2;++i)
			if (!std::isfinite(marker.viewport[i]) || marker.viewport[i]<16 || marker.viewport[i]>8192 ||
				!std::isfinite(marker.anchor[i]) || std::abs(marker.anchor[i])>32768 ||
				!std::isfinite(marker.tangent[i]) || marker.tangent[i]<=0 || marker.tangent[i]>10) return false;
		const vec3 origin{camera[0],camera[1],camera[2]}, forward{camera[3],camera[4],camera[5]},
			right{-camera[6],-camera[7],-camera[8]}, up{camera[9],camera[10],camera[11]};
		vec3 delta{}, center=marker.world;
		for (unsigned i=0;i<3;++i) delta[i]=center[i]-origin[i];
		float depth=dot(delta,forward);
		auto anchor=marker.anchor, tangent=marker.tangent;
		if (marker.clamped)
		{
			// Native arrows retain their edge position, rotation and distance label
			// on a comfortable 80-degree head-relative canvas, including rear targets.
			depth=units*2; tangent={.839099631f,.839099631f*marker.viewport[1]/marker.viewport[0]};
			anchor={marker.viewport[0]*.5f,marker.viewport[1]*.5f};
			for (unsigned i=0;i<3;++i) center[i]=origin[i]+forward[i]*depth;
		}
		if (!std::isfinite(depth) || depth<units*.12f) return false;
		if (!std::isfinite(marker.pixels_per_meter) || marker.pixels_per_meter<0 || marker.pixels_per_meter>8192) return false;
		const float sx=marker.pixels_per_meter>0 ? units/marker.pixels_per_meter : 2*depth*tangent[0]/marker.viewport[0];
		const float sy=marker.pixels_per_meter>0 ? sx : 2*depth*tangent[1]/marker.viewport[1];
		const auto& r=marker.crop;
		const float dx=((r.x+r.right)*.5f-anchor[0])*sx, dy=(anchor[1]-(r.y+r.bottom)*.5f)*sy;
		for (unsigned i=0;i<3;++i) center[i]+=right[i]*dx+up[i]*dy;
		return billboard(center,right,up,(r.right-r.x)*sx,(r.bottom-r.y)*sy,out);
	}
}
