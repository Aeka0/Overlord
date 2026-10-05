#pragma once
#include "component/vr/gameplay/weapon_pose_library.hpp"
#include "component/vr/gameplay/weapons/ak47/profile.hpp"
#include "component/vr/gameplay/grip_presenter.hpp"
#include "component/vr/gameplay/part_hand_constraint.hpp"
#include "component/vr/gameplay/hands/pose_math.hpp"

namespace ak_hand_tests
{
	template<class Check> void run(const vr::gameplay::hands::rig& r,
		std::span<const vr::gameplay::hands::bone_definition> definitions,
		const vr::gameplay::weapons::profile& grip,Check check)
	{
		using namespace vr::gameplay::hands;
		using namespace vr::gameplay::weapons;
		std::array<bone,256> source{},output{};
		for (int i=0;i<r.count;++i) source[i]=definitions[i].bind;
		const auto library=bind_weapon_poses(r,definitions,grip);
		check(library.valid,"AK complete fingers, palms and equip parts bind before presentation");
		if (!library.valid) return;
		const std::array<anchor,2> targets{{{source[r.arms[0].wrist].position,{0,0,0,1}},
			{source[r.arms[1].wrist].position,{0,0,0,1}}}};
		const std::array<vec,2> shoulders{source[r.arms[0].shoulder].position,source[r.arms[1].shoulder].position};
		const std::array<vec,3> axes{{{1,0,0},{0,1,0},{0,0,1}}};
		// The native right wrist leaves the rear grip in tac_pullout_first frame16.
		// Move its complete source subtree; the tracked rear target stays fixed.
		const auto native_right=add(source[r.gun].position,vec{4.08340138f,-4.01689496f,3.41887590f});
		const auto delta=sub(native_right,source[r.arms[1].wrist].position);
		for (int i=0;i<r.count;++i) if (descendant(i,r.arms[1].wrist,r)) source[i].position=add(source[i].position,delta);
		// Residual native cocking/part motion must not survive the equip fallback.
		for (int i=r.gun+1;i<r.count;++i) source[i].position=add(source[r.gun].position,{8,5,-5});
		vr::controller_input::frame input{}; input.sequence=input.reference_generation=1; input.focused=true;
		input.sampled_at=vr::controller_input::clock::now();
		for (int hand=0;hand<2;++hand) { input.grip[hand].valid=input.aim[hand].valid=true; input.squeeze[hand]={true,false,0,1}; }
		hold owner{49,1,hand::right,hand::none,hold_source::engine_default,1};
		grip_presenter presenter; std::array<bool,2> limited{};
		const auto result=presenter.update(grip,library,r,{source.data(),size_t(r.count)},targets,shoulders,axes,
			input,owner,1,39.37007874f,true,grip.suppress_equip("h2_wpn_asl_ak47_tac_pullout_first"),input.sampled_at,
			{output.data(),size_t(r.count)},limited,false);
		check(result.valid && length(sub(output[r.arms[1].wrist].position,targets[1].position))<.001f &&
			length(sub(add(output[r.gun].position,rotate(output[r.gun].rotation,grip.wrists[1].position)),targets[1].position))<.001f,
			"native right-hand cocking cannot move AK off its tracked rear-grip anchor");
		for (const auto& part:grip.equip_rest)
			for (int i=r.gun+1;i<r.count;++i) if (definitions[i].name==part.name)
			{
				const auto expected=vr::gameplay::hands::pose_math::compose(vr::gameplay::hands::pose_math::as_anchor(output[r.parent[i]]),part.local);
				check(length(sub(output[i].position,expected.position))<.001f &&
					length(sub(rotate(output[i].rotation,{1,0,0}),rotate(expected.rotation,{1,0,0})))<.001f,
					"AK equip fallback restores parent-local parts including nested magazine rounds");
			}
		const auto parts=physical_reload::bind_parts(r,definitions,*grip.reload); check(parts.valid,"AK hand regression has a complete mechanical rig");
		if (!parts.valid) return;
		const auto before=output;
		for (const auto& style:ak47::bolt_grips) for (float travel:{0.f,.035f,.094f})
		{
			output=before;
			const auto gun=vr::gameplay::hands::pose_math::as_anchor(output[r.gun]);
			const auto wrist=physical_reload::part_wrist(style.wrist,ak47::reload_interaction.slide_axis,travel*39.37007874f,.094f*39.37007874f);
			const auto desired=vr::gameplay::hands::pose_math::compose(gun,wrist);
			check(constrain_part_hand(r,library,grip,targets,shoulders,axes,1,desired,{output.data(),size_t(r.count)}),
				"both AK contact-side poses constrain the left operating hand");
			vr::gameplay::hands::pose_math::fingers(r,library,grip,style.fingers,0,{output.data(),size_t(r.count)});
			auto bolt=ak47::bolt_rest; bolt.position=add(bolt.position,scale(ak47::reload_interaction.slide_axis,travel*39.37007874f));
			vr::gameplay::hands::pose_math::move_part(r,parts.slide,vr::gameplay::hands::pose_math::compose(gun,bolt),{output.data(),size_t(r.count)});
			check(length(sub(output[r.arms[0].wrist].position,desired.position))<.001f,"AK left wrist follows the selected grip over the full bolt stroke");
			for (int i=0;i<r.count;++i)
			{
				if (!descendant(i,r.arms[0].shoulder,r) && !descendant(i,parts.slide,r))
					check(output[i].position==before[i].position && output[i].rotation==before[i].rotation,
						"AK rack changes only the left arm and bolt, never rear hand, gun, muzzle or magazine");
				for (const auto& joint:style.fingers) if (definitions[i].name==joint.name)
				{
					const auto q=multiply(conjugate(output[r.parent[i]].rotation),output[i].rotation);
					check(length(sub(rotate(q,{1,0,0}),rotate(joint.rotation,{1,0,0})))<.001f,
						"AK side-grasp palms and every finger use the selected pose instead of the native support animation");
				}
			}
		}
	}
}
