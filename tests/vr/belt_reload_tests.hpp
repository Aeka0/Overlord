#pragma once
#include "component/vr/gameplay/weapons/m240/profile.hpp"
#include "component/vr/gameplay/weapons/mg4/profile.hpp"
#include "component/vr/gameplay/weapons/rpd/profile.hpp"
#include "component/vr/gameplay/weapons/m240/reload_profile.hpp"
#include "component/vr/gameplay/weapons/mg4/reload_profile.hpp"
#include "component/vr/gameplay/weapons/rpd/reload_profile.hpp"

namespace belt_reload_tests
{
	template<class Fixture,class Check>void run(Check check)
	{
		namespace w=vr::gameplay::weapons;namespace m=w::mechanics;using vr::hand;
		{
			Fixture f(&w::m240::physical);auto opened=f.state;opened.belt.cover=1;opened.belt.laid=false;f.adopt(opened);f.step();
			f.geometry.belt.cover_distance=f.geometry.belt.chain_distance=0;f.trigger(true);
			check(f.control.belt_grip().part==w::belt_feed::lease::chain,"loose chain wins an exact overlap with enlarged cover capture");
		}
		for(const auto* p:{&w::m240::physical,&w::mg4::physical,&w::rpd::physical,&w::rpd::digital_physical})for(auto rear:{hand::left,hand::right})
		for(std::uint8_t style=0;style<p->slide_grips.size();++style)
		{
			Fixture f(p);f.owner.rear=rear;auto closed=f.state;closed.action=m::action_state::closed;f.adopt(closed);f.step();
			const auto total=m::total_rounds(f.state);
			f.geometry.slide_distance=0;f.geometry.slide_pose=style;f.trigger(true);
			check(f.control.slide_held() && f.control.slide_grip().pose==style,"both belt-fed hook styles acquire with either hand");
			const auto grip=f.control.slide_grip();f.geometry.slide_pose=(style+1)%p->slide_grips.size();
			f.move_slide(grip,p->interaction.slide_stroke);f.move_slide(grip,0);
			check(f.control.slide_grip().pose==style && f.state.action==m::action_state::cocked_open && m::total_rounds(f.state)==total,
				"turning wrist during handle travel keeps grasp style and independently cocks without spending rounds");
			f.trigger(false);check(!f.control.slide_held(),"each hook grasp releases normally");
			f.trigger(true);check(f.control.slide_held() && f.control.slide_grip().pose==f.geometry.slide_pose,"new press can choose a different handle style");
		}
		for(const auto* p:{&w::m240::physical,&w::mg4::physical,&w::rpd::physical,&w::rpd::digital_physical})for(auto rear:{hand::left,hand::right})
		for(const std::uint8_t style:{std::uint8_t{2},w::no_part_grip})
		{
			Fixture f(p);f.owner.rear=rear;f.step();const auto revision=f.state.revision;
			f.geometry.slide_distance=0;f.geometry.slide_pose=style;f.trigger(true);
			check(!f.control.slide_held() && f.state.revision==revision,
				"out-of-range belt-fed style cannot acquire or alter mechanics even at zero contact distance");
		}
		for(const auto* p:{&w::m240::physical,&w::mg4::physical})for(auto rear:{hand::left,hand::right})
		for(bool empty:{false,true})for(int cock_stage:{0,1,2,3})
		{
			Fixture f(p);f.owner.rear=rear;auto supplied=f.state;supplied.reserve_rounds=300;f.adopt(supplied);f.step();const auto off=rear==hand::left?hand::right:hand::left;
			const auto query=[&](m::operation op,float cover=0.f){m::request q{op,f.state.weapon,f.state.instance_generation,f.state.revision,rear,
				(op==m::operation::accepted_shot || op==m::operation::dry_fire)?rear:off};q.cover=cover;return m::plan(*f.rules,f.state,q);};
			const auto apply=[&](m::operation op,float amount=0.f){const auto tx=query(op,amount);check(bool(tx),"belt expected transaction admitted");if(tx && f.commit(tx))f.state=tx.next;};
			if(empty)for(int i=0;i<100;++i)apply(m::operation::accepted_shot);
			else {auto s=f.state;s.action=m::action_state::closed;f.adopt(s);}
			const auto total=m::total_rounds(f.state);
			check(f.state.action==m::action_state::closed && !f.state.chamber_loaded,"empty last shot releases bolt without plus-one chamber");
			check(!query(m::operation::pull_magazine),"closed cover prevents box withdrawal");
			if(cock_stage==0)apply(m::operation::cycle_action);
			apply(m::operation::move_cover,1);check(!m::ready(*f.rules,f.state),"open cover blocks firing even with cocked loaded box");
			apply(m::operation::pull_magazine);
			check(!f.state.belt.laid && f.state.held_rounds==(empty?0:100),"removing old box automatically withdraws belt and transfers actual rounds");
			apply(m::operation::cancel_magazine);
			if(cock_stage==1)apply(m::operation::cycle_action);
			apply(m::operation::draw_magazine);apply(m::operation::insert_magazine);
			check(!f.state.belt.laid && !m::ready(*f.rules,f.state),"new box does not lay belt or cock bolt");
			if(cock_stage==2)apply(m::operation::cycle_action);
			apply(m::operation::move_cover,0);check(!m::ready(*f.rules,f.state) && !query(m::operation::lay_belt),"closing unlaid belt cannot feed or silently seat it");
			apply(m::operation::move_cover,1);apply(m::operation::lay_belt);apply(m::operation::move_cover,0);
			if(cock_stage==3)apply(m::operation::cycle_action);
			check(m::ready(*f.rules,f.state) && m::native_ammo(f.state).loaded==100 && m::total_rounds(f.state)==total,"cock before/during/after reload reaches same conserved ready state");
			apply(m::operation::extract_chamber);apply(m::operation::finish_stroke);
			check(m::total_rounds(f.state)==total && f.state.belt.laid,"recocking loaded gun never ejects ammunition or unseats belt");
			auto saved=f.state;f.interrupt();check(f.state.belt.cover==saved.belt.cover && f.state.belt.laid==saved.belt.laid && f.state.action==saved.action,"interruption preserves feed and bolt mechanics");
		}
		for(const auto* p:{&w::m240::physical,&w::mg4::physical})for(auto rear:{hand::left,hand::right})
		{
			Fixture f(p);f.owner.rear=rear;f.step();auto& c=f.geometry.belt;
			const auto total=m::total_rounds(f.state);c.cover_distance=0;f.trigger(true);
			check(f.control.belt_grip().part==w::belt_feed::lease::cover,"either hand acquires cover");
			c.cover_angle=p->interaction.belt->cover_angle*.01f;f.step();check(f.control.belt_grip().part==w::belt_feed::lease::cover,"cover latch tolerance does not release grip on tiny initial motion");
			c.cover_angle=p->interaction.belt->cover_angle*.5f;f.step();check(f.state.belt.cover>.49f && f.state.belt.cover<.51f,"cover follows hand angle rather than timer");
			f.trigger(false);const auto half=f.state.belt.cover;f.step();check(f.state.belt.cover==half,"released partial cover stays put");
			f.trigger(true);c.cover_angle=p->interaction.belt->cover_angle;f.step();f.trigger(false);check(f.state.belt.cover==1,"regrip continues partial cover without resetting");
			c.cover_distance=10;f.geometry.magazine.grip_distance=0;f.trigger(true);
			f.geometry.hand_in_gun=vr::gameplay::hands::scale(p->interaction.manual_magazine->pull_axis,.06f);f.step();
			check(!f.state.magazine_inserted && !f.state.belt.laid && f.state.held_rounds==100,"physical pull atomically withdraws nonempty box and belt");
			f.trigger(false);f.geometry.magazine.grip_distance=1;f.geometry.waist_distance=0;f.trigger(true);
			f.geometry.magazine_top_in_well={0,0,-.2f};f.step();f.geometry.magazine_top_in_well={0,0,-.04f};f.step();f.trigger(false);f.geometry.waist_distance=10;
			check(f.state.magazine_inserted && !f.state.belt.laid,"physical insertion still requires explicit chain grasp");
			c.chain_distance=0;f.trigger(true);check(f.control.belt_grip().part==w::belt_feed::lease::chain,"leading link gets own hand lease");
			c.feed_distance=0;c.alignment=-1;f.step();check(!f.state.belt.laid,"backward feed contact rejected");
			c.alignment=1;f.writable=false;f.step();f.writable=true;f.step();check(!f.state.belt.laid,"failed belt compare is never deferred");
			f.trigger(false);f.trigger(true);f.step();check(f.state.belt.laid,"fresh belt grasp retries insertion exactly once");
			f.trigger(false);c.chain_distance=10;c.cover_distance=0;f.trigger(true);c.cover_angle=0;f.step();f.trigger(false);
			check(m::ready(*f.rules,f.state) && m::total_rounds(f.state)==total,"physical belt path preserves early cocking and round conservation");
			f.trigger(true);c.cover_angle=p->interaction.belt->cover_angle*.4f;f.step();f.input.focused=false;f.step();
			check(f.control.belt_grip().part==w::belt_feed::lease::none && f.state.belt.cover>0,"tracking loss releases hand and preserves cover angle");
		}
		for(bool box:{false,true})for(float cover:{0.f,.5f,1.f})
		{
			Fixture f(&w::m240::physical);auto s=f.state;s.magazine_inserted=box;s.magazine_rounds=box?100:0;s.belt={cover,false};f.adopt(s);
			m::request q{m::operation::dry_fire,s.weapon,s.instance_generation,s.revision,hand::right,hand::right};
			const auto tx=m::plan(*f.rules,s,q);check(tx && tx.next.action==m::action_state::closed && tx.after==tx.before,"empty feed releases cocked bolt without spending unlaid box ammunition");
		}
		for(const auto* p:{&w::m240::physical,&w::mg4::physical})
		{
			Fixture f(p);auto& c=f.geometry.belt;c.cover_distance=.14f;f.geometry.slide_distance=.10f;f.trigger(true);
			check(f.control.belt_grip().part==w::belt_feed::lease::cover && !f.control.slide_held(),"nearby cover wins overlap against charging handle");
			f.trigger(false);c.cover_distance=.14f;f.geometry.slide_distance=0;f.trigger(true);
			check(f.control.slide_held(),"direct handle grasp outside cover preference remains reachable");f.trigger(false);
			auto s=f.state;s.belt={1,false};f.adopt(s);c.cover_distance=10;c.chain_distance=.07f;c.feed_distance=.09f;c.alignment=0;
			f.geometry.slide_distance=1;f.geometry.magazine.grip_distance=0;f.trigger(true);f.step();
			check(f.control.belt_grip().part==w::belt_feed::lease::chain && !f.control.magazine_grabbed() && !f.state.belt.laid,"chain wins box overlap but overlapping tray radius cannot auto-load on grab");
			c.feed_distance=.06f;f.step();check(f.state.belt.laid,"deliberate short approach seats belt with relaxed position and wrist angle");
		}
	}
}
