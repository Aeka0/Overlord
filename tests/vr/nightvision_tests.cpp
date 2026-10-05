#include "component/vr/hand.hpp"
using vr::hand;
#include "component/vr/gameplay/nightvision.hpp"
#include "component/vr/gameplay/empty_hand_pose.hpp"
#include <iostream>
#include <limits>

namespace nv=vr::gameplay::equipment::nightvision;
namespace hi=vr::gameplay::hand_interaction;
namespace hands=vr::gameplay::hands;
using hi::hand;using hi::button;
int main()
{
	int checks{},failures{};const auto check=[&](bool value,const char* why){++checks;if(!value){++failures;std::cerr<<why<<'\n';}};
	{
		const auto time=nv::timing(.1f,.2f);
		check(time && time->duration(true)==300 && time->duration(false)==100,"device timing comes from native global fades, not a weapon definition");
		nv::device_transition device;
		check(device.begin(1,1000,1000,true,*time),"accepted empty-hand toggle starts a device transition");
		const auto serial=device.serial();
		check(device.claim_foley(serial) && !device.claim_foley(serial),"native and empty-hand equipment foley share one independent claim");
		const auto start=device.render(1,1000,true);
		check(start.owned && !start.vision && start.visibility==1,"power-on starts fading immediately without an arm-animation prelude");
		check(!device.claim_sound(serial,1000) && !device.claim_sound(serial,1100),"power audio cannot precede a rendered black frame even if its timer is due");
		const auto dark=device.render(1,1100,true);
		check(dark.owned && dark.vision && dark.visibility==0,"native night vision changes at the opaque transition boundary");
		check(device.claim_sound(serial,1100) && !device.claim_sound(serial,1100),"blackout dispatches exactly one electrical cue independently of foley");
		const auto power=device.sample(1,1200,true),done=device.sample(1,1300,true);
		check(power.owned && power.vision && std::abs(power.visibility-.5f)<.001f && done.visibility==1,
			"native power-on duration fades from black back to the world");
		check(!device.begin(1,1000,1000,true,*time) && !device.begin(1,999,999,true,*time) && device.serial()==serial,
			"prediction replay cannot restart the transition or queue a duplicate sound");
		check(!device.sample(1,950,true).owned && !device.expired(1,950) && device.sample(1,1200,true).owned,
			"temporary prediction rewind cannot cancel the final frame's presentation");
		check(!device.sample(2,1200,true).owned && !device.sample(1,1200,false).owned,
			"timeline replacement or external native state cannot inherit old feedback");
		check(device.expired(1,1551) && !device.sample(1,1551,true).owned,"completed device feedback hands back to stable native rendering");
		check(device.begin(1,1600,1600,false,*time) && device.serial()>serial,"removal owns a new sound and visual identity");
		check(!device.claim_sound(device.serial(),1600),"power-off also waits for its rendered black frame");
		device.render(1,1600,false);
		check(!device.claim_sound(serial,1600) && device.claim_sound(device.serial(),1600),"late on-sound cannot consume the new off-sound request");
		check(!device.claim_foley(serial) && device.claim_foley(device.serial()),"stale wear foley cannot consume removal's equipment sound");
		check(!device.sample(1,1600,false).vision && device.sample(1,1600,false).visibility==0 &&
			std::abs(device.sample(1,1650,false).visibility-.5f)<.001f && device.sample(1,1700,false).visibility==1,
			"removal begins at native power-down and fades straight back without a hand-animation wait");
		const auto previous=device.serial();device.reset();
		check(!device.sample(1,1650,false).owned && device.begin(2,100,100,true,*time) && device.serial()>previous,
			"level/checkpoint reset permits an earlier clock without reusing queued sound identity");
		check(device.begin(2,110,110,false,*time) && !device.sample(2,120,false).vision,
			"rapid accepted reversal supersedes the previous goggles transition");
		const auto zero=nv::timing(0,0);nv::device_transition instant;
		check(zero && instant.begin(1,1,1,true,*zero) && instant.render(1,1,true).visibility==0 &&
			instant.claim_sound(instant.serial(),1) && instant.sample(1,2,true).visibility==1,
			"explicit zero fades still have one audio blackout without division by zero");
		nv::device_transition skipped;skipped.begin(1,1000,1000,true,*time);
		check(skipped.render(1,1090,true).visibility>0 && skipped.render(1,1120,true).visibility==0,
			"a frame stepping over the fade boundary still renders full black");
		check(skipped.sample(1,1140,true).visibility==0 && skipped.claim_sound(skipped.serial(),1140) &&
			skipped.render(1,1140,true).visibility==0 && std::abs(skipped.sample(1,1240,true).visibility-.5f)<.001f,
			"black holds through main-thread audio dispatch and fade resumes from that game-clock instant");
		nv::device_transition missing;missing.begin(1,1000,1000,true,*time);missing.render(1,1110,true);
		check(!missing.claim_sound(missing.serial(),1351) && missing.sample(1,1450,true).visibility==.5f && missing.expired(1,1551),
			"missing audio acknowledgement releases black within a bounded interval and cannot play a stale cue");
		check(!nv::timing(-1,.2f) && !nv::timing(.1f,11) && !nv::timing(std::numeric_limits<float>::quiet_NaN(),.2f),
			"malformed native duration values cannot create a stuck equipment transition");
	}
	std::array<std::byte,0x1fb0> bytes{};unsigned type=3,flags=0x40;
	std::memcpy(bytes.data()+0x1fa4,&type,4);std::memcpy(bytes.data()+0x3c0,&flags,4);
	check(nv::decode(bytes).slot==1 && nv::decode(bytes).on,"native type-3 slot and forced-nightvision flag decode");
	check(!nv::decode(std::span(bytes).first(0x1faf)),"truncated native slot block is rejected");
	std::memcpy(bytes.data()+0x1fa0,&type,4);check(!nv::decode(bytes),"ambiguous duplicated nightvision slots cannot select a command");
	check(!nv::decode(std::array<unsigned,4>{0,1,2,0},0),"weapon and alternate slots do not grant nightvision");
	check(nv::acquisition(false,{.20f,0,.20f}) && nv::acquisition(true,{.20f,0,0}),"upper and eye-level zones select down/up gestures");
	check(!nv::acquisition(false,{.20f,0,0}) && !nv::acquisition(true,{.20f,0,.20f}) &&
		!nv::acquisition(false,{-.2f,0,.2f}) && !nv::acquisition(false,{.2f,.4f,.2f}),"wrong height, behind head and outside face are rejected");
	// Confirmed bilateral forehead gestures: cosmetic wrists all missed the
	// old height gate while raw controller grips reached the stowed goggles.
	for(const auto grip:{hands::vec{.069f,.092f,.152f},hands::vec{.092f,-.098f,.140f},
		hands::vec{-.071f,.160f,.111f},hands::vec{-.065f,-.195f,.064f}})
		check(nv::acquisition(false,grip),"physical forehead and top-side grips can start the lowering gesture");
	check(!nv::acquisition(false,{-.11f,0,.15f}) && !nv::acquisition(false,{0,.29f,.15f}) &&
		!nv::acquisition(false,{.2f,0,-.08f}),"rear holster, outside head width and low grasps cannot start lowering");
	check(!nv::acquisition(true,{-.06f,0,.05f}),"raising still begins in front of the eyes, not behind them");
	vr::head_pose_bridge::spatial_frame body;body.units_per_meter=100;body.head_position={10,20,30};body.head_forward={0,1,0};body.head_up={0,0,1};
	const auto local=nv::head_local(body,{10,40,50});
	check(local && std::abs((*local)[0]-.2f)<.0001f && std::abs((*local)[2]-.2f)<.0001f,"gesture regions use center-eye coordinates and native world scale");
	body.head_up={1,0,0};const auto rolled=nv::head_local(body,{30,40,30});
	check(rolled && std::abs((*rolled)[2]-.2f)<.0001f,"head roll rotates the upper grasp area with the headset");
	body.units_per_meter=0;check(!nv::head_local(body,{0,0,0}),"invalid world scale fails closed");
	for(auto actor:{hand::left,hand::right})for(auto source:{button::trigger,button::grip})for(bool on:{false,true})
	{
		vr::controller_input::frame f;f.focused=true;f.sequence=f.reference_generation=f.continuity_generation=1;
		f.sampled_at=hi::clock::time_point{}+std::chrono::seconds(1);
		nv::drag drag;const vr::gameplay::hands::vec start{.2f,0,on?0.f:.2f},partial{.2f,0,.1f},end{.2f,0,on?.2f:0.f};
		check(drag.begin(actor,source,on,start,f),"either hand/button starts the correct directional grasp");
		f.sampled_at+=std::chrono::milliseconds(30);
		check(!drag.update(partial,on,true,true,f) && drag.active(),"small displacement does not toggle");
		f.sampled_at+=std::chrono::milliseconds(30);
		check(drag.update(end,on,true,true,f) && !drag.active(),"complete twelve-centimetre drag toggles once");
		check(!drag.update(start,on,true,true,f),"holding or reversing after completion cannot toggle again");
		for(int interruption=0;interruption<8;++interruption)
		{
			auto sample=f;nv::drag cancelled;cancelled.begin(actor,source,on,start,sample);
			if(interruption==0)++sample.reference_generation;
			if(interruption==1)++sample.continuity_generation;
			if(interruption==2)sample.focused=false;
			if(interruption==3)sample.orientation_settling=true;
			if(interruption==4)sample.sampled_at+=std::chrono::seconds(3);
			check(!cancelled.update(end,interruption==5?!on:on,interruption!=6,interruption!=7,sample) && !cancelled.active(),
				"recenter, stale tracking, pause, settling, timeout, external toggle, release and lost ownership cancel the drag");
		}
		nv::drag bad;bad.begin(actor,source,on,start,f);
		check(!bad.update({std::numeric_limits<float>::quiet_NaN(),0,0},on,true,true,f) && !bad.active(),"nonfinite hand input cannot toggle");
	}
	for(auto actor:{hand::left,hand::right})
	{
		vr::controller_input::frame f;f.focused=true;f.reference_generation=f.continuity_generation=1;
		f.sampled_at=hi::clock::time_point{}+std::chrono::seconds(1);
		nv::drag drag;
		check(drag.begin(actor,button::grip,false,{-.07f,.16f,.11f},f),"top-of-head grip starts without cosmetic wrist offsets");
		f.sampled_at+=std::chrono::milliseconds(100);
		check(!drag.update({-.04f,.16f,.06f},false,true,true,f) && drag.active(),"forehead reach alone cannot toggle goggles");
		f.sampled_at+=std::chrono::milliseconds(100);
		check(drag.update({.03f,.16f,-.02f},false,true,true,f),"top-to-eye lowering completes the same twelve-centimetre stroke");
		check(drag.begin(actor,button::trigger,false,{.1f,.1f,.08f},f),"lower forehead grasp can start lowering");
		f.sampled_at+=std::chrono::milliseconds(100);
		check(!drag.update({.1f,.1f,.24f},false,true,true,f),"upward motion cannot turn nightvision on");
	}
	const hi::grasp head{{hi::domain::gesture,{0,1},0,0},hi::role::part,button::trigger,hi::recipe::single,hi::capability::action};
	check(bool(head.destination),"head equipment identity does not impersonate a native firearm");
	hi::arbiter arbiter;arbiter.begin(1,1);
	for(auto actor:{hand::left,hand::right})arbiter.offer({actor,head,1,10,0,1,true,true});
	int commits{};arbiter.resolve([&](const auto&){++commits;return true;});
	check(commits==1,"two simultaneous hands can acquire only one pair of goggles");
	check(nv::command_hand_free(arbiter.sessions(),hand::left,1) && !nv::command_hand_free(arbiter.sessions(),hand::left,2),
		"main dispatch accepts its own previous-frame grasp but rejects an old tracking reference");
	check(!hi::compose_pose(hand::left,arbiter.sessions()).melee,"dragging goggles cannot issue a melee hit");
	for(auto provider:{hi::domain::carry,hi::domain::magazine,hi::domain::knife,hi::domain::grenade,hi::domain::world})
	{
		hi::arbiter occupied;occupied.begin(1,1);
		occupied.observe(hand::left,{{provider,{53,7},0,0},hi::role::control,button::grip,hi::recipe::single,{}});
		occupied.offer({hand::left,head,1,10,0,1,true,true});int accepted{};
		occupied.resolve([&](const auto&){++accepted;return true;});check(!accepted,"held objects exclude goggle gestures");
		check(!nv::command_hand_free(occupied.sessions(),hand::left,1),"new ownership cancels a queued native toggle");
	}
	vr::controller_input::frame input;input.focused=true;input.sequence=input.reference_generation=1;input.sampled_at=hi::clock::now();
	input.grip[0].valid=true;input.trigger[0].active=true;input.trigger[0].down=true;
	vr::gameplay::hands::empty_hand::controller pose;hi::pose_plan plan;plan.driver=head.destination;
	check(pose.update(input,hand::left,plan,false).enabled,"goggle reservation retains tracked empty-hand pinch presentation");
	std::cout<<"nightvision: "<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
