#pragma once
#include "component/vr/gameplay/weapons/cheytac/profile.hpp"
#include "component/vr/gameplay/weapons/cheytac/reload_profile.hpp"

namespace manual_bolt_tests
{
	template <class Fixture, class Check> void run(Check check)
	{
		namespace w = vr::gameplay::weapons;
		namespace m = w::mechanics;
		const auto& definition = w::cheytac::physical;
		const auto& motion = w::cheytac::bolt_motion;
		{
			using namespace vr::gameplay::hands;
			// Captured native tag_brass: gun X forward, -Y right, Z up.
			const anchor port{{7.236948f,-.662933f,4.179535f},{-.21262674f,-.21262674f,-.67438110f,.67438110f}};
			const auto velocity=w::physical_reload::cartridge_exit_velocity(port,100);
			check(std::abs(velocity[0])<.01f && velocity[1]<-120 && velocity[2]>80,
			    "M200 round exits right/up through its port, never rearward");
			const quat turn{0,0,.70710678f,.70710678f};
			const anchor rotated{rotate(turn,port.position),normalize(multiply(turn,port.rotation))};
			check(length(sub(w::physical_reload::cartridge_exit_velocity(rotated,100),rotate(turn,velocity)))<.01f,
			    "port ejection rotates with the gun independently of the holding hand");
		}
		const auto pose = [&](float lift, float travel) {
			const float angle = motion.radians * lift;
			return vr::gameplay::hands::vec{motion.pivot[0] - travel * motion.stroke,
			    motion.pivot[1] - .08f * std::cos(angle), motion.pivot[2] - .08f * std::sin(angle)};
		};
		const auto motion_step = [&](Fixture& f, float lift, float travel) {
			f.geometry.bolt_hand = pose(lift, travel);
			f.step();
		};
		const auto prepare = [&](Fixture& f) {
			f.owner.rear = vr::hand::none;
			f.owner.support = vr::hand::left;
			++f.owner.rear_revision;
			f.step();
			f.geometry.slide_distance = 0;
			motion_step(f, f.state.bolt.lift, f.state.bolt.travel);
			f.trigger(true);
		};
		{
			Fixture f(&definition);
			f.geometry.slide_distance = 0;
			f.geometry.bolt_hand = pose(0, 0);
			f.trigger(true);
			check(f.control.slide_held(), "M200 left hand can acquire its overhand bolt grasp");
			f.trigger(false);
			prepare(f);
			check(f.control.slide_held(), "M200 right hand acquires bolt while left hand supports gun");
			motion_step(f, 0, .4f);
			check(f.state.bolt.travel == 0 && f.state.bolt.lift == 0, "M200 locked bolt rejects a straight rear pull");
			motion_step(f, 0, 0);
			motion_step(f, .5f, 0);
			f.trigger(false);
			check(f.state.bolt.lift > .4f && !m::ready(*f.rules, f.state),
			    "M200 partial lift is retained and blocks fire after release");
			const auto retained = f.state.bolt.lift;
			f.interrupt();
			check(f.state.bolt.lift == retained, "M200 interruption never completes a partial lift");
			prepare(f);
			motion_step(f, 1, 0);
			motion_step(f, 1, .5f);
			f.trigger(false);
			check(f.state.chamber_loaded && f.spent == 0, "M200 short rear pull cannot eject live cartridge");
			prepare(f);
			motion_step(f, 1, 1);
			f.step();
			check(!f.state.chamber_loaded && f.spent == 1 && f.state.bolt.feed_armed,
			    "M200 full extraction discards exactly one live round");
			motion_step(f, 1, .3f);
			f.trigger(false);
			check(f.state.bolt.feeding && f.state.magazine_rounds == 3 && f.native.loaded == 4,
			    "M200 forward feed escrows one cartridge without changing loaded total");
			f.geometry.slide_distance = 1;
			f.geometry.magazine.grip_distance = 0;
			f.geometry.hand_in_gun = {};
			f.trigger(true);
			f.geometry.hand_in_gun = {0, 0, -.051f};
			f.step();
			check(!f.state.magazine_inserted && f.state.bolt.feeding && f.native.loaded == 1,
			    "M200 mid-feed magazine removal preserves the cartridge on the bolt");
			f.trigger(false);
			prepare(f);
			motion_step(f, 1, 0);
			check(f.state.chamber_loaded && !f.state.bolt.feeding && !m::ready(*f.rules, f.state),
			    "M200 forward but unlocked cannot fire");
			motion_step(f, 0, 0);
			f.trigger(false);
			check(m::ready(*f.rules, f.state) && !f.state.magazine_inserted,
			    "M200 closes on its retained chamber without a magazine");
		}
		{
			Fixture f(&definition);
			const auto shot = m::plan(*f.rules, f.state,
			    {m::operation::accepted_shot, f.state.weapon, f.state.instance_generation, f.state.revision,
			        vr::hand::right, vr::hand::right});
			check(shot && f.commit(shot), "M200 native shot debit commits");
			f.state = shot.next;
			check(f.state.bolt.spent_case && !f.state.chamber_loaded && f.state.magazine_rounds == 4 &&
			          !m::ready(*f.rules, f.state),
			    "M200 shot retains spent case and never automatically chambers");
			const auto total = m::total_rounds(f.state);
			prepare(f);
			motion_step(f, 1, 0);
			motion_step(f, 1, 1);
			check(f.last_effect == m::effect::case_eject && f.spent == 1 && !f.state.bolt.spent_case,
			    "M200 rear stroke ejects case without a second ammunition debit");
			motion_step(f, 1, .3f);
			motion_step(f, 1, 1);
			check(f.last_effect == m::effect::live_eject && f.spent == 2,
			    "M200 reversed partial feed ejects its one live cartridge");
			motion_step(f, 1, 0);
			motion_step(f, 0, 0);
			f.trigger(false);
			check(m::ready(*f.rules, f.state) && m::total_rounds(f.state) + 1 == total,
			    "M200 repeated rear/forward stroke feeds once and conserves live rounds");
		}
		{
			Fixture f(&definition);
			prepare(f);
			f.writable = false;
			motion_step(f, 1, 0);
			check(f.state.bolt.lift == 0 && !f.control.slide_held(),
			    "M200 rejected write consumes the grip without publishing motion");
			f.writable = true;
			motion_step(f, 1, 1);
			check(f.state.bolt.lift == 0, "M200 held trigger cannot retry a rejected motion");
			f.trigger(false);
			prepare(f);
			motion_step(f, 1, 0);
			motion_step(f, 1, .4f);
			const auto retained = f.state.bolt.travel;
			f.input.grip[1].valid = false;
			f.step();
			check(f.state.bolt.travel == retained && !f.control.slide_held(),
			    "M200 tracking loss preserves partial bolt position");
		}
		for(auto rear:{vr::hand::left,vr::hand::right})for(int scenario=0;scenario<14;++scenario)
		{
			Fixture f(&definition);f.owner.rear=rear;f.step();f.geometry.slide_distance=0;
			f.geometry.bolt_hand=pose(0,0);f.trigger(true);
			motion_step(f,1,0);motion_step(f,1,1);motion_step(f,1,.3f);
			if(scenario!=3)motion_step(f,1,0);
			motion_step(f,.3f,scenario==3?.3f:0.f);
			motion_step(f,.16f,scenario==3?.3f:0.f);
			const auto ammunition=f.native;const auto spent=f.spent;
			if(scenario==1)motion_step(f,.18f,0); // Reversing away from the locked end.
			if(scenario==2)for(int i=0;i<5;++i)f.step(); // Stop before release.
			if(scenario==4)f.writable=false;
			if(scenario==5)f.input.grip[1-int(rear)].valid=false;
			if(scenario==6)f.geometry.slide_distance=1;
			if(scenario==7){motion_step(f,.15f,0);for(int i=0;i<4;++i)f.step();motion_step(f,.149f,0);}
			if(scenario>=8 && scenario<=11)
			{
				// Regrasp after a completed forward stroke to exercise the wider range.
				f.trigger(false);motion_step(f,0,0);f.trigger(true);
				motion_step(f,1,0);motion_step(f,1,.5f);motion_step(f,1,0);
				const float lift=(scenario==9 ? 41.f : 39.f)*3.14159265359f/180.f/std::abs(motion.radians);
				motion_step(f,scenario==10 ? lift+.001f : lift+.04f,0);
				if(scenario==10)for(int n=0;n<5;++n)f.step();
				if(scenario==10)motion_step(f,lift,0);
				else if(scenario==11)f.geometry.bolt_hand=pose(lift,0); // Final sample crosses 40 degrees on release.
				else motion_step(f,lift,0);
			}
			if(scenario==12)motion_step(f,.159f,0); // Slow immediately after a quick approach.
			if(scenario==13)f.geometry.bolt_hand[0]+=.01f; // Fast axial motion is not closing rotation.
			f.trigger(false);
			check((f.state.bolt.lift==0)==(scenario==0 || scenario==8 || scenario==11),"M200 assist requires completed return, closing velocity, live tracking and accepted write in either hand");
			check(f.native==ammunition && f.spent==spent,"M200 assisted locking never duplicates feed or extraction");
			check(!f.control.slide_held(),"M200 release always ends the hand lease");
			const auto attempts=f.attempts;f.step();check(f.attempts==attempts,"M200 release assist cannot retry without a new grasp");
		}
		// Deterministic adversarial operation ordering: transactional rejection cannot
		// change the ledger, and every accepted transition conserves live ammunition.
		{
			Fixture f(&definition);
			unsigned seed = 0x200;
			for (int i = 0; i < 10000; ++i)
			{
				seed = seed * 1664525u + 1013904223u;
				const auto op = static_cast<m::operation>(seed % (unsigned(m::operation::move_bolt) + 1));
				const bool rear = op == m::operation::accepted_shot || op == m::operation::dry_fire ||
				                  op == m::operation::release_button;
				const m::request request{op, f.state.weapon, f.state.instance_generation, f.state.revision,
				    vr::hand::left, rear ? vr::hand::left : vr::hand::right,
				    {float((seed >> 8) % 5) / 4, float((seed >> 16) % 5) / 4}};
				const auto before = m::total_rounds(f.state);
				const auto tx = m::plan(*f.rules, f.state, request);
				if (tx)
				{
					check(m::valid(*f.rules, tx.next) && before == m::total_rounds(tx.next) + tx.rounds_spent,
					    "M200 arbitrary accepted operations conserve live rounds");
					check(f.commit(tx), "M200 random transaction compares exact native projection");
					f.state = tx.next;
				}
			}
		}
	}
} // namespace manual_bolt_tests
