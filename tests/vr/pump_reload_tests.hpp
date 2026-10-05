#pragma once
#include "component/vr/gameplay/weapons/spas12/profile.hpp"
#include "component/vr/gameplay/weapons/winchester1200/profile.hpp"
#include "component/vr/gameplay/tube_profiles.hpp"

namespace pump_reload_tests
{
	template<class Check> void run(Check check)
	{
		namespace w=vr::gameplay::weapons;namespace t=w::tube;using namespace std::chrono_literals;
		struct fixture
		{
			const w::tube_profile& profile;t::state state;w::hold owner;t::controller control;
			vr::controller_input::frame input{};t::geometry geometry{true,38,1,1,1};
			t::clock::time_point now{1s};w::ammunition::projection native{7,21};bool writable{true};int ejected{},cases{},commits{};
			std::chrono::milliseconds cadence;
			fixture(const w::tube_profile& p,vr::hand rear,std::chrono::milliseconds interval):profile(p),state(t::import_native(p.ammunition,38,1,{7,21})),owner{38,1,rear,vr::hand::none,w::hold_source::interaction,1},cadence(interval)
			{
				input.focused=true;input.sequence=input.reference_generation=input.continuity_generation=1;
				geometry.port_delta=geometry.tube_delta={0,0,1};geometry.port_alignment=geometry.tube_alignment=1;
				for(int h=0;h<2;++h){input.grip[h].valid=input.aim[h].valid=true;input.trigger[h]=input.squeeze[h]=input.secondary[h]={true,false,0,1};}
				step();
			}
			int off()const{return 1-int(owner.rear);}
			bool commit(const t::transaction& tx){if(!writable || native!=tx.before)return false;native=tx.after;ejected+=tx.ejected;cases+=tx.case_ejected;++commits;return true;}
			void step(){now+=cadence;++input.sequence;input.sampled_at=now;geometry.input_sequence=input.sequence;control.update(profile.interaction,profile.ammunition,state,input,owner,geometry,true,now,[&](const auto& tx){return commit(tx);},{owner.support==vr::hand::none});}
			void support(bool value){owner.support=value ? vr::hand(off()) : vr::hand::none;input.squeeze[off()].down=value;step();}
			void secondary(int h,bool value){input.secondary[h].down=value;step();}
			void pinch(bool value){auto& key=input.trigger[off()];if(value && !key.down)++key.presses;key.down=value;step();}
			void move(float travel){geometry.hand_in_gun=vr::gameplay::hands::add(control.rack_grip().start,vr::gameplay::hands::scale(profile.interaction.rack.slide_axis,travel-control.rack_grip().initial_travel));step();}
			bool shot(){auto tx=t::plan(profile.ammunition,state,{t::operation::accepted_shot,38,1,state.revision,owner.rear,owner.rear});if(!tx || !commit(tx))return false;state=tx.next;return true;}
			void draw(){geometry.rack_distance=10;geometry.waist_distance=0;geometry.port_delta=geometry.tube_delta={0,0,1};pinch(false);pinch(true);step();geometry.waist_distance=10;}
		};
		// Fresh producer data may be consumed every 200ms in native slow motion.
		for(auto cadence:{10ms,200ms})for(auto* p:{&w::spas12::feed,&w::spas12::arctic_feed,&w::winchester1200::feed})for(auto rear:{vr::hand::left,vr::hand::right})
		{
			const float stroke=p->interaction.rack.slide_stroke;
			fixture f(*p,rear,cadence);f.support(true);
			check(!f.control.rack_held() && f.control.travel()==0,"loaded pump remains locked while supporting hand holds it");
			f.secondary(int(rear),true);f.secondary(int(rear),false);
			check(!f.control.rack_held(),"releasing unlock before any stroke relocks immediately");
			check(!t::plan(p->ammunition,f.state,{t::operation::rack_open,38,1,f.state.revision,rear,vr::hand(f.off())}),"feed authority rejects live extraction without mechanical release");
			check(f.shot() && !f.state.chamber && f.state.spent_case && f.state.stored==6,"pump shot consumes chamber only and retains case and tube");
			check(!f.shot(),"pump cannot fire a second round before manual cycling");f.step();
			check(f.control.rack_held(),"empty chamber unlocks the already held fore-end without another grip press");
			f.move(stroke*.5f);f.support(false);
			check(f.state.spent_case && f.control.travel()>0 && !f.control.rack_held(),"partial pump release preserves stroke and cannot auto eject or chamber");
			f.support(true);f.move(stroke);f.step();f.step();
			check(f.cases==1 && f.ejected==0 && !f.state.spent_case && f.state.phase==t::action::held_open,"full rear travel ejects one spent case exactly once");
			f.support(false);f.draw();f.geometry.port_delta={};f.step();
			check(f.state.chamber && f.state.phase==t::action::held_open && f.state.stored==6 && !t::ready(p->ammunition,f.state),"open port accepts one round without auto closing or feeding tube");
			f.pinch(false);f.draw();f.geometry.port_delta={};f.step();check(f.state.held_rounds==1,"occupied open port rejects a second round");
			f.geometry.port_delta={0,0,1};f.geometry.tube_delta={};f.step();check(f.state.stored==7 && !f.state.held_rounds,"a held shell may instead top up the tube");
			f.pinch(false);f.support(true);f.move(0);
			check(f.state.chamber && f.state.stored==7 && f.control.travel()==0 && t::ready(p->ammunition,f.state),"front stop chambers port round and preserves full tube");
			for(int release_hand:{int(rear),f.off()})
			{
				f.secondary(release_hand,true);check(f.control.rack_held(),"either holding hand can hold magazine release to unlock pump");
				const int before=f.ejected;f.move(stroke);f.move(0);f.step();
				check(f.ejected==before+1 && f.state.chamber && f.control.rack_held(),"live extraction spends one shell; held release keeps the completed cycle unlocked");
				f.secondary(release_hand,false);
				check(!f.control.rack_held(),"releasing unlock at the front stop relocks the loaded pump");
			}
			for(bool support:{false,true})for(int release_hand:{int(rear),f.off()})
			{
				fixture held(*p,rear,cadence);held.geometry.rack_distance=0;
				if(support)held.support(true);
				held.secondary(int(rear),true);
				if(!support)held.pinch(true);
				if(release_hand!=int(rear)){held.secondary(release_hand,true);held.secondary(int(rear),false);}
				check(held.control.rack_held(),"release held by either holding hand unlocks a supported or pinched pump");
				held.move(stroke*.5f);held.move(0);
				check(held.control.rack_held() && held.ejected==0 && held.native.loaded==7,"partial return preserves held unlock without ejecting ammunition");
				for(int cycle=1;cycle<=3;++cycle)
				{
					held.move(stroke);held.step();
					check(held.ejected==cycle && held.state.phase==t::action::held_open,"each repeated full pull ejects exactly one live shell");
					held.move(0);
					check(held.control.rack_held() && held.control.travel()==0 && held.state.chamber && held.state.stored==6-cycle && held.native.loaded==7-cycle,
						"held release and grasp permit consecutive cycles without another button press or regrip");
				}
				held.move(stroke*.5f);held.secondary(release_hand,false);held.move(stroke);held.move(0);
				check(!held.control.rack_held() && held.control.travel()==0 && held.ejected==4 && held.state.chamber && held.native.loaded==3,
					"releasing unlock midway finishes the current cycle and relocks at the front stop");
				held.move(stroke);
				check(!held.control.rack_held() && held.control.travel()==0 && held.ejected==4,"released unlock prevents another live extraction");
			}
			f.secondary(int(rear),true);f.move(stroke*.5f);f.input.focused=false;f.step();const auto retained=f.control.travel();
			check(retained>0 && !f.control.rack_held(),"tracking interruption retains partial physical travel");
			f.input.focused=true;f.secondary(int(rear),false);f.move(stroke);f.move(0);
			check(f.control.travel()==0 && f.state.chamber,"partial cycle resumes safely after interruption");
			f.secondary(int(rear),true);f.writable=false;const int before=f.ejected;f.move(stroke);
			check(f.state.chamber && f.ejected==before,"failed native compare cannot eject or advance ammunition");
			f.writable=true;f.secondary(int(rear),false);
			fixture empty(*p,rear,cadence);empty.state=t::import_native(p->ammunition,38,1,{0,21});empty.native={0,21};empty.step();empty.draw();empty.geometry.tube_delta={};empty.step();
			check(empty.state.stored==1 && !empty.state.chamber && empty.state.phase==t::action::closed,"bottom-loaded empty pump still needs a full manual cycle");
			empty.pinch(false);empty.support(true);empty.move(stroke*.5f);empty.support(false);empty.draw();empty.geometry.port_delta={};empty.step();
			check(empty.state.held_rounds==1 && !empty.state.chamber,"half-open port cannot load a shell");
			empty.input.grip[empty.off()].valid=false;empty.step();check(!empty.state.held_rounds && empty.native.reserve==20,"tracking loss refunds held shell exactly once");
			check(w::native_tube_shape_supported(p->native_variants[0],7,true,1),"captured pump definitions admitted as segmented single-shell feeds");
		}
	}
}
