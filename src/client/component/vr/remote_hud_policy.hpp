#pragma once
#include "engine_stereo_bridge.hpp"
#include "spatial_panel.hpp"
#include <string_view>

namespace vr::remote_hud
{
	enum class layer : unsigned {targets,instruments,count};
	inline constexpr unsigned stream_count=2,layer_count=unsigned(layer::count),frame_count=stream_count*layer_count;
	inline constexpr unsigned index(unsigned stream,layer which)noexcept{return unsigned(which)*stream_count+stream;}
	inline constexpr std::array<unsigned,frame_count> composition_order{
		index(0,layer::instruments),index(1,layer::instruments),index(0,layer::targets),index(1,layer::targets)};
	inline bool target_material(std::string_view name)noexcept
	{
		return name=="remotemissile_infantry_target" ||
			name=="remotemissile_infantry_target_2plr" || name=="remotemissile_infantry_target_colorblind" ||
			name=="hud_fofbox_self_sp" || name=="hud_fofbox_self_sp_colorblind" ||
			name=="veh_hud_target" || name=="veh_hud_target_colorblind" ||
			name=="veh_hud_target_offscreen" || name=="veh_hud_missile_flash";
	}
	inline bool instrument_material(std::string_view name)noexcept
	{
		return name=="h2_overlays_predator_graded_bar_top" || name=="h2_overlays_predator_graded_bar_side" ||
			name=="h1_deco_option_scrollbar_arrows" || name=="h2_overlays_predator_reticle";
	}
	inline bool material(std::string_view name)noexcept{return target_material(name) || instrument_material(name);}
	inline std::array<float,2> instrument_tangents(unsigned width,unsigned height)noexcept
	{
		if(!width || !height || width>8192 || height>8192)return {};
		// A readable 80-degree instrument canvas, independent of the native
		// missile zoom. Target markers retain the original optical projection.
		constexpr float half_width=.839099631f;
		return {half_width,half_width*float(height)/float(width)};
	}
	inline bool project(std::array<float,2> source,const engine_stereo_bridge::eye_projection& eye,spatial_panel::projected_quad& out)noexcept
	{
		for(float x:{source[0],source[1],eye.tan_left,eye.tan_right,eye.tan_down,eye.tan_up})
			if(!std::isfinite(x) || std::abs(x)>10)return false;
		if(source[0]<=0 || source[1]<=0 || eye.tan_left>=0 || eye.tan_right<=0 || eye.tan_down>=0 || eye.tan_up<=0)return false;
		// Native target boxes are view-projected UI. Map their angular canvas
		// to each asymmetric eye projection at optical infinity, with no
		// invented target depth or arbitrary 60-degree narrative-panel fit.
		for(unsigned corner=0;corner<4;++corner)
		{
			const float x=(corner&1)?source[0]:-source[0],y=corner<2?source[1]:-source[1];
			out[corner]={2*(x-eye.tan_left)/(eye.tan_right-eye.tan_left)-1,
				2*(y-eye.tan_down)/(eye.tan_up-eye.tan_down)-1,.5f,1.f};
		}
		return true;
	}
}
