#pragma once
#include "optic_view.hpp"
#include "../spatial_panel.hpp"

namespace vr::gameplay::weapons::optics
{
	// Clip only the private magnified scene beyond the optic's front housing.
	// Clip W is positive camera-forward distance; use the sampled eye's matrix,
	// not Euclidean distance or the weapon axis as a substitute for view depth.
	inline float scene_near_distance(const view& lens,const hands::vec& center,const hands::vec& eye,
		const spatial_panel::matrix& vp) noexcept
	{
		if(!lens.active || !std::isfinite(lens.scene_clearance) || lens.scene_clearance<=0)return 0;
		const auto front=hands::sub(hands::add(center,hands::scale(lens.axis[0],lens.scene_clearance)),eye);
		const float depth=front[0]*vp[3]+front[1]*vp[7]+front[2]*vp[11]+vp[15];
		return std::isfinite(depth) && depth>0 && depth<10000 ? depth : 0;
	}
	struct projected_view {spatial_panel::projected_quad corners{}; spatial_panel::vec4 parameters{}; bool valid{}; float pupil_radius{1.f},eye_box_scale{1.f};};
	inline projected_view project(const view& lens, const hands::vec& center, const hands::vec& eye,
		const spatial_panel::matrix& vp, float units, bool preserve_depth = false) noexcept
	{
		if (!lens.active || !spatial_panel::finite(center) || !spatial_panel::finite(eye) ||
			!std::isfinite(units) || units<=0 || units>10000 || !std::isfinite(lens.radius) ||
			lens.radius<.001f*units || lens.radius>.1f*units ||
			!std::isfinite(lens.magnification) || lens.magnification<1 || lens.magnification>12 ||
			!std::isfinite(lens.pupil_radius) || lens.pupil_radius<=0 || lens.pupil_radius>1) return {};
		for (const auto& axis:lens.axis)
			if (!spatial_panel::finite(axis) || std::abs(hands::dot(axis,axis)-1)>.002f) return {};
		if (hands::length(hands::sub(hands::cross(lens.axis[0],lens.axis[1]),lens.axis[2]))>.002f) return {};
		const auto delta=hands::sub(eye,center);
		const float depth=-hands::dot(delta,lens.axis[0]);
		if (depth < .03f*units || depth > .8f*units) return {};
		const auto right=hands::scale(lens.axis[1],-1);
		spatial_panel::quad quad;
		if (!spatial_panel::billboard(center,right,lens.axis[2],lens.radius*2,lens.radius*2,quad)) return {};
		projected_view result;
		if (!spatial_panel::project(quad,eye,vp,.001f,result.corners,preserve_depth)) return {};
		const float u=.5f+hands::dot(delta,right)/(2*lens.radius);
		const float v=.5f-hands::dot(delta,lens.axis[2])/(2*lens.radius);
		// Ordinary scopes require complete coverage. Thermal admits a partial
		// image through a wider pupil, fading to scope shadow on the GPU. Keep
		// the physical quad and aiming-axis UV exact; never recenter the reticle.
		const float displacement=std::hypot((u-.5f)*2,(v-.5f)*2);
		result.eye_box_scale=lens.thermal ? 2.f : 1.f;
		const bool visible=lens.thermal ?
			displacement<lens.pupil_radius*result.eye_box_scale+1 &&
			displacement*(1-1/lens.magnification)-1/lens.magnification < .98f*result.eye_box_scale :
			displacement<2 &&
			displacement*(1-1/lens.magnification)+1/lens.magnification <= (preserve_depth ? 1.f : .94f);
		result.parameters={u,v,lens.magnification,visible ? 1.f : 0.f};result.valid=true;result.pupil_radius=lens.pupil_radius;
		return result;
	}
	// Enclose the exact rays sampled by the existing lens mapping. A crop inside
	// this eye has the same origin/near plane and is covered by its shared culling.
	inline bool sampling_window(const projected_view& eye, spatial_panel::vec4& output) noexcept
	{
		output={};
		if(!eye.valid || eye.parameters[3]<=0 || eye.parameters[2]<1) return false;
		float left=1,top=1,right=0,bottom=0;
		for(unsigned i=0;i<4;++i)
		{
			const float u=eye.parameters[0]+(float(i&1)-eye.parameters[0])/eye.parameters[2];
			const float v=eye.parameters[1]+(float(i>>1)-eye.parameters[1])/eye.parameters[2];
			spatial_panel::vec4 p{};
			for(unsigned c=0;c<4;++c)
				p[c]=(eye.corners[0][c]*(1-u)+eye.corners[1][c]*u)*(1-v)+
					(eye.corners[2][c]*(1-u)+eye.corners[3][c]*u)*v;
			if(!std::isfinite(p[3]) || p[3]<=.001f) return false;
			const float x=p[0]/p[3]*.5f+.5f,y=.5f-p[1]/p[3]*.5f;
			if(!std::isfinite(x) || !std::isfinite(y)) return false;
			left=std::min(left,x);right=std::max(right,x);top=std::min(top,y);bottom=std::max(bottom,y);
		}
		// A small guard keeps bilinear samples off the crop edge. Reject rather
		// than clamp a frustum that would introduce rays outside the prepared eye.
		const float gx=std::max(.00001f,(right-left)*.01f),gy=std::max(.00001f,(bottom-top)*.01f);
		left-=gx;right+=gx;top-=gy;bottom+=gy;
		if(left<0 || top<0 || right>1 || bottom>1 || right-left<.0001f || bottom-top<.0001f) return false;
		output={left,top,right-left,bottom-top};
		return true;
	}
}
