#pragma once
#include "engine_stereo_bridge.hpp"
#include "scripted_position.hpp"
#include "spatial_panel.hpp"
#include <array>
#include <cmath>

namespace vr::screen_scope
{
	inline bool window(const engine_stereo_bridge::eye_projection& eye,float half_x,float half_y,std::array<float,4>& out) noexcept
	{
		for(float x:{eye.tan_left,eye.tan_right,eye.tan_up,eye.tan_down,half_x,half_y})if(!std::isfinite(x))return false;
		const float width=eye.tan_right-eye.tan_left,height=eye.tan_up-eye.tan_down;
		if(width<=0 || height<=0 || half_x<=0 || half_y<=0)return false;
		out={(-half_x-eye.tan_left)/width,(eye.tan_up-half_y)/height,2*half_x/width,2*half_y/height};
		return true;
	}
	// The image camera is native-owned. This viewer moves only the display plane,
	// in tracking coordinates; it never receives or modifies a weapon/view angle.
	class anchored_plane
	{
		using vec=spatial_panel::vec3;
		using axis=std::array<vec,3>;
		std::uint64_t epoch_{},reference_{};
		axis anchor_{},viewer_{};
		vec offset_{};
		game_view::scripted_position position_;
		bool valid_{};
		static bool rotation(const axis& value) noexcept
		{
			for(unsigned i=0;i<3;++i)
			{
				if(!spatial_panel::finite(value[i]) || std::abs(spatial_panel::dot(value[i],value[i])-1)>.01f)return false;
				for(unsigned j=0;j<i;++j)if(std::abs(spatial_panel::dot(value[i],value[j]))>.01f)return false;
			}
			return true;
		}
	public:
		bool update(std::uint64_t epoch,std::uint64_t reference,const vec& head_meters,const axis& orientation,float gain) noexcept
		{
			valid_=false;
			if(!epoch){*this={};return false;}
			if(!reference || !spatial_panel::finite(head_meters) || !rotation(orientation))return false;
			if(epoch_!=epoch || reference_!=reference){anchor_=orientation;epoch_=epoch;reference_=reference;}
			auto policy=game_view::camera_profiles::aligned;
			policy.translation_gain=std::isfinite(gain)?std::clamp(gain,0.f,.25f):.1f;
			offset_=position_.offset(epoch,reference,head_meters,policy);viewer_=orientation;valid_=true;return true;
		}
		bool project(const engine_stereo_bridge::eye_projection& eye,float eye_left_meters,
			unsigned width,unsigned height,spatial_panel::projected_quad& output) const noexcept
		{
			if(!valid_ || !width || !height || width>8192 || height>8192 ||
				!std::isfinite(eye_left_meters) || std::abs(eye_left_meters)>.1f)return false;
			std::array<float,4> unused{};
			if(!window(eye,1,1,unused))return false;
			constexpr float distance=2.f,half_width=distance*.57735026919f;
			const float half_height=half_width*height/width;
			const float sx=2/(eye.tan_right-eye.tan_left),sy=2/(eye.tan_up-eye.tan_down);
			const float cx=-(eye.tan_right+eye.tan_left)*sx*.5f,cy=-(eye.tan_up+eye.tan_down)*sy*.5f;
			for(unsigned corner=0;corner<4;++corner)
			{
				vec delta{};
				for(unsigned i=0;i<3;++i)delta[i]=anchor_[0][i]*distance+
					anchor_[1][i]*((corner&1)?-half_width:half_width)+anchor_[2][i]*(corner<2?half_height:-half_height)-
					offset_[i]-viewer_[1][i]*eye_left_meters;
				const float z=spatial_panel::dot(delta,viewer_[0]);
				output[corner]={-spatial_panel::dot(delta,viewer_[1])*sx+cx*z,
					spatial_panel::dot(delta,viewer_[2])*sy+cy*z,z*.5f,z};
				for(float x:output[corner])if(!std::isfinite(x))return false;
			}
			// Homogeneous clipping handles an oblique plane or looking entirely
			// away. Rejecting negative W would make the whole plane pop at its edge.
			return true;
		}
	};
}
