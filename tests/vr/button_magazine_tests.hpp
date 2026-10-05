#pragma once
#include "component/vr/gameplay/weapons/de50/profile.hpp"
#include "component/vr/gameplay/weapons/m1911/profile.hpp"
#include "component/vr/gameplay/weapons/m4/profile.hpp"
#include "component/vr/gameplay/weapons/m9/profile.hpp"
#include "component/vr/gameplay/weapons/usp/profile.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include "component/vr/gameplay/physical_reload_contact_sample.hpp"

namespace button_magazine_tests
{
	template<class Fixture,class Check> void run(Check check)
	{
		namespace w=vr::gameplay::weapons;namespace m=w::mechanics;namespace p=w::physical_reload;
		using namespace vr::gameplay::hands;
		for(const auto* d:w::reload_profiles)if(d->ammunition.release==m::magazine_release::button && !d->interaction.support_magazine_catch)
		for(auto rear:{vr::hand::left,vr::hand::right})for(bool empty:{false,true})
		{
			Fixture f(d);f.owner.rear=rear;f.step();const auto off=vr::hand(1-int(rear));
			auto s=f.state;s.magazine_rounds=empty?0:3;s.chamber_loaded=!empty && d->ammunition.feed==m::feed_type::closed_bolt;
			s.action=empty && d->ammunition.last_round_lock?m::action_state::locked_open:m::action_state::closed;f.adopt(s);
			const auto total=m::total_rounds(s);const auto reserve=s.reserve_rounds;
			const auto wrap=w::select_magazine_grip(*d,{0,0,0,1},int(off),{1,0,0,0},false,false,0,std::nullopt,true);
			f.geometry.attached_magazine_pose=wrap.index;f.geometry.magazine.grip_distance=0;f.geometry.magazine_top_in_well={};f.trigger(true);
			check(f.control.magazine_grabbed() && f.state.magazine_inserted && f.commits==0,"button magazine can be grasped before releasing, with no ammunition transaction");
			f.geometry.hand_in_gun={0,0,-.12f};f.geometry.seated_hand_distance=.12f;f.step();
			check(f.control.magazine_grabbed() && f.state.magazine_rounds==s.magazine_rounds && f.state.magazine_hand==vr::hand::none,"pulling a button-locked magazine cannot detach it");
			f.button(true);
			check(!f.state.magazine_inserted && f.state.magazine_hand==off && f.state.held_rounds==s.magazine_rounds &&
				f.state.reserve_rounds==reserve && m::total_rounds(f.state)==total && f.last_effect==m::effect::magazine_take,
				"rear release transfers the actual partial or empty magazine once without refunding or spawning a drop");
			const int commits=f.commits;f.step(false);f.step();
			check(f.commits==commits && f.control.requires_withdrawal() && !f.state.magazine_inserted,"duplicate frames and stationary contact cannot reinsert a caught magazine");
			f.geometry.magazine_top_in_well={d->interaction.well_radius+d->interaction.well_withdraw_margin-.002f,0,0};f.step();
			check(f.control.requires_withdrawal(),"small motion outside the mouth but inside its clearance margin does not rearm insertion");
			f.geometry.magazine_top_in_well[0]+=.004f;f.step();
			check(!f.control.requires_withdrawal() && !f.state.magazine_inserted,"leaving the full clearance volume rearms without inserting on the exit frame");
			f.geometry.magazine_top_in_well={};f.step();
			check(f.state.magazine_inserted && f.state.magazine_rounds==s.magazine_rounds && f.control.magazine_seated() &&
				f.control.magazine_pose()==wrap.index && m::total_rounds(f.state)==total,"deliberate return inserts the same payload and uses the attached wrap recipe");
			f.button(false);f.button(true);
			check(f.state.magazine_hand==off && !f.state.magazine_inserted,"continued seated grasp can catch another release without a new pinch");
			f.trigger(false);f.step();
			check(f.state.magazine_hand==vr::hand::none && f.state.reserve_rounds==reserve+s.magazine_rounds && m::total_rounds(f.state)==total,"letting go retains the existing one-time refund policy");
		}
		for(auto rear:{vr::hand::left,vr::hand::right})
		{
			{
				Fixture same_frame(&w::m4::physical);same_frame.owner.rear=rear;same_frame.step();same_frame.geometry.magazine.grip_distance=0;
				auto& pinch=same_frame.input.trigger[1-int(rear)];pinch.down=true;++pinch.presses;same_frame.button(true);
				check(same_frame.state.magazine_hand==vr::hand(1-int(rear)) && same_frame.last_effect==m::effect::magazine_take,
					"same-sample new pinch and release catch through the granted attached contact");
			}
			{
			Fixture f(&w::m4::physical);f.owner.rear=rear;f.step();f.geometry.magazine.grip_distance=0;f.trigger(true);
			f.writable=false;f.button(true);const auto attempts=f.attempts;f.writable=true;f.step();
			check(f.attempts==attempts && f.state.magazine_inserted && f.control.magazine_grabbed(),"failed catch retains the locked grasp and consumes the release edge");
			f.button(false);f.button(true);check(f.state.magazine_hand!=vr::hand::none,"fresh release can retry a previously rejected catch");
			}
			for(int separation=0;separation<3;++separation)
			{
				Fixture broken(&w::m4::physical);broken.owner.rear=rear;broken.step();broken.geometry.magazine.grip_distance=0;broken.trigger(true);
				if(separation==0)broken.input.trigger[1-int(rear)].down=false;
				if(separation==1)broken.geometry.seated_hand_distance=.36f;
				if(separation==2)broken.geometry.hand_in_gun={1,0,0};
				broken.button(true);
				check(!broken.control.magazine_grabbed() && broken.state.magazine_hand==vr::hand::none && broken.last_effect==m::effect::magazine_out,
					"same-frame pinch release, separation or tracking jump cannot catch through an obsolete grasp");
			}
			for(const auto* d:{&w::m9::physical,&w::m1911::physical,&w::usp::physical,&w::usp::silenced_physical,&w::de50::physical})
			for(bool empty:{false,true})
			{
				Fixture catch_support(d);auto& f=catch_support;f.owner.rear=rear;const int off=1-int(rear);
				f.owner.support=vr::hand(off);f.input.squeeze[off]={true,true,1,1};f.geometry.magazine_top_in_well={};f.step();
				if(empty){auto s=f.state;s.magazine_rounds=0;s.chamber_loaded=false;s.action=m::action_state::locked_open;f.adopt(s);}
				const auto total=m::total_rounds(f.state);f.trigger(true);f.button(true);
				check(f.state.magazine_hand==vr::hand(off) && f.input.trigger[off].down,
					"short pistol support catches empty and loaded magazines only with the extra Trigger hold");
				f.owner.support=vr::hand::none;f.step();f.input.squeeze[off].down=false;f.step();
				check(f.state.magazine_hand==vr::hand(off) && f.control.requires_withdrawal(),"support removal and Grip release preserve Trigger-held magazine and withdrawal gate");
				f.geometry.magazine_top_in_well={0,0,-.2f};f.step();f.geometry.magazine_top_in_well={};f.step();
				check(f.state.magazine_inserted && f.control.magazine_seated(),"Trigger-held catch can reinsert after leaving the smaller clearance volume");
				f.trigger(false);check(!f.control.magazine_seated() && m::total_rounds(f.state)==total,"Trigger release ends seated holding without deleting the inserted magazine");
				Fixture drop(d);drop.owner.rear=rear;drop.owner.support=vr::hand(off);drop.input.squeeze[off]={true,true,1,1};drop.step();drop.button(true);
				check(drop.state.magazine_hand==vr::hand::none && drop.last_effect==m::effect::magazine_out,"supporting a pistol without Trigger lets the magazine drop normally");
				Fixture let_go(d);let_go.owner.rear=rear;let_go.owner.support=vr::hand(off);let_go.input.squeeze[off]={true,true,1,1};let_go.step();let_go.trigger(true);let_go.button(true);
				let_go.owner.support=vr::hand::none;let_go.trigger(false);
				check(let_go.state.magazine_hand==vr::hand::none && let_go.input.squeeze[off].down,"releasing Trigger drops the caught magazine even while Grip stays held");
			}
			Fixture rejected(&w::m9::physical);rejected.owner.rear=rear;const int off=1-int(rear);rejected.owner.support=vr::hand(off);
			rejected.input.squeeze[off]={true,true,1,1};rejected.step();rejected.trigger(true);rejected.writable=false;rejected.button(true);
			check(rejected.state.magazine_inserted && rejected.state.magazine_hand==vr::hand::none,"rejected support transfer cannot create a magazine lease");
			rejected.writable=true;rejected.step();check(rejected.state.magazine_inserted,"rejected support catch is not a queued release");
			rejected.button(false);rejected.button(true);rejected.owner.support=vr::hand::none;
			const auto before=m::total_rounds(rejected.state);rejected.writable=false;
			check(!rejected.interrupt() && rejected.state.magazine_hand==vr::hand(off),"failed forced refund preserves magazine custody");
			rejected.writable=true;check(rejected.interrupt() && rejected.state.magazine_hand==vr::hand::none && m::total_rounds(rejected.state)==before,"forced cleanup settles exactly once after a rejected compare");
			Fixture lock(&w::m9::physical);lock.owner.rear=rear;lock.owner.support=vr::hand(off);lock.input.squeeze[off]={true,true,1,1};lock.step();
			auto locked=lock.state;locked.chamber_loaded=false;locked.action=m::action_state::locked_open;lock.adopt(locked);lock.button(true);
			check(lock.state.magazine_inserted && lock.state.chamber_loaded && lock.state.magazine_hand==vr::hand::none,"loaded lock release keeps its original bolt-release priority and does not steal support");
		}
	}
}
