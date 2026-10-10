#include "component/vr/gameplay/weapon_pose_library.hpp"
#include "component/vr/hand.hpp"
using vr::hand;
#include "component/vr/gameplay/controller_firing.hpp"
#include "component/vr/gameplay/weapons/m4/profile.hpp"
#include "component/vr/gameplay/weapons/m9/profile.hpp"
#include "component/vr/gameplay/weapons/miniuzi/profile.hpp"
#include "component/vr/gameplay/grip_presenter.hpp"
#include "component/vr/gameplay/part_hand_constraint.hpp"
#include "component/vr/gameplay/magazine_grip_selection.hpp"
#include "component/vr/gameplay/weapons/m9/slide_grips.hpp"
#include "component/vr/gameplay/weapon_profiles.hpp"
#include "component/vr/gameplay/hands/position_offset.hpp"
#include "component/vr/gameplay/weapon_actions.hpp"
#include "component/vr/gameplay/weapon_carry_pose.hpp"
#include "component/vr/gameplay/weapon_recoil.hpp"
#include "component/vr/gameplay/weapon_recoil_tuning.hpp"
#include "recoil_pose_tests.hpp"
#include "ads_comfort_tests.hpp"
#include "part_hand_transition_tests.hpp"
#include "ejection_scatter_tests.hpp"
#include <iostream>
#include <limits>
#include "m4_profile_tests.hpp"
#include "m16_profile_tests.hpp"
#include "precision_profile_tests.hpp"
#include "fal_profile_tests.hpp"
#include "scar_profile_tests.hpp"
#include "tavor_fn2000_profile_tests.hpp"
#include "aa12_profile_tests.hpp"
#include "p90_striker_profile_tests.hpp"
#include "belt_profile_tests.hpp"
#include "arm_clearance_tests.hpp"
#include "m1014_profile_tests.hpp"
#include "pump_profile_tests.hpp"
#include "estate_variant_tests.hpp"
#include "dragunov_profile_tests.hpp"
#include "pp2000_profile_tests.hpp"
#include "bullpup_profile_tests.hpp"
#include "bolt_partition_tests.hpp"
#include "ammunition_presentation_tests.hpp"
#include "reload_attachment_tests.hpp"
#include "acr_profile_tests.hpp"
#include "heartbeat_tests.hpp"
#include "vector_profile_tests.hpp"
#include "latched_handle_profile_tests.hpp"
#include "ak_profile_tests.hpp"
#include "hand_contact_tests.hpp"
#include "palm_contact_tests.hpp"
#include "secondary_motion_tests.hpp"
#include "weapon_carry_tests.hpp"
#include "component/vr/gameplay/support_retention.hpp"
#include "weapon_hud_lifetime_tests.hpp"
#include "pickup_ammunition_tests.hpp"
#include "world_interaction_tests.hpp"
#include "independent_fire_tests.hpp"
#include "scripted_control_tests.hpp"
#include "airport_opening_tests.hpp"
#include "mounted_turret_tests.hpp"
#include "native_hide_tags_tests.hpp"
#include "weapon_registry_tests.hpp"
#include "special_knife_tests.hpp"
#include "shield_tests.hpp"
#include "marine_sniper_hand_tests.hpp"
#include "trigger_discipline_tests.hpp"

