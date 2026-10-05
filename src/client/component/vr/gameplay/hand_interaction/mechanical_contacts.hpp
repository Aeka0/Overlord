#pragma once
#include "../cylinder_runtime.hpp"
#include "../tube_runtime.hpp"
#include "../break_action_runtime.hpp"
#include "component/vr/gameplay/hand_pose_mirror.hpp"

namespace vr::gameplay::hand_interaction
{
	template<class Scene> inline const weapons::carry::scene* bound(const Scene& s,const frame& f,bool require_other=true)noexcept
	{
		const auto* live=f.find(s.owner.id());
		return live && s.definition && s.binding.valid && live->assembly==s.assembly && vr::valid_hand(live->owner.holding_hand()) &&
			(f.valid_hands&(1u<<unsigned(live->owner.holding_hand()))) && (!require_other || (f.valid_hands&(1u<<(1-unsigned(live->owner.holding_hand()))))) && f.body.units_per_meter>0?live:nullptr;
	}
	template<class Scene> inline hands::anchor wrist(const Scene& s,const frame& f,int h)noexcept
	{return {f.wrists[h].position,hands::normalize(hands::multiply(f.wrists[h].rotation,s.binding.wrist))};}
	inline bool sample(weapons::cylinder::scene_frame& s,const weapons::cylinder::presentation& v,const frame& f)noexcept
	{
		using namespace hands;using namespace weapons;using namespace hands::pose_math;
		const auto* live=bound(s,f);if(!live)return false;const auto& p=*s.definition;const int off=1-int(live->owner.holding_hand());const float units=f.body.units_per_meter;
		float progress=0;if(v.active){const float age=std::max(0.f,std::chrono::duration<float>(f.input.sampled_at-v.action_at).count());
			progress=v.ammo.phase==cylinder::action::open?1.f:v.ammo.phase==cylinder::action::opening?std::clamp(age/p.interaction.opening_seconds,0.f,1.f):v.ammo.phase==cylinder::action::closing?1-std::clamp(age/p.interaction.closing_seconds,0.f,1.f):0;}
		auto swing=p.swing_closed;swing.rotation=blend_quat(swing.rotation,p.swing_open.rotation,progress);
		const auto barrel=compose(live->gun,compose(swing,s.cylinder_in_swing)),face=compose(barrel,p.face_in_cylinder);
		const auto grasp=off?hands::pose_mirror::object_in_wrist({},p.loader_in_wrist,s.binding.mirror):p.loader_in_wrist;
		const auto loader=compose(wrist(s,f,off),grasp),tip=compose(loader,{p.loader_tip,{0,0,0,1}});
		s.input=f.input;s.owner=live->owner;s.gameplay=true;s.units=units;s.cylinder_world=barrel;s.loader_world=loader;
		s.contact={true,s.owner.weapon,v.active?v.ammo.instance_generation:0,f.input.reference_generation,f.input.sequence,
			f.waist(hand(off),{},p.interaction.waist_radius),rotate(barrel.rotation,{-1,0,0})[2],dot(rotate(loader.rotation,{1,0,0}),rotate(barrel.rotation,{1,0,0})),scale(compose(inverse(face),tip).position,1/units)};return true;
	}
	inline bool sample(weapons::tube::scene_frame& s,const weapons::tube::presentation& v,const frame& f)noexcept
	{
		using namespace hands;using namespace weapons;using namespace hands::pose_math;
		const auto* live=bound(s,f,!s.definition || !tube::levered(s.definition->ammunition));if(!live)return false;const auto& p=*s.definition;const int off=1-int(live->owner.holding_hand());const float units=f.body.units_per_meter;
		const auto& binding=s.has_shell_bindings?s.shell_bindings[off]:s.binding;
		if(!binding.valid || p.rack_grips.size()>max_part_grips)return false;
		const auto raw=(f.valid_hands&(1u<<off))?anchor{f.wrists[off].position,normalize(multiply(f.wrists[off].rotation,binding.wrist))}:live->gun;const auto local=compose(inverse(live->gun),raw);
		const auto grasp=off?hands::pose_mirror::object_in_wrist({},p.shell_in_wrist,binding.mirror):p.shell_in_wrist;
		const auto shell=compose(local,grasp);const auto centre=compose(shell,{p.shell_center,{0,0,0,1}}).position;
		std::array<part_grip_pose,max_part_grips> grips{};for(size_t i=0;i<p.rack_grips.size();++i)grips[i]=off?hands::pose_mirror::part(p.rack_grips[i],binding.mirror):p.rack_grips[i];
		float travel=tube::manual(p.ammunition)?v.travel:v.ammo.phase==tube::action::locked_open?p.interaction.rack.locked_travel:v.ammo.phase==tube::action::held_open?p.interaction.rack.slide_stroke:0;
		const auto c=choose_part_grip({grips.data(),p.rack_grips.size()},local,scale(p.interaction.rack.slide_axis,travel*units),p.rack_low,p.rack_high,units);
		tube::geometry g{true,live->owner.weapon,v.active?v.ammo.instance_generation:0,f.input.reference_generation,f.input.sequence};
		g.waist_distance=(f.valid_hands&(1u<<off))?f.waist(hand(off),{},p.interaction.rack.waist_radius):10;g.rack_distance=(tube::rotary(p.ammunition) || tube::levered(p.ammunition))?10:c.distance_meters;g.rack_pose=c.pose;g.hand_in_gun=scale(local.position,1/units);
		g.port_delta=scale(sub(centre,p.port_center),1/units);g.tube_delta=scale(sub(centre,p.tube_center),1/units);const auto forward=rotate(shell.rotation,{1,0,0});g.port_alignment=dot(forward,p.port_forward);g.tube_alignment=dot(forward,p.tube_forward);
		s.input=f.input;s.owner=live->owner;s.gameplay=true;s.units=units;s.contact=g;s.shell_world=compose(live->gun,shell);s.ejection_world=compose(live->gun,tube_ejection_pose(p));return true;
	}
	inline bool sample(weapons::break_action::scene_frame& s,const weapons::break_action::presentation& v,const frame& f)noexcept
	{
		using namespace hands;using namespace weapons;using namespace hands::pose_math;
		const auto* live=bound(s,f);if(!live)return false;const auto& p=*s.definition;const int off=1-int(live->owner.holding_hand());const float units=f.body.units_per_meter;
		const auto raw=compose(inverse(live->gun),wrist(s,f,off));const auto grasp=off?hands::pose_mirror::part(p.barrel_grip,s.binding.mirror):p.barrel_grip;
		const auto shell_grasp=off?hands::pose_mirror::object_in_wrist({},p.shell_in_wrist,s.binding.mirror):p.shell_in_wrist;
		const auto shell=compose(raw,shell_grasp);const auto tip=compose(shell,{p.shell_tip,{0,0,0,1}}).position;
		const auto barrel=barrel_pose(p,v.active?v.ammo.hinge:0),barrel_wrist=compose(compose(barrel,inverse(p.barrel_closed)),grasp.wrist);
		const auto point=compose(raw,{grasp.contact_in_wrist,{0,0,0,1}}).position,contact=compose(barrel_wrist,{grasp.contact_in_wrist,{0,0,0,1}}).position,radial=sub(point,p.hinge_pivot);
		break_action::geometry g;g.valid=true;g.weapon=live->owner.weapon;g.instance_generation=v.active?v.ammo.instance_generation:0;g.reference_generation=f.input.reference_generation;g.input_sequence=f.input.sequence;
		g.barrel_hand=scale(point,1/units);g.barrel_distance=std::hypot(radial[0],radial[2])<.04f*units?10:length(sub(point,contact))/units;g.barrel_angle=std::atan2(-radial[2],radial[0]);g.waist_distance=f.waist(hand(off),{},p.interaction.waist_radius);
		for(unsigned n=0;n<p.ammunition.capacity;++n){const auto face=compose(barrel,p.mouth_in_barrel[n]);g.shell_in_chamber[n]=scale(compose(inverse(face),{tip,{0,0,0,1}}).position,1/units);g.alignment[n]=dot(rotate(shell.rotation,{1,0,0}),rotate(face.rotation,{0,0,1}));s.chambers_world[n]=compose(compose(live->gun,barrel),p.chamber_in_barrel[n]);}
		s.input=f.input;s.owner=live->owner;s.gameplay=true;s.units=units;s.contact=g;s.shell_world=compose(live->gun,shell);s.gun_world=live->gun;return true;
	}
}
