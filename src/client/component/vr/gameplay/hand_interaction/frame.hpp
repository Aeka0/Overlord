#pragma once
#include "core.hpp"
#include "../weapon_scene.hpp"
#include "../body_supply_volume.hpp"
#include "../weapon_holsters.hpp"

namespace vr::gameplay::hand_interaction
{
	struct frame
	{
		controller_input::frame input{};
		head_pose_bridge::spatial_frame body{};
		std::array<hands::anchor,2> wrists{};
		std::array<weapons::carry::scene,15> objects{};
		unsigned valid_hands{};
		weapons::carry::holsters holsters{};
		const weapons::carry::scene* find(weapons::weapon_identity id)const noexcept
		{for(const auto& s:objects)if(s.owner.id()==id && s.assembly)return &s;return nullptr;}
		float waist(hand actor,const body_supply_layout& layout,float radius)const noexcept
		{
			if(!vr::valid_hand(actor) || !(valid_hands&(1u<<unsigned(actor))) || body.units_per_meter<=0)return INFINITY;
			float result=INFINITY;
			for(const auto& v:body_supply_volumes(body.head_position,body.head_yaw_axis,body.units_per_meter,layout,radius))
				result=std::min(result,v.distance(wrists[unsigned(actor)].position)/body.units_per_meter);
			return result;
		}
	};
	// Binding data has assembly lifetime. It contains no input edge or world
	// pose: a culled viewmodel cannot make otherwise current tracking stale.
	struct hand_binding
	{
		hands::quat wrist{0,0,0,1},mirror{0,0,0,1};
		bool valid{};
		hands::quat paired_wrist{0,0,0,1};bool paired_valid{};
	};
}
