#pragma once
#include "component/vr/gameplay/grenade_state.hpp"
#include "component/vr/gameplay/grenade_profile.hpp"
#include "component/vr/gameplay/grenade_contact.hpp"
#include "component/vr/gameplay/grenade_throwback.hpp"
#include "component/vr/gameplay/hand_interaction/core.hpp"

namespace grenade_tests
{
	template<class Check>void run(Check& check)
	{
		using namespace vr::gameplay;using namespace grenades;using vr::hand;
		check(throwback_direction({},{1,0,0},{20,30,-30},40) && !throwback_direction({},{1,0,0},{-20,0,0},40),
			"throwback admits rough controller aim but rejects a grenade behind the hand");
		check(throwback_direction({},{1,0,0},{-2,0,0},40) && !throwback_direction({},{0,0,0},{2,0,0},40) &&
			!throwback_direction({},{1,0,0},{NAN,0,0},40) && !throwback_direction({},{1,0,0},{20,0,0},0),
			"near-hand throwback needs no exact aim; invalid geometry never qualifies");
		state recovered;
		check(recovered.take_live(kind::frag,50,hand::left,1,1000,7000) && recovered.spent && recovered.stage==phase::cooking,
			"native live pickup adopts a paid grenade with its longer AI fuse, independently of chest ammo");
		check(!recovered.pull_pin(true) && !recovered.cook(2000) && !recovered.return_to_chest() && recovered.deadline==7000,
			"throwback cannot pull a second pin, restart cooking or enter chest inventory");
		check(recovered.handoff(hand::right) && recovered.deadline==7000,"throwback handoff retains the native deadline");
		recovered.release(2500);recovered.release(3000);
		check(recovered.fuse_at(3000)==4000 && recovered.fuse_at(7100)==1,"throwback release and failed spawn retries preserve remaining time");
		check(!state{}.take_live(kind::frag,50,hand::left,1,1000,1000) &&
			!state{}.take_live(kind::frag,50,hand::left,1,1000,61001) &&
			!state{}.take_live(kind::flash,50,hand::left,1,1000,2000),"expired, unbounded or unsupported native pickups are refused before consumption");
		state last_moment;last_moment.take_live(kind::frag,50,hand::right,1,INT32_MAX-10,INT32_MAX);
		check(last_moment.due(INT32_MAX) && !last_moment.due(INT32_MAX-1),"throwback deadline admission does not overflow near native time limit");
		{
			namespace hi=hand_interaction;hi::arbiter arbitration;arbitration.begin(1,1);
			const hi::target missile{hi::domain::grenade,{50,1},3,128};
			const hi::grasp grasp{missile,hi::role::world,hi::button::grip,hi::recipe::single,hi::capability::action};
			for(auto h:{hand::left,hand::right})arbitration.offer({h,grasp,1,15,1,1,true,true});
			unsigned picked{};arbitration.resolve([&](const auto&){++picked;return true;});
			check(picked==1,"both free hands aiming at one HUD grenade consume only one world target");
			arbitration.retain([](const auto&){return false;});arbitration.begin(2,1);
			arbitration.offer({hand::left,grasp,1,15,1,1,true,true});arbitration.resolve([&](const auto&){++picked;return true;});
			check(picked==1,"a consumed squeeze cannot auto-grab another grenade after ownership ends");
		}
		check(classify("h2_cheatfootball","weapon_m67_grenade")==kind::football && classify("h2_cheatpomegrenade","weapon_m67_grenade")==kind::pomegranate,
			"cheat identity wins over the inherited ordinary M67 world model");
		check(classify("unrelated")==kind::count && classify("smoke","weapon_us_smoke_grenade")==kind::smoke,"unsupported offhands remain outside the throwable provider");
		state fruit;check(fruit.take(kind::pomegranate,142,hand::right,1,3500) && fruit.pull_pin(true) && fruit.cook(100),
			"pomegranate uses the complete frag pin and cook sequence");
		check(fruit.deadline==3600 && authored::profiles[unsigned(kind::pomegranate)].pin_mask==0,
			"pomegranate has native player fuse and no invented pin mesh");
		state ball;check(ball.take(kind::football,143,hand::left,1,0) && !ball.pull_pin(true) && !ball.cook(100),"football admits zero fuse but rejects explosive actions");
		check(ball.return_to_chest() && !ball.held() && !ball.spent,"football placed back in its chest slot returns without an ammo debit");
		ball.take(kind::football,143,hand::right,1,0);
		check(ball.stage==phase::prepared && !ball.spent && ball.handoff(hand::left),"football is throwable immediately after pickup and handoff does not spend ammo");
		ball.release(200);check(ball.stage==phase::release_pending && ball.fuse_at(999999)==0 && !ball.due(999999),"football cannot acquire an explosion deadline on release/retry");
		check(!ball.return_to_chest() && !fruit.return_to_chest(),"released footballs and primed explosives cannot be reclaimed as chest inventory");
		check(!state{}.take(kind::football,143,hand::right,1,3500),"timed football descriptor is rejected");
		const auto boosted=throw_velocity({0,120,160},40,kind::frag,2.5f,1.5f);
		const auto boosted_ball=throw_velocity({0,120,160},40,kind::football,2.5f,1.5f);
		check(vr::gameplay::hands::length(vr::gameplay::hands::sub(boosted,{0,300,400}))<.001f && vr::gameplay::hands::length(vr::gameplay::hands::sub(boosted_ball,{0,450,600}))<.001f,
			"general throw gain and football multiplier compose without changing aim");
		check(std::abs(vr::gameplay::hands::length(throw_velocity({2000,0,0},40,kind::pomegranate,2.5f,1.5f))-1500)<.001f &&
			std::abs(vr::gameplay::hands::length(throw_velocity({2000,0,0},40,kind::football,2.5f,1.5f))-2250)<.001f,
			"raw speed ceiling scales with the configured gains instead of clipping back to 15 m/s");
		check(throw_velocity({},40,kind::football,2.5f,1.5f)==vr::gameplay::hands::vec{} &&
			throw_velocity({NAN,0,0},40,kind::frag,2.5f,1.5f)==vr::gameplay::hands::vec{},"zero and invalid motion cannot acquire a synthetic throw impulse");
		const auto scale=stowed_scale(kind::football,{5.56f,5.58f,5.58f},39.3701f);
		check(scale>.35f && scale<.36f && std::abs(11.16f*scale/39.3701f-.10f)<1e-5f &&
			stowed_scale(kind::pomegranate,{3,3,3},39.3701f)==1,"football chest display is ten centimetres while other native sizes remain intact");
		for(auto kind:{kind::frag,kind::flash,kind::smoke})
		{
			state s;check(s.take(kind,50,hand::right,1,5000),"grenade acquired without starting fuse");
			check(!s.cook(100) && !s.due(999999),"B/Y before pulling pin cannot activate grenade");
			s.release(100);check(!s.held() && !s.spent,"safe grip release returns grenade without spending ammo");
			check(s.take(kind,50,hand::left,1,5000),"returned grenade reacquired");
			check(!s.pull_pin(false) && s.stage==phase::safe,"failed native debit preserves safety pin");
			check(s.pull_pin(true) && !s.pull_pin(true),"pin extraction consumes exactly one committed round");
			check(!s.due(999999),"unpulled lever does not run timer after pin extraction");
			if(kind!=kind::frag)check(!s.cook(100),"flash and smoke cannot cook from B/Y");
			s.release(1000);check(s.stage==phase::release_pending && s.spent && s.fuse_at(1000)==5000,
				"unprimed release starts full native fuse and cannot return to chest");
			s.release(2000);check(s.fuse_at(2000)==4000,"failed spawn retry never restarts the fuse");
			check(s.fuse_at(9000)==1,"expired spawn retry requests imminent native explosion");
		}
		state s;s.take(kind::frag,50,hand::right,1,5000);s.pull_pin(true);
		check(s.cook(100) && !s.cook(200) && s.deadline==5100,"frag cook starts once and repeated B/Y cannot reset it");
		check(!s.due(5099) && s.due(5100),"held grenade becomes due at native fuse boundary");
		s.release(5100);check(s.fuse_at(5100)==1,"held expiry is delivered to native grenade explosion");
		state early;early.take(kind::frag,50,hand::left,1,5000);early.pull_pin(true);early.cook(100);early.release(2100);
		check(early.fuse_at(2100)==3000,"throwing cooked frag retains only its remaining fuse");
		state transfer;transfer.take(kind::frag,50,hand::right,1,5000);
		transfer.puller=hand::left;
		const auto revision=transfer.revision;
		check(!transfer.handoff(hand::left) && transfer.holder==hand::right && transfer.puller==hand::left && transfer.revision==revision,
			"partially extracted pin blocks handoff atomically without losing pin ownership");
		transfer.puller=hand::none;
		check(transfer.handoff(hand::left) && transfer.stage==phase::safe && !transfer.spent,"cancelled pin pull allows safe handoff without ammo debit");
		transfer.pull_pin(true);transfer.cook(100);
		check(transfer.handoff(hand::right) && transfer.stage==phase::cooking && transfer.deadline==5100 && transfer.spent,
			"cooking handoff preserves paid grenade and original deadline");
		transfer.release(2100);
		check(!transfer.handoff(hand::left) && transfer.fuse_at(2100)==3000,"released projectile cannot transfer back into a hand");
		check(segment_distance({0,0,0},{-4,0,0},{4,0,0})==0 && segment_distance({0,3,0},{-4,0,0},{4,0,0})==3,
			"pin contact covers palm-to-fingertip reach rather than wrist distance alone");
		check(pin_acquire_meters>=.10f,"pin acquisition admits a ten-centimetre reach radius");
		check(pin_slide({1,2,3},{9,2,9},40)==vr::gameplay::hands::vec{} && pin_slide({1,2,3},{1,1,3},40)==vr::gameplay::hands::vec{},
			"lateral or backward motion cannot pull a pin");
		check(vr::gameplay::hands::length(vr::gameplay::hands::sub(pin_slide({1,2,3},{8,3.6f,9},40),vr::gameplay::hands::vec{0,1.6f,0}))<1e-5f,"pin motion is projected onto its fixed local axis");
		check(std::abs(pin_slide({1,2,3},{1,99,3},40)[1]-3.2f)<1e-5f,"render travel clamps at the physical extraction stroke");
		check(pin_slide({}, {0,1,0},40)[1]<pin_slide({}, {0,1.2f,0},40)[1],
			"new display-frame tracking advances visible pin travel without a simulation delta");
		state invalid;check(!invalid.take(kind::frag,512,hand::left,1,5000) && !invalid.take(kind::frag,50,hand::none,1,5000) &&
			!invalid.take(kind::frag,50,hand::left,1,0),"invalid native grenade descriptors cannot be acquired");
		for(auto type:{kind::frag,kind::flash,kind::smoke,kind::pomegranate,kind::football})
		{
			const vr::gameplay::hands::quat mirror{0,0,0,1};const auto& original=authored::profiles[unsigned(type)].right_attachment;
			const auto right=authored::attachment(type,mirror,false);
			const auto before=vr::gameplay::hands::pose_mirror::object_in_wrist({},original,mirror);
			const auto left=authored::attachment(type,mirror,true);
			check(right.position==original.position && right.rotation==original.rotation,"native right-hand grenade orientation preserved");
			check(vr::gameplay::hands::length(vr::gameplay::hands::sub(left.position,before.position))<1e-5f &&
				vr::gameplay::hands::length(vr::gameplay::hands::sub(vr::gameplay::hands::rotate(left.rotation,{0,0,1}),vr::gameplay::hands::rotate(before.rotation,{0,0,1})))<1e-5f &&
				vr::gameplay::hands::dot(vr::gameplay::hands::rotate(left.rotation,{1,0,0}),vr::gameplay::hands::rotate(before.rotation,{1,0,0}))<-.999f,
				"opposite hand half-turn preserves upright axis and grip origin");
		}
		release_motion motion;using namespace std::chrono_literals;
		const auto now=vr::controller_input::clock::now();motion.sample({0,0,0},now,40);motion.sample({4,0,0},now+40ms,40);
		check(std::abs(motion.velocity(now+40ms,40)[0]-100)<.01f,"throw velocity comes from recent tracked release motion");
		check(vr::gameplay::hands::length(motion.velocity(now+500ms,40))==0,"stale hand motion cannot throw");
		motion.sample({1000,0,0},now+50ms,40);
		check(vr::gameplay::hands::length(motion.velocity(now+50ms,40))==0,"tracking teleport cannot create an extreme throw");
	}
}
