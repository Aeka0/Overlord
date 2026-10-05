#pragma once
#include "hands/pose_solver.hpp"
#include "weapon_holding.hpp"
#include "../controller_input.hpp"

namespace vr::gameplay::weapons::ads_alignment
{
	struct corridor
	{
		float near_depth,far_depth,lateral,below,above,facing;
	};
	inline constexpr corridor entry{.15f,1.5f,.09f,-.025f,.16f,.8660254f}; // 30 degrees
	inline constexpr corridor exit{.10f,1.7f,.13f,-.06f,.22f,.7660444f}; // 40 degrees
	inline constexpr float sight_reach_meters=.60f;
	inline bool valid_sight_distance(float meters) noexcept
	{
		return std::isfinite(meters) && meters>=0 && meters<=10.f;
	}
	inline float far_extension(float sight_to_muzzle_meters) noexcept
	{
		// Long barrels/suppressors must not consume the user's eye-to-sight
		// reach. Preserve the ordinary corridor and its 20 cm exit hysteresis.
		return std::max(0.f,sight_to_muzzle_meters+sight_reach_meters-entry.far_depth);
	}
	inline constexpr float maximum_translation(bool thermal) noexcept {return thermal ? .45f : .20f;}
	struct sample
	{
		float depth{},lateral{},height{},facing{};
		bool valid{};
	};
	inline bool tracked_hold(const controller_input::frame& input,const hold& owner) noexcept
	{
		return owner.can_fire() && input.grip[int(owner.rear)].valid &&
			(owner.support==hand::none || (valid_hand(owner.support) && owner.support!=owner.rear && input.grip[int(owner.support)].valid));
	}
	inline bool supported(const controller_input::frame& input,const hold& owner) noexcept
	{
		// Ownership is the physical grip lease, not proximity or a second
		// controller merely pointing at the same place as the firing hand.
		return tracked_hold(input,owner) && owner.support!=hand::none;
	}
	inline sample measure(hands::vec head,hands::vec head_forward,hands::vec muzzle,
		const std::array<hands::vec,3>& axis,float units) noexcept
	{
		using namespace hands;
		if(!std::isfinite(units) || units<=0 || units>10000) return {};
		for(const auto& v:{head,head_forward,muzzle,axis[0],axis[1],axis[2]})
			for(float x:v) if(!std::isfinite(x) || std::abs(x)>1e7f) return {};
		if(std::abs(dot(head_forward,head_forward)-1.f)>.002f) return {};
		for(const auto& a:axis) if(std::abs(dot(a,a)-1.f)>.002f) return {};
		if(std::abs(dot(axis[0],axis[1]))>.002f || length(sub(cross(axis[0],axis[1]),axis[2]))>.002f) return {};
		const auto offset=scale(sub(head,muzzle),1.f/units);
		return {-dot(offset,axis[0]),std::abs(dot(offset,axis[1])),dot(offset,axis[2]),
			std::clamp(dot(head_forward,axis[0]),-1.f,1.f),true};
	}
	inline bool inside(const sample& value,bool active,bool thermal=false,float sight_to_muzzle_meters=0) noexcept
	{
		if(!valid_sight_distance(sight_to_muzzle_meters))return false;
		const auto& limits=active ? exit : entry;
		const float facing=thermal ? (active ? .7071068f : .8191520f) : limits.facing; // Thermal: 35/45 degrees.
		return value.valid && value.depth>=limits.near_depth && value.depth<=limits.far_depth+far_extension(sight_to_muzzle_meters) &&
			value.lateral<=limits.lateral && value.height>=limits.below && value.height<=limits.above && value.facing>=facing;
	}
	inline float smooth(float t) noexcept
	{
		t=std::clamp(t,0.f,1.f);
		// Quintic easing also brings acceleration to zero at the endpoints.
		return std::clamp(t*t*t*(t*(t*6.f-15.f)+10.f),0.f,1.f);
	}
	inline float approach(const sample& value,float sight_to_muzzle_meters=0) noexcept
	{
		if(!value.valid || !valid_sight_distance(sight_to_muzzle_meters) || !std::isfinite(value.depth) || !std::isfinite(value.lateral) ||
			!std::isfinite(value.height) || !std::isfinite(value.facing) || value.facing< -1.f || value.facing>1.f) return 0;
		// Angular approach is independent of the native ADS dwell/latch. Full
		// approach within 5 degrees; zero at 45, with smooth endpoint acceleration.
		constexpr float radians_to_degrees=57.2957795131f;
		const float angle=std::acos(value.facing)*radians_to_degrees;
		const float angular=smooth((45.f-angle)/40.f);
		// Use the same positional corridor as native ADS, with smooth edges so
		// a parallel gun at the hip/side cannot receive angular assistance.
		return angular * smooth((value.depth-exit.near_depth)/(entry.near_depth-exit.near_depth)) *
			smooth((exit.far_depth+far_extension(sight_to_muzzle_meters)-value.depth)/(exit.far_depth-entry.far_depth)) *
			smooth((exit.lateral-value.lateral)/(exit.lateral-entry.lateral)) *
			smooth((value.height-exit.below)/(entry.below-exit.below)) *
			smooth((exit.above-value.height)/(exit.above-entry.above));
	}
}
