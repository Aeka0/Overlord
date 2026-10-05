#pragma once
#include "underbarrel_feed.hpp"
#include "hands/pose_solver.hpp"
#include "controller_palm.hpp"

namespace vr::gameplay::weapons::underbarrel
{
	struct palm_facing {float support{-1},firing{-1};};
	inline bool directional_grips(kind type)noexcept{return type==kind::m203 || type==kind::shotgun;}
	inline palm_facing facing(hands::quat wrist_in_gun,hands::vec palm_in_wrist,int hand)noexcept
	{
		float norm{};for(float x:wrist_in_gun){if(!std::isfinite(x))return {};norm+=x*x;}
		const auto length=hands::length(palm_in_wrist);
		if(hand<0 || hand>1 || norm<.5f || norm>1.5f || !std::isfinite(length) || length<.99f || length>1.01f)return {};
		const auto palm=hands::rotate(hands::normalize(wrist_in_gun),palm_in_wrist);
		// +Z is gun-up. Left support approaches from +Y, so its inward palm
		// points -Y; right support is the anatomical mirror. Never use world-up.
		return {palm[2],palm[1]*(hand?1.f:-1.f)};
	}
	inline bool support_facing(palm_facing p,bool retaining=false,kind type=kind::m203)noexcept
	{
		if(!std::isfinite(p.support) || !std::isfinite(p.firing) || std::abs(p.support)>1.001f || std::abs(p.firing)>1.001f)return false;
		// Once acquired, M203 support uses the host foregrip's positional
		// breakaway. Turning a held wrist cannot switch it to a firing grasp or
		// drop it; that role requires release and a fresh directional acquisition.
		if(retaining && type==kind::m203)
			return p.support*p.support+p.firing*p.firing<=1.002f; // Orthogonal unit-palm projections; rejects invalid facing's {-1,-1} sentinel.
		// Shotgun support also accepts an inward palm at its existing angle.
		return (type==kind::shotgun ? std::max(p.support,p.firing):p.support)>=(retaining?-.173648f:.258819f);
	}
	inline bool firing_facing(palm_facing p,bool retaining=false)noexcept
	{return p.firing>=(retaining?.642788f:.766045f) && p.firing>p.support+.12f;}
	inline constexpr float firing_acquire=.075f,firing_release=.095f;
	// Support keeps authored distances with a broad 75-degree palm-up gate.
	// M203 retention follows ordinary foregrip breakaway; firing stays directional.
	// Clear firing intent excludes support only inside the firing contact.
	inline bool support_intent(float fire_distance,palm_facing palm,kind type=kind::m203)noexcept
	{return support_facing(palm,false,type) && !(fire_distance>=0 && fire_distance<=firing_acquire && firing_facing(palm));}
	enum class contact_role { none, firing, action, support };
	struct part_limits {float lateral{},step{},retention{};};
	inline constexpr part_limits limits(kind type)noexcept
	{return type==kind::shotgun?part_limits{.16f,.40f,.18f}:part_limits{.10f,.25f,.12f};}
	inline part_limits support_limits(kind type,float release_radius)noexcept
	{
		auto result=limits(type);
		if(std::isfinite(release_radius) && release_radius>0 && release_radius<=.5f)
		{result.retention=std::max(result.retention,release_radius);result.lateral=std::max(result.lateral,release_radius);}
		return result;
	}
	inline bool retained_support_span(hands::vec rest,float distance,float previous,float release_radius,float max_step)noexcept
	{
		const float expected=hands::length(rest);
		return std::isfinite(expected) && std::isfinite(distance) && std::isfinite(previous) &&
			std::isfinite(release_radius) && release_radius>0 && std::isfinite(max_step) && max_step>0 &&
			distance>=.08f && std::abs(distance-expected)<=release_radius && std::abs(distance-previous)<=max_step;
	}
	struct stroke_result {bool valid{};float travel{};};
	inline stroke_result project_stroke(hands::vec start,hands::vec previous,hands::vec current,hands::vec axis,float initial,float stroke,part_limits tolerance=limits(kind::m203))noexcept
	{
		if(!std::isfinite(initial)||!std::isfinite(stroke)||stroke<=0||initial<0||initial>stroke)return {};
		if(!std::isfinite(tolerance.lateral)||!std::isfinite(tolerance.step)||tolerance.lateral<=0||tolerance.step<=0)return {};
		for(auto v:{start,previous,current,axis})for(float x:v)if(!std::isfinite(x))return {};
		const float length=hands::length(axis);if(length<.99f || length>1.01f)return {};
		const auto delta=hands::sub(current,start);const float axial=hands::dot(delta,axis);
		if(hands::length(hands::sub(delta,hands::scale(axis,axial)))>tolerance.lateral || hands::length(hands::sub(current,previous))>tolerance.step)return {};
		return {true,std::clamp(initial+axial,0.f,stroke)};
	}
	inline contact_role choose_grip(const state& ammo,float travel,float fire_distance,float action_distance,palm_facing palm,float support_radius=.075f)noexcept
	{
		if(!valid(ammo) || !std::isfinite(travel) || travel<0 || !std::isfinite(support_radius) || support_radius<=0 || support_radius>.5f)return contact_role::none;
		if(ammo.id.type==kind::gp25)return action_distance>=0 && action_distance<=support_radius?contact_role::support:contact_role::none;
		const bool fire=fire_distance>=0 && fire_distance<=firing_acquire && firing_facing(palm);
		const float radius=support_radius;
		if(!std::isfinite(radius) || radius<=0 || radius>.5f)return contact_role::none;
		const bool closed_support=ammo.id.type==kind::m203 && !ammo.open && travel<=.001f;
		const bool action=action_distance>=0 && action_distance<=radius && (ammo.id.type==kind::m203 || !ammo.chamber || ammo.open || travel>.001f) &&
			(closed_support?support_intent(fire_distance,palm):!fire && std::max(palm.support,palm.firing)>=.258819f);
		// Grasp permission is independent of firing readiness. An open launcher
		// can be held at its trigger grip but ready() still refuses emission.
		if(fire && (!action || fire_distance/firing_acquire<=action_distance/radius))return contact_role::firing;
		return action?(ammo.id.type==kind::m203 && !ammo.open && travel<=.001f?contact_role::support:contact_role::action):contact_role::none;
	}
	inline palm_facing controller_facing(hands::quat controller_in_gun,int hand)noexcept
	{
		// Calibrated aim coordinates: +X forward, +Y left, +Z up. The
		// physical palm is inward on an upright controller. A rifle's authored
		// wrist pose is a VISUAL offset (SCAR palm-up, M4 vertical foregrip),
		// not a controller calibration and must never rotate input admission.
		return facing(controller_in_gun,hands::controller_palm_axis(hand),hand);
	}
	inline float firing_distance(hands::vec delta,float forward_extension)noexcept
	{
		if(!std::isfinite(forward_extension)||forward_extension<0||forward_extension>.15f)return INFINITY;
		for(float f:delta)if(!std::isfinite(f))return INFINITY;
		delta[0]-=std::clamp(delta[0],0.f,forward_extension);
		return hands::length(delta); // Sphere swept forward; rear/lateral bounds stay fixed.
	}
}
