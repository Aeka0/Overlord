#pragma once
#include "physical_reload_rig.hpp"
#include "component/vr/gameplay/hand_pose_math.hpp"
#include "chamber_cartridge.hpp"
#include "part_presentation.hpp"
#include "physical_reload_runtime.hpp"

namespace vr::gameplay::weapons::physical_reload
{
	inline std::optional<mechanics::state> stowed_ammunition(weapon_identity id,const reload_profile& definition,
		const presentation& live,int native_loaded) noexcept
	{
		if(!id)return std::nullopt;
		if(live.active)
		{
			if(live.owner.id()!=id || live.definition!=&definition || live.ammo.weapon!=id.weapon ||
				!mechanics::valid(definition.ammunition,live.ammo))return std::nullopt;
			return live.ammo;
		}
		mechanics::state preview{id.weapon,id.generation,1,false,false,0,0};
		return mechanics::from_native_automatic(definition.ammunition,native_loaded,preview) ? std::optional{preview} : std::nullopt;
	}
	// Read-only settled presentation: an unheld action follows its mechanical
	// stop, not the departed hand or its last animated recoil/return sample.
	inline part_presentation::result pose_stowed(const hands::rig& r,const part_rig& parts,
		const reload_profile& p,const mechanics::state& ammo,float units,std::span<hands::bone> solved) noexcept
	{
		using namespace hands;using namespace hands::pose_math;
		if(!parts.valid || r.count<=0 || r.count>256 || solved.size()<size_t(r.count) ||
			!std::isfinite(units) || units<=0 || !mechanics::valid(p.ammunition,ammo))return {};
		// These use independent geometry presenters and are not waist-admitted.
		if(p.interaction.belt || partition_mesh(p))return {};
		const auto gun=as_anchor(solved[r.gun]);
		const auto magazine=compose(gun,p.magazine_rest);
		move_part(r,parts.magazine,magazine,solved);
		if(p.receiver_parented_bullets)move_part(r,parts.bullets,compose(magazine,parts.bullets_in_magazine),solved);
		const auto travel=minimum_slide_travel(p.interaction,p.ammunition,ammo);
		const auto action=p.interaction.manual_bolt ? rotating_bolt::pose(p.slide_rest,*p.interaction.manual_bolt,
			{ammo.bolt.lift,ammo.bolt.travel},units) : handle_pose(p.slide_rest,p.handle_catch,p.interaction.slide_axis,
			travel*units,ammo.action==mechanics::action_state::latched_open?1.f:0.f);
		auto slide=action;
		if(p.handle_fold && p.handle_fold->end_bone.empty())slide=folded_handle_pose(slide,*p.handle_fold,0,1);
		move_part(r,parts.slide,compose(gun,slide),solved);
		if(p.handle_fold && parts.fold_end>=0)
			move_part(r,parts.fold_end,folded_handle_pose(as_anchor(solved[parts.slide]),*p.handle_fold,0,1),solved);
		if(p.bolt && parts.bolt>=0)
		{
			auto bolt=p.bolt->rest;
			bolt.position=add(bolt.position,scale(p.interaction.slide_axis,
				displayed_internal_bolt(p,travel,0,retained_internal_bolt(p.ammunition,ammo),-1)*units));
			move_part(r,parts.bolt,compose(gun,bolt),solved);
			if(p.handle_child_of_bolt)move_part(r,parts.slide,compose(gun,action),solved);
		}
		auto hidden=magazine_visibility(r,parts,p,ammo);
		if(p.chamber_round)
		{
			const auto chamber=chamber_cartridge_pose(p,ammo,travel,0,units);
			if(chamber)move_part(r,parts.bullets,compose(gun,*chamber),solved);
			else hidden=combine_part_masks(hidden,parts.bullet_mask);
		}
		return {true,hidden};
	}
}
