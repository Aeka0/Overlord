#pragma once
#include "component/vr/gameplay/weapon_pose_library.hpp"
#include "component/vr/gameplay/weapons/fal/profile.hpp"
#include "component/vr/gameplay/weapons/famas/profile.hpp"
#include "component/vr/gameplay/weapons/l86/profile.hpp"
#include "component/vr/gameplay/weapon_profiles.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include "component/vr/gameplay/physical_reload_rig.hpp"
#include <iostream>
#include <vector>

namespace bullpup_profile_tests
{
	inline int run()
	{
		using namespace vr::gameplay::weapons;using namespace vr::gameplay::hands;namespace p=physical_reload;
		int failed{};const auto check=[&](bool ok,const char* why){if(!ok){++failed;std::cerr<<"FAIL: "<<why<<'\n';}};
		struct fixture
		{
			rig r{};std::array<bone_definition,256> bones{};std::vector<model_definition> models;const profile& definition;
			fixture(const profile& d,std::span<const assembly_attachment* const> items,std::string_view glove="viewhands_us_army"):definition(d)
			{
				const bool lmg=d.id=="l86";r.parent.fill(-1);r.gun=68;r.count=lmg?84:88;r.parent[68]=13;
				models={{glove,0,68},{d.receiver,68,r.count-68}};
				constexpr std::string_view l86_names[]{"j_gun","j_bipods","j_reload","tag_acog_2","tag_brass","tag_clip","tag_eotech","tag_flash",
					"tag_foregrip","tag_heartbeat","tag_iron_sight","tag_red_dot","tag_sa80_scope","tag_silencer","tag_thermal_scope","j_bullets"};
				constexpr std::string_view famas_names[]{"j_gun","j_bolt","j_reload_trigger","j_trigger","tag_acog_2","tag_brass","tag_clip","tag_eotech","tag_flash",
					"tag_heartbeat","tag_laser","tag_m203","tag_red_dot","tag_shotgun","tag_sight_off","tag_sight_on","tag_silencer","tag_tape","tag_thermal_scope","j_bullet"};
				for(int i=68;i<r.count;++i){bones[i].name=lmg?l86_names[i-68]:famas_names[i-68];r.weapon_bones[i]=true;if(i>68)r.parent[i]=68;}
				r.parent[r.count-1]=lmg?73:74;r.arms[0].wrist=0;r.arms[1].wrist=1;
				for(size_t i=0;i<d.fingers.size();++i){bones[2+i].name=d.fingers[i].name;r.parent[2+i]=0;}
				const int receiver_end=r.count;
				for(const auto* item:items)
				{
					int parent=-1;for(int i=68;i<receiver_end;++i)if(bones[i].name==item->contract.receiver_parent)parent=i;
					const int start=r.count;models.push_back({item->contract.model,start,item->bones});
					for(int n=0;n<item->bones;++n){r.weapon_bones[start+n]=true;r.parent[start+n]=n?start:parent;}
					bones[start].name=item->contract.root;
					if(!item->contract.muzzle.empty()){bones[start+1].name=item->contract.muzzle;bones[start+1].bind.position={6,0,0};}
					r.count+=item->bones;
				}
				for(int i=0;i<r.count;++i){bones[i].parent=r.parent[i];bones[i].bind.rotation={0,0,0,1};}
			}
			profile_match resolve() const{return select_profile(models,r,{bones.data(),size_t(r.count)});}
			p::part_rig parts() const{return p::bind_parts(r,{bones.data(),size_t(r.count)},*definition.reload);}
		};
		unsigned combinations{};
		for(const auto* definition:{&l86::base,&famas::assemblies[0],&famas::assemblies[1]})
		{
			const bool lmg=definition==&l86::base;const auto& d=*definition->reload;const int capacity=lmg?100:30;
			for(bool silencer:{false,true})for(bool sensor:{false,true})for(int optic=-1;optic<13;++optic)
			for(bool reverse:{false,true})for(auto glove:{"viewhands_us_army","viewhands_arctic"})
			{
				std::vector<const assembly_attachment*> items;
				if(silencer)items.push_back(&rifle_attachments::common[0]);if(sensor)items.push_back(&rifle_attachments::common[14]);
				if(optic>=0)items.push_back(&rifle_attachments::common[1+optic]);if(reverse)std::reverse(items.begin(),items.end());
				fixture f(*definition,items,glove);const auto match=f.resolve();const auto parts=f.parts();
				check(match.value==definition && (match.muzzle>=0)==silencer,"bullpup optics/sensor/suppressor keep receiver-specific reload and muzzle");
				check(parts.valid && parts.magazine==(lmg?73:74) && parts.bullets==(lmg?83:87) && parts.slide==(lmg?70:69),"bullpup action and magazine roots bind independently of sight and bipod bones");
				check(bind_weapon_poses(f.r,{f.bones.data(),size_t(f.r.count)},*definition).valid,"bullpup complete left/right hand and equip poses bind");
				++combinations;
			}
			fixture bare(*definition,{});
			const assembly_attachment* laser[]{&rifle_attachments::common[15]};check(bool(fixture(*definition,laser).resolve().value)==!lmg,"only FAMAS provides the native laser mount");
			const assembly_attachment* duplicate[]{&rifle_attachments::common[5],&rifle_attachments::common[7]};check(!fixture(*definition,duplicate).resolve().value,"bullpup rejects two optics");
			const assembly_attachment* unreviewed[]{&fal::attachments[0]};check(!fixture(*definition,unreviewed).resolve().value,"bullpup does not infer an underbarrel grip from a bare mount tag");
			const assembly_attachment* silenced[]{&rifle_attachments::common[0]};fixture malformed(*definition,silenced);malformed.r.parent[lmg?84:88]=0;
			check(!malformed.resolve().value,"bullpup attachment cannot be parented to a glove");
			check(!select_profile(bare.models,bare.r).value,"bullpup identity without complete bones cannot authorize interaction");
			const auto parts=bare.parts();
			for(int index:{parts.magazine,parts.slide,parts.bullets}){auto bad=bare;bad.r.parent[index]=0;check(!bad.parts().valid,"bullpup malformed physical part parenting is rejected");}
			check(definition->aiming==aim_rule::two_hand && d.ammunition.feed==mechanics::feed_type::closed_bolt && d.ammunition.plus_one &&
				d.ammunition.last_round_lock && d.ammunition.release_control && d.ammunition.release==mechanics::magazine_release::physical_pull,
				"bullpup policy has chamber, follower lock, rapid release and physical-only magazine removal");
			check(p::valid(d.interaction) && d.interaction.manual_magazine->spare_strike==!lmg && !d.interaction.manual_catch && !d.handle_catch,
				"FAMAS enables its directed magazine latch while L86 keeps pull-only removal and neither inherits HK handle latching");
			check(p::native_action_recoil(d.interaction) && d.authored_action_fire==lmg,
				"L86 whole carrier uses authored recoil even without a moving native clip; FAMAS keeps its native clip");
			if(lmg)
			{
				mechanics::state state{};state.action=mechanics::action_state::locked_open;
				check(p::minimum_slide_travel(d.interaction,d.ammunition,state)==.078f,
					"L86 shared handle and bolt retain the authored follower stop after the last shot");
				state.action=mechanics::action_state::closed;
				check(p::minimum_slide_travel(d.interaction,d.ammunition,state)==0 &&
					d.interaction.slide_stroke*action_shot_fraction(.02f)>.08f,
					"L86 closed carrier cycles visibly and returns to its original rest");
			}
			for(const auto& grip:d.slide_grips)
				check(choose_part_grip(d.slide_grips,grip.wrist,{},d.slide_grab_low,d.slide_grab_high,39.37007874f).distance_meters<.001f,
					"bullpup action grasp contacts the actual handle instead of wrist origin");
			for(auto gun:{anchor{{},{0,0,0,1}},anchor{{20,5,-4},normalize({.2f,-.3f,.1f,.8f})}})
			{
				const auto mag=compose_reload(gun,d.magazine_rest);const auto exit=p::translate_local(mag,magazine_exit_translation(d,39.37007874f));
				const auto tip=magazine_tip_in_well(d,gun,exit,39.37007874f);
				check(magazine_alignment(d,gun,mag)>.999f && tip[2]<-.0099f && std::abs(tip[0])<.001f && std::abs(tip[1])<.001f,"bullpup released magazine lip clears the rear receiver");
				const auto wrist=compose_reload(mag,inverse_reload(d.magazine_in_wrist));
				check(magazine_contacts(d,gun,wrist,mag,39.37007874f).grip_distance<.001f,"bullpup authored wrist reaches physical magazine grab volume");
			}
			for(const auto suffix:{"","_reflex","_silencer"})
				check(native_reload_profile(std::string(d.native_name)+suffix,capacity,&d)==&d,"bullpup native family retains exact scene appearance");
			for(const auto name:{std::string(d.native_name)+"_",std::string(d.native_name)+"0",std::string(d.native_name)+"/bad",std::string(10000,'x')})
				check(!d.native_family(name),"bullpup malformed and unbounded native names are rejected");
			check(!native_reload_profile(d.native_name,capacity+1,&d) && !native_reload_profile(lmg?"famas":"sa80",capacity,&d),"bullpup native mismatch cannot acquire ammunition authority");
			check(d.sound_key(mechanics::effect::magazine_take) && d.sound_key(mechanics::effect::action_close) && !d.sound_key(mechanics::effect::shot),"bullpup own removal/chamber keys retain native shot audio");
		}
		check(l86::suppress_equip("h2_wpn_lmg_sa80_first_time_pullout") && famas::suppress_equip("h2_wpn_asl_famas_pullout_first") &&
			!famas::suppress_equip("h2_wpn_asl_famas_reload_empty") && !l86::suppress_equip("h2_wpn_lmg_sa80_fire"),"bullpup right-hand equip suppression is bounded to equip clips");
		std::cout<<"Bullpup assembly combinations checked: "<<combinations<<'\n';return failed;
	}
}
