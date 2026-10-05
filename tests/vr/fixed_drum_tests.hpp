#pragma once
#include "component/vr/gameplay/weapons/striker/profile.hpp"
#include "component/vr/gameplay/tube_runtime.hpp"
namespace fixed_drum_tests
{
	template<class Check>void run(Check check)
	{
		namespace w=vr::gameplay::weapons;namespace t=w::tube;using namespace std::chrono_literals;
		const auto r=w::striker::feed.ammunition;
		for(auto rear:{vr::hand::left,vr::hand::right})
		{
			const auto off=rear==vr::hand::left?vr::hand::right:vr::hand::left;auto s=t::import_native(r,132,1,{0,36});const auto total=t::total_rounds(s);
			const auto plan=[&](t::operation op){return t::plan(r,s,{op,s.weapon,s.instance_generation,s.revision,rear,op==t::operation::accepted_shot ? rear : off});};
			const auto apply=[&](t::operation op){const auto tx=plan(op);check(bool(tx),"fixed-drum expected transaction succeeds");if(tx)s=tx.next;return tx;};
			check(t::valid(r,s) && s.phase==t::action::closed && !s.chamber,"empty Striker has no fictional locked-open chamber");
			for(int i=0;i<12;++i){apply(t::operation::draw);apply(t::operation::load_port);check(s.stored==i+1 && s.drum_index==unsigned(i+1)%12,"single shell loads and automatically indexes exactly once");}
			check(t::native_ammo(s).loaded==12 && t::loaded_capacity(r)==12 && t::total_rounds(s)==total,"Striker capacity is twelve total, never twelve plus one");
			apply(t::operation::draw);check(!plan(t::operation::load_port),"full drum retains incoming held shell");apply(t::operation::cleanup);
			for(auto op:{t::operation::rack_open,t::operation::rack_close,t::operation::load_tube})check(!plan(op),"Striker has no rack or bottom tube transaction");
			for(int i=0;i<12;++i){check(t::ready(r,s),"every loaded drum chamber can fire");apply(t::operation::accepted_shot);}
			check(!t::ready(r,s) && !plan(t::operation::accepted_shot) && t::total_rounds(s)==total-12,"empty drum cannot spend another round or auto refill");
			apply(t::operation::draw);apply(t::operation::load_port);check(t::ready(r,s),"one shell loaded into empty drum is immediately usable");
			apply(t::operation::draw);const auto stale=plan(t::operation::load_port);apply(t::operation::accepted_shot);
			check(stale.before!=t::native_ammo(s),"interleaved fire invalidates stale shell insertion compare");apply(t::operation::load_port);
			check(s.stored==1 && t::total_rounds(s)==total-13,"interleaved fire and load conserve rounds");
			auto bad=s;bad.chamber=true;check(!t::valid(r,bad),"fixed drum rejects extra chamber bit");bad=s;bad.drum_index=12;check(!t::valid(r,bad),"fixed drum rejects out-of-range index");
			t::presentation saved;saved.active=true;saved.ammo=s;saved.resume_transfer();check(saved.ammo.stored==s.stored && saved.ammo.drum_index==s.drum_index,"drop and pickup retain drum count and index");
		}
		struct fixture
		{
			t::state state=t::import_native(w::striker::feed.ammunition,132,1,{0,36});
			w::hold owner{132,1,vr::hand::right,vr::hand::none,w::hold_source::engine_default,1};
			t::controller control;vr::controller_input::frame input{};t::geometry geometry{true,132,1,1,1};
			t::clock::time_point now{1s};w::ammunition::projection native{0,36};bool writable{true};int commits{};
			fixture(){input.focused=true;input.sequence=input.reference_generation=1;geometry.port_delta=geometry.tube_delta={0,0,1};geometry.port_alignment=geometry.tube_alignment=1;
				for(int h=0;h<2;++h){input.grip[h].valid=input.aim[h].valid=true;input.trigger[h]={true,false,0,1};}step();}
			void step(){now+=10ms;++input.sequence;input.sampled_at=now;geometry.input_sequence=input.sequence;
				control.update(w::striker::feed.interaction,w::striker::feed.ammunition,state,input,owner,geometry,true,now,[&](const auto& tx){if(!writable || native!=tx.before)return false;native=tx.after;++commits;return true;});}
			void trigger(bool down){auto& key=input.trigger[1-int(owner.holding_hand())];if(down && !key.down)++key.presses;key.down=down;step();}
			void draw(){geometry.waist_distance=0;geometry.port_delta=geometry.tube_delta={0,0,1};trigger(false);trigger(true);step();geometry.waist_distance=10;}
		};
		for(auto rear:{vr::hand::left,vr::hand::right})
		{
			fixture f;f.owner.rear=rear;f.step();f.geometry.rack_distance=0;f.geometry.rack_pose=0;f.trigger(true);check(!f.control.rack_held(),"Striker gesture never captures an invented charging handle");
			f.draw();check(f.state.held_rounds==1,"either hand draws a Striker shell");
			f.geometry.tube_delta={};f.step();check(f.state.held_rounds==1 && !f.state.stored,"bottom contact cannot load fixed drum");
			f.geometry.port_delta={};f.geometry.port_alignment=-1;f.step();check(!f.state.stored,"backward shell rejected at drum port");
			f.geometry.port_alignment=1;f.step();check(f.state.stored==1 && f.state.drum_index==1 && t::ready(r,f.state),"proper side contact loads without rack or extra button");
			const auto count=f.commits;for(int i=0;i<20;++i)f.step();check(f.commits==count,"held trigger cannot repeatedly insert or index");
			f.draw();f.writable=false;f.geometry.port_delta={};f.step();f.writable=true;f.step();check(f.state.held_rounds==1 && f.state.drum_index==1,"failed native write retains shell and index");
			f.geometry.port_delta=f.geometry.tube_delta={0,0,1};f.step();f.geometry.port_delta={};f.step();check(f.state.stored==2,"new approach retries a failed insertion");
			f.draw();const auto total=t::total_rounds(f.state);f.input.focused=false;f.step();check(!f.state.held_rounds && t::total_rounds(f.state)==total,"focus loss returns escrow without duplicate shell");
		}
	}
}