int main()
{
	using namespace vr::gameplay::weapons;
	using namespace vr::gameplay::hands;
	using vr::controller_input::clock;
	int failures=dragunov_profile_tests::run()+pump_profile_tests::run()+m4_profile_tests::run()+m16_profile_tests::run()+ak_profile_tests::run()+acr_profile_tests::run()+vector_profile_tests::run()+latched_handle_profile_tests::run()+fal_profile_tests::run()+scar_profile_tests::run()+tavor_fn2000_profile_tests::run()+aa12_profile_tests::run()+m1014_profile_tests::run()+pp2000_profile_tests::run()+bullpup_profile_tests::run();
	failures+=precision_profile_tests::run();
	failures+=bolt_partition_tests::run();
	failures+=ammunition_presentation_tests::run();
	failures+=weapon_registry_tests::run();
	failures+=estate_variant_tests::run();
	const auto check = [&](bool value, const char* text) {
		if (!value)
		{
			std::cerr << "FAIL: " << text << '\n';
			++failures;
		}
	};
	const auto close = [](vec a, vec b) { return length(sub(a, b)) < 0.002f; };
	part_hand_transition_tests::run(check);
	ejection_scatter_tests::run(check);
	trigger_discipline_tests::run(check);
	reload_attachment_tests::run(check);
	{
		using namespace recoil;
		using vr::controller_input::posture;
		using vr::controller_input::native_posture;
		check(native_pitch(-30,-35,native_posture(0))==4.875f &&
			native_pitch(-30,-35,native_posture(2))==3.25f && native_pitch(-30,-35,native_posture(1))==1.625f &&
			native_pitch(-30,-35,native_posture(3))==1.625f && native_pitch(-30,-35,posture::unknown)==4.875f,
			"actual native posture selects 3x/2x/1x strength with prone precedence and safe unknown fallback");
		for (const auto mode:{penalty_mode::all,penalty_mode::long_weapons,penalty_mode::off})
			for (const bool supported:{false,true}) for (const auto name:{"m4","tmp","deserteagle"})
				check(multiplier(mode,supported,0,name,posture::prone)==1.f,
					"prone suppresses the single-hand penalty in every mode and grip state");
		for (const auto stance:{posture::stand,posture::crouch,posture::prone})
		{
			const auto rifle=native_pitch(-10,-15,stance)*weapon_scale("m4",0)*
				multiplier(penalty_mode::long_weapons,false,0,"m4",stance);
			check(rifle==(stance==posture::stand?3.75f:stance==posture::crouch?2.5f:.3125f),
				"M4 family tuning composes with posture and prone penalty suspension");
		}
		check(weapon_scale("deserteagle_gold",5)==5.f && weapon_scale("deserteagle_akimbo",5)==5.f &&
			weapon_scale("coltanaconda",5)==5.f && weapon_scale("usp_silencer",5)==.6f,
			"pistol recoil tuning covers native skin, akimbo and silencer names");
		check(weapon_scale("m4_grunt",0)==.5f && weapon_scale("m4m203_acog",0)==.5f &&
			weapon_scale("ak47_grenadier",0)==1.5f && weapon_scale("fal_shotgun",0)==2.f &&
			weapon_scale("scar_h_reflex",0)==1.2f,"rifle primary feeds retain tuning across attachments");
		check(weapon_scale("m14_scoped",1)==.5f && weapon_scale("m21_silencer",1)==.5f &&
			weapon_scale("m14ebr_thermal",1)==.5f,"M14 EBR native aliases share recoil tuning");
		check(weapon_scale("tmp_reflex",5)==.3f && weapon_scale("pp2000",5)==.4f &&
			weapon_scale("mp5_reflex",3)==.25f && weapon_scale("kriss_silencer",3)==.25f,
			"compact weapon tuning resolves MP5K and Vector native names");
		check(weapon_scale("p90",3)==.5f && weapon_scale("p90_silencer",3)==.5f &&
			weapon_scale("uzi_akimbo",3)==1.f,"P90 variants are reduced and Mini Uzi retains its base scale");
		for (const auto name:{"tmp","tmp_reflex","uzi","uzi_akimbo","pp2000","pp2000_reflex"})
		{
			check(multiplier(penalty_mode::long_weapons,false,3,name)==1.f,
				"short compact families bypass long-only recoil penalty even with native SMG class");
			check(multiplier(penalty_mode::all,false,3,name)==4.f && multiplier(penalty_mode::all,true,3,name)==1.f &&
				multiplier(penalty_mode::off,false,3,name)==1.f,"short weapons preserve all/off modes and actual support gating");
		}
		check(multiplier(penalty_mode::long_weapons,false,3,"p90")==4.f &&
			multiplier(penalty_mode::long_weapons,false,3,"mp5_reflex")==4.f &&
			multiplier(penalty_mode::long_weapons,false,3,"uzix")==4.f,
			"P90/other SMGs remain long weapons and short-family names respect suffix boundaries");
		check(weapon_scale("masada_acog",0)==.5f && weapon_scale("fn2000_reflex",0)==1.5f &&
			weapon_scale("famas",0)==1.2f && weapon_scale("tavor_mars",0)==.75f &&
			weapon_scale("ump45_silencer",3)==1.2f && weapon_scale("beretta393_akimbo",5)==.5f &&
			weapon_scale("aa12",4)==3.f,"new family adjustments resolve native names and attachment variants");
		check(weapon_scale("m203_m4",6)==1.f && weapon_scale("fal_shotgun_attach",4)==1.f &&
			weapon_scale("scar_h_m203",6)==1.f && weapon_scale("scar_h_shotgun_attach",4)==1.f &&
			weapon_scale("m40a3",1)==1.f && weapon_scale("beretta",5)==1.f && weapon_scale("",0)==1.f,
			"alternate feeds, unrelated prefixes and untuned weapons retain the 3x baseline");
		// Read-only H2 registered WeaponDef capture, 2026-09-24. The original
		// degree/range validation rejected every one of these gun-kick ranges.
		struct captured_kick {const char* name;float a,b,expected;int cls;};
		const captured_kick captured[]{
			{"M9",-30,-35,1.625f,5}, {"M4",-10,-15,.625f,0},
			{"AK47",5,-15,.3125f,0}, {"MP5",35,40,1.875f,3},
			{"G18",35,40,1.875f,5}, {"M1887",50,60,2.75f,4},
			{"Dragunov",80,85,4.125f,1}, {"Desert Eagle",-30,-35,1.625f,5}};
		for (const auto& sample:captured)
		{
			const auto normal=native_pitch(sample.a,sample.b);
			check(std::abs(normal-sample.expected*3.f)<.0001f && normal==native_pitch(sample.b,sample.a),sample.name);
			check(multiplier(penalty_mode::long_weapons,false,sample.cls)==(sample.cls==5?1.f:4.f),
				"live H2 class values distinguish pistols from rifles/SMGs/shotguns");
		}
		check(native_pitch(NAN,1.f)==0.f && native_pitch(INFINITY,0)==0.f && native_pitch(0,0)==0.f,
			"non-finite and zero native recoil cannot create muzzle motion");
		check(multiplier(penalty_mode::long_weapons,false,0)==4.f &&
			multiplier(penalty_mode::long_weapons,false,6)==4.f &&
			multiplier(penalty_mode::long_weapons,false,5)==1.f &&
			multiplier(penalty_mode::all,false,5)==4.f &&
			multiplier(penalty_mode::off,false,1)==1.f &&
			multiplier(penalty_mode::all,true,1)==1.f,"single-hand policy uses weapon class and actual support");
		state kick;
		hold gun{42,1,hand::right,hand::none,hold_source::interaction,7};
		gun.instance_generation=10;
		const auto t=clock::now();
		kick.shot(gun,3,t,2.f);
		check(kick.current(gun,3,t)==2.f,"confirmed shot raises the active muzzle");
		kick.shot(gun,3,t,2.f);
		check(kick.current(gun,3,t)==4.f && kick.current(gun,3,t+std::chrono::seconds(2))==0.f,
			"automatic shots accumulate and then settle");
		auto other=gun;other.instance_generation=11;
		check(kick.current(other,3,t)==0.f && kick.current(gun,4,t)==0.f,
			"other weapon instance and tracking reference cannot inherit recoil");
		other=gun;++other.rear_revision;
		check(kick.current(other,3,t)==0.f,"regripping cannot inherit an old recoil impulse");
		const auto penalized=native_pitch(-30,-35)*weapon_scale("deserteagle",5)*multiplier(penalty_mode::all,false,5);
		check(std::abs(penalized-97.5f)<.001f,"Desert Eagle uses the 3x baseline, replacement 5x tuning and 4x penalty");
		kick.clear();kick.shot(gun,3,t,penalized);
		check(kick.current(gun,3,t)==55.f && kick.current(gun,3,t+std::chrono::milliseconds(10))<55.f,
			"large penalized impulses cap at 55 degrees and immediately recover");
	}
	recoil_pose_tests::run(check);
	ads_comfort_tests::run(check);
	hand_contact_tests::run(check);
	palm_contact_tests::run(check);
	shield_tests::run(check);
	marine_sniper_hand_tests::run(check);
	p90_striker_profile_tests::run(check);
	belt_profile_tests::run(check);
	arm_clearance_tests::run(check);
	heartbeat_tests::run(check);
	secondary_motion_tests::run(check);
	weapon_carry_tests::run(check);
	special_knife_tests::run(check);
	scripted_control_tests::run(check);
	airport_opening_tests(check);
	mounted_turret_tests::run(check);
	native_hide_tags_tests::run(check);
	weapon_hud_lifetime_tests::run(check);
	pickup_ammunition_tests::run(check);
	world_interaction_tests::run(check);
	independent_fire_tests::run(check);
	for (auto name:{"h2_wpn_pst_colt_anaconda_first_time_pullout","h2_wpn_pst_m9_pullout",
		"h1_wpn_pst_m1911_putaway","viewmodel_desert_eagle_pullout_empty","h2_wpn_pst_usp_tactical_pullout1",
		"h2_wpn_pst_usp_tactical_pullout_first_alt","h2_wpn_asl_m4a1_pullout","h2_wpn_asl_ak47_putaway",
		"h2_wpn_pst_glock_first_time_pullout","h2_wpn_pst_beretta393_first_time_pullout","h2_wpn_pst_mp9_pullout_first"})
		check(equip_presentation_index(40,1,name,"h2_wpn_pst_m9_idle",true,0,false)==1,
			"generic pickup/switch presentation policy covers authored firearm families");
	for (auto name:{"h2_wpn_pst_m9_reload","h2_wpn_asl_m4a1_fire","h2_wpn_pst_m9_breach",
		"h2_wpn_pst_m9_inspect","npc_m9_pullout","h2_wpn_pst_m9_sprint_in"})
		check(!native_equip_clip(name),"non-equip, scripted and NPC actions are not classified as equip");
	check(equip_presentation_index(40,2,"h2_wpn_pst_m9_pullout_empty","h2_wpn_pst_m9_empty_idle",true,0,false)==2,
		"preserve native empty-idle selection");
	for (int guard=0;guard<5;++guard)
		check(equip_presentation_index(40,guard==3 ? 0 : 1,"h2_wpn_pst_m9_pullout",
			guard==4 ? "h2_wpn_pst_m9_fire" : "h2_wpn_pst_m9_idle",guard!=0,guard==1,guard==2)==40,
			"non-VR/secondary/alternate/invalid idle pass through unchanged");
	auto now = clock::now();
	vr::controller_input::frame input{};
	input.focused = true;
	input.sequence = 1;
	input.reference_generation = 1;
	input.sampled_at = now;
	for (int h = 0; h < 2; ++h)
		input.grip[h].valid = input.aim[h].valid = true;
	std::array<vr::controller_input::digital_sampler, 2> samplers;
	const auto sample = [&](bool left, bool right = false, bool active = true) {
		now += std::chrono::milliseconds(10);
		input.sampled_at = now;
		++input.sequence;
		input.squeeze[0] = samplers[0].sample(active, left, now);
		input.squeeze[1] = samplers[1].sample(active, right, now);
	};
	holding_state holding;
	auto owner = holding.equipped(49);
	support_grip support;
	std::uint64_t assembly = 1;
	const auto consume = [&](float distance = 0.05f, bool available = true) {
		return support.consume(input, owner, assembly, available, distance, 0.10f, 0.22f, now);
	};
	sample(true);
	check(consume() == hand::none, "initial held squeeze cannot grab");
	sample(false);
	check(consume() == hand::none, "neutral arms");
	sample(true);
	check(consume() == hand::left, "near press acquires support");
	check(consume() == hand::left, "same input cannot toggle grip");
	check(consume(0.18f) == hand::left, "hysteresis retains attached support");
	check(consume(0.23f) == hand::none, "overextension cancels");
	check(consume() == hand::none, "return while held does not reacquire");
	sample(false);
	(void)consume();
	sample(true);
	check(consume(0.3f) == hand::none, "far press rejected");
	check(consume() == hand::none, "far held then near does not auto-grab");
	sample(false);
	(void)consume();
	sample(true);
	check(consume() == hand::left, "release and new near press works");
	++assembly;
	check(consume() == hand::none, "attachment change cancels held grip");
	sample(false);
	(void)consume();
	sample(true);
	(void)consume();
	++input.reference_generation;
	check(consume() == hand::none, "recenter cancels");
	sample(false);
	(void)consume();
	sample(true);
	(void)consume();
	input.focused = false;
	check(consume() == hand::none, "focus loss cancels");
	input.focused = true;
	check(consume() == hand::none, "focus recovery waits release");
	sample(false);
	(void)consume();
	sample(true);
	(void)consume();
	input.grip[0].valid = false;
	check(consume() == hand::none, "tracking loss cancels");
	input.grip[0].valid = true;
	check(consume() == hand::none, "tracking held recovery blocked");
	sample(false);
	(void)consume();
	sample(true);
	(void)consume();
	check(consume(0.05f, false) == hand::none && consume() == hand::none,
		  "menu cancels and requires release");
	sample(false);
	(void)consume();
	sample(true);
	check(consume(std::numeric_limits<float>::quiet_NaN()) == hand::none, "NaN distance rejected");
	sample(false);
	(void)consume();
	sample(true);
	check(consume(-1) == hand::none, "negative distance rejected");
	sample(false);
	(void)consume();
	sample(true);
	(void)consume();
	check(support.consume(input, owner, assembly, true, 0.05f, 0.1f, 0.22f,
						  now + std::chrono::milliseconds(151)) == hand::none,
		  "stale frame rejected");
	owner = holding.equipped(50);
	sample(true);
	check(consume() == hand::none, "weapon switch waits release");
	check(holding.grip(owner, grip_role::rear, hand::left), "left rear transaction");
	owner = holding.current();
	sample(false, false);
	(void)consume();
	sample(true, false);
	check(consume() == hand::none, "rear hand cannot support itself");
	sample(false, true);
	check(consume() == hand::right, "opposite physical hand supports left owner");
	const auto fire_revision = owner.rear_revision;
	check(holding.grip(owner, grip_role::support, hand::right) &&
			  holding.current().rear_revision == fire_revision,
		  "support changes do not change rear fire authority");
	owner = holding.current();
	trigger_policy firing;
	input.trigger[0] = {true, false, 0, 1};
	check(!firing.consume(input, owner, true, true, now), "rear trigger arms before support test");
	input.trigger[0] = {true, true, 1, 1};
	check(firing.consume(input, owner, true, true, now), "rear trigger fires before support change");
	check(holding.grip(owner, grip_role::support, hand::none), "release support during fire");
	owner = holding.current();
	check(firing.consume(input, owner, true, true, now), "support release does not interrupt held fire");
	check(holding.grip(owner, grip_role::support, hand::right), "acquire support during fire");
	owner = holding.current();
	check(firing.consume(input, owner, true, true, now), "support acquisition does not interrupt held fire");
	const quat identity{0, 0, 0, 1};
	check(close(rotate(aimed_rotation(aim_rule::rear_hand, identity, {0, 0, 0}, {0, 10, 0}, {10, 0, 0}),
					   {1, 0, 0}),
				{1, 0, 0}),
		  "pistol support cannot steer gun");
	check(close(rotate(aimed_rotation(aim_rule::two_hand, identity, {0, 0, 0}, {0, 10, 0}, {10, 0, 0}),
					   {1, 0, 0}),
				{0, 1, 0}),
		  "long gun uses two-hand baseline");
	check(close(rotate(aimed_rotation(aim_rule::two_hand, identity, {0, 0, 0}, {0, 0, 0}, {10, 0, 0}),
					   {1, 0, 0}),
				{1, 0, 0}),
		  "coincident hands retain finite aim");
	check(close(rotate(aimed_rotation(aim_rule::two_hand, identity, {0, 0, 0}, {-10, 0, 0}, {10, 0, 0}),
					   {1, 0, 0}),
				{-1, 0, 0}),
		  "crossed hands finite antiparallel aim");
	check(close(rotate(aimed_rotation(aim_rule::two_hand, identity, {0, 0, 0}, {2, 10, 0}, {2, 10, 0}),
					   {1, 0, 0}),
				{1, 0, 0}),
		  "authored off-axis foregrip retained");
	check(m9::suppress_equip("h2_wpn_pst_m9_pullout_first") && m9::suppress_equip("h2_wpn_pst_m9_putaway"),
		  "M9 equip action matching");
	check(!m9::suppress_equip("h2_wpn_pst_m9_fire") && !m9::suppress_equip("h2_wpn_pst_m9_reload") &&
			  !m9::suppress_equip("npc_pullout"),
		  "firing reload and foreign actions untouched");
	// Small valid arm rig plus all reviewed M9 finger names. Bind lengths belong
	// to this synthetic glove; authored rotations must not overwrite them.
	rig rig{};
	rig.parent.fill(-1);
	rig.arms = {arm{1, 2, 3}, arm{4, 5, 6}};
	rig.rear_grip_wrist = 6;
	rig.weapon_tag = 7;
	rig.gun = 8;
	rig.muzzle = 8;
	rig.count = 48;
	rig.parent[1] = 0;
	rig.parent[2] = 1;
	rig.parent[3] = 2;
	rig.parent[4] = 0;
	rig.parent[5] = 4;
	rig.parent[6] = 5;
	rig.parent[7] = 0;
	rig.parent[8] = 7;
	rig.weapon_bones[8] = true;
	std::array<bone, 48> native{}, output{};
	std::array<bone_definition, 48> definitions{};
	for (int i = 0; i < rig.count; ++i)
	{
		native[i].rotation = identity;
		native[i].weight = 2;
	}
	native[1].position = {0, 2, 0};
	native[2].position = {3, 2, -4};
	native[3].position = {6, 2, 0};
	native[4].position = {0, -2, 0};
	native[5].position = {3, -2, -4};
	native[6].position = {6, -2, 0};
	native[8].position = sub(native[6].position, m9::base.wrists[1].position);
	native[7] = native[8];
	for (size_t i = 0; i < m9::base.fingers.size(); ++i)
	{
		const int j = 9 + static_cast<int>(i);
		const auto name = m9::base.fingers[i].name;
		const int h = name.find("_le_") != std::string_view::npos ? 0 : 1;
		rig.parent[j] = rig.arms[h].wrist;
		native[j].position = add(native[rig.parent[j]].position, {1, 0, 0});
		definitions[j].name = name;
	}
	for (int i = 0; i < rig.count; ++i)
	{
		definitions[i].parent = rig.parent[i];
		definitions[i].bind = native[i];
	}
	for (int j = 0; j < 3; ++j)
	{
		const int i = 39 + j;
		rig.parent[i] = rig.gun;
		rig.weapon_bones[i] = true;
		definitions[i].name = m9::base.equip_rest[j].name;
		definitions[i].parent = rig.gun;
		native[i].position = add(native[8].position, {10, 10, 10});
		definitions[i].bind = native[i];
	}
	definitions[3].name="j_wrist_le";definitions[6].name="j_wrist_ri";
	// The full rifle library also articulates palm/webbing, unlike the older
	// 15-joint pistol fixture. Keep these real semantic nodes under each wrist.
	const std::array<std::string_view,6> palms{"j_pinkypalm_le","j_ringpalm_le","j_webbing_le",
		"j_pinkypalm_ri","j_ringpalm_ri","j_webbing_ri"};
	for(size_t i=0;i<palms.size();++i)
	{
		const int bone=42+static_cast<int>(i),parent=i<3?3:6;
		definitions[bone].name=palms[i];rig.parent[bone]=definitions[bone].parent=parent;
		native[bone]=native[parent];definitions[bone].bind=native[bone];
	}
	const auto library = bind_weapon_poses(rig, definitions, m9::base);
	check(library.valid, "all named fingers bind");
	auto missing = definitions;
	missing[9].name = "missing";
	check(!bind_weapon_poses(rig, missing, m9::base).valid, "missing joint rejects profile");
	const std::array<anchor, 2> targets{{{native[3].position, identity}, {native[6].position, identity}}};
	const std::array<vec, 2> shoulders{native[1].position, native[4].position};
	const std::array<vec, 3> axes{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
	std::array<bool, 2> limited{};
	{
		std::array<part_hand_transition,2> motion;auto displayed=native;
		vr::controller_input::frame sample;sample.focused=true;sample.sequence=sample.reference_generation=1;
		sample.sampled_at=clock::time_point{}+std::chrono::seconds(1);
		for(int h=0;h<2;++h)sample.grip[h].valid=sample.aim[h].valid=true;
		hold holder{9,1,hand::right,hand::none,hold_source::interaction,1};
		const auto shift=vec{0,0,3};
		for(int step=0;step<3;++step)
		{
			displayed=native;sample.sampled_at=clock::time_point{}+std::chrono::milliseconds(1000+step*45);++sample.sequence;
			part_hand_frame frame(motion,rig,sample,holder,targets,shoulders,axes,displayed,1,sample.sampled_at,39.37007874f,true);
			vr::gameplay::hands::pose_math::move_part(rig,rig.arms[0].wrist,{add(native[3].position,shift),native[3].rotation},displayed);
			frame.apply(0,part_hand_attachment::seated_magazine);frame.finish();
			check(close(displayed[3].position,add(native[3].position,scale(shift,step*.5f))),
				"shared presentation frame actually retargets the wrist through arm IK");
			for(int b=0;b<rig.count;++b)
			{
				if(rig.weapon_bones[b] || descendant(b,rig.arms[1].shoulder,rig))
					check(displayed[b].position==native[b].position && displayed[b].rotation==native[b].rotation,
						"wrist entry blend preserves gun, muzzle and controlling arm");
				if(descendant(b,rig.arms[0].wrist,rig))
					check(close(sub(displayed[b].position,displayed[3].position),sub(native[b].position,native[3].position)),
						"entry blend keeps finger articulation rigidly attached to wrist");
			}
		}
	}
	{
		hold left{9,1,hand::left,hand::none,hold_source::interaction,1};
		carry::pose_profile mirrored(m9::base,left,rig,library);
		for(auto rear:{hand::left,hand::right})
		{
			hold mini_owner{9,1,rear,hand::none,hold_source::interaction,1};
			carry::pose_profile mini(miniuzi::base,mini_owner,rig,library);
			check(close(mini.supports[0].position,{3.93700787f,1.18110236f,-1.96850394f}) &&
				std::abs((mini.supports[0].position[1]+mini.supports[1].position[1])*.5f-miniuzi::stock_contact_center[1])<.00001f &&
				mini.supports[1].position[1]<miniuzi::stock_contact_center[1],"Mini-Uzi leaves left wrist unchanged and places right wrist around the same right-offset hardware");
			check(close(mini.value.wrists[1-int(rear)].position,mini.supports[1-int(rear)].position),
				"Mini-Uzi rendered support and published carry contact agree after handover");
			const auto expected=vr::gameplay::hands::pose_mirror::wrist(miniuzi::stock_support,library.mirror_basis[rig.arms[1].wrist],miniuzi::stock_contact_center);
			check(close(rotate(mini.supports[1].rotation,{1,0,0}),rotate(expected.rotation,{1,0,0})),
				"Mini-Uzi right support mirrors wrist direction about its fixed position");
			check(mini.value.free_hand_reference==&miniuzi::wrists,"Mini-Uzi wrist tuning leaves raw reload controller basis independent");
			mini_owner.pose_rear=rear;mini_owner.rear=hand::none;mini_owner.support=hand(1-int(rear));
			const quat retained_rotation{0,0,.3826834f,.9238795f};
			carry::pose_profile support_only(miniuzi::base,mini_owner,rig,library,retained_rotation);
			check(support_only.value.control_rotations &&
				close(rotate((*support_only.value.control_rotations)[int(mini_owner.support)],{1,0,0}),rotate(retained_rotation,{1,0,0})),
				"sole-support pose adapter uses the captured relative orientation");
			check(close(rotate(support_only.rotations[int(mini_owner.support)],{1,0,0}),rotate(mini.rotations[int(mini_owner.support)],{1,0,0})),
				"temporary support carry never contaminates published rear-grip calibration");
			check(close(support_only.value.wrists[1-int(rear)].position,mini.supports[1-int(rear)].position),
				"Mini-Uzi retains the same point when carried by support grip alone");
		}
		check(mirrored.value.authored_rear==0 && close(mirrored.value.wrists[0].position,
			vr::gameplay::hands::pose_mirror::position(m9::base.wrists[1].position)),"left control uses mirrored authored grip");
		check(solve(rig,native,targets,shoulders,axes,0,output,limited,&mirrored.value.wrists[0].position) &&
			apply_poses(rig,library,mirrored.value,targets,{1,0},false,output),"left grip solves and poses fingers");
		check(close(add(output[rig.gun].position,rotate(output[rig.gun].rotation,mirrored.controls[0].position)),
			output[rig.arms[0].wrist].position),"mirrored left wrist remains attached to real gun");
		left.rear=hand::none;left.support=hand::right;left.pose_rear=hand::left;
		carry::pose_profile foregrip(m9::base,left,rig,library);
		check(!left.can_fire() && foregrip.solver_owner.rear==hand::right && foregrip.value.authored_rear==1,
			"carry-only pose keeps last control side without gaining firing ownership");
		const quat basis=normalize({.2f,-.3f,.1f,.8f});
		for (const auto& p:m9::slide_grips)
		{
			auto side=p;side.wrist.position[1]+=4; // deliberately side-mounted, not centered
			const auto mirrored_part=vr::gameplay::hands::pose_mirror::part(side,basis);
			const auto contact=vr::gameplay::hands::pose_math::compose(side.wrist,{side.contact_in_wrist,identity}).position;
			check(close(contact,vr::gameplay::hands::pose_math::compose(mirrored_part.wrist,{mirrored_part.contact_in_wrist,identity}).position),
				"mirroring hand keeps side handle contact on its physical side");
			const auto returned=vr::gameplay::hands::pose_mirror::part(mirrored_part,conjugate(vr::gameplay::hands::pose_mirror::rotation(basis)));
			check(close(returned.wrist.position,side.wrist.position) && close(rotate(returned.wrist.rotation,{1,0,0}),rotate(side.wrist.rotation,{1,0,0})),
				"mirror round trip preserves asymmetric grasp and anatomical axes");
		}
		const anchor object{{2,3,4},normalize({.1f,.4f,.2f,.8f})},in_wrist{{3,-1,2},normalize({.4f,-.2f,.1f,.7f})};
		const auto original_hand=vr::gameplay::hands::pose_math::compose(object,vr::gameplay::hands::pose_math::inverse(in_wrist));
		const auto mirrored_hand=vr::gameplay::hands::pose_mirror::wrist(original_hand,basis,object.position);
		const auto recovered=vr::gameplay::hands::pose_math::compose(mirrored_hand,vr::gameplay::hands::pose_mirror::object_in_wrist(object,in_wrist,basis));
		check(close(recovered.position,object.position) && close(rotate(recovered.rotation,{0,0,1}),rotate(object.rotation,{0,0,1})),
			"magazine mirror changes grasp without reflecting its feed geometry");
	}
	check(solve(rig, native, targets, shoulders, axes, 1, output, limited, &m9::base.wrists[1].position),
		  "authored rear solve");
	const auto gun_before = output[8];
	check(apply_poses(rig, library, m9::base, targets, {0, 1}, false, output), "open and grip pose apply");
	check(close(output[8].position, gun_before.position), "hand pose cannot move weapon");
	check(close(sub(output[8].position, output[6].position), scale(m9::base.wrists[1].position, -1)),
		  "stable gun-to-rear offset");
	{
		const float q = std::sqrt(.5f);
		for (const quat rotation : {quat{0,0,0,1}, quat{q,0,0,q}, quat{0,q,0,q}, quat{0,0,q,q}})
		{
			auto corrected = targets;
			for (int h = 0; h < 2; ++h)
			{
				const vec local_meters{-.12f, h == 0 ? .02f : -.02f, -.05f};
				vr::head_pose_bridge::world_pose grip{
					sub(targets[h].position, rotate(rotation, scale(local_meters,40))),
					{rotate(rotation,{1,0,0}),rotate(rotation,{0,1,0}),rotate(rotation,{0,0,1})}};
				check(make_wrist_target(grip,grip,{},40,h,{-.02f,.12f,-.05f},corrected[h]) &&
					close(corrected[h].position, targets[h].position), "M9 physical wrist pivot recovered from moving controller");
			}
			auto posed = output;
			check(solve(rig, native, corrected, shoulders, axes, 1, posed, limited, &m9::base.wrists[1].position) &&
				apply_poses(rig, library, m9::base, corrected, {0,1}, false, posed), "M9 rotated corrected pose");
			check(close(posed[6].position, corrected[1].position), "M9 authored pose keeps corrected wrist pivot");
			check(close(add(posed[8].position, rotate(posed[8].rotation, m9::base.wrists[1].position)), posed[6].position),
				"M9 authored rear grip remains coincident with wrist through rotation");
		}
	}
	for (int i = 9; i < 39; ++i)
	{
		check(std::abs(length(sub(output[i].position, output[rig.parent[i]].position)) - 1) < 0.001f,
			  "model finger lengths preserved");
		const auto local = normalize(multiply(conjugate(output[rig.parent[i]].rotation), output[i].rotation));
		const bool left = definitions[i].name.find("_le_") != std::string_view::npos;
		check(close(rotate(local, {1, 0, 0}),
					rotate(left ? identity : m9::base.fingers[i - 9].rotation, {1, 0, 0})),
			  "open uses bind rotation; grip uses sampled local rotation");
	}
	check(apply_poses(rig, library, m9::base, targets, {0, 1}, true, output),
		  "equip action pose suppression");
	for (int j = 0; j < 3; ++j)
		check(close(sub(output[39 + j].position, output[8].position), m9::base.equip_rest[j].local.position),
			  "equip uses reviewed rest parts without changing native state");
	grip_presenter presenter;
	owner = holding.equipped(51);
	{
		grip_presenter authoritative;
		sample(false,false);
		(void)authoritative.update(m9::base,library,rig,native,targets,shoulders,axes,input,owner,assembly,39.37007874f,true,false,now,output,limited,true,true);
		sample(true,false);
		const auto rejected=authoritative.update(m9::base,library,rig,native,targets,shoulders,axes,input,owner,assembly,39.37007874f,true,false,now,output,limited,true,true);
		check(rejected.valid && rejected.support==hand::none,"renderer cannot invent a support grasp denied by the hand coordinator");
	}
	const auto present = [&] {
		return presenter.update(m9::base, library, rig, native, targets, shoulders, axes, input, owner,
								assembly, 39.37007874f, true, false, now, output, limited);
	};
	sample(false, false);
	check(present().valid, "presenter initializes open free hand");
	sample(true, false);
	check(present().support == hand::left, "presenter acquires authored wrist anchor");
	grip_result result;
	for (int i = 0; i < 15; ++i)
	{
		sample(true, false);
		result = present();
	}
	check(result.valid && result.blend > 0.999f, "support blend converges");
	check(close(output[3].position,
				add(output[8].position, rotate(output[8].rotation, m9::base.wrists[0].position))),
		  "support wrist attaches exactly to weapon-relative anchor");
	check(close(rotate(output[8].rotation, {1, 0, 0}), {1, 0, 0}), "pistol presenter retains rear aim");
	sample(false, false);
	check(present().support == hand::none, "release immediately relinquishes logical grip");
	for (int i = 0; i < 15; ++i)
	{
		sample(false, false);
		result = present();
	}
	check(result.blend == 0 && close(output[3].position, targets[0].position),
		  "release restores free tracked hand");
	sample(true, false);
	check(present().support == hand::left, "support reacquires before physical lease");
	const auto leased_gun = output[8];
	const auto leased_rear = output[6];
	sample(true, false);
	result = presenter.update(m9::base, library, rig, native, targets, shoulders, axes, input, owner,
		assembly, 39.37007874f, true, false, now, output, limited, false);
	check(result.valid && result.support == hand::none && result.blend == 0,
		"physical magazine or slide lease immediately excludes support and its blend");
	check(close(output[8].position, leased_gun.position) && close(output[6].position, leased_rear.position),
		"physical lease cannot displace gun or rear wrist");
	sample(true, false);
	check(present().support == hand::none, "held squeeze cannot auto acquire after physical lease");
	sample(false, false);
	(void)present();
	sample(true, false);
	check(present().support == hand::left, "neutral then fresh squeeze reacquires after physical lease");
	{
		const auto before=output;
		const auto raw=targets;
		const anchor desired{add(before[3].position,vec{.5f,-.5f,.5f}),normalize(quat{.4f,.1f,.2f,.85f})};
		check(constrain_part_hand(rig,library,m9::base,raw,shoulders,axes,1,desired,output), "part hand IK constraint applied");
		check(close(output[3].position,desired.position) && close(rotate(output[3].rotation,{1,0,0}),rotate(desired.rotation,{1,0,0})),
			"constrained wrist exactly attaches to part position and orientation");
		for (int i=0;i<rig.count;++i)
			if (rig.weapon_bones[i] || !descendant(i,rig.arms[0].shoulder,rig))
				check(output[i].position==before[i].position && output[i].rotation==before[i].rotation,
					"part constraint cannot mutate gun, rear arm, camera or unrelated bones");
		check(raw[0].position==targets[0].position && raw[0].rotation==targets[0].rotation,
			"visual hand constraint cannot feed back into controller target");
		for (const auto& style : m9::slide_grips)
		{
			auto candidate=before;
			const anchor slide_wrist{add(before[8].position,rotate(before[8].rotation,style.wrist.position)),
				multiply(before[8].rotation,style.wrist.rotation)};
			check(constrain_part_hand(rig,library,m9::base,raw,shoulders,axes,1,slide_wrist,candidate),
				"both slide wrist variants apply through the shared hand constraint");
			check(close(candidate[3].position,slide_wrist.position) &&
				close(rotate(candidate[3].rotation,{1,0,0}),rotate(slide_wrist.rotation,{1,0,0})),
				"variant wrist pose reaches its exact authored contact");
			for (int i=0;i<rig.count;++i)
				if (rig.weapon_bones[i] || !descendant(i,rig.arms[0].shoulder,rig))
					check(candidate[i].position==before[i].position && candidate[i].rotation==before[i].rotation,
						"overhand retarget cannot displace gun, main hand or unrelated bones");
		}
	}
	// The M1887 fore-end constraint must retain authored support fingers and
	// exactly cancel the mirrored wrist basis used by the final pose pass.
	for(auto rear:{hand::left,hand::right})
	{
		hold owned{51,1,rear,hand(1-int(rear)),hold_source::interaction,1};
		carry::pose_profile mirrored(m9::base,owned,rig,library);const auto& grip=mirrored.value;const int off=1-int(rear);
		auto posed=native;
		check(solve(rig,native,targets,shoulders,axes,int(rear),posed,limited,&grip.wrists[int(rear)].position) && apply_poses(rig,library,grip,targets,{1,1},false,posed),"supported hand fixture starts in authored grip");
		const auto before=posed;const anchor desired{add(posed[rig.arms[off].wrist].position,vec{.5f,-.5f,.5f}),normalize(quat{.4f,.1f,.2f,.85f})};
		check(constrain_part_hand(rig,library,grip,targets,shoulders,axes,int(rear),desired,posed,1.f),"fore-end constraint explicitly preserves full support grip");
		check(close(posed[rig.arms[off].wrist].position,desired.position) && close(rotate(posed[rig.arms[off].wrist].rotation,{1,0,0}),rotate(desired.rotation,{1,0,0})),"grip weight does not rotate or displace the constrained support wrist on either side");
		for(int bone=0;bone<rig.count;++bone)
		{
			if(library.finger[bone]>=0 && descendant(bone,rig.arms[off].wrist,rig))
			{
				const auto local=normalize(multiply(conjugate(posed[rig.parent[bone]].rotation),posed[bone].rotation));
				const auto expected=grip.fingers[library.finger[bone]].rotation;
				check(close(rotate(local,{1,0,0}),rotate(expected,{1,0,0})) && close(rotate(local,{0,1,0}),rotate(expected,{0,1,0})),"every constrained support finger retains its authored local rotation");
			}
			if(rig.weapon_bones[bone] || !descendant(bone,rig.arms[off].shoulder,rig))check(posed[bone].position==before[bone].position && posed[bone].rotation==before[bone].rotation,"support finger correction cannot affect the gun or controlling arm");
		}
	}
	auto invalid_bind = definitions;
	invalid_bind[9].bind.rotation = {};
	check(!bind_weapon_poses(rig, invalid_bind, m9::base).valid, "invalid bind quaternion rejected");
	const model_definition base_models[]{{"viewhands_test", 0, 8}, {m9::base.receiver, 8, 34}};
	auto pistol_rig=rig;pistol_rig.count=42; // Original pistol fixture excludes appended rifle palm nodes.
	check(select_profile(base_models, pistol_rig).value == &m9::base, "M9 receiver resolves");
	const model_definition attached[]{
		{"viewhands_test", 0, 8}, {m9::base.receiver, 8, 1}, {"unknown_foregrip", 9, 30}};
	auto attachment_rig = rig;
	attachment_rig.weapon_bones[9] = true;
	check(!select_profile(attached, attachment_rig).value, "unknown attachment cannot reuse base grip");
	{
		const model_definition m4_models[]{
			{"viewhands_test", 0, 8}, {m4::foregrip.receiver, 8, 31},
			{"attach_h2_mp5k_foregrip_vm", 39, 1}, {"attach_h2_m4_cover_vm", 40, 1}, {"attach_h2_silencer_01_vm", 41, 1}};
		check(!select_profile(m4_models, rig).value, "legacy M4 name-only synthetic assembly needs actual attachment topology");
		auto missing_grip = std::to_array(m4_models);
		missing_grip[2].name = "attach_h2_m203_vm";
		check(!select_profile(missing_grip, rig).value, "M4 launcher cannot inherit vertical foregrip pose");
		check(!select_profile(std::span(m4_models).first(2), rig).value, "M4 without foregrip requires another authored variant");
		const auto m4_library = bind_weapon_poses(rig, definitions, m4::foregrip);
		check(m4_library.valid, "M4 fingers bind through shared glove library");
		for(int actor=0;actor<2;++actor)for(std::uint8_t style=0;style<2;++style)
		{
			auto posed=native;auto tracked=targets;auto comfort=m4::foregrip;auto free_wrists=comfort.wrists;
			tracked[actor].rotation=normalize({.2f,-.3f,.1f,.8f});
			const auto mirror=m4_library.mirror_basis[rig.arms[actor].wrist];
			const auto grasp=select_magazine_grip(m4::physical,{0,0,0,1},actor,mirror,false,true,style);
			free_wrists[actor].rotation=magazine_wrist_basis(m4::physical,grasp,free_wrists[actor].rotation,false);
			comfort.free_hand_reference=&free_wrists;
			check(apply_poses(rig,m4_library,comfort,tracked,{0,0},false,posed),"magazine-owned free wrist frame passes actual hand posing");
			const auto object=multiply(posed[rig.arms[actor].wrist].rotation,grasp.in_wrist.rotation);
			check(dot(rotate(object,{0,0,1}),rotate(tracked[actor].rotation,{0,0,1}))>.99999f,
				"rendered magazine up matches raw controller up after actual free-hand pose application");
		}
		// Long synthetic arms avoid reach clamping while exercising the actual
		// authored M4 grip span. Position/rotation use the production presenter.
		auto rifle_native = native;
		rifle_native[1].position = {-25, 2, 0}; rifle_native[2].position = {-12, 2, -20};
		rifle_native[4].position = {-25, -2, 0}; rifle_native[5].position = {-12, -2, -20};
		const std::array<vec, 2> rifle_shoulders{rifle_native[1].position, rifle_native[4].position};
		auto rifle_targets = targets;
		const auto span = sub(m4::foregrip.wrists[0].position, m4::foregrip.wrists[1].position);
		rifle_targets[0].position = add(rifle_targets[1].position, span);
		grip_presenter rifle;
		owner = holding.equipped(52);
		const auto present_rifle = [&] {
			return rifle.update(m4::foregrip, m4_library, rig, rifle_native, rifle_targets, rifle_shoulders, axes,
				input, owner, assembly, 39.37007874f, true, false, now, output, limited);
		};
		sample(false); check(present_rifle().valid, "M4 initializes single hand");
		sample(true); check(present_rifle().support == hand::left, "M4 fresh squeeze acquires authored grip");
		for (int i=0;i<15;++i) { sample(true); result=present_rifle(); }
		check(result.blend > .999f, "M4 acquisition blend converges");
		const float q = std::sqrt(.5f);
		// Rotating only the rear wrist must not measure against the displaced
		// one-hand foregrip, which would spuriously release this valid grip.
		rifle_targets[1].rotation = {0,0,q,q};
		sample(true); result=present_rifle();
		check(result.support == hand::left && result.distance_meters < .001f,
			"M4 retained grip measures solved two-hand reach after 90 degree rear turn");
		check(close(rotate(output[8].rotation, unit(span)), unit(span)), "M4 axis follows both hands despite rear turn");
		rifle_targets[0].position = add(rifle_targets[1].position, rotate(quat{0,q,0,q},span));
		sample(true); result=present_rifle();
		check(result.support == hand::left && close(rotate(output[8].rotation,unit(span)),
			rotate(quat{0,q,0,q},unit(span))), "M4 support moves the actual weapon axis vertically");
		auto release_a = rifle, release_b = rifle;
		sample(false);
		std::array<bone,48> released_a{}, released_b{};
		(void)release_a.update(m4::foregrip,m4_library,rig,rifle_native,rifle_targets,rifle_shoulders,axes,
			input,owner,assembly,39.37007874f,true,false,now,released_a,limited);
		auto far_targets=rifle_targets; far_targets[0].position=add(rifle_targets[1].position,vec{-20,-10,10});
		(void)release_b.update(m4::foregrip,m4_library,rig,rifle_native,far_targets,rifle_shoulders,axes,
			input,owner,assembly,39.37007874f,true,false,now,released_b,limited);
		check(close(rotate(released_a[8].rotation,{1,0,0}),rotate(released_b[8].rotation,{1,0,0})),
			"moving free hand after release cannot drag returning M4 aim");
		for (int i=0;i<15;++i) { sample(false); result=present_rifle(); }
		check(result.blend == 0 && close(rotate(output[8].rotation,{1,0,0}),rotate(rifle_targets[1].rotation,{1,0,0})),
			"M4 release returns to rear aim");
		rifle_targets[0].position = add(rifle_targets[1].position,rotate(rifle_targets[1].rotation,span));
		sample(true); check(present_rifle().support == hand::left, "M4 reacquires before crossing test");
		rifle_targets[0].position = rifle_targets[1].position;
		sample(true); check(present_rifle().support == hand::none, "coincident hands release M4 without undefined axis");
		rifle_targets[0].position = add(rifle_targets[1].position,rotate(rifle_targets[1].rotation,span));
		sample(true); check(present_rifle().support == hand::none, "M4 cannot auto regrab after crossing while squeezed");
	}
	{
		using namespace carry;
		const float units = 39.37007874f;
		const auto m4_library = bind_weapon_poses(rig, definitions, m4::foregrip);
		for (unsigned scenario = 0; scenario < 3; ++scenario)
		{
			inventory authority;
			const owned_weapon entry[]{{52, {false, false}}};
			check(authority.reconcile(entry), "support retention admits a real carry instance");
			const auto id = authority.find_definition(52)->id;
			check(authority.equip(id, hand::right), "support retention equips the primary grip");
			grip_presenter presented;
			auto current_targets = targets;
			const auto span = sub(m4::foregrip.wrists[0].position, m4::foregrip.wrists[1].position);
			current_targets[0].position = add(current_targets[1].position, span);
			auto long_native = native;
			long_native[1].position = {-25, 2, 0}; long_native[2].position = {-12, 2, -20};
			long_native[4].position = {-25, -2, 0}; long_native[5].position = {-12, -2, -20};
			const std::array<vec, 2> retention_shoulders{long_native[1].position, long_native[4].position};
			bool tracked = true;
			const auto present_authority = [&]
			{
				return presented.update(m4::foregrip, m4_library, rig, long_native, current_targets, retention_shoulders,
					axes, input, authority.find(id)->owner, assembly, units, true, false, now, output, limited, tracked, true);
			};
			const auto gun = [&]
			{
				const auto rotation = aimed_rotation(m4::foregrip.aiming,
					control_rotation(m4::foregrip, 1, current_targets[1].rotation),
					current_targets[1].position, current_targets[0].position, span);
				return anchor{sub(current_targets[1].position, rotate(rotation, m4::foregrip.wrists[1].position)), rotation};
			};
			const auto retain = [&]
			{
				const auto status = retained_support(m4::foregrip, gun(), m4::foregrip.wrists[0],
					current_targets[1].position, current_targets[0].position, units, tracked);
				if (status == support_retention::released)
					authority.release(id, 1u, location::absent, false, [](const instance&) { return false; });
				return status;
			};
			sample(false); check(present_authority().valid, "authoritative presenter initializes primary grip");
			check(support_contact(m4::foregrip, gun(), m4::foregrip.wrists[0],
				current_targets[1].position, current_targets[0].position, units), "carry acquisition admits the actual authored contact");
			check(claim_grip(authority, {.actor=hand::left, .pressed=true, .available=true, .support=id}).pose_changed,
				"real carry claim acquires support before retention tests");
			for (unsigned i=0;i<15;++i) { sample(true); (void)present_authority(); }
			const float q = std::sqrt(.5f);
			current_targets[1].rotation = {0,0,q,q};
			check(retain()==support_retention::retained && present_authority().support==hand::left,
				"authority and presenter retain the solved contact through a 90 degree rear turn");
			if (scenario == 0) current_targets[0].position = add(current_targets[1].position, scale(unit(span), 60));
			if (scenario == 1) current_targets[0].position = current_targets[1].position;
			if (scenario == 2) tracked = false;
			const auto status = retain(); sample(true); const auto view = present_authority();
			check(view.support==hand::none && authority.in_hand(hand::right),
				"suspended/released support never drops the primary weapon or keeps steering");
			check((scenario==2 && status==support_retention::suspended && authority.find(id)->owner.support==hand::left) ||
				(scenario!=2 && status==support_retention::released && authority.find(id)->owner.support==hand::none),
				"tracking suspension preserves ownership while invalid geometry releases only the support lease");
			tracked=true; current_targets[0].position=add(current_targets[1].position,span);
			(void)retain(); sample(true);
			if (scenario==2) check(present_authority().support==hand::left, "tracking recovery restores the existing support without a new grab");
			else
			{
				check(!claim_grip(authority,{.actor=hand::left,.available=true,.support=id}).pose_changed && present_authority().support==hand::none,
					"returning while squeezed cannot manufacture a new support grab");
				check(claim_grip(authority,{.actor=hand::left,.pressed=true,.available=true,.support=id}).pose_changed,
					"fresh press can reacquire support after a spatial release");
			}
		}
	}
	std::cout << "weapon grip failures=" << failures << '\n';
	return failures ? 1 : 0;
}
