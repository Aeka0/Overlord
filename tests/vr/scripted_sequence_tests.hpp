#pragma once
#include "component/vr/gameplay/scripted_sequence_policy.hpp"
#include "component/vr/gameplay/sequences/rappel.hpp"
#include "component/vr/gameplay/sequences/favela.hpp"
#include "component/vr/gameplay/sequences/gulag.hpp"
#include "component/vr/gameplay/sequences/sliding.hpp"
#include "component/vr/gameplay/sequences/trainer.hpp"
#include "component/vr/gameplay/sequences/cliffhanger.hpp"
#include "component/vr/gameplay/sequences/roadkill.hpp"
#include "component/vr/gameplay/sequences/dcemp.hpp"
#include "component/vr/gameplay/sequences/airport.hpp"
#include "component/vr/gameplay/scripted_control.hpp"
#include "component/vr/gameplay/script_function_extent.hpp"
#include "component/vr/scripted_view.hpp"
#include "component/vr/scripted_position.hpp"
#include "component/vr/gameplay/player_life.hpp"
#include "ending_sequence_tests.hpp"
#include "camera_policy_tests.hpp"

template<class Check> void scripted_sequence_tests(Check check)
{
	ending_sequence_tests::run(check);
	camera_policy_tests(check);
	{
		using namespace vr::gameplay::sequences;
		const airport::evidence native{true,false,41,41,"player_ending"};
		const auto boarding=airport::classify(native);
		check(boarding.scene==scenario::airport && boarding.stage==phase::hookup &&
			boarding.rotation_tag==vr::game_view::scripted_camera_tag::player,
			"No Russian boarding requires its linked ending body and uses the original camera tag");
		for(unsigned mutation=0;mutation<5;++mutation)
		{
			auto e=native;
			if(mutation==0)e.boarding=false;
			if(mutation==1)e.parent=0;
			if(mutation==2)e.body=0;
			if(mutation==3)e.parent=42;
			if(mutation==4)e.rig="player_rig";
			check(airport::classify(e).stage==phase::none,
				"ordinary combat, waiting beside the ambulance and unrelated bodies cannot acquire the ending camera");
		}
		auto e=native;e.shot=true;
		const auto shot=airport::classify(e);
		e.boarding=false;
		check(shot.stage==phase::execution && airport::classify(e).stage==phase::execution,
			"the native fire flag locks both normal and alternate linked shooting sequences");
		check(boarding.suspend_weapons && shot.suspend_weapons && !boarding.allow_movement && !boarding.allow_turn &&
			!shot.allow_movement && !shot.allow_turn && !shot.hide_body_arms && !shot.retain_weapon,
			"airport ending blocks interactive input while retaining the original animated body and props");
	}
	{
		using namespace vr::gameplay::sequences;
		using namespace vr::game_view;
		const auto wake=dcemp::classify({"player_rig",17,17});
		const auto space=dcemp::classify({"iss_rig",31,17});
		check(wake.camera.owns_rotation() && wake.camera.head==head_rotation::free &&
			wake.camera.translation==head_translation::attenuated,"Second Sun waking camera frees look and attenuates head movement");
		check(space.camera.owns_rotation() && space.camera.head==head_rotation::free &&
			space.camera.translation==head_translation::tracked,"ISS frees look and preserves full physical head movement");
		scripted_position position;
		(void)position.offset(17,1,{},wake.camera);
		check(std::abs(position.offset(17,1,{.2f,0,0},wake.camera)[0]-.02f)<.0001f,
			"wakeup retains the shared default ten percent head gain");
		(void)position.offset(31,1,{.2f,0,0},space.camera);
		check(std::abs(position.offset(31,1,{.5f,0,0},space.camera)[0]-.3f)<.0001f,
			"ISS link starts its own anchor and applies physical displacement one to one");
		check(dcemp::classify({"player_rig",18,17}).stage==phase::none &&
			dcemp::classify({"iss_rig",0,17}).stage==phase::none &&
			dcemp::classify({"tag_origin",17,17}).stage==phase::none,
			"rescue rig, missing parent and later linked combat cannot inherit opening camera ownership");
		check(!wake.suspend_weapons && !space.suspend_weapons && wake.allow_movement && space.allow_movement,
			"camera adapters retain native weapon and movement permissions");
		const auto ground=dcemp::classify({"",41,17,41,true});
		check(space.camera.needs_tag() && space.rotation_tag==scripted_camera_tag::player &&
			ground.camera.needs_tag() && ground.rotation_tag==scripted_camera_tag::origin,
			"ISS entry and ground return align to independent native camera anchors");
		check(!ground.camera.owns_rotation() && ground.allow_movement && ground.allow_turn && !ground.suspend_weapons,
			"ground reset retains player input and native combat permissions");
		check(dcemp::classify({"",41,17,41,false}).stage==phase::none &&
			dcemp::classify({"",42,17,41,true}).stage==phase::none,
			"return reset waits for native angle restoration and requires the actual ground carrier");
	}
	{
		const auto axes=[](float yaw,float pitch,float roll) {
			const float y=yaw*.01745329252f,p=pitch*.01745329252f,r=roll*.01745329252f;
			const float cy=std::cos(y),sy=std::sin(y),cp=std::cos(p),sp=std::sin(p),cr=std::cos(r),sr=std::sin(r);
			return std::array<std::array<float,3>,3>{{{cp*cy,cp*sy,-sp},
				{-sy*cr+sp*cy*sr,cy*cr+sp*sy*sr,cp*sr},{sy*sr+sp*cy*cr,-cy*sr+sp*sy*cr,cp*cr}}};
		};
		vr::game_view::scripted_view camera;
		camera.follow_authored(1,1,100,30,0,10,axes(20,80,0));
		for(int pitch=81;pitch<=100;++pitch)
			check(std::abs(camera.follow_authored(1,1,100+pitch,0,0,10,axes(20,float(pitch),0))-30)<.001f,
				"scripted pitch across vertical cannot manufacture a 180-degree head turn");
		check(std::abs(camera.follow_authored(1,1,210,0,45,10,axes(50,100,0))-60)<.001f,
			"actual authored yaw at steep pitch is inherited independently of physical head yaw");
		check(std::abs(camera.follow_authored(1,1,210,0,45,10,axes(50,100,0))-60)<.001f,
			"repeated camera queries cannot integrate one authored turn twice");
		const float stable=camera.follow_authored(1,1,220,0,45,11,axes(-120,0,0));
		check(std::abs(stable-60)<.001f,"replacement camera source seeds its axes without snapping to their heading");
		for(int roll=1;roll<=180;++roll)
			check(std::abs(camera.follow_authored(1,1,220+roll,0,45,11,axes(-120,0,float(roll)))-stable)<.001f,
				"authored roll uses the stable forward axis and cannot rotate horizontal head control");
		const float turned=camera.follow_authored(1,1,410,0,45,11,axes(60,0,180));
		check(std::abs(std::abs(std::remainder(turned-stable,360.f))-180)<.001f,
			"a real authored half-turn remains valid; the fix does not clamp large yaw changes");
	}
	{
		const std::array<std::pair<const char*,const char*>,4> names{{
			{"canonical_lowercase",reinterpret_cast<const char*>(0x1000)},
			{"later",reinterpret_cast<const char*>(0x1800)},
			{"different_alias",reinterpret_cast<const char*>(0x1400)},
			{"__end__",reinterpret_cast<const char*>(0x2000)}}};
		const auto extent=vr::gameplay::script_function_extent(names,0x1000);
		check(extent && extent->first==0x1000 && extent->second==0x1400,
			"script range uses function address across canonical aliases and registration order");
		check(!vr::gameplay::script_function_extent(names,0x1100) && !vr::gameplay::script_function_extent(names,0x2000),
			"unregistered function and missing end boundary cannot admit a hook caller");
	}
	{
		using namespace vr::gameplay::sequences;
		const roadkill::boarding_evidence boarding{true,true,false,"player_rig",41,41};
		const auto view=roadkill::classify(boarding);
		check(view.scene==scenario::roadkill && view.stage==phase::hookup && view.camera==vr::game_view::camera_profiles::free,
			"Team Player player_getin on the actual convoy vehicle allows free HMD angles");
		check(!view.suspend_weapons && !view.hide_body_arms && !view.retain_weapon && !view.allow_world_use && view.allow_movement && view.allow_turn,
			"boarding camera preserves native arms, inventory, inputs and original useby progression");
		for(unsigned mutation=0;mutation<8;++mutation)
		{
			auto other=boarding;
			switch(mutation)
			{
			case 0:other.alive=false;break;
			case 1:other.triggered=false;break;
			case 2:other.mounted=true;break;
			case 3:other.rig="player_worldbody";break; // Intro and ejection body.
			case 4:other.rig="player_latvee";break; // Direct mounted vehicle parent.
			case 5:other.rig_vehicle=0;break;
			case 6:other.player_vehicle=0;break;
			case 7:other.rig_vehicle=42;break;
			}
			check(roadkill::classify(other).stage==phase::none,
				"boarding ends for death, missing cue/link, other bodies/vehicles and mounted turret takeover");
		}
		check(roadkill::classify(boarding).stage==phase::hookup,
			"a saved mid-boarding relationship needs no replayed local start event");
		const roadkill::recovery_evidence recovery{true,true,false,false,"player_worldbody"};
		const auto rescued=roadkill::classify_recovery(recovery);
		check(rescued.stage==phase::recovery && rescued.camera==vr::game_view::camera_profiles::free &&
			!rescued.hide_body_arms && !rescued.suspend_weapons,
			"Shepherd's linked pickup animation frees head rotation while native arms and control progression remain owned by the script");
		for(unsigned mutation=0;mutation<5;++mutation)
		{
			auto other=recovery;
			switch(mutation)
			{
			case 0:other.alive=false;break;
			case 1:other.intro_done=false;break;
			case 2:other.on_the_line=true;break;
			case 3:other.mounted=true;break;
			case 4:other.rig="player_rig";break;
			}
			check(roadkill::classify_recovery(other).stage==phase::none,
				"pickup free look cannot escape to death, the RPG ride, ordinary gameplay, mounted use or another rig");
		}
	}
	{
		using namespace vr::gameplay::sequences;
		for(const auto start:{"default","cave","e3","climb","jump"})
		{
			const auto view=cliffhanger::classify(start,true,false,true,2400);
			check(view.scene==scenario::cliffhanger && view.camera==vr::game_view::camera_profiles::free,
				"Cliffhanger early entry/checkpoint frees head angles without reseeding each replacement rig");
			check(view.allow_movement && view.allow_turn && !view.suspend_weapons && !view.hide_body_arms && !view.retain_weapon && !view.allow_world_use,
				"Cliffhanger camera does not replace native body, arms, weapons, movement or interaction permissions");
		}
		for(const auto start:{"clifftop","camp","c4","hanger","satellite","icepick","snowmobile","ending",""})
			check(cliffhanger::classify(start,true,false,true,2400).stage==phase::none,
				"later Cliffhanger checkpoints never inherit the early camera policy");
		const auto resting=cliffhanger::classify("default",true,false,true,2400,"worldbody",false);
		const auto rising=cliffhanger::classify("default",true,false,true,2400,"worldbody",true);
		check(rising.camera==vr::game_view::camera_profiles::authored &&
			rising.rotation_tag==vr::game_view::scripted_camera_tag::player && resting.camera==rising.camera,
			"Cliffhanger inherits authored tag_player yaw while keeping head rotation independent");
		check(resting.arms.reserved() && resting.arms.mode==vr::gameplay::scripted_arms::control::authored,
			"crouched body reserves arms while preserving authored rest");
		check(rising.arms.reserved() && rising.arms.mode==vr::gameplay::scripted_arms::control::tracked &&
			!rising.suspend_weapons && rising.allow_movement && rising.allow_turn && !rising.hide_body_arms,
			"get-up requests animation-gated tracking without locking native stand/jump input or hiding body arms");
		check(!cliffhanger::classify("default",true,false,true,2400,"tag_origin",true).arms.reserved(),
			"temporary camera helper is not mistaken for an animated body");
        check(!cliffhanger::classify("default",true,true,true,2400,"worldbody",true).arms.reserved(),
            "reached top releases scripted arms");
        check(cliffhanger::classify("jump",true,false,true,2400,"worldbody",true,true).arms.mode==vr::gameplay::scripted_arms::control::authored,
            "native jump and final handoff own authored arms between independent climb stages");
        const auto rescue=cliffhanger::classify("jump",true,false,true,2400,"worldbody",true,true,true);
        check(rescue.camera.script==vr::game_view::script_rotation::additive && rescue.camera.axes==vr::game_view::script_axes::all &&
            rescue.camera.head==vr::game_view::head_rotation::free && rescue.rotation_tag==vr::game_view::scripted_camera_tag::player,
            "big-jump rescue adds all authored camera axes while preserving independent head rotation");
		check(cliffhanger::classify("climb",true,false,true,2400,"worldbody",false).arms.mode==vr::gameplay::scripted_arms::control::tracked,
			"direct climbing checkpoint needs no replay of original ledge flag");
		for(const auto state:{cliffhanger::classify("default",false,false,true,2400),
			cliffhanger::classify("default",true,true,true,2400),cliffhanger::classify("default",true,false,false,2400),
			cliffhanger::classify("default",true,false,true,0),cliffhanger::classify("default",true,false,true,4000)})
			check(state.stage==phase::none,"missing native flags, reached_top, death and unlink release early camera ownership");
		vr::game_view::scripted_view camera;
		camera.follow_authored(20,1,100,35,0,9,170);
		check(camera.follow_authored(20,1,110,-100,45,9,-170)==55,
			"authored body yaw wraps correctly and remains independent of looking sideways");
		check(camera.follow_authored(20,1,120,170,90,9,-140)==85,
			"body turn adds to the tracking frame even when native player angles are clamped");
	}
	{
		using namespace vr::gameplay::sequences;
		const auto approach=trainer::classify(true,false,true,false,"script_origin","");
		const auto animation=trainer::classify(true,false,true,false,"script_model","worldbody");
		check(approach.scene==scenario::trainer && animation.scene==scenario::trainer &&
			approach.camera==vr::game_view::camera_profiles::free && animation.camera==approach.camera &&
			approach.suspend_weapons && !approach.allow_movement && !approach.allow_turn,
			"rifle and grenade pickups free head rotation across approach-to-body handoff while preserving the scripted interaction");
		check(trainer::classify(true,false,false,false,"script_model","worldbody").stage==phase::none &&
			trainer::classify(true,false,true,true,"script_model","worldbody").stage==phase::none &&
			trainer::classify(true,true,true,false,"script_model","worldbody").stage==phase::none &&
			trainer::classify(false,false,true,false,"script_origin","").stage==phase::none &&
			trainer::classify(true,false,true,false,"script_model","other").stage==phase::none,
			"unlink, weapon return, completed training, missing flags and unrelated rigs do not retain pickup camera ownership");
	}
	using namespace vr::gameplay::sequences;
	using namespace vr::gameplay::sequences::rappel;
	{
		using vr::gameplay::player_life::dead;
		check(!dead(0,100) && !dead(1,1) && !dead(2,0) && !dead(3,0),
			"living/linked play and native noclip/UFO are not death ownership");
		for(const auto type:{5,6,7})check(dead(std::uint8_t(type),100) && dead(std::uint8_t(type),0),
			"native dead/free, dead/linked and authored death remain suppressed across health projection skew");
		check(dead(0,0) && dead(1,-1),"lethal health edge blocks interaction before native movement type catches up");
		const auto death=death_view();
		check(death.stage==phase::death && death.scene==scenario::death && death.suspend_weapons &&
			!death.allow_turn && !death.allow_movement && !death.allow_world_use && !death.retain_weapon &&
			!death.hide_body_arms && death.camera==vr::game_view::camera_profiles::aligned,
			"death releases interactive control while retaining the authored body and free tracked rotation");
	}
	{
		const auto slide=sliding::classify(true,true,401,401);
		check(slide.scene==scenario::sliding && slide.stage==phase::transport &&
			slide.camera==vr::game_view::camera_profiles::aligned,
			"shared slidemodel identity enables free look for forward, reverse and exit animation paths");
		check(slide.allow_movement && slide.allow_turn && !slide.suspend_weapons && !slide.retain_weapon && !slide.hide_body_arms,
			"slide camera adaptation leaves native locomotion, weapons and body animation ownership intact");
		for(const auto state:{sliding::classify(false,true,401,401),sliding::classify(true,false,401,401),
			sliding::classify(true,true,402,401),sliding::classify(true,true,0,0),sliding::classify(true,true,401,0)})
			check(state.stage==phase::none,"slide death, unlink, missing or stale slidemodel cannot own another camera");
		const auto price=gulag::cinematic({.alive = true, .linked = true, .rig = "player_rig", .price_anchor = gulag::same_entity(35371,35371), .cafeteria = false, .evacuation_rig = false});
		const auto impact=gulag::cinematic({.alive = true, .linked = true, .rig = "worldbody", .price_anchor = false, .cafeteria = true, .evacuation_rig = false});
		const auto rock=gulag::cinematic({.alive = true, .linked = true, .rig = "player_rig", .price_anchor = false, .cafeteria = false, .evacuation_rig = true});
		for(const auto state:{price,impact,rock})
			check(state.scene==scenario::gulag && state.stage==phase::execution &&
				state.camera==scene_cameras::gulag_later && state.rotation_tag==vr::game_view::scripted_camera_tag::player && !state.suspend_weapons &&
				state.allow_turn && state.allow_movement && !state.retain_weapon && !state.hide_body_arms,
				"Price knockdown, falling rock and rock removal own rotation only until the native rig unlinks");
		check(!gulag::same_entity(0,0) && !gulag::same_entity(35371,35372),"empty or recycled story anchors cannot match");
		for(const auto state:{gulag::cinematic({.alive = true, .linked = true, .rig = "worldbody", .price_anchor = false, .cafeteria = false, .evacuation_rig = false}),
			gulag::cinematic({.alive = true, .linked = true, .rig = "player_rig", .price_anchor = false, .cafeteria = false, .evacuation_rig = false}),gulag::cinematic({.alive = true, .linked = true, .rig = "h2_active_breacher_rig", .price_anchor = true, .cafeteria = true, .evacuation_rig = true}),
			gulag::cinematic({.alive = false, .linked = true, .rig = "player_rig", .price_anchor = true, .cafeteria = false, .evacuation_rig = false}),gulag::cinematic({.alive = true, .linked = false, .rig = "worldbody", .price_anchor = false, .cafeteria = true, .evacuation_rig = false})})
			check(state.stage==phase::none,"unrelated worldbody, ordinary breach, death and unlink retain existing camera policy");
	}
	{
		const auto helicopter=gulag::classify({.alive = true, .linked = true, .intro_controller = true});
		check(helicopter.scene==scenario::gulag && helicopter.stage==phase::scripted_combat && helicopter.retain_weapon,
			"Gulag intro helicopter retains the final hand on release");
		check(!helicopter.suspend_weapons && helicopter.allow_movement && helicopter.allow_turn &&
			helicopter.camera==vr::game_view::camera_profiles::authored_yaw_roll && !helicopter.hide_body_arms,
			"helicopter aligns once then adds authored yaw/roll with free head movement and weapon switching");
		check(gulag::intro_parent(26799,27230,26799) && gulag::intro_parent(27230,27230,26799),
			"captured remastered rig and legacy camera both retain weapons during helicopter handoff");
		check(!gulag::intro_parent(0,0,0) && !gulag::intro_parent(99,27230,26799),
			"missing identities and unrelated rappel rigs cannot claim the intro");
		check(gulag::intro_tag(26799,27230,26799)==vr::game_view::scripted_camera_tag::player &&
			gulag::intro_tag(27230,27230,26799)==vr::game_view::scripted_camera_tag::aim &&
			gulag::intro_tag(99,27230,26799)==vr::game_view::scripted_camera_tag::none,
			"remastered and legacy helicopter controllers read their actual authored camera tags");
		const auto attached=gulag::evacuation({.alive = true, .linked = true, .rig = "player_rig", .begun = true, .used = true});
		check(attached.camera==scene_cameras::gulag_evacuation && attached.rotation_tag==vr::game_view::scripted_camera_tag::player &&
			!attached.allow_world_use && !attached.independent_hands,
			"accepted attachment uses authored camera/body presentation rather than interactive rope hands");
		check(gulag::evacuation({.alive = true, .linked = true, .rig = "player_rig", .begun = true, .used = false}).stage==phase::none &&
			gulag::evacuation({.alive = true, .linked = false, .rig = "player_rig", .begun = true, .used = true}).stage==phase::none &&
			gulag::evacuation({.alive = true, .linked = true, .rig = "worldbody", .begun = true, .used = true}).stage==phase::none,
			"rope camera requires accepted use plus a linked player rig, never the preceding rock scene");
		check(!gulag::rope_ready({.alive = true, .evacuation_begun = false, .linked = false, .used = false}) && !gulag::rope_ready({.alive = true, .evacuation_begun = true, .linked = true, .used = false}),
			"evac arrival and linked flare/rope animation cannot expose the attachment target");
		check(gulag::rope_ready({.alive = true, .evacuation_begun = true, .linked = false, .used = false}) && !gulag::rope_ready({.alive = true, .evacuation_begun = true, .linked = false, .used = true}) &&
			!gulag::rope_ready({.alive = false, .evacuation_begun = true, .linked = false, .used = false}),"native animation end opens rope use until activation or death");
		check(gulag::rope_target("player_uses_rig","") && gulag::rope_target("","ending_rope1") &&
			gulag::rope_target("","ending_rope") && !gulag::rope_target("player_can_rappel","player_rappel"),
			"rope gating covers the trigger and visual proxies without blocking other rappels");
		check(gulag::retired_rope_target("player_ropes") && !gulag::retired_rope_target("player_uses_rig") &&
			!gulag::retired_rope_target("player_rappels"),
			"captured adjacent legacy rope trigger remains excluded when the remastered target becomes ready");
		// Live time 754899: actor unlinked, all evacuation readiness flags zero.
		// Entity 1766 was player_uses_rig, entity 1978 was retired player_ropes.
		check(!gulag::rope_ready({.alive = true, .evacuation_begun = false, .linked = false, .used = false}) && gulag::retired_rope_target("player_ropes"),
			"both prematurely exposed native rope volumes reject the captured pre-evac frame");
		// Live time 790450: native cursor type=1, entity=1766; weapon flags=0x80.
		const auto ready=gulag::rope_use({.alive = true, .evacuation_begun = true, .linked = false, .used = false});
		check(ready.stage==phase::scripted_use && ready.allow_world_use && ready.independent_hands && !vr::gameplay::scripted_control::permits_weapons(0x80) &&
			ready.camera==vr::game_view::camera_profiles::gameplay && !ready.retain_weapon && !ready.hide_body_arms,
			"captured rope-ready frame grants native world use without granting firearm permission or taking over the camera");
		check(!gulag::rope_use({.alive = true, .evacuation_begun = false, .linked = false, .used = false}).allow_world_use && !gulag::rope_use({.alive = true, .evacuation_begun = true, .linked = true, .used = false}).allow_world_use &&
			!gulag::rope_use({.alive = true, .evacuation_begun = true, .linked = false, .used = true}).allow_world_use && !gulag::rope_use({.alive = false, .evacuation_begun = true, .linked = false, .used = false}).allow_world_use,
			"world-use override is absent before readiness, while linked, after attachment and on death");
		for(const auto state:{gulag::rope_use({.alive = true, .evacuation_begun = false, .linked = false, .used = false}),gulag::rope_use({.alive = true, .evacuation_begun = true, .linked = true, .used = false}),
			gulag::rope_use({.alive = true, .evacuation_begun = true, .linked = false, .used = true}),gulag::rope_use({.alive = false, .evacuation_begun = true, .linked = false, .used = false})})
			check(!state.independent_hands,"tracked rope hands end with readiness and cannot override authored or dead-player arms");
		// Captured Grip reached entity 1766/9 and native +activate, but the
		// G_PlayerUse hook runs outside the scheduler's VM-safe callback scope.
		auto published=ready;published.epoch=23;
		const gulag::use_witness witness{23,9,gulag::use_kind::rope};
		check(gulag::native_use_allowed(witness.current(23,9), published, {.weapon_permission = false, .alive = true, .linked = false}),
			"native use consumes a VM-classified rope witness with guns disabled and no scheduler/VM call");
		check(!gulag::native_use_allowed(witness.current(24,9), published, {.weapon_permission = false, .alive = true, .linked = false}) &&
			!gulag::native_use_allowed(witness.current(23,10), published, {.weapon_permission = false, .alive = true, .linked = false}),
			"checkpoint replacement and recycled entity cannot inherit rope authorization");
		check(!gulag::native_use_allowed(gulag::use_kind::retired, published, {.weapon_permission = true, .alive = true, .linked = false}) &&
			!gulag::native_use_allowed(gulag::use_kind::unknown, published, {.weapon_permission = true, .alive = true, .linked = false}),
			"legacy and unobserved targets stay rejected even when weapons are enabled");
		check(!gulag::native_use_allowed(gulag::use_kind::rope, published, {.weapon_permission = false, .alive = true, .linked = true}) &&
			!gulag::native_use_allowed(gulag::use_kind::rope, published, {.weapon_permission = false, .alive = false, .linked = false}) &&
			!gulag::native_use_allowed(gulag::use_kind::rope, {}, {.weapon_permission = true, .alive = true, .linked = false}),
			"native link/death and lost sequence ownership revoke a retained rope target");
		check(!gulag::native_use_allowed(gulag::use_kind::ordinary, published, {.weapon_permission = false, .alive = true, .linked = false}) &&
			gulag::native_use_allowed(gulag::use_kind::ordinary, {}, {.weapon_permission = true, .alive = true, .linked = false}),
			"rope-specific use permission does not grant ordinary interactions during weapon suppression");
		for (const auto state:{gulag::classify({.alive = false, .linked = true, .intro_controller = true}),gulag::classify({.alive = true, .linked = false, .intro_controller = true}),gulag::classify({.alive = true, .linked = true, .intro_controller = false})})
			check(state.stage==phase::none && !state.retain_weapon,
				"death, touchdown unlink and unrelated rappel/rescue controllers release helicopter retention");
		check(gulag::classify({.alive = true, .linked = true, .intro_controller = true}).retain_weapon,
			"checkpoint entry can retain weapons without a preceding helicopter transition");
	}
	{
		const auto car=favela::classify(true,true,"player_rig");
		check(car.scene==scenario::favela && car.stage==phase::transport &&
			car.camera==vr::game_view::camera_profiles::aligned,
			"takedown passenger rig enables initially aligned unrestricted HMD rotation");
		check(!car.suspend_weapons && car.allow_movement && car.allow_turn,
			"car camera ownership preserves native input and duck command notifications");
		check(favela::classify(true,false,"player_rig").stage==phase::none &&
			favela::classify(false,true,"player_rig").stage==phase::none &&
			favela::classify(true,true,"worldbody").stage==phase::none,
			"exit unlink, death and unrelated rigs cannot retain the car camera");
		vr::game_view::scripted_position position;
		check(position.offset(9,1,{.4f,.2f,.1f},vr::game_view::camera_profiles::aligned)==std::array<float,3>{},"car entry anchors the current physical head");
		check(std::abs(position.offset(9,1,{.6f,.2f,.1f},vr::game_view::camera_profiles::aligned)[0]-.02f)<.0001f,
			"car reuses 10 percent head motion instead of full room-scale displacement");
		check(std::abs(position.offset(9,1,{4.f,.2f,.1f},vr::game_view::camera_profiles::aligned)[0]-.05f)<.0001f,
			"large physical steps remain inside the five-centimetre head envelope");
		check(position.offset(0,1,{.6f,.2f,.1f},vr::game_view::camera_profiles::aligned)==std::array<float,3>{.6f,.2f,.1f},
			"leaving the car restores ordinary head translation");
	}
	evidence e{true,true,true,true};
	check(classify(e)==phase::hookup,"rappel ownership starts with confirmed native link and phase");
	e.descending=true;check(classify(e)==phase::descent,"descent is independently interactive");
	e.at_bottom=true;check(classify(e)==phase::melee,"weapon enable is not body ownership release");
	e.killing=true;check(classify(e)==phase::execution,"kill disables story input but retains native body");
	e.ending=e.was_active=true;check(classify(e)==phase::release,"end flag before unlink retains body");
	e.was_active=false;check(classify(e)==phase::none,"later vehicle links cannot reenter finished rappel");
	e.was_active=true;e.linked=false;check(classify(e)==phase::none,"unlink restores normal controls");
	e.linked=true;e.alive=false;check(classify(e)==phase::none,"death releases scene identity");
	e.alive=true;e.supported=false;check(classify(e)==phase::none,"other linked levels remain untouched");
	using namespace vr::controller_input;
	const auto now=clock::now();frame f{};f.sequence=f.reference_generation=1;f.focused=true;f.sampled_at=now;
	for(int h=0;h<2;++h){f.trigger[h].active=true;f.trigger[h].generation=1;f.grip[h].valid=f.aim[h].valid=true;}
	actions action;
	check(action.consume(f,phase::descent,1,true,now)==0,"neutral sample arms descent");
	f.trigger[0].down=true;++f.trigger[0].presses;
	check(action.consume(f,phase::descent,1,true,now)==1,"either trigger provides native held brake");
	check(action.consume(f,phase::melee,1,true,now)==0,"held brake cannot become assassination");
	f.trigger[0].down=false;action.consume(f,phase::melee,1,true,now);
	f.trigger[0].down=true;++f.trigger[0].presses;
	check(action.consume(f,phase::melee,1,true,now)==4,"fresh trigger requests native melee");
	check(action.consume(f,phase::melee,1,true,now)==0,"held trigger does not repeat melee");
	f.trigger[0].down=false;action.consume(f,phase::melee,1,true,now);
	f.trigger[0].down=true;++f.trigger[0].presses;
	check(action.consume(f,phase::melee,1,true,now)==4,"native rejected early press can be retried");
	check(action.consume(f,phase::execution,1,true,now)==0,"execution cannot issue damage or brake input");
	check(action.consume(f,phase::descent,2,true,now)==0,"checkpoint generation requires release");
	check(action.consume(f,phase::descent,2,false,now)==0,"pause cancels actions");
	check(action.consume(f,phase::descent,2,true,now+std::chrono::seconds(1))==0,"stale tracking cannot control story");
	f.trigger[0].down=false;action.consume(f,phase::descent,2,true,now);
	f.trigger[1].down=true;++f.trigger[1].presses;f.aim[1].valid=false;
	check(action.consume(f,phase::descent,2,true,now)==0,"lost hand tracking does not create brake");
	vr::game_view::scripted_view camera;
	check(camera.compose(0,1,10,90,20)==90,"ordinary camera unchanged");
	check(camera.compose(7,1,11,8,20)==90,"script clamp cannot change established tracking anchor");
	check(camera.compose(7,1,12,-80,60)==90,"free head turn survives animation yaw and rig handoff");
	check(camera.compose(7,2,13,0,0)==150,"recenter retains world viewing direction");
	float yaw{};check(camera.restore_command(0,2,yaw,10,30) && yaw==170,"exit restores native command heading without double yaw");
	camera.record(15);
	check(camera.compose(0,2,14,-40,30)==150,"old prediction retains sequence anchor during exit");
	check(camera.compose(0,2,15,150,30)==150 && !camera.owns_camera(),"matching exit command hands back camera seamlessly");
	check(camera.compose(0,2,16,170,30)==170,"ordinary turns work after exit");
	check(camera.compose(0,3,1,25,0)==25,"backwards checkpoint discards old heading");
	camera.compose(8,3,2,25,60);
	check(camera.restore_command(0,4,yaw,0,0) && yaw==85,"recenter immediately before exit retains final world heading");
}
