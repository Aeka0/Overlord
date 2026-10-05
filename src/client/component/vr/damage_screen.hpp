#pragma once
#include "native_hud_quad.hpp"
#include "engine_stereo_bridge.hpp"
#include "spatial_panel.hpp"
#include <algorithm>
#include <string_view>

namespace vr::damage_screen
{
	// One angular canvas at infinity, shared by both eyes. Project its union
	// bounds into each eye so equal view directions sample equal blood UVs.
	// In particular, nasal screen edges must not restart the full texture.
	inline bool shared_canvas(const std::array<engine_stereo_bridge::eye_projection,2>& eyes,
		std::array<spatial_panel::projected_quad,2>& output) noexcept
	{
		for (const auto& eye : eyes)
		{
			for (float value : {eye.tan_left,eye.tan_right,eye.tan_down,eye.tan_up})
				if (!std::isfinite(value) || std::abs(value)>100) return false;
			if (eye.tan_left>=0 || eye.tan_right<=0 || eye.tan_down>=0 || eye.tan_up<=0 ||
				eye.tan_right-eye.tan_left<.001f || eye.tan_up-eye.tan_down<.001f) return false;
		}
		const float left=std::min(eyes[0].tan_left,eyes[1].tan_left);
		const float right=std::max(eyes[0].tan_right,eyes[1].tan_right);
		const float down=std::min(eyes[0].tan_down,eyes[1].tan_down);
		const float up=std::max(eyes[0].tan_up,eyes[1].tan_up);
		std::array<spatial_panel::projected_quad,2> result{};
		for (unsigned i=0;i<eyes.size();++i)
		{
			const auto& eye=eyes[i];
			const float width=eye.tan_right-eye.tan_left, height=eye.tan_up-eye.tan_down;
			const float x0=2*(left-eye.tan_left)/width-1, x1=2*(right-eye.tan_left)/width-1;
			const float y0=2*(down-eye.tan_down)/height-1, y1=2*(up-eye.tan_down)/height-1;
			result[i]={{{x0,y1,0,1},{x1,y1,0,1},{x0,y0,0,1},{x1,y0,0,1}}};
		}
		output=result;
		return true;
	}
	inline bool material(std::string_view name) noexcept
	{
		return name == "h1_fullscreen_lit_bloodsplat_01" || name == "overlay_low_health" ||
			name == "overlay_low_health_alt" || name == "h1_screen_blood";
	}
	inline bool command(const void* data, std::size_t size, std::string_view name) noexcept
	{
		native_hud_quad::quad quad{};
		// Preserve RGBA verbatim: the dedicated blood shader uses R for intensity,
		// while its alpha can remain 255 throughout recovery. +52 is padding.
		return material(name) && native_hud_quad::decode(data, size, quad) &&
			native_hud_quad::field<std::uint8_t>(data, 3) == 0;
	}
	inline bool current(std::uint64_t timestamp, std::uint64_t now) noexcept
	{
		return timestamp && now >= timestamp && now - timestamp <= 250;
	}
}
