#pragma once
#include "component/vr/gameplay/weapons/m1014/profile.hpp"
#include "component/vr/gameplay/weapons/spas12/profile.hpp"
#include "component/vr/gameplay/weapons/winchester1200/profile.hpp"
#include "component/vr/gameplay/tube_profiles.hpp"
#include "component/vr/gameplay/tube_presenter.hpp"
#include "m1014_data.hpp"
#include "component/vr/gameplay/native_reload_layout.hpp"

namespace tube_reload_tests
{
	template<class Check> void run(Check check)
	{
		namespace w=vr::gameplay::weapons;namespace t=w::tube;using namespace std::chrono_literals;
		const t::rules rules{7};
		for(auto* p:{&w::m1014::feed,&w::spas12::feed,&w::winchester1200::feed})
		{
			const auto& extension=p->interaction.loading;const float radius=p->interaction.tube_radius;
			check(extension.distance({0,0,-radius-.039f})<=radius,"shotgun loading extends four centimetres down");
			check(extension.distance({0,0,-radius-.041f})>radius,"shotgun loading retains a bounded lower limit");
			check(extension.distance({0,0,radius+.001f})>radius,"shotgun upper loading boundary never expands");
			for(float direction:{-1.f,1.f})
			{
				check(extension.distance({direction*(radius+.014f),0,0})<=radius && extension.distance({0,direction*(radius+.014f),0})<=radius,
					"shotgun loading expands each horizontal direction by one and a half centimetres");
				check(extension.distance({direction*(radius+.016f),0,0})>radius,"horizontal assistance stays bounded");
			}
			check(extension.distance({0,0,radius})<=radius,"every old sphere contact remains accepted");
		}
		for(auto rear:{vr::hand::left,vr::hand::right})
		{
			const auto off=rear==vr::hand::left ? vr::hand::right : vr::hand::left;
			auto s=t::import_native(rules,42,1,{7,28});const auto initial=t::total_rounds(s);
			const auto apply=[&](t::operation op){const auto tx=t::plan(rules,s,{op,s.weapon,s.instance_generation,s.revision,rear,op==t::operation::accepted_shot ? rear : off});check(bool(tx),"tube expected transaction succeeds");if(tx)s=tx.next;return tx;};
			check(s.stored==6 && s.chamber,"native total is partitioned into chamber and fixed tube");
			apply(t::operation::draw);apply(t::operation::load_tube);
			check(s.stored==7 && s.chamber && t::native_ammo(s).loaded==8 && t::total_rounds(s)==initial,"bottom top-up supports seven plus one without replacing contents");
			apply(t::operation::draw);
			for(auto op:{t::operation::load_tube,t::operation::load_port,t::operation::rack_open})
				check(!t::plan(rules,s,{op,s.weapon,s.instance_generation,s.revision,rear,off}),"full tube, closed side port and held shell reject invalid manipulation");
			apply(t::operation::cleanup);
			for(int n=0;n<8;++n)apply(t::operation::accepted_shot);
			check(!s.chamber && !s.stored && s.phase==t::action::locked_open && t::total_rounds(s)==initial-8,"last shot retains empty bolt open");
			apply(t::operation::draw);apply(t::operation::load_tube);
			check(s.stored==1 && !s.chamber && s.phase==t::action::locked_open && !t::ready(rules,s),"bottom loading while empty never auto chambers or releases bolt");
			apply(t::operation::rack_open);apply(t::operation::rack_close);
			check(s.chamber && !s.stored && t::ready(rules,s),"overpull and release feeds exactly one tube shell");
			const auto rack=apply(t::operation::rack_open);
			check(rack.ejected==1 && rack.rounds_spent==1 && !s.chamber,"manual rack ejects a live shell");
			check(!t::plan(rules,s,{t::operation::rack_open,s.weapon,s.instance_generation,s.revision,rear,off}),"holding the rack rear cannot repeatedly eject");
			apply(t::operation::rack_close);apply(t::operation::draw);apply(t::operation::load_port);
			check(s.chamber && s.phase==t::action::closed && !s.stored,"side-port shell directly chambers and releases empty bolt");
			apply(t::operation::accepted_shot);
			apply(t::operation::draw);apply(t::operation::load_tube);apply(t::operation::draw);apply(t::operation::load_port);
			check(s.stored==1 && s.chamber,"side loading does not consume the shell already in tube");
			const auto stale=t::plan(rules,s,{t::operation::draw,s.weapon,2,s.revision,rear,off});check(!stale,"tube stale instance cannot draw ammunition");
			auto corrupt=s;corrupt.revision=UINT64_MAX;check(!t::plan(rules,corrupt,{t::operation::draw,s.weapon,s.instance_generation,UINT64_MAX,rear,off}),"tube revision overflow fails closed");
		}
		struct fixture
		{
			t::state state=t::import_native({7},42,1,{0,28});w::hold owner{42,1,vr::hand::right,vr::hand::none,w::hold_source::engine_default,1};
			t::controller control;vr::controller_input::frame input{};t::geometry geometry{true,42,1,1,1};
			t::clock::time_point now{1s};w::ammunition::projection native{0,28};bool writable{true},manipulation{true};int commits{},attempts{},ejected{};
			fixture(){input.focused=true;input.sequence=input.reference_generation=1;geometry.port_delta=geometry.tube_delta={0,0,1};geometry.port_alignment=geometry.tube_alignment=1;
				for(int h=0;h<2;++h){input.grip[h].valid=input.aim[h].valid=true;input.trigger[h]={true,false,0,1};}step();}
			bool commit(const t::transaction& tx){++attempts;if(!writable || native!=tx.before)return false;native=tx.after;++commits;ejected+=tx.ejected;return true;}
			void step(bool advance=true){if(advance){now+=10ms;++input.sequence;}input.sampled_at=now;geometry.input_sequence=input.sequence;control.update(w::m1014::interaction,{7},state,input,owner,geometry,true,now,[&](const auto& tx){return commit(tx);},{manipulation});}
			void trigger(bool down){auto& key=input.trigger[1-int(owner.holding_hand())];if(down && !key.down)++key.presses;key.down=down;step();}
			void draw(){geometry.rack_distance=10;geometry.waist_distance=0;geometry.port_delta=geometry.tube_delta={0,0,1};trigger(false);trigger(true);step();geometry.waist_distance=10;}
			void rack(float travel){geometry.hand_in_gun=vr::gameplay::hands::add(control.rack_grip().start,vr::gameplay::hands::scale(w::m1014::interaction.rack.slide_axis,travel-control.rack_grip().initial_travel));step();}
		};
		for(auto rear:{vr::hand::left,vr::hand::right})
		{
			fixture f;f.owner.rear=rear;f.step();f.draw();check(f.state.held_rounds==1 && f.native.reserve==27,"each waist pinch draws one physical shell");
			f.geometry.tube_delta={};f.step();check(f.state.stored==1 && !f.state.chamber && f.state.phase==t::action::locked_open,"bottom insertion preserves lock in gesture path");
			const auto n=f.commits;f.step(false);f.step();check(f.commits==n,"held pinch and duplicate frames cannot auto draw or load another shell");
			f.geometry.rack_distance=0;f.trigger(false);f.trigger(true);check(f.control.rack_held(),"fresh pinch grips locked-open action");
			f.trigger(false);check(!f.state.chamber,"grabbing and releasing existing empty lock without travel cannot release it");
			f.trigger(true);f.rack(w::m1014::interaction.rack.slide_stroke);f.trigger(false);check(f.state.chamber && !f.state.stored,"overpull unlocks and release chambers bottom-loaded shell");
			f.trigger(true);f.rack(.025f);f.trigger(false);check(f.state.chamber && f.ejected==0,"partial rack retains live shell");
			f.trigger(true);f.rack(w::m1014::interaction.rack.slide_stroke);f.step();f.step();check(f.ejected==1,"full held rack ejects exactly once");
			f.trigger(false);f.draw();f.geometry.port_delta={};f.step();check(f.state.chamber && f.state.phase==t::action::closed,"side insertion immediately releases lock");
			f.draw();f.geometry.port_delta={};f.step();check(f.state.held_rounds==1 && !f.state.stored,"closed side port cannot accept ordinary top-up");
			f.geometry.port_delta={0,0,1};f.geometry.tube_delta={};f.geometry.tube_alignment=-1;f.step();check(!f.state.stored,"backward shell cannot fill tube");
			f.geometry.tube_alignment=1;f.step();check(f.state.stored==1,"correct orientation can fill bottom port");
			f.draw();const auto total=t::total_rounds(f.state);f.input.focused=false;f.step();check(f.state.loader_hand==vr::hand::none && t::total_rounds(f.state)==total,"focus loss cleans held shell without losing ammunition");
			f.input.focused=true;f.trigger(false);f.draw();f.writable=false;f.geometry.tube_delta={};f.step();const auto attempts=f.attempts;f.writable=true;f.step();check(f.attempts==attempts,"failed insertion requires fresh exit and contact, no repeated native write");
			f.geometry.tube_delta={0,0,1};f.step();f.geometry.tube_delta={};f.step();check(f.state.held_rounds==0,"shell can retry after deliberate withdrawal");
			f.draw();f.geometry.hand_in_gun={1,0,0};f.geometry.port_delta={};f.geometry.tube_delta={};f.step();check(f.state.held_rounds==1,"tracking teleport ending inside cannot load either port");
			f.input.grip[1-int(rear)].valid=false;f.step();check(f.state.held_rounds==0,"offhand tracking loss cancels shell lease");
		}
		for(auto rear:{vr::hand::left,vr::hand::right})for(std::uint8_t style:{0,1})
		{
			fixture f;f.owner.rear=rear;f.step();f.geometry.rack_distance=0;f.geometry.rack_pose=style;
			f.trigger(false);f.trigger(true);
			check(f.control.rack_held() && f.control.rack_grip().pose==style,"each operating hand latches its selected tube charging style");
			f.geometry.rack_pose=1-style;f.rack(w::m1014::interaction.rack.slide_stroke);
			check(f.control.rack_grip().pose==style && f.state.phase==t::action::held_open,"rotation across the style boundary cannot change a held rack pose");
			f.trigger(false);f.trigger(true);
			check(f.control.rack_held() && f.control.rack_grip().pose==1-style,"release and a new pinch may choose the other style");
			f.trigger(false);f.geometry.rack_pose=255;f.trigger(true);
			check(!f.control.rack_held(),"an invalid tube style cannot capture or index an authored pose");
		}

		for(auto name:w::m1014::names)
		{
			// 2026-09-12 raw M1014 definitions (all four variants): +6F0=7,
			// +B34=1, +E95=00, +E96=01. Reconstruct captured fields at their
			// literal offsets so a decoder off-by-one cannot pass this test.
			std::array<std::byte,3848> bytes{};
			bytes[0x6f0]=std::byte{7};bytes[0xb34]=std::byte{1};bytes[0xe96]=std::byte{1};
			const auto decoded=w::native_ammunition::reload_layout::decode(bytes);
			check(decoded && !decoded->no_partial && decoded->segmented &&
				w::native_tube_shape_supported(name,decoded->capacity,decoded->segmented,decoded->add),
				"captured native bytes admit M1014 independently of adjacent partial-reload flag");
			bytes[0xe95]=std::byte{1};bytes[0xe96]=std::byte{};
			const auto adjacent=w::native_ammunition::reload_layout::decode(bytes);
			check(adjacent && adjacent->no_partial && !adjacent->segmented &&
				!w::native_tube_shape_supported(name,adjacent->capacity,adjacent->segmented,adjacent->add),
				"noPartialReload cannot stand in for segmentedReload");
			check(!w::native_ammunition::reload_layout::decode(std::span(bytes).first(0xe96)),"truncated native shape cannot read past its extent");
			check(!w::native_tube_shape_supported(name,7,false,1) && !w::native_tube_shape_supported(name,7,true,7),"wrong native reload shape is rejected");
		}
		check(w::native_tube_shape_supported("spas12",7,true,1) && !w::native_tube_shape_supported("aa12",8,false,8),"pump tubes share individual-shell admission; detachable feeds remain independent");
		// Imported source hierarchy rather than a synthetic magazine/slide rig.
		for(const auto& source:{m1014_data::base,m1014_data::arctic})
		{
			vr::gameplay::hands::rig r{};r.count=12;r.gun=0;std::array<vr::gameplay::hands::bone_definition,12> bones{};
			for(int n=0;n<12;++n){r.weapon_bones[n]=true;r.parent[n]=source[n].parent;bones[n].name=source[n].name;}
			const auto parts=t::bind_parts(r,bones);check(parts.valid && parts.bolt==1 && parts.lifter==2 && parts.shell==5,"M1014 action, lifter and single shell bind source topology");
			r.parent[5]=1;check(!t::bind_parts(r,bones).valid,"single shell cannot silently become a bolt child");
		}
		t::presentation saved;saved.active=true;saved.ammo=t::import_native(rules,42,1,{0,10});saved.ammo.stored=2;saved.event_sequence=10;saved.events[2].sequence=10;saved.resume_transfer();
		check(saved.ammo.stored==2 && saved.ammo.phase==t::action::locked_open && saved.event_sequence==10 && !saved.events[2].sequence,"drop/pickup preserves tube versus chamber and event watermark");
	}
}
