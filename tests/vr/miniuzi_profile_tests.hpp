#pragma once
#include "component/vr/gameplay/weapons/miniuzi/profile.hpp"
#include "component/vr/gameplay/weapons/tmp/profile.hpp"
#include "component/vr/gameplay/weapon_profiles.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include "component/vr/gameplay/weapon_mechanics_profiles.hpp"
#include "component/vr/gameplay/physical_reload_rig.hpp"
#include "component/vr/gameplay/rigid_part_visibility.hpp"
#include <iostream>

namespace miniuzi_profile_tests
{
	inline int run()
	{
		using namespace vr::gameplay::weapons;
		using namespace vr::gameplay::hands;
		int failed{}; const auto check=[&](bool ok,const char* why) { if (!ok) { ++failed; std::cerr<<"FAIL: "<<why<<'\n'; } };
		const auto& grip=miniuzi::base; const auto& p=miniuzi::physical;
		check(native_reload_profile("uzi",32)==&p && !native_chamber_profile("uzi",32),
			"exact Mini Uzi candidate opts into open-bolt physical feed and never native chamber-plus-one patching");
		check(!native_reload_profile("uzi",33) && !native_reload_profile("uzi",30),"unverified native capacities reject Mini Uzi");
		for (auto name:{"miniuzi","mini_uzi","uzi_akimbo","uzi_silencer","uzi_xmags"})
			check(!native_reload_profile(name,32),"unreviewed Mini Uzi aliases and variants cannot inherit physical authority");
		// README.md / stock_support.hpp: headset feedback moved the contact to
		// (10, 3, -5) cm and yawed the MP9 donor five degrees clockwise about +Z.
		check(grip.aiming==aim_rule::two_hand && grip.authored_rear==1 && grip.fixed_support_position &&
			length(sub(scale(grip.wrists[0].position,2.54f),vec{10,3,-5}))<.0001f,
			"Mini Uzi uses the authored stock contact shared by both hands");
		const float half_yaw=-5.f*3.14159265359f/360.f;
		const auto fitted_rotation=multiply(quat{0,0,std::sin(half_yaw),std::cos(half_yaw)},tmp::wrists[0].rotation);
		for (const auto axis:{vec{1,0,0},vec{0,1,0},vec{0,0,1}})
			check(length(sub(rotate(grip.wrists[0].rotation,axis),rotate(fitted_rotation,axis)))<.00001f,
				"Mini Uzi support applies the authored clockwise yaw in gun space");
		check(grip.variant=="folded_stock" && grip.wrists[1].position==miniuzi::wrists[1].position &&
			grip.wrists[1].rotation==miniuzi::wrists[1].rotation &&
			free_hand_rotation(grip,0)==miniuzi::wrists[0].rotation &&
			free_hand_rotation(grip,1)==miniuzi::wrists[1].rotation,
			"Mini Uzi support fitting preserves rear and free/reload wrist bases");
		for (const auto& joint:tmp::idle_fingers) if (joint.name.find("_le_")!=std::string_view::npos)
		{
			bool found=false; for (const auto& fitted:grip.fingers) if (fitted.name==joint.name) found=fitted.rotation==joint.rotation;
			check(found,"Mini Uzi stock grip retains MP9 left finger curls");
		}
		const auto span=sub(grip.wrists[0].position,grip.wrists[1].position);
		for (const auto direction:{vec{0,1,0},vec{0,0,1},vec{-1,0,0}})
		{
			const auto target=scale(direction,length(span)); const auto q=aimed_rotation(grip.aiming,{0,0,0,1},{},target,span);
			check(length(sub(rotate(q,span),target))<.001f,"Mini Uzi two-hand baseline remains valid in extreme aim directions");
		}
		constexpr std::array<std::string_view,21> names{"j_gun","j_bolt","j_mag_release","j_open_reload",
			"j_support_chin_arm","j_switch_auto_manual","j_trigger","tag_acog_2","tag_brass","tag_clip",
			"tag_eotech","tag_flash","tag_iron_sight","tag_rail","tag_red_dot","tag_sight_off","tag_sight_on",
			"tag_silencer","tag_thermal_scope","tag_bullet","tag_front_sight_on"};
		for (auto glove:{"viewhands_us_army","viewhands_arctic"})
		{
			rig r{}; r.count=89; r.gun=68; r.parent.fill(-1); r.parent[68]=13;
			std::array<bone_definition,89> bones{};
			for (int i=68;i<89;++i)
			{
				bones[i].name=names[i-68]; bones[i].bind.rotation={0,0,0,1}; r.weapon_bones[i]=true;
				if (i>68) r.parent[i]=68;
			}
			r.parent[87]=77; r.parent[88]=69;
			for (int i=68;i<89;++i) bones[i].parent=r.parent[i];
			std::array<model_definition,2> models{{{glove,0,68},{grip.receiver,68,21}}};
			const auto parts=physical_reload::bind_parts(r,bones,p);
			check(parts.valid && parts.magazine==77 && parts.slide==69 && parts.bolt==71 && parts.bullets==87 &&
				select_profile(models,r,bones).value==&grip,"exported Mini Uzi receiver separates top handle, internal bolt and magazine-child round");
			part_mask hidden=parts.bullet_mask; hidden[77/32]|=0x80000000u>>(77%32);
			// Exact exported metal surface: neither the bolt nor handle/selector/
			// sight groups may disappear when hiding its magazine body and round.
			const std::array<rigid_group_range,8> groups{{{1*64,699,0,738},{2*64,238,738,361},
				{3*64,259,1099,314},{5*64,58,1413,48},{6*64,100,1461,90},
				{9*64,726,1551,854},{16*64,1586,2405,1773},{19*64,338,4178,416}}};
			const auto empty=plan_rigid_visibility(groups,68,4004,4594,parts.bullet_mask);
			const auto detached=plan_rigid_visibility(groups,68,4004,4594,hidden);
			check(empty.valid && empty.hidden_groups==128 && detached.valid && detached.hidden_groups==160,
				"Mini Uzi shared metal surface hides only magazine body/round and retains all six other groups");
			auto wrong=p; wrong.bolt=nullptr;
			check(!physical_reload::bind_parts(r,bones,wrong).valid,"open-bolt authority requires an independently bound internal bolt");
			auto follower=*p.bolt; wrong=p; wrong.bolt=&follower; follower.bone=p.slide_bone;
			check(!physical_reload::bind_parts(r,bones,wrong).valid,"handle cannot be substituted for the Mini Uzi internal bolt");
			follower=*p.bolt; follower.locked_m=0;
			check(!physical_reload::bind_parts(r,bones,wrong).valid,"open bolt needs a nonzero retained sear position");
			auto bad=r; bad.parent[71]=77;
			check(!physical_reload::bind_parts(bad,bones,p).valid,"unreviewed internal bolt parenting rejects before posing");
			auto missing=bones; missing[71].name="missing_bolt";
			check(!physical_reload::bind_parts(r,missing,p).valid,"missing internal bolt cannot admit only the handle");
			missing=bones; missing[87].name="missing_round";
			check(!physical_reload::bind_parts(r,missing,p).valid,"missing magazine round cannot bypass the exact subset contract");
			models[0].name="unreviewed_weapon_attachment"; r.weapon_bones[0]=true;
			check(!select_profile(models,r,bones).value,"unreviewed Mini Uzi attachments do not borrow the base support position");
		}
		check(p.rigid_magazine_source==grip.receiver && !p.magazine_model && p.additional_bullet_bones.empty(),
			"Mini Uzi magazine uses exact receiver subsets with one independently visible round");
		const auto& bolt=miniuzi::internal_bolt;
		check(valid_bolt(bolt,p.interaction.slide_stroke) && !physical_reload::native_action_recoil(p.interaction),
			"Mini Uzi bolt curve is bounded and its handle never follows native shot recoil");
		const auto idle=add(bolt.rest.position,scale(p.interaction.slide_axis,bolt.locked_m*39.37007874f));
		check(length(sub(idle,miniuzi::equip_rest[2].local.position))<.0001f,
			"retained bolt from closed bind matches native open idle instead of applying the open offset twice");
		check(bolt_travel(bolt,0,false)==0 && bolt_travel(bolt,0,true)==bolt.locked_m &&
			std::abs(bolt_travel(bolt,.08463376f,false)-.05251632f)<1e-7f,"sear stop is independent of the longer top-handle travel");
		float previous{};
		check(displayed_internal_bolt(p,0,0,true,0)==0 &&
			std::abs(displayed_internal_bolt(p,0,.085f,true,.03f)-bolt.locked_m*.5f)<1e-7f &&
			displayed_internal_bolt(p,0,0,true,.06f)==bolt.locked_m,
			"Mini Uzi leaves the sear at discharge and returns; fake handle recoil cannot pin its bolt open");
		for(float age:{0.f,.01f,.03f,.06f,1.f})
		{
			check(displayed_internal_bolt(p,0,.085f,false,age)==0,"last open-bolt shot stays forward without inventing a retained cycle");
			check(displayed_internal_bolt(p,.085f,0,true,age)>.0525f,"held rear handle takes priority over the open-bolt cosmetic return");
		}
		check(displayed_internal_bolt(p,0,0,true,-1)==bolt.locked_m &&
			displayed_internal_bolt(p,0,0,true,std::numeric_limits<float>::quiet_NaN())==bolt.locked_m,
			"inactive or invalid shot age retains the authoritative open rest");
		for(float hz:{45.f,72.f,90.f,120.f,144.f})for(float phase:{0.f,.35f,.75f})
		{
			float low=bolt.locked_m,high=0;
			for(int frame=0;frame<20;++frame)
			{
				const auto travel=displayed_internal_bolt(p,0,.085f,true,(frame+phase)/hz);
				low=std::min(low,travel);high=std::max(high,travel);
			}
			check(high-low>.035f,"open-bolt cycle remains visible across refresh rates and render phase offsets");
		}
		for (int i=0;i<=1000;++i)
		{
			const auto value=bolt_travel(bolt,i*.0001f,false);
			check(std::isfinite(value) && value>=previous && value<=.05251633f,"Mini Uzi manual bolt travel clamps monotonically under overpull"); previous=value;
		}
		check(p.well.position[2]*2.54f>-5.94f && p.well.position[2]*2.54f<-5.6f,
			"Mini Uzi pistol well is at the grip mouth above the magazine baseplate");
		check(miniuzi::suppress_equip("h2_wpn_smg_miniuzi_pullout_first") &&
			!miniuzi::suppress_equip("h2_wpn_smg_miniuzi_fire") && !miniuzi::suppress_equip("h2_wpn_smg_miniuzi_lastfire") &&
			!miniuzi::suppress_equip("h2_wpn_smg_miniuzi_reload_empty"),"Mini Uzi equip suppression does not classify native firing or reload clips");
		check(p.sound_key && !p.sound_key(mechanics::effect::shot) && !p.sound_key(mechanics::effect::action_rear),
			"Mini Uzi chamber clip plays once per full stroke and native firing audio stays native");
		const auto dry=p.interaction_sound(mechanics::effect::dry_fire);
		check(dry.kind==sound_reference_kind::alias && dry.name && std::string_view(dry.name)=="wpn_dryfire_smg_plr" &&
			p.interaction_sound(mechanics::effect::action_close).kind==sound_reference_kind::notetrack &&
			!p.interaction_sound(mechanics::effect::shot).name,
			"confirmed dry closure resolves its native empty-fire alias while reload keeps notetrack routing");
		return failed;
	}
}
