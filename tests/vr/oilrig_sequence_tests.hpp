#pragma once
#include "component/vr/gameplay/sequences/oilrig.hpp"
#include "component/vr/gameplay/sequences/breach.hpp"
#include "component/vr/gameplay/sequences/estate.hpp"
#include <limits>

template<class Check> void oilrig_sequence_tests(Check check)
{
	namespace oil=vr::gameplay::sequences::oilrig;
	using vr::gameplay::sequences::phase;
	const auto dragging=vr::gameplay::sequences::estate::classify(true,true,"worldbody",true);
	check(dragging.stage==phase::scripted_combat && !dragging.suspend_weapons && dragging.retain_weapon && dragging.hide_body_arms,"Ghost dragging allows gun control, hides scripted arms and retains weapon on release");
	const auto execution=vr::gameplay::sequences::estate::classify(true,true,"worldbody",false);
	check(execution.camera==vr::game_view::camera_profiles::authored_aligned && execution.rotation_tag==vr::game_view::scripted_camera_tag::player &&
		execution.suspend_weapons && !execution.hide_body_arms,"Shepherd execution aligns to the actual body camera and inherits authored yaw");
	check(vr::gameplay::sequences::estate::classify(false,true,"worldbody",true).stage==phase::none &&
		vr::gameplay::sequences::estate::classify(true,false,"worldbody",true).stage==phase::none,"DSM installation and unlinked gameplay do not inherit ending restrictions");
	oil::evidence e{};e.alive=e.linked=e.attached=true;
	check(oil::classify(e)==phase::transport && !oil::moving(phase::transport),"underwater ride retains native transport and constrained view");
	check(oil::expanded_yaw(3,55,true)==100 && oil::expanded_yaw(4,43,true)==88,"opening yaw range gains forty-five degrees per side");
	check(oil::expanded_yaw(5,5,true)==5 && oil::expanded_yaw(6,20,true)==20,"vertical bounds remain authored");
	check(oil::expanded_yaw(3,55,false)==55,"other scenes and non-VR calls retain native range");
	check(oil::expanded_yaw(3,170,true)==180 && oil::expanded_yaw(3,-1,true)==-1,"native envelope stays bounded and invalid parameters unchanged");
	check(std::isnan(oil::expanded_yaw(3,std::numeric_limits<float>::quiet_NaN(),true)),"invalid native float is not converted to a new permission");
	e.surfaced=true;check(oil::classify(e)==phase::swim && oil::moving(phase::swim) && oil::turning(phase::swim),"surface approach retains controller translation and native gaze");
	e.looking_at_guard=true;check(oil::classify(e)==phase::swim,"gaze alone cannot unlock an early assassination");
	e.swim_done=true;check(oil::classify(e)==phase::melee,"native guard admission allows Trigger while the native HUD owns the sole prompt");
		e.looking_at_guard=false;check(oil::classify(e)==phase::swim,"looking away removes the action");
		e.kill_rig=true;check(oil::classify(e)==phase::execution,"native body handoff owns the camera before the delayed kill flag");e.kill_rig=false;
	e.kill_started=true;check(oil::classify(e)==phase::execution && !oil::moving(phase::execution) && !oil::turning(phase::execution),"kill animation owns body and locomotion");
	e.out_of_water=true;check(oil::classify(e)==phase::none,"climb-out releases scene before later linked gameplay");
	e.out_of_water=false;e.linked=false;check(oil::classify(e)==phase::none,"native unlink releases ownership");
	e={};e.alive=e.linked=e.surfaced=e.swim_done=e.looking_at_guard=true;
	check(oil::classify(e)==phase::melee,"surface checkpoint does not require SDV intro flags");
	e={};e.alive=e.linked=e.leaving_water=true;
	check(oil::classify(e)==phase::none,"rig-start checkpoint sentinel alone cannot capture later animations");
	e.kill_started=e.weapon_recovery=true;
	check(oil::classify(e)==phase::recovery && oil::moving(phase::recovery) && !oil::turning(phase::recovery),"camera stays scripted until weapon recovery without preventing the walk out of water");
	e.weapon_recovery=false;e.out_of_water=true;check(oil::classify(e)==phase::none,"restored weapon permission releases recovery camera");
	for(bool boarded:{false,true})
	{
		const auto ride=oil::evacuation({.alive = true, .parent = 17, .worldbody = 17, .boarded = boarded, .landed = true});
		check(ride.camera==vr::game_view::camera_profiles::aligned && ride.retain_weapon && !ride.suspend_weapons,
			"Oilrig boarding and helicopter ride reuse free look and weapon retention without overriding native disableweapons");
	}
	check(oil::evacuation({.alive = true, .parent = 18, .worldbody = 17, .boarded = false, .landed = true, .boarding_helper = true}).retain_weapon &&
		oil::evacuation({.alive = true, .parent = 18, .worldbody = 17, .boarded = true, .landed = true, .boarding_helper = true}).stage==phase::none,
		"temporary boarding origin is admitted only before the native boarded flag");
	check(oil::evacuation({.alive = false, .parent = 17, .worldbody = 17, .boarded = true, .landed = true}).stage==phase::none &&
		oil::evacuation({.alive = true, .parent = 18, .worldbody = 17, .boarded = true, .landed = true}).stage==phase::none &&
		oil::evacuation({.alive = true, .parent = 17, .worldbody = 17, .boarded = false, .landed = false}).stage==phase::none &&
		oil::evacuation({.alive = true, .parent = 0, .worldbody = 0, .boarded = true, .landed = true}).stage==phase::none,
		"death, unrelated parents, absent bodies and earlier scripted scenes cannot inherit helicopter policy");
	check(oil::permits_equipment(oil::equipment::detonator,"c4") && !oil::permits_equipment(oil::equipment::detonator,"claymore") &&
		!oil::permits_equipment(oil::equipment::claymore,"c4") && oil::permits_equipment(oil::equipment::claymore,"claymore"),
		"native ambush stage selects exactly one abdominal item while preserving both native inventory entries");
	check(oil::permits_equipment(oil::equipment::unrestricted,"claymore") &&
		!oil::permits_equipment(oil::equipment::unavailable,"c4") && oil::permits_equipment(oil::equipment::unavailable,"m4m203_reflex"),
		"Oilrig equipment policy leaves other maps and firearms intact and fails closed on missing mission evidence");
	check(oil::can_detonate({.planted = true, .triggered = false, .native_c4 = true, .weapons_enabled = true, .empty_feed = true}) && !oil::can_detonate({.planted = false, .triggered = false, .native_c4 = true, .weapons_enabled = true, .empty_feed = true}) &&
		!oil::can_detonate({.planted = true, .triggered = true, .native_c4 = true, .weapons_enabled = true, .empty_feed = true}) && !oil::can_detonate({.planted = true, .triggered = false, .native_c4 = false, .weapons_enabled = true, .empty_feed = true}) &&
		!oil::can_detonate({.planted = true, .triggered = false, .native_c4 = true, .weapons_enabled = false, .empty_feed = true}) && !oil::can_detonate({.planted = true, .triggered = false, .native_c4 = true, .weapons_enabled = true, .empty_feed = false}),
		"Oilrig remote requires native planting completion, selected zero-clip C4, weapon permission and an unconsumed ambush");
	namespace breach=vr::gameplay::sequences::breach;
	check(breach::classify(true,true,"h2_active_breacher_rig",false)==phase::breach_plant,"planting owns body even when native weapon-disable bit is clear");
	check(breach::classify(true,true,"h2_active_breacher_rig",true)==phase::breach_combat,"slowmo permits weapons before native unlink");
	check(breach::classify(true,true,"player_rig",true)==phase::none && breach::classify(true,false,"h2_active_breacher_rig",true)==phase::none,"breach does not claim unrelated or detached rigs");
	for(const auto rig:{"h2_active_breacher_rig","active_breacher_rig","passive_breacher_rig"})
	{
		const auto plant=breach::presentation(breach::classify(true,true,rig,false));
		const auto combat=breach::presentation(breach::classify(true,true,rig,true));
		check(plant.suspend_weapons && !plant.allow_movement && !plant.allow_turn,"shared native breach rig owns hands and weapons while planting");
		check(!combat.suspend_weapons && combat.allow_movement && combat.allow_turn,"linked slowmo combat releases all normal weapon manipulation");
		check(plant.camera==vr::game_view::camera_profiles::yaw && combat.camera==plant.camera,
			"shared breach rig uses yaw-only animation throughout linked planting and combat");
		check(breach::presentation(breach::classify(false,true,rig,true)).stage==phase::none,"death releases shared breach ownership");
	}
	check(breach::presentation(phase::execution).stage==phase::none,"unrelated execution cannot become a breach");

	using namespace vr::controller_input;
	frame input{};input.sequence=input.reference_generation=1;input.focused=true;const auto now=clock::now();input.sampled_at=now;
	input.aim[0].valid=input.grip[0].valid=input.trigger[0].active=true;input.trigger[0].generation=1;
	vr::gameplay::sequences::actions actions;
	actions.consume(input,phase::swim,1,true,now);input.trigger[0].down=true;++input.trigger[0].presses;
	check(actions.consume(input,phase::swim,1,true,now)==0,"pre-held trigger cannot request melee without native guard admission");
	check(actions.consume(input,phase::melee,1,true,now)==0,"entering guard cone while holding requires release");
	input.trigger[0].down=false;actions.consume(input,phase::melee,1,true,now);
	input.trigger[0].down=true;++input.trigger[0].presses;
	check(actions.consume(input,phase::melee,1,true,now)==4,"fresh admitted trigger submits meleebuttonpressed input bit");
	check(actions.consume(input,phase::swim,1,true,now)==0,"leaving guard cone clears pending action");
	{
		using namespace vr::gameplay::sequences;
		// Simulate 90 Hz command delivery and every possible millisecond phase
		// of the native 50 ms GSC poll. A between-frame tap must survive polling.
		unsigned missed_edges{};
		for(int offset=0;offset<50;++offset)
		{
			vr::gameplay::sequences::actions edge,polled;frame f=input;f.trigger[0].down=false;f.trigger[0].presses=0;
			std::array<int,200> old_bits{},new_bits{};
			int old_latest{},new_latest{};
			for(int ms=0;ms<200;++ms)
			{
				if(ms%11==0)
				{
					f.sampled_at=now+std::chrono::milliseconds(ms);++f.sequence;
					if(ms==11)++f.trigger[0].presses; // tap already released
					old_latest=edge.consume(f,phase::melee,1,true,f.sampled_at);
					new_latest=polled.consume(f,phase::melee,1,true,f.sampled_at,melee_delivery::polled);
				}
				old_bits[ms]=old_latest;new_bits[ms]=new_latest;
			}
			bool old_seen{},new_seen{};
			for(int ms=offset;ms<200;ms+=50){old_seen|=(old_bits[ms]&4)!=0;new_seen|=(new_bits[ms]&4)!=0;}
			if(!old_seen)++missed_edges;
			check(new_seen,"polled short-tap delivery survives all script polling phases");
			check(new_bits[199]==0,"short-tap latch expires without repeating indefinitely");
		}
		check(missed_edges>0,"regression reproduces native script missing a one-command edge");
		vr::gameplay::sequences::actions held;auto f=input;f.trigger[0].down=false;f.trigger[0].presses=0;f.sampled_at=now;
		held.consume(f,phase::melee,1,true,now,melee_delivery::polled);
		f.trigger[0].down=true;++f.trigger[0].presses;
		check(held.consume(f,phase::melee,1,true,now,melee_delivery::polled)==4,"polled action begins on deliberate press");
		for(int ms=50;ms<=500;ms+=50){f.sampled_at=now+std::chrono::milliseconds(ms);
			check(held.consume(f,phase::melee,1,true,f.sampled_at,melee_delivery::polled)==4,"physical hold retains native held-button meaning beyond tap floor");}
		check(held.consume(f,phase::execution,1,true,f.sampled_at,melee_delivery::polled)==0,"native acceptance phase cancels held melee immediately");
		check(held.consume(f,phase::melee,1,true,f.sampled_at,melee_delivery::polled)==0,"still-held input cannot rearm after phase handoff");
		for(int loss=0;loss<6;++loss)
		{
			vr::gameplay::sequences::actions pending;f=input;f.trigger[0].down=false;f.trigger[0].presses=0;f.sampled_at=now;
			pending.consume(f,phase::melee,1,true,now,melee_delivery::polled);++f.trigger[0].presses;
			pending.consume(f,phase::melee,1,true,now,melee_delivery::polled);
			if(loss==0)f.focused=false;if(loss==1)f.aim[0].valid=false;
			if(loss==2)++f.reference_generation;if(loss==3)++f.trigger[0].generation;
			if(loss==4)f.trigger[0].active=false;
			check(pending.consume(f,phase::melee,loss==5 ? 2 : 1,true,now,melee_delivery::polled)==0,
				"focus, tracking, reference, action or checkpoint change discards pending polled input");
		}
	}
}
