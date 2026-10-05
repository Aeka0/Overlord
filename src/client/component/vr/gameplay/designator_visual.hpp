#pragma once
#include "component/vr/gameplay/hand_pose_math.hpp"
#include "../world_beam_renderer.hpp"
namespace vr::gameplay::equipment::special::designator_visual
{
	using namespace hands;
	struct surface {vec point{},normal{};float range{};bool hit{};};
	inline bool endpoint(vec origin,vec forward,const surface& hit,float units,vec& out)noexcept
	{
		if(!std::isfinite(units) || units<=0 || !std::isfinite(hit.range) || hit.range<=0)return false;
		if(!hit.hit){out=add(origin,scale(forward,hit.range));return true;}
		const float denominator=dot(forward,hit.normal);if(!std::isfinite(denominator) || denominator>=-.001f)return false;
		const float distance=dot(sub(hit.point,origin),hit.normal)/denominator;
		if(!std::isfinite(distance) || distance<=0 || distance>hit.range)return false;
		out=add(origin,scale(forward,distance));
		// Late latch only a nearby collision plane; never extend it through a doorway.
		return length(sub(out,hit.point))<=units*.5f;
	}
	inline std::array<vec,8> geometry(vec start,vec end,vec normal,vec eye,float units)
	{
		const auto forward=unit(sub(end,start));start=add(start,scale(forward,units*.002f));
		auto across=cross(forward,sub(eye,start));if(length(across)<.001f)across=cross(forward,{0,0,1});
		if(length(across)<.001f)across={0,1,0};across=scale(unit(across),units*.0006f);
		normal=unit(normal);auto right=unit(cross(normal,std::abs(normal[2])<.8f?vec{0,0,1}:vec{0,1,0}));
		const auto up=scale(unit(cross(normal,right)),units*.004f);right=scale(right,units*.004f);
		const auto center=add(end,scale(normal,units*.001f));
		return {sub(start,across),add(start,across),sub(end,across),add(end,across),
			sub(add(center,up),right),add(add(center,up),right),sub(sub(center,up),right),add(sub(center,up),right)};
	}
	inline world_beam::projected project(const std::array<vec,8>& world,vec eye,const spatial_panel::matrix& vp,bool hit)
	{
		world_beam::projected result;result.spot=hit;
		for(unsigned i=0;i<8;++i)for(unsigned c=0;c<4;++c){result.points[i][c]=vp[12+c];for(unsigned a=0;a<3;++a)result.points[i][c]+=(world[i][a]-eye[a])*vp[a*4+c];}
		return result; // Preserve native reverse-Z, unlike diagnostic/HUD lines.
	}
}
