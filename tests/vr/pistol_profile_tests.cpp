#include "component/vr/gameplay/weapon_pose_library.hpp"
#include "component/vr/hand.hpp"
using vr::hand;
#include "component/vr/gameplay/weapon_profiles.hpp"
#include "component/vr/gameplay/weapons/de50/profile.hpp"
#include "component/vr/gameplay/weapons/designator/profile.hpp"
#include "component/vr/gameplay/weapons/g18/profile.hpp"
#include "component/vr/gameplay/weapons/m1911/profile.hpp"
#include "component/vr/gameplay/weapons/m9/profile.hpp"
#include "component/vr/gameplay/weapons/m93r/profile.hpp"
#include "component/vr/gameplay/weapons/miniuzi/profile.hpp"
#include "component/vr/gameplay/weapons/tmp/profile.hpp"
#include "component/vr/gameplay/weapons/usp/profile.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include "component/vr/gameplay/physical_reload_rig.hpp"
#include "component/vr/gameplay/hand_pose_library.hpp"
#include "component/vr/gameplay/rigid_part_visibility.hpp"
#include <iostream>
#include <limits>
#include "g18_profile_tests.hpp"
#include "foregrip_pistol_tests.hpp"
#include "miniuzi_profile_tests.hpp"

int main()
{
	using namespace vr::gameplay::weapons;
	using namespace vr::gameplay::hands;
	int failures=g18_profile_tests::run()+foregrip_pistol_tests::run()+miniuzi_profile_tests::run();
	const auto check = [&](bool v, const char* why) { if (!v) { ++failures; std::cerr << "FAIL: " << why << '\n'; } };
	const auto close = [](vec a, vec b) { return length(sub(a,b)) < .001f; };
	const float units=39.37007874f;
	{
		rig device{};device.count=5;device.gun=0;device.weapon_bones[0]=true;
		const std::array<model_definition,1> models{{{"h2_viewmodel_laser_designator_base",0,5}}};
		check(select_profile(models,device).value==&designator::base && !designator::base.reload,"native designator receiver selects its own grip without pistol magazine mechanics");
		check(designator::base.fingers.data()==usp::idle_fingers && designator::base.support_enabled && designator::base.authored_rear==1,
			"designator reuses USP complete hand pose and ordinary mirrored support adapter");
	}
	for (const auto* grip : {&m9::base,&m1911::base,&de50::base,&usp::base,&g18::base,&m93r::base,&tmp::base,&miniuzi::base})
	{
		const auto& p=*grip->reload;
		check(p.id==grip->id && native_reload_profile(p.native_name,p.ammunition.magazine_capacity)==&p,"grip/native/mechanical identities agree");
		check(reload_profile_index(&p)<reload_profiles.size(),"asset registry contains each profile once");
		check(grip->fingers.size()==(grip==&miniuzi::base ? 33 : 30) && p.magazine_fingers.size()==15,"complete live-glove finger sets and optional support palms");
		const bool foregrip=grip==&tmp::base || grip==&miniuzi::base;
		check(grip->aiming==(foregrip ? aim_rule::two_hand : aim_rule::rear_hand) && grip->authored_rear==1,
			"TMP/Mini Uzi steer with both hands; M93R and ordinary pistols retain rear-hand aim");
		check(p.interaction.well_radius==.035f && p.interaction.well_capture_below==.06f &&
			p.interaction.well_contact_depth==.09f && std::abs(p.interaction.insertion_cosine+.08715574f)<1e-7f,
			"all pistol-well weapons share six-cm lower/nine-cm upper capture and 95-degree tolerance");
		const auto inside=[&](vec tip) {
			return physical_reload::sweep_well(tip,tip,p.interaction.well_radius,
				p.interaction.well_contact_depth,p.interaction.well_capture_below);
		};
		check(inside({0,0,-.06f}) && inside({0,0,.09f}) && !inside({0,0,-.061f}) &&
			!inside({0,0,.091f}) && inside({.034f,0,0}) && !inside({.036f,0,0}),
			"pistol capture extends only upper depth and retains lower/radial boundaries");
		rig r{}; r.count=64; r.gun=1; r.weapon_tag=0; r.arms[0].wrist=2; r.arms[1].wrist=3;
		r.parent.fill(-1); r.parent[1]=0; r.weapon_bones[1]=true;
		std::array<bone_definition,64> bones{};
		bones[1].name="j_gun";
		for (size_t i=0;i<grip->fingers.size();++i)
		{
			const int j=4+static_cast<int>(i);
			const auto& joint=grip->fingers[i]; bones[j].name=joint.name;
			r.parent[j]=joint.name.find("_le")!=std::string_view::npos ? 2 : 3;
			bones[j].bind.position={1,2,3};
			float norm{}; for(float x:joint.rotation) norm+=x*x;
			check(std::isfinite(norm) && std::abs(norm-1)<.001f,"finger quaternion normalized");
		}
		const int part_start=4+static_cast<int>(grip->fingers.size());
		int magazine=-1;
		for (size_t i=0;i<grip->equip_rest.size();++i)
		{
			const int j=part_start+static_cast<int>(i); bones[j].name=grip->equip_rest[i].name;
			r.weapon_bones[j]=true; r.parent[j]=r.gun;
			if (bones[j].name==p.magazine_bone) magazine=j;
		}
		const int round_root=part_start+8; // Room for all seven Mini Uzi equip parts.
		bones[round_root].name=p.bullets_bone; r.weapon_bones[round_root]=true; r.parent[round_root]=p.receiver_parented_bullets ? r.gun : magazine;
		for (size_t i=0;i<p.additional_bullet_bones.size();++i)
		{ bones[round_root+1+i].name=p.additional_bullet_bones[i]; r.weapon_bones[round_root+1+i]=true; r.parent[round_root+1+i]=magazine; }
		// This fixture supplies round binding separately, even when equip-rest
		// also contains the receiver-parented round's authored rest pose.
		for (size_t i=0;i<grip->equip_rest.size();++i) if (bones[part_start+i].name==p.bullets_bone)
			bones[part_start+i].name={};
		for (int i=0;i<r.count;++i) { bones[i].bind.rotation={0,0,0,1}; bones[i].parent=r.parent[i]; }
		check(bind_weapon_poses(r,bones,*grip).valid,"all authored fingers and equip parts bind");
		check(physical_reload::bind_parts(r,bones,p).valid,"expected three mechanical roots bind");
		auto malformed=r; malformed.parent[round_root]=p.receiver_parented_bullets ? magazine : 1;
		check(!physical_reload::bind_parts(malformed,bones,p).valid,"wrong bullet subtree fails closed");
		malformed=r; malformed.count=257;
		check(!physical_reload::bind_parts(malformed,bones,p).valid,"oversized rig rejected before indexing");
		std::array<model_definition,1> models{{{grip->receiver,0,r.count}}};
		if(grip==&tmp::base)
			check(!select_profile(models,r).value,"TMP requires its complete receiver and attachment topology; covered by foregrip fixture");
		else check(select_profile(models,r).value==grip,"exact base receiver selected");
		models[0].name="unreviewed_receiver";
		check(!select_profile(models,r).value,"unknown assembly never guessed by clip capacity");
		const auto validate_geometry = [&](anchor gun) {
			const auto well=compose_reload(gun,p.well);
			auto magazine=compose_reload(gun,p.magazine_rest);
			const vec expected{.005f,-.004f,-.10f};
			const auto top=add(well.position,rotate(well.rotation,scale(expected,units)));
			magazine.position=sub(top,rotate(magazine.rotation,p.magazine_top));
			check(close(magazine_tip_in_well(p,gun,magazine,units),expected),"well-space contact survives translation and rotation");
			check(magazine_alignment(p,gun,magazine)>.9999f,"native magazine tilt does not penalize aligned contact");
			for (float degrees : {75.f,86.f,90.f,94.f,96.f,110.f,180.f})
			{
				const float radians=degrees*3.14159265359f/180.f;
				auto tilted=compose_reload(gun,p.magazine_rest);
				tilted.rotation=normalize(multiply(tilted.rotation,{std::sin(radians/2),0,0,std::cos(radians/2)}));
				const vec grip_tip{.005f,-.004f,.04f};
				const auto top_inside=add(well.position,rotate(well.rotation,scale(grip_tip,units)));
				tilted.position=sub(top_inside,rotate(tilted.rotation,p.magazine_top));
				const auto angle=magazine_alignment(p,gun,tilted);
				check(close(magazine_tip_in_well(p,gun,tilted,units),grip_tip) &&
					std::abs(angle-std::cos(radians))<.0001f &&
					(angle>=p.interaction.insertion_cosine)==(degrees<95.f),
					"steep real magazine transform uses the same rail and threshold after gun rotation");
			}
			auto seated=compose_reload(gun,p.magazine_rest);
			const auto exit=physical_reload::translate_local(seated,magazine_exit_translation(p,units));
			const auto exited=magazine_tip_in_well(p,gun,exit,units);
			check(exited[2]<=-.0099f && std::abs(exited[0])<.001f && std::abs(exited[1])<.001f,"whole magazine clears the mouth before free drop");
		};
		validate_geometry({{0,0,0},{0,0,0,1}});
		validate_geometry({{210,-100,35},normalize({.2f,-.6f,.3f,.7f})});
		for (const auto& pose:p.slide_grips)
		{
			const auto finger_count=std::count_if(pose.fingers.begin(),pose.fingers.end(),[](const auto& joint){
				for(const auto prefix:{"j_index_","j_mid_","j_ring_","j_pinky_","j_thumb_"})if(joint.name.starts_with(prefix))return true;return false;});
			check(finger_count==15,"slide grip contains all 15 finger joints, allowing authored palm bones");
			const auto chosen=choose_part_grip(p.slide_grips,pose.wrist,{},p.slide_grab_low,p.slide_grab_high,units);
			check(chosen.distance_meters<.001f,"authored slide wrist and contact agree");
			const auto travel=scale(p.interaction.slide_axis,p.interaction.locked_travel*units);
			auto locked=pose.wrist; locked.position=add(locked.position,travel);
			const auto locked_grip=choose_part_grip(p.slide_grips,locked,travel,p.slide_grab_low,p.slide_grab_high,units);
			check(locked_grip.pose==chosen.pose && locked_grip.distance_meters<.001f,
				"calibrated rear grasp acquires the locked slide in the same moving part frame");
		}
		check(p.sound_key && !p.sound_key(mechanics::effect::shot),"native firing audio never duplicated");
	}
	{
		// Synthetic native duplicate-root assembly: one H2 receiver attachment,
		// one separate knife model. No skin or ammo state is needed for binding.
		rig r{}; r.count=8; r.gun=2; r.parent.fill(-1);
		r.parent[2]=0; r.parent[3]=2; r.parent[4]=2; r.parent[5]=4; r.parent[6]=5; r.parent[7]=2;
		for (int i=2;i<8;++i) r.weapon_bones[i]=true;
		std::array<bone_definition,8> bones{};
		bones[2].name="j_gun"; bones[4].name=bones[5].name="tag_knife"; bones[6].name="tag_knife_fx";
		std::array<model_definition,4> models{{{"viewhands_us_army",0,2},
			{usp::base.receiver,2,3},{"h2_viewmodel_knife",5,2},{"unreviewed_silencer",7,1}}};
		check(!select_profile(models,r,bones).value,"USP unknown weapon attachment fails closed");
		r.count=7;
		const auto assemble=[&] { return select_profile({models.data(),3},r,{bones.data(),7}); };
		for (const auto& policy:usp::hidden_props)
		{
			models[2].name=policy.model;
			const auto match=assemble();
			check(match.value==&usp::base && match.hidden[0]==((0x80000000u>>4)|(0x80000000u>>5)|(0x80000000u>>6)),
				"USP hides only reviewed prop/alias, never gun, magazine or hands");
			check(std::all_of(match.hidden.begin()+1,match.hidden.end(),[](auto x){ return x==0; }),"no unrelated mask bits");
		}
		check(!select_profile({models.data(),3},r).value,"knife assembly requires actual bone contract, not name alone");
		r.parent[5]=0;
		check(!assemble().value,"knife attached to hand/origin cannot use USP gun prop policy");
		r.parent[5]=4; r.parent[6]=256;
		check(!assemble().value,"corrupt attachment parent rejected before descendant traversal");
		r.parent[6]=5; bones[5].name="tag_other";
		check(!assemble().value,"wrong attachment root rejected");
		bones[5].name="tag_knife"; models[2].name="viewmodel_commando_knife";
		check(!assemble().value,"unaudited knife variant not guessed");
		models[2].name="h2_viewmodel_knife"; models[1].name=m9::base.receiver;
		check(!assemble().value,"USP prop exception never applies to M9");
		models[1].name=usp::base.receiver; models[2].count=257;
		check(!assemble().value,"oversized model rejected before bone indexing");
		r.count=5;
		check(select_profile({models.data(),2},r,{bones.data(),5}).value==&usp::base,
			"USP receiver without a knife needs no synthetic attachment");
		check(usp::suppress_equip("h2_wpn_pst_usp_tactical_pullout1") &&
			usp::suppress_equip("h2_wpn_pst_usp_tactical_pullout2") &&
			usp::suppress_equip("h2_wpn_pst_usp_tactical_pullout_first_alt") &&
			!usp::suppress_equip("h2_wpn_pst_usp_tactical_melee") &&
			!usp::suppress_equip("h2_wpn_pst_usp_tactical_reload") &&
			!usp::suppress_equip("viewmodel_akimbo_usp_pullout"),"USP exact equip variants leave reload/melee/akimbo alone");
		check(usp::base.viewmodel.visibility==part_visibility::rigid_groups,"USP rounds cannot cull shared magazine surface");
		check(std::string_view(usp::sound_key(mechanics::effect::action_close))=="weap_m9_chamber_plr",
			"H2 USP actual close notetrack is preserved, not guessed from family");
	}
	{
		// Actual native USP layout from a read-only desktop capture, 2026-09-08.
		// Only relevant roots are needed here; the glove pose binding is above.
		rig r{}; r.count=82; r.gun=68; r.parent.fill(-1); r.parent[68]=13;
		std::array<bone_definition,82> bones{};
		const std::array<std::string_view,12> names{"j_gun","j_bolt","j_press_rear","j_slide_release",
			"j_trigger","tag_brass","tag_clip","tag_flash","tag_knife","tag_laser","tag_silencer","tag_bullets"};
		for (int i=68;i<80;++i) { bones[i].name=names[i-68]; r.weapon_bones[i]=true; if (i>68) r.parent[i]=68; }
		r.parent[79]=74; r.parent[80]=76; r.parent[81]=80;
		r.weapon_bones[80]=r.weapon_bones[81]=true;
		bones[80].name="tag_knife"; bones[81].name="tag_knife_fx";
		const std::array<model_definition,3> models{{{"viewhands_us_army",0,68},
			{usp::base.receiver,68,12},{"h2_viewmodel_knife",80,2}}};
		const auto match=select_profile(models,r,bones);
		check(match.value==&usp::base && physical_reload::bind_parts(r,bones,usp::physical).valid,
			"captured USP duplicate-root assembly and mechanics bind together");
		auto mask=match.hidden;
		part_mask interaction{}; interaction[79/32]=0x80000000u>>(79%32);
		const auto merged=combine_part_masks(match.hidden,interaction);
		check(combine_part_masks(match.hidden,{})==match.hidden &&
			combine_part_masks({},interaction)==interaction &&
			combine_part_masks(merged,interaction)==merged &&
			!(match.hidden[79/32] & interaction[79/32]) &&
			(merged[79/32] & interaction[79/32]),
			"cosmetic and mechanical masks compose without mutating or requiring each other");
		const auto hide_bone=[&](unsigned i){ mask[i/32] |= 0x80000000u>>(i%32); };
		const std::array<rigid_group_range,1> knife{{{0,4608,0,6186}}};
		const std::array<rigid_group_range,2> magazine{{{6*64,445,0,543},{11*64,746,543,1153}}};
		const std::array<rigid_group_range,4> frame{{{0,1963,0,2462},{3*64,172,2462,164},
			{2*64,369,2626,439},{4*64,358,3065,455}}};
		check(plan_rigid_visibility(knife,80,4608,6186,mask).hidden_groups==1,
			"captured knife rigid group suppressed independently of reload state");
		check(plan_rigid_visibility(magazine,68,1191,1696,mask).hidden_groups==0,
			"knife suppression alone retains loaded USP magazine");
		hide_bone(79);
		check(plan_rigid_visibility(magazine,68,1191,1696,mask).hidden_groups==2,
			"empty USP magazine hides rounds but retains magazine body");
		hide_bone(74);
		check(plan_rigid_visibility(magazine,68,1191,1696,mask).hidden_groups==3 &&
			plan_rigid_visibility(frame,68,2862,3520,mask).hidden_groups==0,
			"ejected USP magazine and suppressed knife never hide frame, trigger or release");
	}
	{
		// Suppressed campaign USP captured 2026-09-08: the knife follows the
		// silencer in this DObj. Only attachment topology, never fixed indices,
		// selects the variant or determines the cosmetic mask/firing origin.
		rig r{}; r.count=84; r.gun=68; r.parent.fill(-1); r.parent[68]=13;
		std::array<bone_definition,84> bones{};
		const std::array<std::string_view,12> names{"j_gun","j_bolt","j_press_rear","j_slide_release",
			"j_trigger","tag_brass","tag_clip","tag_flash","tag_knife","tag_laser","tag_silencer","tag_bullets"};
		for (int i=68;i<84;++i)
		{
			r.weapon_bones[i]=true; bones[i].bind.rotation={0,0,0,1};
			if (i<80) bones[i].name=names[i-68];
			if (i>68) r.parent[i]=68;
		}
		r.parent[79]=74; r.parent[80]=78; r.parent[81]=80; r.parent[82]=76; r.parent[83]=82;
		bones[80].name="tag_silencer"; bones[81].name="tag_flash_silenced";
		bones[81].bind.position={13.34313965f/2.54f,0,0};
		bones[82].name="tag_knife"; bones[83].name="tag_knife_fx";
		std::array<model_definition,4> models{{{"viewhands_arctic",0,68},{usp::base.receiver,68,12},
			{"attach_h2_silencer_02_vm",80,2},{"h2_viewmodel_knife",82,2}}};
		const auto assemble=[&] { return select_profile(models,r,bones); };
		const auto match=assemble();
		check(match.value==&usp::silenced && match.muzzle==81 &&
			physical_reload::bind_parts(r,bones,usp::silenced_physical).valid,
			"captured suppressed USP assembly binds shared mechanics and actual suppressor muzzle");
		part_mask expected{};
		for (unsigned i:{76u,82u,83u}) expected[i/32]|=0x80000000u>>(i%32);
		check(match.hidden==expected,"suppressed USP hides only knife, never suppressor or receiver");
		const std::array<rigid_group_range,1> silencer{{{0,2267,0,3338}}};
		check(plan_rigid_visibility(silencer,80,2267,3338,match.hidden).hidden_groups==0,
			"captured suppressor rigid geometry stays visible");
		check(native_reload_profile("usp_silencer",12)==&usp::silenced_physical &&
			reload_profile_index(&usp::silenced_physical)<reload_profiles.size() &&
			!native_reload_profile("usp_silencer",15) && !native_reload_profile("usp_silenced",12) &&
			!native_reload_profile("usp_silencer_akimbo",12),"suppressed USP has explicit native identity, not prefix admission");
		check(usp::silenced.reload==&usp::silenced_physical && usp::base.reload==&usp::physical &&
			usp::silenced.reload!=usp::base.reload &&
			usp::silenced_physical.slide_grips.data()==usp::physical.slide_grips.data() &&
			usp::silenced_physical.magazine_fingers.data()==usp::physical.magazine_fingers.data() &&
			usp::silenced_physical.magazine_model==usp::physical.magazine_model,
			"variant identity is separate; poses and magazine geometry are shared");
		check(!select_profile(models,r).value,"visible attachment requires actual bone contract");
		r.parent[80]=76; check(!assemble().value,"suppressor cannot bind to knife alias"); r.parent[80]=78;
		bones[81].name="tag_flash"; check(!assemble().value,"unknown muzzle rejected"); bones[81].name="tag_flash_silenced";
		bones[81].bind.rotation={}; check(!assemble().value,"uninitialized muzzle basis rejected");
		bones[81].bind.rotation={0,0,1,0}; check(!assemble().value,"backward muzzle orientation rejected");
		bones[81].bind.rotation={0,0,0,1}; bones[81].bind.position[0]=-1;
		check(!assemble().value,"muzzle behind attachment rejected"); bones[81].bind.position[0]=5.2532f;
		bones[81].bind.position[1]=std::numeric_limits<float>::quiet_NaN();
		check(!assemble().value,"nonfinite visible attachment rejected"); bones[81].bind.position[1]=0;
		r.parent[81]=256; check(!assemble().value,"invalid attachment topology cannot loop or index past rig"); r.parent[81]=80;
		models[3].name=models[2].name; bones[82]=bones[80]; bones[83]=bones[81];
		r.parent[82]=78;
		check(!assemble().value,"multiple replacement muzzles rejected instead of choosing one");
	}
	check(close(magazine_exit_translation(m9::physical,units),physical_reload::well_exit_translation(
		m9::equip_rest[2].local,m9::magazine_top,m9::magazine_well,m9::magazine_exit_clearance_m*units)),"M9 reviewed exit path unchanged");
	check(m1911::suppress_equip("h2_wpn_pst_m1911_pullout_first") &&
		de50::suppress_equip("h2_wpn_pst_de50_first_time_pullout") &&
		de50::suppress_equip("viewmodel_desert_eagle_pullout"),"mixed animation families explicitly supported");
	check(!de50::suppress_equip("h2_wpn_pst_de50_akimbo_r_pullout") &&
		!de50::suppress_equip("h2_wpn_pst_de50_reload") && !m1911::suppress_equip("h2_wpn_pst_m9_pullout"),"dual wield, reload and unrelated animations untouched");
	check(reload_profile_index(nullptr)==reload_profiles.size(),"unknown profile has no asset cache slot");
	check(!native_reload_profile("de50",7) && !native_reload_profile("desert_eagle",7),"animation aliases cannot accidentally opt into ammo writes");
	check(de50::physical.rigid_in_magazine.rotation[1]<-.12f && de50::physical.magazine_rest.rotation[1]>.12f,
		"standalone de50 mesh un-tilts the bone exactly once");
	check(m1911::base.viewmodel.visibility==part_visibility::rigid_groups &&
		m9::base.viewmodel.visibility==part_visibility::rigid_groups && m9::physical.magazine_fill() &&
		de50::base.viewmodel.visibility==part_visibility::surface,
		"counted M9 magazine opts into precise group hiding while Deagle keeps its accepted visibility");
	check(std::abs(m1911::slide_wrist_rest.position[0]*2.54f-(-10.88177031f))<.0001f &&
		std::abs(usp::slide_wrist_rest.position[0]*2.54f-(-13.26409921f))<.0001f &&
		std::abs(m1911::slide_finger_front_cm-(4.84335427f+1.5f))<.0001f &&
		std::abs(usp::slide_finger_front_cm-(-1.5f+(usp::slide_source_finger_front_cm+1.5f)/3.f))<.0001f,
		"M1911 moves forward another 1.5 cm while the accepted USP grasp stays unchanged");
	check(close(add(m1911::slide_wrist_rest.position,rotate(m1911::slide_wrist_rest.rotation,m1911::slide_contact)),
		scale(vec{2.12521291f,1.19250453f,7.92911577f},1/2.54f)) &&
		close(add(usp::slide_wrist_rest.position,rotate(usp::slide_wrist_rest.rotation,usp::slide_contact)),
		scale(vec{-2.90877223f,.50351691f,6.63878822f},1/2.54f)),
		"rearward wrist calibration keeps acquisition contact on actual rear slide geometry");
	for (unsigned base=0;base<256;++base)
		for (unsigned local_bone=0;local_bone+base<256;++local_bone)
		{
			part_mask local{},mask{};
			local[local_bone/32]=0x80000000u>>(local_bone%32);
			const auto bone=local_bone+base;
			mask[bone/32]=0x80000000u>>(bone%32);
			check(surface_intersects(local,base,mask),"surface prefilter handles every cross-word bone base");
			mask={}; const auto other=(bone+1)%256;
			mask[other/32]=0x80000000u>>(other%32);
			check(!surface_intersects(local,base,mask),"unrelated rigid/skinned surface bypasses filtering");
		}
	check(!surface_intersects({},256,{}),"out-of-domain surface base rejected");
	// Live M1911 LOD0 body shares frame, hammer, trigger, magazine and rounds.
	// Bone 9 / global 77 is an isolated draw group, NOT an isolated XSurface.
	const std::array<rigid_group_range,5> body{{
		{5*64,455,0,420},{0,3120,420,3472},{9*64,266,3892,360},
		{3*64,192,4252,188},{2*64,522,4440,474}}};
	part_mask hidden{};
	const auto hide=[&](unsigned bone) { hidden[bone/32] |= 0x80000000u >> (bone%32); };
	check(plan_rigid_visibility(body,68,4555,4914,hidden).hidden_groups==0,"loaded magazine retains all rigid groups");
	hide(77);
	auto plan=plan_rigid_visibility(body,68,4555,4914,hidden);
	check(plan.valid && plan.hidden_groups==(1u<<2),"chamber-only/empty magazine hides only rounds, never frame");
	hide(73);
	plan=plan_rigid_visibility(body,68,4555,4914,hidden);
	check(plan.valid && plan.hidden_groups==((1u<<0)|(1u<<2)),"no magazine hides magazine and rounds, keeps frame/trigger/hammer");
	hidden={}; hide(24);
	check(plan_rigid_visibility(body,68,4555,4914,hidden).hidden_groups==0,"hand bits cannot alias model-local bones");
	auto bad=body; bad[2].bone_offset=9*64+1;
	check(!plan_rigid_visibility(bad,68,4555,4914,hidden).valid,"misaligned bone offset rejected");
	bad=body; bad[2].first_triangle=0;
	check(!plan_rigid_visibility(bad,68,4555,4914,hidden).valid,"overlapping triangle ranges rejected");
	check(!plan_rigid_visibility(body,250,4555,4914,hidden).valid &&
		!plan_rigid_visibility(body,68,1,4914,hidden).valid &&
		!plan_rigid_visibility(body,68,4555,1,hidden).valid &&
		!plan_rigid_visibility(body,68,4556,4914,hidden).valid &&
		!plan_rigid_visibility({},68,0,0,hidden).valid,"invalid bone/count bounds rejected");
	std::cout << "pistol profile failures=" << failures << '\n';
	return failures ? 1 : 0;
}
