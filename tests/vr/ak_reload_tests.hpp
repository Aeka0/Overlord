#pragma once
#include "component/vr/gameplay/weapons/ak47/profile.hpp"
#include "component/vr/gameplay/weapons/cheytac/profile.hpp"
#include "component/vr/gameplay/weapons/dragunov/profile.hpp"
#include "component/vr/gameplay/weapons/fal/profile.hpp"
#include "component/vr/gameplay/weapons/m14ebr/profile.hpp"
#include "component/vr/gameplay/weapons/mp5/profile.hpp"
#include "component/vr/gameplay/weapons/ak47/reload_profile.hpp"
#include "component/vr/gameplay/weapons/mp5/reload_profile.hpp"

namespace ak_reload_tests
{
	template<class Fixture,class Check> void run(Check check)
	{
		namespace w=vr::gameplay::weapons;
		namespace m=w::mechanics;
		namespace p=w::physical_reload;
		using vr::gameplay::hands::scale;
		for(const auto* definition:{&w::ak47::physical,&w::m14ebr::physical,&w::dragunov::physical})for(float offset:{-.039f,.039f,.041f})
		{
			vr::gameplay::weapons::physical_reload::magazine_latch_contact contact;
			auto points=magazine_box_tests::point_fixture({-.09f,offset,0});
			const auto& tuning=*definition->interaction.manual_magazine;
			const auto now=std::chrono::steady_clock::time_point{};
			(void)contact.update(tuning,points,now,.35f);
			points.frame.position={.01f,offset,0};
			check(contact.update(tuning,points,now+std::chrono::milliseconds(20),.35f)==(std::abs(offset)<.04f),"AK, M14 and Dragunov share the widened but bounded latch strike");
			check(!contact.update(tuning,points,now+std::chrono::milliseconds(40),.35f),"wider strike volume still consumes contact until a new separated approach");
		}

		const auto strike=[](Fixture& f,float x,float y=0.f) {
			f.geometry.magazine.strike->frame.position={x,y,0};
			f.step();
		};
		for(const auto* definition:{&w::ak47::physical,&w::m14ebr::physical,&w::dragunov::physical,&w::fal::physical,&w::mp5::physical,&w::mp5::arctic,&w::cheytac::physical,&w::cheytac::desert})for(auto rear:{vr::hand::left,vr::hand::right})
		{
			Fixture f(definition);f.owner.rear=rear;f.step();f.geometry.waist_distance=0;f.trigger(true);strike(f,-.09f);strike(f,.01f);
			check(!f.state.magazine_inserted,"delay fixture first commits a real latch strike");const auto hit_at=f.now;
			f.geometry.magazine_top_in_well={0,0,-.2f};f.step();f.geometry.magazine_top_in_well={0,0,-.04f};f.step();
			while(f.now<hit_at+std::chrono::milliseconds(290)){check(!f.state.magazine_inserted,"insertion remains rejected during the post-strike interval");f.step();}
			f.now=hit_at+std::chrono::milliseconds(299);++f.input.sequence;f.step(false);
			check(!f.state.magazine_inserted && f.state.magazine_hand!=vr::hand::none,"299 ms retains the spare without seating or losing rounds");
			f.now=hit_at+std::chrono::milliseconds(300);++f.input.sequence;f.step(false);
			check(f.state.magazine_inserted,"300 ms restores insertion acceptance for the aligned spare on either hand");
		}
		{
			Fixture f(&w::ak47::physical);f.geometry.waist_distance=0;f.trigger(true);strike(f,-.09f);strike(f,.01f);
			const auto hit_at=f.now;f.input.focused=false;f.step();f.input.focused=true;f.trigger(false);
			f.geometry.magazine_top_in_well={0,0,-.2f};f.trigger(true);
			f.geometry.magazine_top_in_well={0,0,-.04f};f.step();
			check(f.now<hit_at+std::chrono::milliseconds(300) && !f.state.magazine_inserted && f.state.magazine_hand!=vr::hand::none,
				"focus reset and a fresh spare cannot bypass the same weapon's insertion delay");
		}

		const auto draw=[](Fixture& f) { f.geometry.waist_distance=0; f.trigger(true); };
		for (const auto holder:{vr::hand::left,vr::hand::right})
		{
			Fixture f(w::ak47::skins[0]);
			f.owner.rear=vr::hand::none;f.owner.support=holder;++f.owner.rear_revision;f.step();
			const auto other=static_cast<vr::hand>(1-static_cast<int>(holder));
			check(!f.owner.can_fire() && f.owner.holding_hand()==holder && f.owner.manipulation_hand()==other,
				"foregrip-only carry exposes the opposite manipulation hand without granting fire ownership");
			const auto total=m::total_rounds(f.state);
			f.geometry.slide_distance=0;f.trigger(true);
			check(f.control.slide_held(),"either foregrip hand permits the other hand to acquire the real charging handle");
			f.move_slide(f.control.slide_grip(),.094f);f.trigger(false);
			check(f.spent==1 && f.state.chamber_loaded && m::total_rounds(f.state)+f.spent==total,
				"foregrip-only full bolt cycle preserves feed and spends exactly one extraction");
			f.geometry.slide_distance=1;f.geometry.magazine.grip_distance=0;f.trigger(true);
			check(f.control.magazine_grabbed(),"free hand can acquire the attached magazine while opposite hand holds the foregrip");
			f.geometry.hand_in_gun=vr::gameplay::hands::add(f.geometry.hand_in_gun,scale(w::ak47::manual_magazine.pull_axis,.051f));f.step();
			check(!f.state.magazine_inserted && f.state.magazine_hand==other && !f.owner.can_fire(),
				"foregrip magazine pull transfers escrow to the manipulating hand without promoting it to rear grip");
			check(f.interrupt() && f.state.magazine_hand==vr::hand::none && m::total_rounds(f.state)+f.spent==total,
				"dropping or stowing a foregrip-only weapon cleans mechanical escrow exactly once");
			f.trigger(false);f.owner.rear=other;++f.owner.rear_revision;f.step();
			check(f.owner.manipulation_hand()==vr::hand::none,"both hands occupied by the same gun leave no manipulation hand");
		}
		for (auto definition:w::ak47::skins) for (auto rear:{vr::hand::right,vr::hand::left}) for (std::uint8_t style=0;style<2;++style)
		{
			Fixture f(definition); f.owner.rear=rear; f.step(); f.geometry.slide_distance=0; f.geometry.slide_pose=style;
			f.trigger(true); const auto grip=f.control.slide_grip();
			check(f.control.slide_held() && grip.pose==style,"AK fresh press latches either contact-side pose");
			f.geometry.slide_pose=1-style; f.move_slide(grip,.03f);
			check(f.control.slide_grip().pose==style && f.spent==0,"turning wrist while holding cannot switch AK grip or spend a partial stroke");
			f.move_slide(grip,.094f); f.trigger(false);
			check(f.state.chamber_loaded && f.spent==1 && f.native.loaded==29,"either AK pose cycles the same bolt and spends a live extraction once");
			f.trigger(true); check(f.control.slide_grip().pose==1-style,"AK release and fresh press may select the other hand edge");
			f.interrupt(); check(!f.control.slide_held(),"tracking interruption clears either AK pose lease");
		}
		for (auto definition:w::ak47::skins) for (auto rear:{vr::hand::right,vr::hand::left})
		{
			Fixture f(definition); f.owner.rear=rear; f.step();
			const auto total=m::total_rounds(f.state);
			f.button(true); f.button(false);
			check(f.commits==0 && f.state.magazine_inserted,"AK B/Y cannot eject or chamber");
			f.geometry.magazine.grip_distance=0; f.trigger(true);
			check(f.control.magazine_grabbed() && f.commits==0 && f.control.offhand_busy(f.state),"AK fresh physical contact grabs without an ammo write");
			f.geometry.hand_in_gun=scale(w::ak47::manual_magazine.pull_axis,.025f); f.step(); f.trigger(false);
			check(!f.control.magazine_grabbed() && f.state.magazine_inserted && f.commits==0,"partial pull/release leaves original magazine seated");
			f.geometry.hand_in_gun={}; f.trigger(true);
			f.geometry.hand_in_gun=scale(w::ak47::manual_magazine.pull_axis,.051f); f.step();
			check(!f.state.magazine_inserted && f.state.chamber_loaded && f.state.held_rounds==29 && f.state.reserve_rounds==60 &&
				f.last_effect==m::effect::magazine_take && f.state.magazine_hand!=vr::hand::none,"full AK pull transfers actual old magazine into hand without refund/drop");
			check(m::total_rounds(f.state)==total && f.native.loaded==1,"manual removal preserves chamber and ammunition escrow");
			f.geometry.magazine_top_in_well={0,0,-.2f}; f.step();
			f.geometry.magazine_top_in_well={0,0,-.04f}; f.step();
			check(f.state.magazine_inserted && f.native.loaded==30 && f.state.reserve_rounds==60,"reinserting old partial magazine does not refill it");
			const auto commits=f.commits; f.step();
			check(f.commits==commits && !f.control.magazine_grabbed(),"held insertion cannot become another extraction without release/repress");
			f.trigger(false); f.trigger(true);
			f.geometry.hand_in_gun=scale(w::ak47::manual_magazine.pull_axis,.102f); f.step(); f.trigger(false);
			check(f.state.magazine_hand==vr::hand::none && f.state.reserve_rounds==89 && m::total_rounds(f.state)==total,
				"dropping physically removed magazine refunds remaining rounds exactly once");
		}
		for (bool empty:{false,true}) for (auto definition:w::ak47::skins)
		{
			Fixture f(definition);
			if (empty) { auto s=f.state; s.magazine_rounds=0; s.chamber_loaded=false; f.adopt(s); }
			const auto total=m::total_rounds(f.state); draw(f); strike(f,-.08f); strike(f,.005f);
			check(!f.state.magazine_inserted && f.state.held_rounds==30 && f.state.magazine_hand==vr::hand::left &&
				f.last_effect==m::effect::magazine_out,"spare impact drops only old magazine and keeps spare held");
			check(m::total_rounds(f.state)==total && f.state.action==m::action_state::closed,"latch strike conserves ammo and does not create a bolt lock");
			const auto commits=f.commits; strike(f,.006f); strike(f,.006f);
			check(f.commits==commits,"stationary post-impact contact cannot repeat ejection");
			for(int n=0;n<30;++n)f.step();
			f.geometry.magazine_top_in_well={0,0,-.2f}; f.step(); f.geometry.magazine_top_in_well={0,0,-.04f}; f.step(); f.trigger(false);
			check(f.state.magazine_inserted && f.state.chamber_loaded==!empty && f.native.loaded==(empty ? 30 : 31),
				"AK fresh spare insertion preserves empty/tactical chamber state");
			if (empty)
			{
				f.button(true); f.button(false);
				check(!m::ready(*f.rules,f.state) && !f.state.chamber_loaded,"AK empty chamber cannot use B/Y release");
				f.geometry.slide_distance=0; f.trigger(true); const auto grip=f.control.slide_grip();
				f.move_slide(grip,.03f); f.trigger(false);
				check(!f.state.chamber_loaded,"short AK rack cannot chamber");
				f.trigger(true); f.move_slide(f.control.slide_grip(),.094f); f.trigger(false);
				check(f.state.chamber_loaded && f.spent==0 && f.state.magazine_rounds==29,"full AK rack and release chambers one without spending an absent round");
				for (int n=0;n<30;++n)
				{
					const auto tx=m::plan(*f.rules,f.state,{m::operation::accepted_shot,f.state.weapon,f.state.instance_generation,
						f.state.revision,f.owner.rear,f.owner.rear});
					check(tx && f.commit(tx),"AK accepted shot commits"); f.state=tx.next;
				}
				check(f.native.loaded==0 && !f.state.chamber_loaded && f.state.action==m::action_state::closed && !m::ready(*f.rules,f.state),
					"last AK round leaves bolt forward and fire unavailable");
			}
		}
		for(const auto* definition:{&w::ak47::physical,&w::mp5::physical,&w::mp5::arctic})for (int mode=0;mode<5;++mode)
		{
			Fixture f(definition); draw(f);
			if (mode==0) { strike(f,.005f); strike(f,.006f); }
			if (mode==1) { strike(f,.08f); strike(f,-.005f); }
			if (mode==2) { strike(f,-.08f,.2f); strike(f,.005f,.2f); }
			if (mode==3) { strike(f,-.5f); strike(f,.005f); }
			if (mode==4) for (int n=0;n<100;++n) strike(f,-.08f+n*.001f);
			check(f.state.magazine_inserted && f.commits==1,"spawn overlap, reverse sweep, stock hit, teleport and slow drift never unlatch");
		}
		{
			Fixture f(&w::ak47::physical); draw(f); strike(f,-.08f); f.writable=false; strike(f,.005f);
			const auto attempts=f.attempts; f.writable=true; strike(f,.006f); strike(f,.005f);
			check(f.state.magazine_inserted && f.attempts==attempts,"rejected latch compare never retries while touching");
			strike(f,-.08f); strike(f,.005f);
			check(!f.state.magazine_inserted && f.commits==2,"separate deliberate strike retries once");
		}
		for (int mode=0;mode<4;++mode)
		{
			Fixture f(&w::ak47::physical); draw(f); strike(f,-.08f);
			if (mode==0) ++f.input.reference_generation;
			if (mode==1) f.input.grip[0].valid=false;
			if (mode==2) f.geometry.magazine.strike->frame.position[0]=std::numeric_limits<float>::quiet_NaN();
			if (mode==3) f.geometry.magazine.valid=false;
			f.step();
			check(f.state.magazine_inserted && f.state.magazine_hand==vr::hand::none && f.state.reserve_rounds==60,
				"recenter/tracking loss/invalid contact refunds spare without unlatching old magazine");
		}
		{
			Fixture f(&w::ak47::physical); f.trigger(true); f.geometry.magazine.grip_distance=0; f.step();
			check(!f.control.magazine_grabbed(),"holding trigger while entering magazine cannot acquire it");
			f.trigger(false); f.owner.support=vr::hand::left; f.trigger(true);
			check(!f.control.magazine_grabbed(),"support lease blocks magazine grip");
		}
		{
			Fixture f(&w::ak47::physical); f.geometry.magazine.grip_distance=0; f.trigger(true);
			f.writable=false; f.geometry.hand_in_gun=scale(w::ak47::manual_magazine.pull_axis,.051f); f.step();
			const auto attempts=f.attempts; f.writable=true; f.step();
			check(!f.control.magazine_grabbed() && f.state.magazine_inserted && f.attempts==attempts,"failed hand pull also requires a new grasp");
		}
	}
}
