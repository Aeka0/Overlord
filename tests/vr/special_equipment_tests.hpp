#pragma once
#include "component/vr/gameplay/special_equipment_policy.hpp"
#include "component/vr/gameplay/notebook_policy.hpp"
#include "component/vr/gameplay/notebook_profile.hpp"
#include "component/vr/digital_button_gate.hpp"
#include "component/vr/gameplay/designator_event_policy.hpp"
#include "component/vr/gameplay/hand_interaction/core.hpp"
namespace special_equipment_tests
{
	template<class Check>void run(Check& check)
	{
		using namespace vr::gameplay;using namespace equipment::special;using vr::hand;
		{
			using namespace designator_events;
			std::array<unsigned char,10> locks{0x70,0xa9,0xd5,0x82,0x6a,0x70,0xa9,0xd5,0x82,0x6a};
			check(pickup_lock(std::as_bytes(std::span(locks).first(5)))==0 && !pickup_lock(std::as_bytes(std::span(locks))),"pickup restriction bypass requires one complete unique native call");
			activation_state activation{true,true,true,false,true,activation_phase::opening};
			check(!activation.ready() && activation.orphaned(true,false),"enabled laser with empty native selection and switch lock identifies interrupted startup");
			check(!activation.orphaned(false,false) && !activation.orphaned(true,true),"normal requested transitions and reserved physical draws are not abandoned");
			activation.selected=true;check(!activation.ready(),"native selection alone cannot acknowledge a still-locked mission draw");
			activation.locked=false;check(activation.ready(),"handoff waits for actual weapon and original switch-unlock completion");
			activation.enabled=false;check(!activation.ready() && !activation.orphaned(true,false),"mission-disabled equipment is never force-enabled by recovery");
			activation={true,true,true,false,true,activation_phase::closing};
			check(!activation.orphaned(true,false) && !activation.ready(),"closing and opening with identical native flags require opposite recovery and cannot be conflated");
			// Captured control-flow opcodes, with two local GetString relocations.
			std::array<unsigned char,59> flow{0x53,0x84,0xc5,0,0,0x70,0x86,0x4d,0xaa,0x20,0x83,0x6a,
				0x70,0xa9,0x1c,0x83,0x53,0x14,0xb4,0,0,0x90,0xb0,4,0,0x64,0x92,0x11,0,
				0xaa,0x20,0x83,0x6a,0x70,0xa9,0x1c,0x83,0x53,0x14,0xb4,0,0,0x28,0xb0,4,0,0x64,0x92,0x11,0,0x2b,0xcd,0xcc,0x4c,0x3d,0x7a,0x92,59,0};
			const auto sites=transition_layout(std::as_bytes(std::span(flow)),0xb414,0xc584);
			check(sites && sites->closing==12 && sites->opening==33 && sites->closing+17==29 && sites->switches[0]==8,
				"closing wait resumes at its existing cleanup and switch operands are independently identified");
			flow[21]=0x28;check(!transition_layout(std::as_bytes(std::span(flow)),0xb414,0xc584),"duplicate opening loops cannot be mistaken for a closing continuation");
			// Native maps/arcadia_code bytecode at0x656, captured before modification.
			std::array<unsigned char,26> animation_wait{0x2b,0xcd,0xcc,0xcc,0x3d,0x7a,0x70,0xa9,0x9b,0x85,0x3f,0xe8,0x03,0x3d,0x2b,0xcd,0xcc,0xcc,0x3d,0x7b,0x7a,0x70,0xa9,0x2d,0x83,0x6a};
			check(startup_wait(std::as_bytes(std::span(animation_wait)))==0 && animation_wait[21]==0x70 && animation_wait[23]==0x2d,
				"instant startup skips stack-neutral animation waits but resumes at native weapon unlock");
			animation_wait[23]=0x2c;check(!startup_wait(std::as_bytes(std::span(animation_wait))),"changed native startup cannot bypass an unverified unlock boundary");
			cooldown charge;check(charge.accept(100) && !charge.accept(3099) && charge.accept(3100),"designator accepts repeated use only after three seconds of game time");
			std::array<std::byte,8> close_code{std::byte{0x53},{},{},{},{},std::byte{0x41},std::byte{0x69},std::byte{0x34}};
			unsigned close_id=55;std::memcpy(close_code.data()+1,&close_id,4);check(notify_operand(close_code,close_id)==1,"only the validated automatic-close notification is redirected");
			for(const auto& sequence:std::array<std::array<std::string_view,3>,2>{{{{"m4","usp_laserdesignator","usp"}},{{"usp_laserdesignator","m4","usp"}}}})
			{unsigned confirmations{};for(auto weapon:sequence)confirmations+=forward_confirmation(true,weapon);check(confirmations==1,"simultaneous firearm/device events confirm only the real designator in either order");}
			check(!forward_confirmation(true,"usp_laserdesignator_fake") && !forward_confirmation(true,{}) && forward_confirmation(false,"m4"),"event source filtering is exact and preserves flat-game notification behavior");
			std::array<std::byte,16> code{};code[0]=std::byte{0x53};code[5]=std::byte{0x41};code[6]=std::byte{0x86};code[7]=std::byte{0x4d};
			unsigned fired=6124;std::memcpy(code.data()+1,&fired,4);
			check(wait_operand(code,fired)==1 && !wait_operand(std::span(code).first(7),fired),"waiter patch validates complete captured instruction sequence and operand bounds");
			std::copy_n(code.begin(),8,code.begin()+8);check(!wait_operand(code,fired),"ambiguous multiple waiters are never patched arbitrarily");
		}
		std::array<std::byte,0x1fc0> bytes{};unsigned type=1,token=36;
		std::memcpy(bytes.data()+0x1fa0+12,&type,4);std::memcpy(bytes.data()+0x1fb0+12,&token,4);
		const auto slots=weapon_slots(bytes);
		check(slots[3].weapon==36 && slots[3].index==3 && !slots[0],"native fourth action slot selects extra equipment independently of offhand slots");
		check(!weapon_slots(std::span(bytes).first(0x1fbf))[3],"truncated action slot block rejected");
		type=2;std::memcpy(bytes.data()+0x1fac,&type,4);check(!weapon_slots(bytes)[3],"native switch-weapon action is not a physical item");
		type=1;token=512;std::memcpy(bytes.data()+0x1fac,&type,4);std::memcpy(bytes.data()+0x1fbc,&token,4);check(!weapon_slots(bytes)[3],"invalid weapon token cannot enter abdominal slot");
		grasp_state s;check(s.take(hand::left) && !s.take(hand::right) && s.join(hand::right),"one claymore supports two authored grips without duplicate acquisition");
		check(!s.trigger(true,true,true,false,true) && !s.preview,"held trigger at pickup cannot begin placement");
		s.trigger(true,false,false,true,true);s.trigger(true,true,true,false,true);
		check(s.preview && !s.trigger(true,true,false,false,true),"holding trigger only previews and never commits");
		check(s.trigger(true,false,false,true,true) && !s.trigger(true,false,false,true,true),"release commits exactly once");
		s.trigger(true,true,true,false,true);check(!s.trigger(true,false,false,true,false) && !s.preview,"invalid ground at release cancels placement");
		s.trigger(true,true,true,false,true);s.release(1u<<unsigned(hand::left));
		check(s.primary==hand::right && s.support==hand::none && !s.preview && !s.trigger_armed,"primary release promotes support and cancels old trigger preview");
		s.release(1u<<unsigned(hand::right));check(!s.held(),"last hand release returns unplaced equipment");
		vr::head_pose_bridge::spatial_frame body;body.units_per_meter=40;body.head_position={100,200,300};body.head_yaw_axis={vr::gameplay::hands::vec{1,0,0},vr::gameplay::hands::vec{0,1,0},vr::gameplay::hands::vec{0,0,1}};
		const auto chest=equipment::locate_chest(body);const auto place=abdomen(body);
		{
			const auto notebook_slot=notebook::stowed_root(body);
			check(hands::length(hands::sub(hands::sub(notebook_slot.position,place.position),{1.2f,0,0}))<.0001f &&
				hands::dot(hands::rotate(notebook_slot.rotation,{0,0,-1}),body.head_yaw_axis[0])<-.999f &&
				hands::dot(hands::rotate(notebook_slot.rotation,{-1,0,0}),body.head_yaw_axis[2])>.999f,
				"stowed notebook clears the knife by moving three centimetres outward, with its back toward the torso and lip up");
			auto turned=body;turned.body.valid=true;turned.body.position=body.head_position;turned.body.yaw_axis=body.head_yaw_axis;
			turned.head_yaw_axis={hands::vec{0,1,0},hands::vec{-1,0,0},hands::vec{0,0,1}};
			check(hands::length(hands::sub(hands::rotate(notebook::stowed_root(turned).rotation,{0,0,-1}),{-1,0,0}))<.0001f,
				"notebook stow orientation follows torso heading independently of a sideways glance");
		}
		for(unsigned h=0;h<2;++h)
		{
			const vr::gameplay::hands::quat identity{0,0,0,1};
			check(abdominal_grab_distance(body,{place.position,identity},identity,identity,h)==0,"shared abdominal interaction is centered on the displayed item for both hands");
			check(abdominal_grab_distance(body,{vr::gameplay::hands::add(place.position,{0,7,0}),identity},identity,identity,h)<=1,"rounded abdominal volume admits contact across the item width");
			check(abdominal_grab_distance(body,{vr::gameplay::hands::add(place.position,{25,0,0}),identity},identity,identity,h)>1,"abdominal reach does not acquire ordinary forward hand motion");
		}
		const auto display=abdominal_display(body,true);const auto front=vr::gameplay::hands::rotate(display.rotation,{1,0,0}),left=vr::gameplay::hands::rotate(display.rotation,{0,1,0});
		check(std::abs(front[2]+.422618262f)<1e-5f && left[0]<-.999f,"designator left face points toward torso with muzzle 25 degrees down");
		{
			auto turned=body;turned.body.valid=true;turned.body.position=body.head_position;turned.body.yaw_axis=body.head_yaw_axis;
			turned.head_yaw_axis={vr::gameplay::hands::vec{0,1,0},vr::gameplay::hands::vec{-1,0,0},vr::gameplay::hands::vec{0,0,1}};
			const auto stable=abdominal_display(turned,true);
			check(vr::gameplay::hands::length(vr::gameplay::hands::sub(vr::gameplay::hands::rotate(stable.rotation,{1,0,0}),front))<1e-5f,"abdominal designator follows the shared chest body frame when the head turns");
		}
		check(chest.valid && std::abs(place.position[2]-(chest.anchors[1].position[2]-9.6f))<1e-4f && place.position[1]==chest.anchors[1].position[1],"abdominal slot stays centered 24 cm below chest without changing chest positions");
		check(std::abs(ray_box({0,0,0},{1,0,0},{10,0,0},{1,1,1},20)-9)<1e-5f,"free-hand recovery ray intersects native linked mine bounds");
		check(!std::isfinite(ray_box({0,0,0},{1,0,0},{10,3,0},{1,1,1},20)) &&
			!std::isfinite(ray_box({0,0,0},{1,0,0},{10,0,0},{1,1,1},8)),"recovery rejects off-axis and out-of-reach mines");
		namespace hi=vr::gameplay::hand_interaction;hi::arbiter arbiter;arbiter.begin(1,1);
		const hi::target mine{hi::domain::special,{1300,15},2,0};
		for(auto actor:{hand::left,hand::right})arbiter.offer({actor,{mine,hi::role::world,hi::button::grip,hi::recipe::single,{}},1,15,1,1,true,true});
		int accepted{};arbiter.resolve([&](const auto&){++accepted;return true;});
		check(accepted==1,"two free hands cannot recover and credit the same mine twice");
		{
			using namespace notebook;
			for(float angle:{0.f,45.f,max_angle})
			{
				const auto top=lip(angle),middle=hands::scale(hands::add(hinge,top),.5f);
				for(const auto point:{top,hinge,hands::add(middle,{0,-7,0}),hands::add(middle,{0,7.2f,0})})
					check(screen_edge_distance(point,angle)<.001f,"all four native screen edges admit a lid grasp at every opening angle");
				check(screen_edge_distance(hands::add(top,{0,30,0}),angle)>20,"screen perimeter never admits a distant sideways hand");
			}
			{
				hinge_drag drag;
				check(drag.begin(0,{hinge,{0,0,0,1}}),"a grasp at the hinge edge no longer fails for lack of a position lever");
				check(std::abs(drag.sample(drag.constrained(45))-45)<.01f,"hinge-edge wrist rotation supplies a stable virtual screen lever");
			}
			check(max_angle>106.5f && max_angle<106.7f,"screen uses the captured native fully-open endpoint, not the old 100 degree clamp");
			check(needs_empty_return(72,0,72,72),"completed UAV still selected natively must restore physical empty hands before another action-slot use");
			check(!needs_empty_return(72,39,72,72) && !needs_empty_return(72,0,39,72) &&
				!needs_empty_return(72,0,72,39) && !needs_empty_return(72,0,0,0) && !needs_empty_return(0,0,0,0),
				"native empty return never overrides held guns, another script selection, an in-flight return or an empty device identity");
			check(!camera_settled(10000,-1) && !camera_settled(11999,11000) && camera_settled(12000,11000) && !camera_settled(1000,11000),
				"docking waits for a native camera acknowledgement and its game-time settling interval, including pause/rewind safety");
			{
				hinge_drag drag;drag.begin(0,{lip(0),{0,0,0,1}});
				check(std::abs(drag.sample(lip(.1f))-.1f)<.001f && std::abs(drag.sample(lip(.2f))-.2f)<.001f,"sub-degree lid motion follows continuous hinge travel");
				check(std::abs(drag.sample(lip(max_angle+10))-max_angle)<.001f && std::abs(drag.sample(lip(max_angle+5))-max_angle)<.001f &&
					std::abs(drag.sample(lip(max_angle-5))-(max_angle-5))<.001f,"notebook reuses sensor hard-stop overtravel without premature reversal");
			}
			for(auto holder:{hand::left,hand::right})for(const bool trigger:{false,true})
			{
				session laptop;const auto off=hand(1-int(holder));
				check(laptop.take(holder) && laptop.physical() && !laptop.requested(),"taking a closed notebook never activates AGM");
				check(!laptop.open(90) && !laptop.release_lid(true),"opening and releasing require an actual lid grip");
				check(laptop.take_lid(off,trigger,{lip(0),{0,0,0,1}}) && laptop.opener_trigger==trigger &&
					!laptop.take_lid(holder,!trigger,{lip(0),{0,0,0,1}}),"both hands admit either lid button and retain the button that owns the grasp");
				check(std::abs(laptop.drag.sample(lip(60))-60)<.01f,"hinge follows angular travel rather than hand distance");
				check(vr::gameplay::hands::length(vr::gameplay::hands::sub(laptop.drag.constrained(60).position,lip(60)))<.01f,"constrained hand follows the screen arc");
				check(laptop.open(65) && !laptop.requested() && !laptop.release_lid(true),"old trigger angle and early release cannot start AGM");
				laptop.opener=off;laptop.drag.begin(laptop.angle,{lip(laptop.angle),{0,0,0,1}});laptop.open(max_angle);
				check(!laptop.requested() && !laptop.release_lid(false),"the hard stop alone or loss of tracking cannot commit an opening");
				laptop.opener=off;laptop.drag.begin(laptop.angle,{lip(laptop.angle),{0,0,0,1}});
				check(laptop.release_lid(true) && !laptop.release_lid(true) && laptop.requested() && laptop.held(),"full opening plus explicit lid release requests once while retaining the visible case");
				const auto control_revision=laptop.control_revision;
				laptop.observe(true,false,false,false);laptop.release();
				check(laptop.stage==phase::pending && laptop.held() && !laptop.docked,"activation and native preparation do not prematurely dock or cancel the case");
				laptop.observe(true,true,false,false);
				check(laptop.stage==phase::remote && laptop.held() && laptop.controller==off,"camera acknowledgement admits native controls before the visual handoff finishes");
				vr::controller_input::digital_button_gate fire;
				vr::controller_input::digital_action key{true,false,0,1,0};fire.consume(key);key.down=true;++key.presses;
				check(fire.consume(key),"launch/boost input can be active while the camera settles");
				laptop.observe(true,true,true,false);laptop.release();
				check(laptop.docked && !laptop.held() && laptop.holder==hand::none && laptop.angle==0 && laptop.requested() && laptop.controller==off &&
					laptop.control_revision==control_revision && fire.consume(key),"automatic docking frees both hands without resetting an ongoing AGM trigger or the control session");
				laptop.observe(false,false,false,false);check(laptop.stage==phase::stowed && !laptop.requested(),"only native completion ends the admitted remote control session");
				laptop.take(holder);laptop.opener=off;laptop.drag.begin(0,{lip(0),{0,0,0,1}});laptop.open(max_angle);laptop.release_lid(true);laptop.observe(false,false,false,true);
				check(laptop.stage==phase::stowed,"unaccepted activation times out without issuing a native exit request");
				for(unsigned attempt=0;attempt<4;++attempt)
				{
					laptop.take(holder);laptop.take_lid(off,trigger,{lip(0),{0,0,0,1}});laptop.open(max_angle);
					check(laptop.release_lid(true) && laptop.awaiting_native() && laptop.physical() && !laptop.native_control(),
						"repeated pending requests retain physical interaction until native acceptance");
					const auto request=laptop.control_revision;
					check(laptop.take_lid(off,trigger,{lip(max_angle),{0,0,0,1}}) && laptop.open(0) && !laptop.release_lid(true) &&
						laptop.angle==0 && laptop.control_revision==request && laptop.requested(),
						"an unacknowledged open notebook can close without issuing another activation");
					laptop.release();
					check(laptop.docked && !laptop.held() && laptop.holder==hand::none && laptop.opener==hand::none &&
						laptop.requested() && laptop.control_revision==request,
						"releasing a pending case stows it immediately while preserving its in-flight native request");
					if(attempt&1)
					{
						laptop.observe(true,true,false,false);
						check(laptop.native_control() && laptop.stage==phase::remote && laptop.control_revision==request,
							"a late camera acknowledgement remains admitted after voluntary stow");
						laptop.observe(false,false,false,false);
					}
					else laptop.observe(false,false,false,true);
					check(laptop.stage==phase::stowed && !laptop.docked && !laptop.requested(),"completion and timeout fully reset the next notebook use");
				}
			}
			for(bool auxiliary:{false,true})
			{
				const auto full=lid(max_angle,auxiliary);const auto pivot=auxiliary?auxiliary_hinge:hinge;
				check(vr::gameplay::hands::length(full.position)<.0001f && vr::gameplay::hands::length(vr::gameplay::hands::sub(vr::gameplay::hands::rotate(full.rotation,{1,2,3}),{1,2,3}))<.0001f,
					"fully open panels preserve their original source-model geometry without a doubled bind transform");
				for(float a:{0.f,65.f,max_angle})check(vr::gameplay::hands::length(vr::gameplay::hands::sub(vr::gameplay::hands::pose_math::compose(lid(a,auxiliary),{pivot,{0,0,0,1}}).position,pivot))<.0001f,
					"main and auxiliary panel hinges stay fixed throughout the full native range");
			}
			check(notebook::authored::case_fingers.size()==15 && notebook::authored::screen_fingers.size()==15 && notebook::authored::right_case_wrist.position[2]<0,
				"case support and screen grip use distinct captured native hand poses");
		}
	}
}
