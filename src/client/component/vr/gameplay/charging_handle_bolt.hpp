#pragma once
#include "hands/pose_solver.hpp"
#include <span>
#include <string_view>

namespace vr::gameplay::weapons
{
	struct bolt_travel_sample { float handle_m, bolt_m; };
	// Optional visual follower of a non-reciprocating handle. It has no input
	// or ammunition authority. Native firing animates it where the asset provides
	// that motion; a static fire clip uses the presentation-only return below.
	struct charging_handle_bolt
	{
		std::string_view bone;
		hands::anchor rest;
		std::span<const bolt_travel_sample> travel;
		float locked_m;
		float shot_stroke_m{}; // Optional direct fire travel; manual take-up is not a firing curve.
	};
	inline bool valid_bolt(const charging_handle_bolt& p,float handle_stroke) noexcept
	{
		if (p.bone.empty() || p.travel.size()<2 || p.travel.size()>16 ||
			!std::isfinite(handle_stroke) || handle_stroke<=0 || handle_stroke>1 ||
			!std::isfinite(p.locked_m) || p.locked_m<0 || p.locked_m>1 ||
			!std::isfinite(p.shot_stroke_m) || p.shot_stroke_m<0 || p.shot_stroke_m>1) return false;
		float norm{};
		for (float x:p.rest.position) if (!std::isfinite(x) || std::abs(x)>10000) return false;
		for (float x:p.rest.rotation) { if (!std::isfinite(x)) return false; norm+=x*x; }
		if (std::abs(norm-1)>.001f || p.travel.front().handle_m!=0 || p.travel.front().bolt_m!=0) return false;
		for (size_t i=0;i<p.travel.size();++i)
		{
			const auto point=p.travel[i];
			if (!std::isfinite(point.handle_m) || !std::isfinite(point.bolt_m) ||
				point.handle_m<0 || point.handle_m>1 || point.bolt_m<0 || point.bolt_m>1 ||
				(i && (point.handle_m<=p.travel[i-1].handle_m || point.bolt_m<p.travel[i-1].bolt_m))) return false;
		}
		return p.travel.back().handle_m>=handle_stroke && p.locked_m<=p.travel.back().bolt_m;
	}
	inline float bolt_travel(const charging_handle_bolt& p,float handle_m,bool locked) noexcept
	{
		if (!std::isfinite(handle_m) || !valid_bolt(p,p.travel.empty() ? 0 : p.travel.back().handle_m)) return 0;
		handle_m=std::clamp(handle_m,0.f,p.travel.back().handle_m);
		float result=p.travel.back().bolt_m;
		for (size_t i=1;i<p.travel.size();++i) if (handle_m<=p.travel[i].handle_m)
		{
			const auto a=p.travel[i-1], b=p.travel[i];
			result=a.bolt_m+(b.bolt_m-a.bolt_m)*(handle_m-a.handle_m)/(b.handle_m-a.handle_m);
			break;
		}
		return std::max(result,locked ? p.locked_m : 0.f);
	}
	inline float fired_bolt_travel(const charging_handle_bolt& p,float handle_m,bool locked,float shot_age) noexcept
	{
		const auto manual=bolt_travel(p,handle_m,false);
		// Accepted shot marks discharge with the bolt forward. Recoil returns it
		// to the sear only if the authoritative open-bolt state retained it.
		const auto fraction=std::isfinite(shot_age) && shot_age>=0 ? std::clamp(shot_age/.06f,0.f,1.f):1.f;
		return std::max(manual,locked?p.locked_m*fraction:0.f);
	}
	inline float action_shot_fraction(float age) noexcept
	{
		return std::isfinite(age) && age>=0 && age<.08f ?
			std::clamp(std::min(age/.02f,(.08f-age)/.06f),0.f,1.f) : 0.f;
	}
}
