#pragma once
#include "component/vr/gameplay/hand_rig_builder.hpp"
#include "component/vr/gameplay/weapon_pose_library.hpp"
#include "component/vr/gameplay/weapon_profiles.hpp"
#include "component/vr/gameplay/weapon_carry_profiles.hpp"
#include "component/vr/gameplay/weapon_fire_delivery.hpp"
#include "component/vr/gameplay/native_viewmodel_policy.hpp"
#include "component/vr/gameplay/melee_motion.hpp"
#include "component/vr/gameplay/grip_presenter.hpp"
#include "component/vr/gameplay/weapon_carry_pose.hpp"
#include "riot_shield_data.hpp"
#include <limits>

namespace special_knife_tests
{
	template<class Check> void run(Check check)
	{
		using namespace vr::gameplay;using namespace hands;using namespace weapons;
		for(const std::string_view name:{"ending_knife","ending_knife_bloody","alt_ending_knife","alt_ending_knife_bloody","h2_cheatcommandoknife"})
		{
			check(native_fire_delivery(1,name)==fire_delivery::unsupported && !special_melee::uses_ammunition(1,name),"knife native BULLET metadata cannot enter firearm or ammunition adapters");
			check(personal_firearm_definition(1,3,name.starts_with("alt_") ? 3 : 0,name),"primary and alternate knives replace their flat viewmodel");
			const auto receiver=special_melee::receiver(name);const int count=special_melee::bone_count(receiver);
			std::array<bone_definition,11> bones{{{"root",-1},{"tag_weapon",0},{"j_shoulder_le",0},{"j_shoulder_ri",0},
				{"j_elbow_le",2},{"j_elbow_ri",3},{"j_wrist_le",4},{"j_wrist_ri",5},
				{special_melee::root(receiver),1},{count==2 ? "tag_knife_fx" : "tag_clip",8},{"tag_knife_fx",8}}};
			for(auto& b:bones)b.bind={{0,0,0,1},{0,0,0},2};
			const std::array<model_definition,2> models{{{"viewhands_test",0,8},{receiver,8,count}}};
			const auto span=std::span<const bone_definition>{bones.data(),std::size_t(8+count)};
			const auto resolved=resolve_rig(models,span,rig_kind::melee);
			check(!resolved.rejection && resolved.layout.gun==8 && resolved.layout.muzzle==-1 && resolved.muzzle_bone==-1,"reviewed knife resolves without a synthetic gun root or muzzle");
			check(resolve_rig(models,span).rejection!=nullptr,"firearm resolver still rejects a receiver without a muzzle");
			const auto match=select_profile(models,resolved.layout,span);const auto* p=match.value;
			check(p && p->melee && !p->reload && !p->tube && !p->cylinder && !p->break_open && !p->launcher && !p->support_enabled,"knife registers only its single-hand blade capability");
			if(p)check(length(sub(p->melee->tip,p->melee->base))>5 && p->fingers.size()==30,"knife retains distinct authored blade and articulated native fingers");
			check(bool(match.hidden[0]&(0x80000000u>>9))==(name=="h2_cheatcommandoknife"),"only the bayonet sheath is removed from tracked presentation");
			if(p)
			{
				// Reuse the captured common glove fixture; only the knife subtree differs.
				constexpr int hand_count=riot_shield_data::models[0].count;
				std::array<bone_definition,hand_count+3> actual{};
				std::copy_n(riot_shield_data::bones.begin(),hand_count,actual.begin());
				int tag=-1;for(int i=0;i<hand_count;++i)if(actual[i].name=="tag_weapon")tag=i;
				actual[hand_count]={special_melee::root(receiver),tag,{{0,0,0,1},{0,0,0},2}};
				for(int i=1;i<count;++i)actual[hand_count+i]={p->equip_rest[i-1].name,hand_count,
					{p->equip_rest[i-1].local.rotation,p->equip_rest[i-1].local.position,2}};
				const std::array<model_definition,2> assembly{{riot_shield_data::models[0],{receiver,hand_count,count}}};
				const auto actual_bones=std::span<const bone_definition>{actual.data(),std::size_t(hand_count+count)};
				const auto actual_rig=resolve_rig(assembly,actual_bones,rig_kind::melee);
				const auto library=bind_weapon_poses(actual_rig.layout,actual_bones,*p);
				check(!actual_rig.rejection && library.valid,"native knife fingers and effect socket bind to the captured glove");
				if(library.valid)for(const auto side:{hand::left,hand::right})
				for(const auto tracked:{quat{0,0,0,1},normalize(quat{.3f,-.2f,.4f,.8f})})
				{
					hold owner{137,1,side,hand::none};owner.instance_generation=1;
					carry::pose_profile fit(*p,owner,actual_rig.layout,library);
					std::array<bone,hand_count+3> native{},solved{};
					for(std::size_t i=0;i<actual_bones.size();++i)native[i]=actual[i].bind;
					const std::array<anchor,2> targets{{{{20,9,0},tracked},{{20,-9,0},tracked}}};
					const std::array<vec,2> shoulders{vec{0,7,8},vec{0,-7,8}};
					const std::array<vec,3> axes{vec{1,0,0},vec{0,1,0},vec{0,0,1}};
					std::array<bool,2> limited{};vr::controller_input::frame input;
					const auto now=vr::controller_input::clock::now();input.sequence=1;input.reference_generation=1;input.sampled_at=now;input.focused=true;
					grip_presenter presenter;const auto result=presenter.update(fit.value,library,actual_rig.layout,native,targets,shoulders,axes,
						input,fit.solver_owner,1,39.37007874f,true,true,now,solved,limited,true,true);
					check(result.valid && result.support==hand::none,"knife completes bilateral IK and finger solve with no muzzle or support hand");
					const auto contact=vr::gameplay::hands::pose_math::compose(vr::gameplay::hands::pose_math::as_anchor(solved[actual_rig.layout.gun]),fit.controls[int(side)]);
					check(length(sub(contact.position,solved[actual_rig.layout.arms[int(side)].wrist].position))<.001f,"knife handle remains registered to the controlling wrist in either hand");
					for(auto axis:{vec{1,0,0},vec{0,1,0},vec{0,0,1}})
						check(dot(rotate(solved[actual_rig.layout.arms[int(side)].wrist].rotation,axis),
							rotate(multiply(targets[int(side)].rotation,library.neutral_wrists[int(side)]),axis))>.9999f,
							"special knife wrist retains empty-VR-hand alignment while blade contact remains authored");
				}
			}
			auto malformed=bones;malformed[9].parent=7;
			check(resolve_rig(models,{malformed.data(),span.size()},rig_kind::melee).rejection!=nullptr,"knife rejects a detached effect/part bone");
			malformed=bones;malformed[8].bind.rotation[0]=std::numeric_limits<float>::quiet_NaN();
			check(resolve_rig(models,{malformed.data(),span.size()},rig_kind::melee).rejection!=nullptr,"knife rejects nonfinite bind geometry");
			auto unknown=models;unknown[1].name="unreviewed_knife";
			check(resolve_rig(unknown,span,rig_kind::melee).rejection!=nullptr,"knife admission is not a name substring or arbitrary muzzle bypass");
			for(auto h:{hand::left,hand::right})
			{
				carry::inventory inventory;const carry::owned_weapon items[]{{137,carry::profile_for(name)}};
				check(inventory.reconcile(items) && inventory.equip_definition(137,h),"knife can be held in either hand");
				const auto id=inventory.find_definition(137)->id;int drops{};
				check(inventory.release(id,1u<<unsigned(h),carry::location::right_waist,true,[&](const auto&){++drops;return true;}).action==carry::outcome::stowed && !drops,"knife can be stowed without world transfer");
				check(inventory.draw(carry::location::right_waist,h).action==carry::outcome::drawn,"knife can be drawn again");
				check(inventory.release(id,1u<<unsigned(h),carry::location::absent,false,[&](const auto&){++drops;return true;}).action==carry::outcome::rejected && !drops,"occluded knife drop retains ownership");
				check(inventory.release(id,1u<<unsigned(h),carry::location::absent,true,[&](const auto&){++drops;return true;}).action==carry::outcome::dropped && drops==1 && inventory.invariant(),"clear knife drop transfers its native identity once");
			}
		}
		for(const std::string_view name:{"ending_knife_extra","alt_ending_knife_extra","h2_cheatcommandoknife_extra","usp",""})
			check(!special_melee::supported(name) && special_melee::uses_ammunition(1,name) && native_fire_delivery(1,name)==fire_delivery::bullets,"near names cannot inherit knife admission or remove real firearm ammo");
		check(melee::scaled_damage(200,melee::tool::knife)==200 && melee::scaled_damage(200,melee::tool::firearm)==67,"special blade uses full native knife damage instead of firearm blunt damage");
	}
}
