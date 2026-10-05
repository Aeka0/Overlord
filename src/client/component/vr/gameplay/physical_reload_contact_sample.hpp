#pragma once
#include "physical_reload_runtime.hpp"
#include "component/vr/gameplay/hand_pose_mirror.hpp"
#include "belt_presentation.hpp"
#include "magazine_grip_selection.hpp"

namespace vr::gameplay::weapons::physical_reload
{
	inline bool sample_contact(scene_frame& s,const presentation& v,const hand_interaction::frame& f)noexcept
	{
		using namespace hands;using namespace hands::pose_math;
		const auto* live=f.find(s.owner.id());const auto* p=s.definition;
		if(!live || !p || !s.binding.valid || live->assembly!=s.assembly || !valid_hand(live->owner.holding_hand()))return false;
		const auto& d=*p;const int rear=int(live->owner.holding_hand()),off=1-rear;const float units=f.body.units_per_meter;
		if(!(f.valid_hands&(1u<<off)) || units<=0)return false;
		const auto gun=live->gun;const auto mirror=s.binding.mirror;
		const bool knife=s.contact.knife_held;
		const bool knife_mag=d.knife_magazine_in_wrist && (v.magazine_leased()?v.knife_magazine_grasp:knife);
		const bool knife_slide=!d.knife_slide_grips.empty() && (v.slide_held?v.knife_slide_grasp:knife);
		const bool paired=knife || (v.magazine_leased() && v.knife_magazine_grasp) || (v.slide_held && v.knife_slide_grasp);
		const auto basis=paired && s.binding.paired_valid?s.binding.paired_wrist:s.binding.wrist;
		const anchor wrist{f.wrists[off].position,normalize(multiply(f.wrists[off].rotation,basis))};
		const auto raw=compose(inverse(gun),wrist);
		const auto magazine_pose=select_magazine_grip(d,raw.rotation,off,mirror,knife_mag,v.magazine_leased(),v.magazine_pose,
			controller_in_body(f.wrists[off].rotation,f.body.body),v.ammo.magazine_inserted && (v.magazine_seated || v.magazine_grabbed));
		const auto attached_pose=select_magazine_grip(d,raw.rotation,off,mirror,knife_mag,v.magazine_leased(),v.magazine_pose,
			controller_in_body(f.wrists[off].rotation,f.body.body),true);
		const auto in_wrist=magazine_pose.in_wrist;
		const anchor magazine_wrist{wrist.position,normalize(multiply(f.wrists[off].rotation,
			magazine_wrist_basis(d,magazine_pose,basis,knife_mag)))};
		const auto mag=compose(magazine_wrist,in_wrist),attached=compose(gun,d.magazine_rest);
		const auto source=knife_slide?d.knife_slide_grips:d.slide_grips;
		if(source.size()>max_part_grips)return false;
		std::array<part_grip_pose,max_part_grips> grips{};
		for(size_t i=0;i<source.size();++i)grips[i]=off?hands::pose_mirror::part(source[i],mirror):source[i];
		const auto hand_local=scale(raw.position,1/units);
		const auto* manual=d.interaction.manual_bolt;
		float travel=minimum_slide_travel(d.interaction,d.ammunition,v.ammo);
		if(v.slide_held)travel=v.slide_travel;
		auto offset=scale(d.interaction.slide_axis,travel*units);auto low=d.slide_grab_low,high=d.slide_grab_high;
		if(manual || (d.handle_catch && v.ammo.action==mechanics::action_state::latched_open))
		{
			const auto raised=manual?rotating_bolt::pose(d.slide_rest,*manual,{v.ammo.bolt.lift,v.ammo.bolt.travel},units):handle_pose(d.slide_rest,d.handle_catch,{},0,1);
			if(manual)offset={};
			for(size_t i=0;i<source.size();++i)grips[i].wrist=carry_with_handle(d.slide_rest,raised,grips[i].wrist);
			const auto bounds=handle_bounds(d.slide_rest,raised,low,high);low=bounds[0];high=bounds[1];
		}
		const bool held_style=v.slide_held && v.slide_grip.pose<source.size();
		const auto palm=rotate(compose(inverse(gun),f.wrists[off]).rotation,controller_palm_axis(off));
		auto chosen=choose_part_grip(held_style?std::span<const part_grip_pose>{&grips[v.slide_grip.pose],1}:
			std::span<const part_grip_pose>{grips.data(),source.size()},raw,offset,low,high,units,d.slide_capture,off,palm[2],held_style);
		if(held_style && chosen.pose!=no_part_grip)chosen.pose=v.slide_grip.pose;
		geometry g{true,live->owner.weapon,v.active?v.ammo.instance_generation:0,f.input.reference_generation,f.input.sequence,
			f.waist(hand(off),d.supply,d.interaction.waist_radius),chosen.distance_meters,
			magazine_alignment(d,gun,mag),hand_local,magazine_tip_in_well(d,gun,mag,units),
			length(sub(wrist.position,compose(attached,inverse(in_wrist)).position))/units,chosen.pose};
		const auto bolt_style=v.slide_held?v.slide_grip.pose:chosen.pose;
		g.knife_held=knife;g.bolt_hand=bolt_style<source.size()?
			scale(compose(raw,{grips[bolt_style].contact_in_wrist,{0,0,0,1}}).position,1/units):hand_local;
		g.magazine_pose=magazine_pose.index;
		sample_attached_magazine(d,g,gun,wrist,f.wrists[off].rotation,basis,attached_pose,mag,units,knife_mag);
		if(d.interaction.manual_magazine && d.interaction.manual_magazine->prefer_grasp_facing && chosen.pose<source.size())
			g.magazine_facing=closer_grasp_facing(raw.rotation,compose(d.magazine_rest,inverse(attached_pose.in_wrist)).rotation,grips[chosen.pose].wrist.rotation);
		if(d.handle_catch || d.interaction.receiver_release)
		{
			const auto centre=action_slap_centre(d,v.ammo.action,units);
			g.catch_input.valid=s.contact.catch_input.valid;g.catch_input.rotation=raw.rotation;g.catch_input.hand_world=scale(wrist.position,1/units);g.diagnose_slap=s.contact.diagnose_slap;
			for(size_t i=0;i<s.contact_in_wrist.size();++i)g.catch_input.slap_points[i]=scale(sub(compose(raw,{s.contact_in_wrist[i],{0,0,0,1}}).position,centre),1/units);
			if(d.interaction.receiver_release && s.palm_in_wrist)
				g.catch_input.palm=palm_relative_to_target(*s.palm_in_wrist,raw,centre,units);
		}
		if(d.interaction.belt)
		{
			g.belt=belt_feed::present(*d.interaction.belt,{},{},v.ammo,v.belt_grip,gun,wrist,mag,d.magazine_rest,off,mirror,units,v.active,false,{},true).query;
			if(s.contact.belt.push.valid)
			{
				const auto palm_point=compose(wrist,{s.contact_in_wrist.back(),{0,0,0,1}}).position;
				g.belt.push=belt_feed::sample_cover_push(*d.interaction.belt,gun,palm_point,
					rotate(f.wrists[off].rotation,controller_palm_axis(off)),palm_point,units,sub(palm_point,wrist.position));
			}
		}
		s.owner=live->owner;s.input=f.input;s.contact=g;s.gameplay=true;s.units_per_meter=units;s.attached_world=attached;s.held_world=mag;
		return true;
	}
}
