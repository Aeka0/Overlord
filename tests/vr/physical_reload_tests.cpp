#include "component/vr/hand.hpp"
using vr::hand;
#include "component/vr/gameplay/weapons/m9/mechanics.hpp"
#include "component/vr/gameplay/weapons/fn2000/profile.hpp"
#include "component/vr/gameplay/weapons/g18/profile.hpp"
#include "component/vr/gameplay/weapons/m16/profile.hpp"
#include "component/vr/gameplay/weapons/m4/profile.hpp"
#include "component/vr/gameplay/weapons/m9/profile.hpp"
#include "component/vr/gameplay/weapons/p90/profile.hpp"
#include "component/vr/gameplay/weapons/m9/reload_interaction.hpp"
#include "component/vr/gameplay/weapons/m9/reload_poses.hpp"
#include "component/vr/gameplay/weapons/m9/slide_grips.hpp"
#include "component/vr/gameplay/native_shot_history.hpp"
#include "component/vr/gameplay/physical_reload_runtime.hpp"
#include "component/vr/gameplay/cylinder_runtime.hpp"
#include "component/vr/gameplay/weapons/m9/poses.hpp"
#include "component/scheduler_context.hpp"
#include "component/console.hpp"
#include "component/vr/gameplay/part_return_transition.hpp"
#include "component/vr/gameplay/native_ammo_grant.hpp"
#include "component/vr/gameplay/underbarrel_feed.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include "component/vr/gameplay/weapons/m4/mechanics.hpp"
#include "component/vr/gameplay/weapons/m4/reload_interaction.hpp"
#include "component/vr/gameplay/weapons/m4/feedback.hpp"
#include "magazine_box_tests.hpp"
#include "ak_reload_tests.hpp"
#include "foregrip_reload_tests.hpp"
#include "miniuzi_reload_tests.hpp"
#include "handle_catch_tests.hpp"
#include "receiver_bolt_release_tests.hpp"
#include "chambering_guide_tests.hpp"
#include "receiver_palm_tests.hpp"
#include "vector_receiver_release_tests.hpp"
#include "magazine_body_tests.hpp"
#include "comfort_interaction_tests.hpp"
#include "ergonomics_tests.hpp"
#include "launcher_tests.hpp"
#include "magazine_comfort_tests.hpp"
#include "button_magazine_tests.hpp"
#include "magazine_feedback_tests.hpp"
#include "magazine_orientation_tests.hpp"
#include "tavor_latch_tests.hpp"
#include "weapon_sound_slice_tests.hpp"
#include "reload_item_tests.hpp"
#include "supply_exchange_tests.hpp"
#include "handle_refinement_tests.hpp"
#include "weapon_polish_tests.hpp"
#include "left_handle_tests.hpp"
#include "slap_timing_tests.hpp"
#include "fal_reload_tests.hpp"
#include "scar_reload_tests.hpp"
#include "pp2000_reload_tests.hpp"
#include "manual_magazine_reload_tests.hpp"
#include "support_only_reload_tests.hpp"
#include "interaction_schedule_tests.hpp"
#include "support_handoff_tests.hpp"
#include "native_binding_tests.hpp"
#include "tube_reload_tests.hpp"
#include "fixed_drum_tests.hpp"
#include "belt_reload_tests.hpp"
#include "belt_cover_push_tests.hpp"
#include "rpd_reload_tests.hpp"
#include "pump_reload_tests.hpp"
#include "lever_reload_tests.hpp"
#include "lever_profile_tests.hpp"
#include "precision_reload_tests.hpp"
#include "manual_bolt_tests.hpp"
#include "native_ammunition_storage_tests.hpp"
#include "weapon_clip_tests.hpp"
#include "akimbo_split_tests.hpp"
#include "knife_reload_tests.hpp"
#include "knife_slide_tests.hpp"
#include "break_action_tests.hpp"
#include "break_action_profile_tests.hpp"
#include <iostream>
#include <limits>
#include <thread>

namespace w = vr::gameplay::weapons;
namespace m = w::mechanics;
namespace p = w::physical_reload;
using vr::controller_input::frame;
using namespace std::chrono_literals;

struct fixture
{
	const m::rules* rules{&w::m9::reload_rules};
	const p::profile* tuning{&w::m9::reload_interaction};
	p::controller control;
	m::state state{49, 1, 1, true, true, 14, 60};
	w::hold owner{49, 1, vr::hand::right, vr::hand::none, w::hold_source::engine_default, 1};
	frame input{};
	p::geometry geometry{true, 49, 1, 1, 1, 1, 1, 1, {}, {0, 0, -.2f}};
	p::clock::time_point now{1s};
	m::ammo_projection native{15, 60};
	bool writable{true};
	bool manipulation{true};
	bool acquire_supply{true},acquire_parts{true};
	int attempts{}, commits{}, spent{};
	int audible{}, silent{};
	m::effect last_effect{};
	fixture(const w::reload_profile* definition = nullptr)
	{
		if (definition)
		{
			rules = &definition->ammunition; tuning = &definition->interaction;
			m::from_native_automatic(*rules,rules->magazine_capacity,state);
			geometry.magazine.valid=true; geometry.magazine.grip_distance=1;
			geometry.catch_input.valid=definition->interaction.manual_catch!=nullptr;
			geometry.catch_input.slap_points.fill({0,0,.2f});
			geometry.magazine.strike=magazine_box_tests::point_fixture({-1,0,0});
			native = m::native_ammo(state);
		}
		input.sequence = input.reference_generation = 1;
		input.focused = true;
		for (int h = 0; h < 2; ++h)
		{
			input.grip[h].valid = input.aim[h].valid = true;
			input.trigger[h] = input.secondary[h] = {true, false, 0, 1};
		}
		step();
	}
	bool commit(const m::transaction& tx)
	{
		++attempts;
		if (!writable || tx.before != native) return false;
		native = tx.after; spent += tx.rounds_spent; ++commits;
		if (tx.silent) ++silent; else ++audible;
		last_effect = tx.feedback;
		return true;
	}
	void step(bool advance = true)
	{
		if (advance) { now += 10ms; ++input.sequence; }
		input.sampled_at = now;
		geometry.input_sequence = input.sequence;
		control.update(*tuning, *rules, state, input, owner, geometry, true, now,
			[&](const auto& tx) { return commit(tx); }, {manipulation,acquire_parts,acquire_supply});
	}
	void trigger(bool down)
	{
		auto& key = input.trigger[1 - static_cast<int>(owner.holding_hand())];
		if (down && !key.down) ++key.presses;
		key.down = down;
		step();
	}
	void button(bool down)
	{
		auto& key = input.secondary[static_cast<int>(owner.holding_hand())];
		if (down && !key.down) ++key.presses;
		key.down = down;
		step();
	}
	void adopt(m::state s)
	{
		state = s;
		native = m::native_ammo(state);
	}
	void move_slide(const p::slide_constraint& grip, float travel)
	{
		using namespace vr::gameplay::hands;
		geometry.hand_in_gun = add(grip.start, scale(tuning->slide_axis, travel - grip.initial_travel));
		step();
	}
	bool interrupt()
	{
		return control.interrupt(*rules, state, owner.holding_hand(), [&](const auto& tx) { return commit(tx); });
	}
};

int main()
{
	int failures{};
	const auto check = [&](bool ok, const char* text) {
		if (!ok) { ++failures; std::cerr << "FAIL: " << text << '\n'; }
	};
	weapon_sound_slice_tests::run(check);
	comfort_interaction_tests::run<fixture>(check);
	ergonomics_tests::run<fixture>(check);
	launcher_tests::run(check);
	magazine_comfort_tests::run<fixture>(check);
	button_magazine_tests::run<fixture>(check);
	magazine_feedback_tests::run<fixture>(check);
	magazine_orientation_tests::run(check);
	tavor_latch_tests::run(check);
	reload_item_tests::run<fixture>(check);
	supply_exchange_tests::run<fixture>(check);
	handle_refinement_tests::run<fixture>(check);
	left_handle_tests::run<fixture>(check);
	native_ammunition_storage_tests::run(check);
	for(auto rear:{vr::hand::left,vr::hand::right})for(bool primary_first:{false,true})
	{
		fixture f;f.owner.rear=rear;f.step();const auto off=vr::hand(1-int(rear));const auto before=f.native;
		f.geometry.waist_distance=.01f;f.input.squeeze[int(off)]={true,true,1,1};f.acquire_supply=false;
		namespace u=w::underbarrel;const u::identity id{{49,1},54,u::kind::m203};auto secondary=u::import_native(id,0,6);
		const auto draw=[&]{const auto tx=u::plan(secondary,{u::operation::draw,id,secondary.revision,rear,off});check(bool(tx),"secondary belt request accepted");if(tx)secondary=tx.next;};
		if(primary_first){f.trigger(true);draw();}else{draw();f.trigger(true);}
		check(f.state.magazine_hand==vr::hand::none && f.commits==0 && f.native==before,"primary-first scheduler cannot steal Grip-modified waist Trigger");
		check(secondary.held==1 && secondary.reserve==5,"either scheduler order transfers exactly one secondary round");
		f.acquire_supply=true;f.step();check(f.commits==0,"enabling primary supply cannot replay the consumed secondary press");
	}
	{
		fixture f;f.geometry.waist_distance=.01f;f.trigger(true);const auto held=f.state.held_rounds;const auto native=f.native;
		f.acquire_supply=false;f.acquire_parts=false;f.input.squeeze[0]={true,true,1,1};f.step();
		check(f.state.magazine_hand==vr::hand::left && f.state.held_rounds==held && f.native==native,"modifier selection never cancels an existing primary magazine lease");
	}
	{
		fixture f;f.geometry.slide_distance=f.geometry.waist_distance=.01f;f.acquire_parts=false;f.trigger(true);
		check(!f.control.slide_held() && f.state.magazine_hand==vr::hand::none,"secondary part reservation excludes both primary part and waist fallthrough");
		f.acquire_parts=true;f.step();check(!f.control.slide_held(),"reserved pinch cannot become a deferred primary grab");
	}
	fixed_drum_tests::run(check);
	weapon_clip_tests::run(check);
	akimbo_split_tests::run(check);
	{
		vr::gameplay::weapons::physical_reload::presentation gun;
		gun.ammo.instance_generation=9;gun.effect_sequence=8;gun.events[0].sequence=8;
		gun.reference_generation=1;gun.input_sequence=20;
		const auto consumed=gun.effect_sequence;
		gun.resume_transfer();
		check(gun.ammo.instance_generation==9 && gun.events[0].sequence==0 && gun.reference_generation==0,
			"transferred gun keeps identity while dropping old scene and event payloads");
		++gun.effect_sequence;
		check(gun.effect_sequence>consumed,"first ejection after pickup is newer than the pre-drop render watermark");
		vr::gameplay::weapons::cylinder::presentation revolver;
		revolver.event_sequence=8;revolver.fire_armed=true;revolver.resume_transfer();
		check(++revolver.event_sequence>consumed && !revolver.fire_armed,"transferred cylinder restarts trigger edges without rewinding cosmetic events");
	}
	magazine_box_tests::run(check);
	knife_reload_tests::run<fixture>(check);
	knife_slide_tests::run<fixture>(check);
	break_action_tests::run(check);
	break_action_profile_tests::run(check);
	ak_reload_tests::run<fixture>(check);
	foregrip_reload_tests::run<fixture>(check);
	miniuzi_reload_tests::run<fixture>(check);
	handle_catch_tests::run<fixture>(check);
	receiver_bolt_release_tests::run<fixture>(check);
	chambering_guide_tests(check);
	receiver_palm_tests::run<fixture>(check);
	vector_receiver_release_tests::run<fixture>(check);
	magazine_body_tests::run(check);
	slap_timing_tests::run(check);
	fal_reload_tests::run<fixture>(check);
	scar_reload_tests::run<fixture>(check);
	pp2000_reload_tests::run<fixture>(check);
	manual_magazine_reload_tests::run<fixture>(check);
	support_only_reload_tests::run<fixture>(check);
	interaction_schedule_tests::run(check);
	support_handoff_tests::run(check);
	native_binding_tests::run(check);
	belt_reload_tests::run<fixture>(check);
	belt_cover_push_tests::run<fixture>(check);
	rpd_reload_tests::run<fixture>(check);
	tube_reload_tests::run(check);
	pump_reload_tests::run(check);
	lever_reload_tests::run(check);
	lever_profile_tests::run(check);
	precision_reload_tests::run<fixture>(check);
	weapon_polish_tests::run<fixture>(check);
	manual_bolt_tests::run<fixture>(check);
	{
		fixture left, right;
		left.owner.rear=vr::hand::left;
		left.owner.weapon=left.state.weapon=left.geometry.weapon=50;
		left.state.instance_generation=left.geometry.instance_generation=2;
		left.manipulation=right.manipulation=false;
		left.step();right.step();
		left.input.secondary[0]={true,true,1,1};
		right.input.secondary[1]={true,true,1,1};
		left.step();right.step();
		check(!left.state.magazine_inserted && !right.state.magazine_inserted && left.native.loaded==1 && right.native.loaded==1,
			"two independent rear buttons eject both magazines with opposite hands occupied");
		const auto lc=left.commits,rc=right.commits;
		left.step(false);right.step(false);left.step();right.step();
		check(left.commits==lc && right.commits==rc,"duplicate scene and held button never replay either ejection");
		left.input.grip[1].valid=left.input.aim[1].valid=false;
		left.button(false);left.step();
		left.input.trigger[0]={true,true,1,1};left.step();
		check(left.native.loaded==1 && right.commits==rc,"opposite tracking loss cannot mutate the other weapon");
		left.owner.rear=vr::hand::right;++left.owner.rear_revision;
		left.input.grip[1].valid=left.input.aim[1].valid=true;
		left.input.secondary[1]={true,true,3,1};left.step();
		check(left.commits==lc,"handover consumes held buttons without applying a queued action");
	}
	for (auto rear:{vr::hand::left,vr::hand::right})
	{
		fixture f;f.owner.rear=rear;f.step();
		const auto off=1-int(rear);f.input.grip[off].valid=f.input.aim[off].valid=false;
		f.input.trigger[off].active=false;f.step();f.button(true);
		check(!f.state.magazine_inserted && f.native.loaded==1,"rear eject works without opposite controller tracking");
		f.geometry.waist_distance=.01f;f.input.grip[off].valid=f.input.aim[off].valid=true;
		f.input.trigger[off]={true,true,4,2};f.step();
		check(f.state.magazine_hand==vr::hand::none,"recovered offhand cannot replay an old pinch");
		f.trigger(false);f.trigger(true);
		check(f.state.magazine_hand==vr::hand(off),"fresh recovered pinch can draw a spare");
	}
	for (const auto* definition:{&w::m4::physical,&w::m16::physical})
	{
		// Shared AR gestures use each registered receiver and its measured stroke.
		const auto& candidate=*definition;
		check(p::valid(candidate.interaction) && !p::native_action_recoil(candidate.interaction),
			"AR charging handle has a validated independent motion policy");
		auto invalid=candidate.interaction; invalid.locked_travel=.04f;
		check(!p::valid(invalid),"charging handle cannot inherit pistol locked travel");
		for (auto rear:{vr::hand::left,vr::hand::right})
		{
			fixture f(&candidate); f.owner.rear=rear; f.step();
			const int initial=f.native.loaded+f.native.reserve;
			f.geometry.waist_distance=.01f; f.trigger(true); f.button(true); f.button(false);
			check(!f.state.magazine_inserted && f.state.chamber_loaded && f.state.held_rounds==30,
				"AR can take thirty-round spare before ejecting while preserving chamber");
			f.geometry.magazine_top_in_well={0,0,-.04f}; f.step();
			check(f.native.loaded==31 && f.native.loaded+f.native.reserve==initial,
				"AR tactical insertion reaches thirty plus one without creating ammo");
			f.trigger(false); f.geometry.slide_distance=.01f; f.trigger(true);
			auto grip=f.control.slide_grip();
			f.move_slide(grip,.02f); f.move_slide(grip,0);
			check(f.spent==0,"AR short handle stroke does not eject chamber");
			for (int n=0;n<2;++n) { f.move_slide(grip,candidate.interaction.slide_stroke); f.move_slide(grip,0); }
			check(f.spent==2 && f.state.chamber_loaded && f.native.loaded+f.native.reserve+f.spent==initial,
				"AR repeated held handle cycles consume one live chamber each");
			f.trigger(false);
			auto empty=f.state; empty.magazine_rounds=0; empty.chamber_loaded=false; empty.action=m::action_state::locked_open;
			f.adopt(empty); f.step();
			check(f.control.slide_travel()==0,"AR empty-lock retains forward handle position");
			f.trigger(true); grip=f.control.slide_grip(); f.move_slide(grip,candidate.interaction.slide_stroke); f.move_slide(grip,0);
			check(f.state.action==m::action_state::locked_open && f.control.slide_travel()==0,
				"handle returns forward while empty follower still holds bolt open");
			f.trigger(false); f.button(true); f.button(false);
			check(!f.state.magazine_inserted && f.state.action==m::action_state::locked_open,
				"AR B/Y ejects empty magazine before bolt release");
			f.button(true); f.button(false);
			check(f.state.action==m::action_state::closed && !f.state.chamber_loaded,
				"AR no-magazine release closes without inventing a chamber round");
			f.geometry.slide_distance=1; f.geometry.magazine_top_in_well={0,0,-.2f}; f.trigger(true);
			f.geometry.magazine_top_in_well={0,0,-.04f}; f.step(); f.trigger(false);
			check(!f.state.chamber_loaded && f.state.magazine_rounds==30,
				"AR insertion into closed empty chamber still needs a handle cycle");
			f.geometry.slide_distance=.01f; f.trigger(true); grip=f.control.slide_grip();
			const auto before=f.spent; f.move_slide(grip,candidate.interaction.slide_stroke); f.move_slide(grip,0);
			check(f.state.chamber_loaded && f.spent==before,"AR empty-chamber handle cycle feeds without live ejection");
			f.move_slide(grip,candidate.interaction.slide_stroke); f.move_slide(grip,0);
			check(f.spent==before+1,"next held cycle ejects the newly chambered round once");
			f.trigger(false);
			check(!f.control.slide_held() && f.control.slide_travel()==0,"AR handle release settles independently");
		}
		check(candidate.sound_key(m::effect::magazine_in) && !candidate.sound_key(m::effect::shot),
			"AR semantic reload sounds do not duplicate native firing audio");
	}
	for (auto rear : {vr::hand::right,vr::hand::left})
	{
		fixture f(&w::g18::physical); f.owner.rear=rear; f.step();
		f.geometry.slide_distance=.01f; f.trigger(true);
		const auto grip=f.control.slide_grip(); const auto& tuning=*f.tuning;
		f.move_slide(grip,tuning.full_stroke-.001f);
		check(f.audible==0,"G18 has no rear-stop sound before a full manual pull");
		f.move_slide(grip,tuning.slide_stroke);
		check(f.audible==1 && f.last_effect==m::effect::action_rear &&
			std::string_view(w::g18::physical.interaction_sound(f.last_effect).name)=="weap_glock_first_lift_chamber_plr",
			"G18 full manual pull selects the native rear-stop sound once");
		for (int i=0;i<8;++i) { f.move_slide(grip,tuning.slide_stroke-.0005f*(i%2)); f.step(false); }
		check(f.audible==1,"holding and jittering at the G18 rear stop cannot repeat its sound");
		f.move_slide(grip,0); f.move_slide(grip,tuning.slide_stroke);
		check(f.audible==3 && f.last_effect==m::effect::action_rear,"G18 rear sound rearms after a complete forward return");
		f.interrupt(); check(f.audible==3 && f.silent==1,"G18 lifecycle closure remains silent");
	}
	for (const auto* definition : w::reload_profiles)
	{
		{
			fixture inspected(definition);
			const auto consumed=inspected.control.examined();
			inspected.geometry.magazine_top_in_well={.2f,.1f,0};
			inspected.step(false);
			check(inspected.control.examined().magazine_top_in_well==consumed.magazine_top_in_well,
				"duplicate input cannot relabel a newer render point as consumed simulation");
			inspected.step();
			check(inspected.control.examined().magazine_top_in_well==inspected.geometry.magazine_top_in_well,
				"new input publishes exact examined geometry");
			check(inspected.interrupt() && !inspected.control.examined().valid &&
				!inspected.control.well_contact() && !inspected.control.requires_withdrawal(),
				"interrupt invalidates debug geometry and latches");
		}
		const auto& rules = definition->ammunition;
		const auto& tuning = definition->interaction;
		check(p::valid(tuning) && tuning.slide_pose_count == definition->slide_grips.size(),"all authored slide ranges valid");
		check(w::native_reload_profile(definition->native_name,rules.magazine_capacity,definition) == definition &&
			!w::native_reload_profile(definition->native_name,rules.magazine_capacity+1),"exact identity and base capacity admission");
		if (rules.release!=m::magazine_release::button || rules.feed!=m::feed_type::closed_bolt) continue; // Other feeds have dedicated suites.
		for (auto rear : {vr::hand::right,vr::hand::left})
		{
			fixture f(definition); f.owner.rear=rear; f.step();
			const auto total = f.native.loaded+f.native.reserve;
			f.geometry.waist_distance=.01f; f.trigger(true); // take new before ejecting old
			check(f.state.held_rounds==rules.magazine_capacity,"each pistol draws its own capacity");
			f.button(true); f.button(false);
			check(!f.state.magazine_inserted && f.state.chamber_loaded,"tactical eject preserves chamber with new mag already held");
			f.geometry.magazine_top_in_well={0,0,-.04f}; f.step();
			check(f.state.magazine_inserted && f.state.magazine_rounds==rules.magazine_capacity &&
				f.state.held_rounds==0 && f.native.loaded==rules.magazine_capacity+1,"authored contact inserts immediately to capacity plus one");
			check(f.native.loaded+f.native.reserve==total,"new magazine transfer conserves rounds");
			f.trigger(false); f.geometry.slide_distance=.01f; f.trigger(true);
			check(f.control.slide_held(),"each authored slide acquires");
			const auto grip=f.control.slide_grip();
			f.move_slide(grip,tuning.full_stroke*.5f); f.move_slide(grip,0);
			check(f.spent==0,"partial slide stroke never extracts");
			f.move_slide(grip,tuning.slide_stroke); f.move_slide(grip,0);
			f.move_slide(grip,tuning.slide_stroke); f.move_slide(grip,0);
			check(f.spent==2 && f.state.chamber_loaded && f.native.loaded+f.native.reserve+f.spent==total,
				"each pistol supports repeated held cycles with one spent round per full stroke");
			f.trigger(false);
			f.geometry.slide_distance=1; f.geometry.magazine_top_in_well={0,0,-.2f}; f.trigger(true);
			check(f.state.held_rounds==rules.magazine_capacity,"draw another magazine before switch");
			check(f.interrupt() && f.state.held_rounds==0 && f.native.loaded+f.native.reserve+f.spent==total,
				"switch refund uses old weapon profile capacity and escrow");
			fixture empty(definition); empty.owner.rear=rear;
			auto s=empty.state; s.magazine_rounds=0; s.chamber_loaded=false;
			s.action=rules.last_round_lock ? m::action_state::locked_open : m::action_state::closed; empty.adopt(s); empty.step();
			empty.button(true); empty.button(false);
			check(!empty.state.magazine_inserted && empty.state.action==s.action,
				"empty magazine eject preserves each profile's last-round action state");
			empty.button(true); empty.button(false);
			check(empty.state.action==m::action_state::closed && !empty.state.chamber_loaded,"no-mag release closes without chambering");
			empty.geometry.waist_distance=.01f; empty.trigger(true); empty.geometry.magazine_top_in_well={0,0,-.04f}; empty.step();
			check(empty.state.magazine_inserted && !empty.state.chamber_loaded && !m::ready(rules,empty.state),"closed empty chamber stays empty after insertion");
			empty.trigger(false); empty.geometry.slide_distance=.01f; empty.trigger(true);
			const auto empty_grip=empty.control.slide_grip();
			empty.move_slide(empty_grip,tuning.slide_stroke); empty.move_slide(empty_grip,0);
			check(empty.spent==0 && empty.state.chamber_loaded,"first rack feeds without losing a round");
			empty.move_slide(empty_grip,tuning.slide_stroke); empty.move_slide(empty_grip,0);
			check(empty.spent==1 && empty.state.chamber_loaded,"next held rack extracts the newly chambered round");
			for (bool draw_first : {false,true}) for (float degrees : {0.f,75.f,84.f})
			{
				fixture tilted(definition); tilted.owner.rear=rear; tilted.step();
				const auto total_before=m::total_rounds(tilted.state);
				if (!draw_first)
				{
					const auto shot=m::plan(rules,tilted.state,{m::operation::accepted_shot,tilted.state.weapon,
						tilted.state.instance_generation,tilted.state.revision,rear,rear});
					check(shot && tilted.commit(shot),"shot before tilted insertion commits once");
					tilted.state=shot.next; tilted.button(true); tilted.button(false);
				}
				tilted.geometry.waist_distance=0; tilted.trigger(true);
				tilted.geometry.insertion_alignment=std::cos(degrees*3.14159265359f/180.f);
				tilted.geometry.magazine_top_in_well={0,0,-.10f}; tilted.step();
				check(tilted.state.held_rounds==rules.magazine_capacity && !tilted.control.well_contact(),
					"even aligned pistol magazines ten cm below the mouth no longer snap in");
				tilted.geometry.magazine_top_in_well={0,0,.04f}; tilted.step();
				if (draw_first)
				{
					check(tilted.control.well_contact() && tilted.state.held_rounds==rules.magazine_capacity,
						"steep spare inside grip stages without replacing occupied magazine");
					tilted.button(true);
				}
				check(tilted.state.magazine_inserted && tilted.state.held_rounds==0 &&
					tilted.native.loaded==rules.magazine_capacity+1 &&
					m::total_rounds(tilted.state)+tilted.spent==total_before,
					"steep above-mouth contact inserts on first eligible tick for both reload orders");
				const auto committed=tilted.commits;
				for (int n=0;n<20;++n) tilted.step();
				check(tilted.commits==committed,"permissive contact cannot repeat the magazine transfer");
			}
			for (float rejected_degrees : {86.f,90.f,96.f,110.f,180.f})
			{
				if (std::cos(rejected_degrees*3.14159265359f/180.f)>=tuning.insertion_cosine) continue;
				fixture corrected(definition); corrected.owner.rear=rear; corrected.step();
				corrected.button(true); corrected.geometry.waist_distance=0; corrected.trigger(true);
				corrected.geometry.insertion_alignment=std::cos(rejected_degrees*3.14159265359f/180.f);
				corrected.geometry.magazine_top_in_well={0,0,0}; corrected.step();
				corrected.geometry.magazine_top_in_well={0,0,.04f}; corrected.step();
				check(!corrected.state.magazine_inserted && !corrected.control.requires_withdrawal(),
					"backward or over-limit magazine stays held without inventing a withdrawal requirement");
				corrected.geometry.insertion_alignment=std::cos(75.f*3.14159265359f/180.f); corrected.step();
				check(corrected.state.magazine_inserted && corrected.native.loaded==rules.magazine_capacity+1,
					"correcting angle after overshooting mouth inserts in place without dwell or reentry");
			}
		}
	}
	{
		using namespace vr::gameplay::hands;
		// 16 cm wrist-to-contact offset exposes the former rotation sensitivity.
		const std::array<w::part_grip_pose,1> long_grip{{{"test",{{-.16f,0,0},{0,0,0,1}},{.16f,0,0},{}}}};
		const auto assisted=[&](float degrees,float z=0.f) {
			const float half=degrees*3.14159265359f/360.f;
			return w::choose_part_grip(long_grip,{{-.16f,0,z},{0,0,std::sin(half),std::cos(half)}},{},{},{},1);
		};
		check(assisted(90).distance_meters>.08f && assisted(90).distance_meters<w::part_grip_capture::radius_m,
			"larger action capture admits a quarter-turn wrist after bounded angle assistance");
		check(assisted(45).distance_meters<.001f && assisted(-45).distance_meters<.001f,
			"either wrist rotation receives the same contact-only angular slack");
		check(assisted(180).distance_meters>w::part_grip_capture::radius_m &&
			assisted(0,.3f).distance_meters>w::part_grip_capture::radius_m,
			"angular assistance cannot reverse a hand or acquire from a remote wrist");
		for (const auto* p:w::reload_profiles)
		{
			const bool crowded=p==&w::p90::physical || p==&w::p90::arctic;
			const bool precision=p->native_name=="m14" || p->native_name.starts_with("dragunov");
			check(p->interaction.slide_radius==((crowded || precision) ? .06f : w::part_grip_capture::radius_m+(p==&w::fn2000::physical?.03f:0.f)),
				"P90 and magazine-adjacent precision handles narrow capture; F2000 retains its explicit expansion");
			check(bool(p->interaction.manual_magazine && p->interaction.manual_magazine->prefer_grasp_facing)==crowded,
				"grasp-facing arbitration is enabled only for the crowded P90 receiver");
		}
		const auto choose = [](anchor wrist, vec offset = {}) {
			return w::choose_part_grip(w::m9::slide_grips,wrist,offset,
				w::m9::slide_grab_low,w::m9::slide_grab_high,39.37007874f);
		};
		for (unsigned style = 0; style < w::m9::slide_grips.size(); ++style)
		{
			const auto& pose = w::m9::slide_grips[style];
			check(pose.fingers.size() == 15, "each slide grasp supplies all fifteen left finger joints");
			check(choose(pose.wrist).pose == style && choose(pose.wrist).distance_meters < .001f,
				"both authored wrist contacts acquire the same closed M9 slide");
			auto sign = pose.wrist;
			for (auto& x : sign.rotation) x = -x;
			check(choose(sign).pose == style, "quaternion sign cannot change grip selection");
			const auto lock = scale(w::m9::reload_interaction.slide_axis,w::m9::reload_interaction.locked_travel*39.37007874f);
			auto locked = pose.wrist; locked.position=add(locked.position,lock);
			check(choose(locked,lock).pose == style && choose(locked,lock).distance_meters < .001f,
				"each grasp follows native locked-open contact offset");
			const float q=std::sqrt(.5f);
			const quat gun_rotation{0,0,q,q}; const vec gun_position{40,-30,10};
			const anchor world{add(gun_position,rotate(gun_rotation,pose.wrist.position)),multiply(gun_rotation,pose.wrist.rotation)};
			const anchor local{rotate(conjugate(gun_rotation),sub(world.position,gun_position)),multiply(conjugate(gun_rotation),world.rotation)};
			check(choose(local).pose == style && choose(local).distance_meters < .001f,
				"selection and contact are invariant to moving and turning the gun");
			auto far=pose.wrist; far.position[2]+=30;
			check(choose(far).pose == style && choose(far).distance_meters > w::m9::reload_interaction.slide_radius,
				"matching orientation does not bypass the contact region");
		}
		auto invalid=w::m9::slide_grips[1].wrist;
		invalid.rotation={}; check(choose(invalid).pose==w::no_part_grip,"zero orientation rejects slide candidate");
		invalid.rotation[0]=std::numeric_limits<float>::quiet_NaN();
		check(choose(invalid).pose==w::no_part_grip,"nonfinite orientation cannot select a slide pose");
		check(w::choose_part_grip({},w::m9::slide_grips[0].wrist,{}, {},{},40).pose==w::no_part_grip,
			"empty pose library rejects cleanly");
		// Anatomical landmarks from the source glove, wrist-local native units.
		// Positive gun X points to the muzzle; the pinky must be in front of the index web.
		const vec index{3.89650488f,.75424694f,-.37168501f}, pinky{2.75793929f,-2.10006121f,.81065892f};
		check(rotate(w::m9::overhand_slide_wrist.rotation,pinky)[0] >
			rotate(w::m9::overhand_slide_wrist.rotation,index)[0] + 2,
			"new overhand actually points the pinky toward the muzzle");
	}
	for (auto rear : {vr::hand::right, vr::hand::left}) for (std::uint8_t style=0; style<2; ++style)
	{
		fixture f;
		f.owner.rear=rear; f.step();
		f.geometry.slide_distance=.01f; f.geometry.slide_pose=style; f.trigger(true);
		check(f.control.slide_held() && f.control.slide_grip().pose==style,"fresh contact latches the selected slide pose");
		const auto grip=f.control.slide_grip();
		f.geometry.slide_pose=1-style;
		f.move_slide(grip,.061f);
		check(f.control.slide_grip().pose==style && f.spent==1,"turning during grip cannot flip pose or change extraction");
		f.move_slide(grip,0);
		check(f.state.chamber_loaded && f.control.slide_grip().pose==style,"both grips feed using the existing forward boundary");
		f.move_slide(grip,.061f); f.move_slide(grip,0);
		check(f.spent==2 && f.control.slide_grip().pose==style,"repeated slide cycles retain the same pose and conservation");
		f.trigger(false); f.trigger(true);
		check(f.control.slide_held() && f.control.slide_grip().pose==1-style,"release and new press may choose the other pose");
		++f.input.reference_generation; f.geometry.reference_generation=f.input.reference_generation; f.step();
		check(!f.control.slide_held(),"recenter cancels either slide grip instead of carrying the old pose");

		fixture held; held.owner.rear=rear; held.step(); held.geometry.slide_pose=style;
		held.trigger(true); held.geometry.slide_distance=.01f; held.geometry.slide_pose=1-style; held.step();
		check(!held.control.slide_held(),"entering or changing pose while trigger held cannot acquire");
	}
	{
		fixture invalid;
		invalid.geometry.slide_distance=0; invalid.geometry.slide_pose=w::no_part_grip; invalid.trigger(true);
		check(!invalid.control.slide_held(),"out-of-range pose is rejected before acquisition");
		invalid.geometry.slide_pose=1; invalid.step();
		check(!invalid.control.slide_held(),"invalid-pose rejection cannot defer a held press");
	}
	for (auto rear : {vr::hand::right, vr::hand::left})
	{
		fixture f;
		f.owner.rear = rear;
		const int off = 1-static_cast<int>(rear);
		f.step();
		f.geometry.slide_distance = .01f;
		f.input.squeeze[off] = {true, true, 1, 1};
		f.trigger(true);
		check(!p::support_available(f.input, f.owner, false), "render support acquisition cannot steal simultaneous pinch");
		check(f.control.slide_held(), "simultaneous squeeze does not block fresh slide pinch on either hand");
		f.geometry.hand_in_gun[0] = -.061f; f.step();
		check(f.last_effect == m::effect::action_rear && f.audible == 1, "full pull has its own confirmed feedback");
		f.interrupt();
		check(f.silent == 1 && f.audible == 1, "lifecycle closure cannot replay player feedback");

		fixture draw;
		draw.owner.rear = rear; draw.step();
		draw.input.squeeze[off] = {true, true, 1, 1};
		draw.geometry.waist_distance = .01f; draw.trigger(true);
		check(draw.state.magazine_hand == static_cast<vr::hand>(off), "squeezed free hand may draw at waist");

		fixture owned;
		owned.owner.rear = rear; owned.owner.support = static_cast<vr::hand>(off); owned.step();
		owned.geometry.slide_distance = owned.geometry.waist_distance = .01f;
		owned.trigger(true);
		check(p::support_available(owned.input, owned.owner, false), "existing support remains exclusive when trigger presses");
		check(!p::support_available(owned.input, owned.owner, true), "part ownership always excludes support");
		check(!owned.control.offhand_busy(owned.state), "actual support ownership still excludes parts");
		owned.owner.support = vr::hand::none; owned.step();
		check(!owned.control.offhand_busy(owned.state), "support release cannot replay a held trigger");
		owned.trigger(false); owned.trigger(true);
		check(owned.control.slide_held(), "fresh pinch after support release acquires slide");

		fixture held;
		held.owner.rear = rear; held.step();
		held.input.squeeze[off] = {true, true, 1, 1};
		held.trigger(true); held.geometry.slide_distance = held.geometry.waist_distance = .01f; held.step();
		check(!held.control.offhand_busy(held.state), "holding trigger before entering still cannot acquire with squeeze");
	}
	{
		fixture f;
		f.geometry.slide_distance = .01f; f.trigger(true);
		check(f.control.feedback_effect() == m::effect::action_grab, "valid slide grab emits one tactile event");
		f.step(false);
		check(f.control.feedback_effect() == m::effect::none, "same input cannot repeat grab feedback");
		f.geometry.hand_in_gun[0] = -.02f; f.step(); f.trigger(false);
		check(f.commits == 0 && f.control.feedback_effect() == m::effect::action_close,
			"short spring return emits feedback without changing chamber or ammunition");
		f.step();
		check(f.control.feedback_effect() == m::effect::none, "short return does not replay");
	}
	{
		w::native_shot_history history;
		check(history.allow(49,1,100,1,true,true), "last chamber shot admitted on server");
		history.spent(49,1,100);
		check(history.allow(49,1,100,1,false,false), "prediction replays pre-shot chamber after server lock");
		check(!history.allow(49,1,100,1,true,true), "duplicate authoritative command cannot spend twice");
		check(!history.allow(49,1,100,0,false,true), "replay requires matching native before count");
		check(!history.allow(49,2,100,1,false,false), "new instance cannot inherit historical readiness");
		check(!history.allow(73,1,100,1,false,false), "other weapon cannot inherit historical readiness");
		check(!history.allow(49,1,101,15,true,false), "inserted loaded mag with empty chamber not ready");
		check(!history.allow(49,1,101,15,false,true), "later manual feed cannot turn old blocked command into shot");
		for (int cmd = 102; cmd < 260; ++cmd) (void)history.allow(49,1,cmd,15,true,false);
		check(!history.allow(49,1,100,1,false,false), "evicted replay fails without current readiness");
	}
	{
		using namespace vr::gameplay::hands;
		const anchor wrist{{-3.11256858f,3.58275434f,-12.01773318f},{.14772220f,-.26474063f,.04328734f,.95195418f}};
		const auto reconstructed = add(wrist.position,rotate(wrist.rotation,w::m9::magazine_in_wrist.position));
		check(length(sub(reconstructed,{-.44427284f,3.19274767f,-8.01377709f})) < .001f,
			"authored magazine/wrist transform reconstructs original reload pose");
		check(w::m9::magazine_fingers[0].name == "j_index_le_0" && std::size(w::m9::magazine_fingers) == 15 &&
			std::size(w::m9::slide_fingers) == 15, "both offhand action poses retain all fifteen named finger joints");
	}
	{
		fixture f;
		f.button(true);
		check(!f.state.magazine_inserted && f.state.chamber_loaded && f.native.reserve == 74, "B eject retains chamber");
		const int count = f.commits;
		f.step(false); f.step();
		check(f.commits == count, "same frame and held B never repeat");
		f.geometry.waist_distance = .01f;
		f.trigger(true);
		check(f.state.held_rounds == 15 && f.native.reserve == 59 && f.control.offhand_busy(f.state), "waist draw escrows once");
		f.step();
		f.geometry.magazine_top_in_well[2] = 0;
		f.step();
		check(f.state.magazine_inserted && f.native.loaded == 16 && f.native.reserve == 59, "top contact latches 15+1");
		f.step(); f.trigger(false);
		check(f.commits == 3 && f.state.magazine_hand == vr::hand::none, "repeat contact/release cannot insert or refund twice");
	}
	{
		fixture f;
		f.button(true); f.geometry.waist_distance = 0; f.trigger(true);
		f.geometry.magazine_top_in_well = {.05f,0,-.04f}; f.step();
		f.geometry.magazine_top_in_well[2] = 0; f.step();
		check(!f.state.magazine_inserted, "crossing mouth height outside its radius does not attach");
		f.geometry.magazine_top_in_well[0] = .02f; f.step();
		check(f.state.magazine_inserted && f.native.loaded == 16,
			"aligning sideways at mouth after a below approach completes insertion");
	}
	{
		fixture f;
		f.button(true); f.geometry.waist_distance = 0; f.trigger(true);
		f.geometry.magazine_top_in_well = {.05f,0,-.04f}; f.step();
		f.geometry.magazine_top_in_well[2] = .05f; f.step();
		f.geometry.magazine_top_in_well[0] = 0; f.step();
		f.geometry.magazine_top_in_well[2] = 0; f.step();
		check(f.state.magazine_inserted && f.native.loaded == 16,
			"returning to valid contact after above-mouth overshoot needs no hidden below approach");
	}
	for (int cadence_ms : {10, 50, 140, 200})
	{
		fixture f; f.input.continuity_generation=1; f.step(); f.button(true); f.geometry.waist_distance = 0; f.trigger(true);
		f.geometry.magazine_top_in_well = {.05f, .02f, -.04f}; f.step();
		// Retained observed trajectory: its old above-mouth miss is now INSIDE
		// the user-selected upper half. It must insert there, not at a later dip.
		f.geometry.magazine_top_in_well = {-.025012f, .003928f, .059608f}; f.step();
		f.geometry.magazine_top_in_well = {-.011393f, .012761f, .030384f};
		f.geometry.insertion_alignment = .96717f;
		f.now += std::chrono::milliseconds(cadence_ms - 10); f.step();
		check(f.state.magazine_inserted && f.native.loaded == 16 && f.commits == 3,
			"observed above-mouth point now inserts immediately at every fresh cadence");
		f.geometry.magazine_top_in_well = {.001612f, .023494f, -.010314f}; f.step();
		for (int n = 0; n < 800; ++n) f.step();
		check(f.commits == 3, "stationary inserted contact cannot repeat during eight seconds of dwell");
	}
	{
		fixture f; f.geometry.waist_distance = 0; f.trigger(true);
		f.geometry.magazine_top_in_well = {.05f, .02f, -.04f}; f.step();
		f.geometry.magazine_top_in_well = {-.011393f, .012761f, .030384f}; f.step();
		f.geometry.magazine_top_in_well = {.001612f, .023494f, -.010314f}; f.step();
		for (int n = 0; n < 200; ++n) f.step();
		check(f.state.held_rounds == 15 && f.commits == 1, "occupied well preserves real contact without insertion");
		f.button(true);
		check(f.state.held_rounds == 0 && f.native.loaded == 16 && f.commits == 3,
			"observed contact staged before B inserts in that same update regardless of dwell");
	}
	{
		using namespace vr::gameplay::hands;
		const float units=39.37007874f, q=std::sqrt(.5f);
		for (const quat rotation : {quat{0,0,0,1},quat{0,q,0,q},quat{q,0,0,q},quat{0,0,q,q}})
		{
			const anchor gun{{1000,-800,90},rotation};
			const auto world = [&](vec v) { return p::translate_local(gun,v).position; };
			for (int side : {-1,1})
			{
				const vec lower{0,side*w::m9::waist_half_width_m*units,-w::m9::waist_down_m*units};
				const auto upper=add(lower,vec{0,0,w::m9::waist_extend_up_m*units});
				const auto distance=[&](vec point) { return p::segment_distance(world(point),world(lower),world(upper))/units; };
				check(distance(add(lower,{0,0,-.17f*units})) < .18f, "waist retains old lower reach");
				check(distance(add(upper,{0,0,.17f*units})) < .18f, "waist upper reach extends exactly twenty cm");
				check(distance(add(upper,{0,0,.19f*units})) > .18f, "waist does not extend above requested boundary");
				check(distance(add(lower,{.19f*units,0,0})) > .18f, "waist horizontal radius unchanged");
			}
			const auto delta=p::well_exit_translation(w::m9::equip_rest[2].local,w::m9::magazine_top,
				w::m9::magazine_well,w::m9::magazine_exit_clearance_m*units);
			const anchor seated{world(w::m9::equip_rest[2].local.position),rotation};
			const auto middle=p::translate_local(seated,scale(delta,.5f));
			const auto exit=p::translate_local(seated,delta);
			const auto local_top=rotate(conjugate(rotation),sub(p::translate_local(exit,w::m9::magazine_top).position,gun.position));
			check(local_top[2] < w::m9::magazine_well[2]-.009f*units, "entire magazine clears well before gravity");
			check(length(sub(sub(middle.position,seated.position),scale(sub(exit.position,seated.position),.5f))) < .001f,
				"tilted gun magazine exits on its local rail, not world down");
			const auto velocity=scale(rotate(rotation,delta),1/w::m9::magazine_exit_seconds);
			check(length(sub(p::free_drop(exit,velocity,0,units).position,exit.position)) < .001f, "rail/free drop position continuous");
			const auto after=p::free_drop(exit,velocity,.1f,units);
			check(std::abs(after.position[2]-(exit.position[2]+velocity[2]*.1f-.5f*9.81f*units*.01f)) < .001f,
				"only released magazine receives world gravity and exit velocity");
			const quat wrist_rotation{.99370920f,.05250228f,.07731445f,-.06170910f};
			const auto wrist=anchor{world(w::m9::slide_grab_wrist),normalize(multiply(rotation,wrist_rotation))};
			const auto contact=p::translate_local(wrist,w::m9::slide_contact_in_wrist).position;
			const auto local=rotate(conjugate(rotation),sub(contact,gun.position));
			check(length(sub(local,{0,0,3.4f})) < .001f &&
				p::box_distance(local,w::m9::slide_grab_low,w::m9::slide_grab_high) < .001f,
				"sampled hand contact touches rear slide through rotated/translated gun frames");
		}
		check(p::sweep_well({0,0,-.1f},{0,0,.1f},.025f,.012f), "fast continuous insertion crosses finite mouth");
		check(!p::sweep_well({.1f,0,-.1f},{.1f,0,.1f},.025f,.012f), "outside radial sweep rejected");
	}
	{
		fixture f;
		f.adopt({49, 1, 1, true, false, 0, 60, 0, vr::hand::none, m::action_state::locked_open});
		f.button(true);
		check(!f.state.magazine_inserted && f.state.action == m::action_state::locked_open, "B ejects empty follower first");
		f.step();
		check(f.state.action == m::action_state::locked_open, "holding B cannot also close lock");
		f.button(false); f.button(true);
		check(f.state.action == m::action_state::closed && !f.state.chamber_loaded, "second B without magazine closes empty");
		f.geometry.waist_distance = 0;
		f.trigger(true); f.step();
		f.geometry.magazine_top_in_well[2] = 0; f.step();
		check(f.native.loaded == 15 && !m::ready(w::m9::reload_rules, f.state), "closed insertion does not silently chamber");
		f.trigger(false);
		f.geometry.slide_distance = 0;
		f.trigger(true);
		f.geometry.hand_in_gun[0] = -.03f; f.step(); f.trigger(false);
		check(f.native.loaded == 15 && !f.state.chamber_loaded && f.spent == 0, "partial empty rack does not feed");
		f.trigger(true);
		f.geometry.hand_in_gun[0] -= .06f; f.step();
		check(f.state.action == m::action_state::held_open && f.native.loaded == 15, "full empty stroke held open before release");
		f.trigger(false);
		check(f.state.chamber_loaded && f.state.magazine_rounds == 14 && f.native.loaded == 15, "release feeds one without spending");
	}
	{
		fixture f;
		f.geometry.slide_distance = 0;
		f.trigger(true);
		check(f.control.slide_held(), "trigger grabs slide");
		f.button(true);
		check(f.state.magazine_inserted, "B cannot operate while slide is held even before full stroke");
		f.geometry.hand_in_gun[0] = -.02f; f.step(); f.trigger(false);
		check(f.native.loaded == 15 && f.spent == 0, "partial live pull loses nothing");
		f.trigger(true);
		f.geometry.hand_in_gun[0] -= .06f; f.step();
		check(f.native.loaded == 14 && f.native.reserve == 60 && f.spent == 1 && !f.state.chamber_loaded,
			"full stroke loses exactly the live chambered round before release");
		for (int i = 0; i < 30; ++i) f.step();
		check(f.native.loaded == 14 && f.spent == 1, "holding rearward cannot keep spending");
		f.input.focused = false; f.step();
		check(f.state.chamber_loaded && f.native.loaded == 14 && f.spent == 1 && !f.control.slide_held(),
			"tracking interruption finishes stroke without refunding extracted live round");
		f.input.focused = true; f.step();
		check(!f.control.slide_held(), "reconnect while trigger down cannot reacquire");
	}
	for (const auto rear : {vr::hand::left, vr::hand::right}) for (bool locked : {false, true})
	{
		fixture f;
		f.owner.rear = rear; ++f.owner.rear_revision; f.step();
		if (locked) f.adopt({49, 1, 1, true, false, 15, 60, 0, vr::hand::none, m::action_state::locked_open});
		f.geometry.slide_distance = 0;
		f.geometry.hand_in_gun = {.12f, .02f, .03f};
		f.trigger(true);
		const auto grip = f.control.slide_grip();
		const auto& profile = w::m9::reload_interaction;
		check(grip.initial_travel == (locked ? profile.locked_travel : 0), "grab preserves initial locked-open travel");
		for (int cycle = 1; cycle <= 3; ++cycle)
		{
			f.move_slide(grip, profile.slide_stroke);
			const int spent = cycle - int(locked);
			check(f.spent == spent && f.native.loaded == 15 - spent && !f.state.chamber_loaded &&
				f.state.action == m::action_state::held_open, "each full pull in one grab extracts only the current chamber");
			const int attempts = f.attempts;
			f.step(false); f.step(); f.step();
			check(f.attempts == attempts, "same sample and rear-stop dwell never repeat extraction");
			f.move_slide(grip, profile.close_travel * .5f);
			check(f.state.action == m::action_state::closed && f.state.chamber_loaded &&
				f.state.magazine_rounds == 14 - spent && f.spent == spent && f.native.loaded == 15 - spent &&
				f.native.reserve == 60 && f.commits == cycle * 2, "held forward return feeds once without spending or refunding");
			check(f.control.slide_held() && f.control.slide_grip().start == grip.start &&
				f.control.slide_grip().initial_travel == grip.initial_travel &&
				f.input.trigger[1 - static_cast<int>(rear)].presses == 1,
				"continuous cycles retain the same grip anchor and one trigger press for either rear hand");
			check(!m::ready(w::m9::reload_rules, f.state, f.control.slide_held()), "closed slide still blocks firing while held");
		}
		const int attempts = f.attempts;
		f.trigger(false);
		check(!f.control.slide_held() && f.attempts == attempts, "release after held forward completion does not feed twice");
	}
	{
		fixture f;
		f.geometry.slide_distance = 0; f.trigger(true);
		const auto grip = f.control.slide_grip();
		const auto& profile = w::m9::reload_interaction;
		for (int i = 0; i < 3; ++i)
		{
			f.move_slide(grip, profile.full_stroke - .001f);
			f.move_slide(grip, 0);
		}
		check(f.attempts == 0 && f.spent == 0 && f.state.chamber_loaded, "repeated held short strokes neither extract nor feed");
		f.move_slide(grip, profile.full_stroke);
		for (float travel : {.061f, .054f, .056f, .050f, .061f, profile.close_travel + .0005f, .061f})
			f.move_slide(grip, travel);
		check(f.attempts == 1 && f.spent == 1 && !f.state.chamber_loaded,
			"rear-stop jitter and incomplete forward return cannot rearm extraction");
		f.move_slide(grip, profile.close_travel);
		check(f.commits == 2 && f.state.chamber_loaded && f.state.magazine_rounds == 13,
			"explicit close threshold completes a held stroke at its boundary");
		for (float travel : {.0035f, .0025f, .0035f, .0025f, -.02f, profile.full_stroke - .001f})
			f.move_slide(grip, travel);
		check(f.attempts == 2 && f.spent == 1 && f.control.slide_grip().start == grip.start,
			"forward jitter and stop overshoot neither repeat feeding nor rebase the continuous grip");
		f.move_slide(grip, profile.full_stroke);
		check(f.commits == 3 && f.spent == 2 && !f.state.chamber_loaded, "next full pull ejects next round without releasing trigger");
	}
	for (bool loaded : {false, true})
	{
		fixture f;
		f.adopt({49, 1, 1, true, loaded, 0, 60, 0, vr::hand::none,
			loaded ? m::action_state::closed : m::action_state::locked_open});
		f.geometry.slide_distance = 0; f.trigger(true);
		const auto grip = f.control.slide_grip();
		const float lock = w::m9::reload_interaction.locked_travel;
		for (int cycle = 0; cycle < 3; ++cycle)
		{
			f.move_slide(grip, .061f); f.step();
			check(f.state.action == m::action_state::held_open && f.spent == int(loaded), "held empty follower never repeats live extraction");
			f.move_slide(grip, lock + .0005f);
			check(f.state.action == m::action_state::held_open, "empty follower return must reach the lock stop");
			f.move_slide(grip, lock); f.step();
			check(f.state.action == m::action_state::locked_open && !f.state.chamber_loaded && f.native.loaded == 0 &&
				f.native.reserve == 60 && f.spent == int(loaded) && f.control.slide_held() && f.control.slide_travel() == lock,
				"held empty follower finishes at 47mm without needing a return to zero");
			const int attempts = f.attempts;
			f.move_slide(grip, 0); f.move_slide(grip, lock - .001f); f.move_slide(grip, lock);
			check(f.attempts == attempts && f.control.slide_travel() == lock &&
				f.control.slide_grip().start == grip.start && f.control.slide_grip().initial_travel == grip.initial_travel,
				"forward overshoot stays at follower lock without rebasing the grip or repeating a transaction");
		}
		f.trigger(false);
		check(f.control.slide_travel() == w::m9::reload_interaction.locked_travel && f.commits == 6,
			"releasing empty follower after forward completion retains lock without another transaction");
	}
	{
		const auto& profile = w::m9::reload_interaction;
		auto rules = w::m9::reload_rules;
		m::state s{49, 1, 1, true, false, 0, 60, 0, vr::hand::none, m::action_state::held_open};
		check(p::minimum_slide_travel(profile, rules, s) == profile.locked_travel, "held empty follower shares the physical lock stop");
		rules.last_round_lock = false;
		check(p::minimum_slide_travel(profile, rules, s) == 0, "nonlocking action has no empty-follower travel floor");
		rules.last_round_lock = true;
		s.magazine_inserted = false;
		check(p::minimum_slide_travel(profile, rules, s) == 0, "no magazine permits full forward return");
		s.magazine_inserted = true; s.magazine_rounds = 15;
		check(p::minimum_slide_travel(profile, rules, s) == 0, "held loaded magazine permits full forward return for feeding");
		s.action = m::action_state::locked_open;
		check(p::minimum_slide_travel(profile, rules, s) == profile.locked_travel, "loaded magazine retains existing lock until released");
		s.action = m::action_state::closed; s.magazine_rounds = 0;
		check(p::minimum_slide_travel(profile, rules, s) == 0, "closed empty action is not silently forced open");
	}
	{
		fixture f;
		f.adopt({49, 1, 1, true, false, 0, 60, 0, vr::hand::none, m::action_state::locked_open});
		f.geometry.slide_distance = 0; f.trigger(true);
		const auto grip = f.control.slide_grip();
		const float lock = w::m9::reload_interaction.locked_travel;
		f.move_slide(grip, .061f);
		f.writable = false; f.move_slide(grip, lock);
		check(f.attempts == 2 && f.commits == 1 && f.state.action == m::action_state::held_open &&
			f.control.slide_travel() == lock, "rejected follower return retains held-open mechanics at the lock stop");
		f.writable = true;
		f.move_slide(grip, 0); f.move_slide(grip, lock + .001f); f.move_slide(grip, lock);
		check(f.attempts == 2 && f.state.action == m::action_state::held_open && f.control.slide_travel() == lock,
			"held-open empty follower stays clamped through overshoot and cannot retry a rejected lock return on jitter");
		f.move_slide(grip, .061f); f.move_slide(grip, lock);
		check(f.commits == 2 && f.state.action == m::action_state::locked_open && f.control.slide_travel() == lock && f.spent == 0,
			"fresh full pull retries follower return at the lock stop");
	}
	for (bool locked : {false, true})
	{
		fixture f;
		if (locked) f.adopt({49, 1, 1, true, false, 15, 60, 0, vr::hand::none, m::action_state::locked_open});
		f.geometry.slide_distance = 0; f.trigger(true);
		const auto grip = f.control.slide_grip();
		f.move_slide(grip, .061f);
		const auto before = f.native;
		const auto revision = f.state.revision;
		f.writable = false; f.move_slide(grip, .002f);
		check(f.attempts == 2 && f.commits == 1 && f.state.revision == revision && f.native == before &&
			!f.state.chamber_loaded && f.state.action == m::action_state::held_open && f.control.slide_held(),
			"failed held forward commit publishes nothing and is attempted only once");
		f.writable = true; f.step(false);
		for (float travel : {.002f, .002f, .004f, .001f, .03f, .054f, .002f}) f.move_slide(grip, travel);
		check(f.attempts == 2 && f.native == before && f.state.revision == revision && !f.state.chamber_loaded,
			"restored writer, close jitter and short strokes cannot defer a rejected forward feed");
		f.move_slide(grip, .061f); f.move_slide(grip, .002f);
		check(f.attempts == 3 && f.commits == 2 && f.native == before && f.state.chamber_loaded &&
			f.spent == int(!locked), "fresh full pull and forward return retries feeding without another extraction");
		f.move_slide(grip, .061f);
		check(f.spent == int(!locked) + 1 && !f.state.chamber_loaded, "same grab cycles normally after deliberate forward retry");
	}
	{
		fixture f;
		f.adopt({49, 1, 1, false, true, 0, 60});
		f.geometry.slide_distance = 0; f.trigger(true);
		const auto grip = f.control.slide_grip();
		for (int cycle = 0; cycle < 3; ++cycle)
		{
			f.move_slide(grip, .061f); f.move_slide(grip, 0);
			check(f.state.action == m::action_state::closed && !f.state.chamber_loaded && f.spent == 1 &&
				f.native.loaded == 0 && f.native.reserve == 60, "continuous no-magazine cycles cannot create or refund rounds");
		}
	}
	for (float close : {0.f, -.001f, w::m9::reload_interaction.full_stroke, .062f,
		std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
	{
		auto profile = w::m9::reload_interaction;
		profile.close_travel = close;
		check(!p::valid(profile), "close threshold must be finite, positive and below full stroke");
	}
	for (int loss = 0; loss < 5; ++loss)
	{
		fixture f;
		f.geometry.waist_distance = 0; f.trigger(true);
		check(f.native.reserve == 45, "draw before interruption");
		if (loss == 0) f.input.focused = false;
		if (loss == 1) f.input.grip[0].valid = false;
		if (loss == 2) ++f.input.reference_generation;
		if (loss == 3) ++f.input.trigger[0].generation;
		if (loss == 4) f.now += 1s;
		f.step(); f.step();
		check(f.state.magazine_hand == vr::hand::none && f.native.reserve == 60 && f.commits == 2,
			"focus/pose/recenter/input-generation/time gap refunds held magazine once");
	}
	{
		fixture f;
		f.geometry.waist_distance = 0; f.trigger(true);
		f.writable = false;
		check(!f.interrupt() && f.state.held_rounds == 15 && f.native.reserve == 45,
			"failed native refund preserves escrow for retry");
		f.writable = true;
		check(f.interrupt() && f.interrupt() && f.native.reserve == 60 && f.state.held_rounds == 0,
			"successful cancellation then repeat cannot duplicate refund");
		f.adopt({73, 2, 1, true, true, 14, 30});
		f.owner.weapon = f.geometry.weapon = 73;
		f.geometry.instance_generation = 2;
		f.step();
		check(f.native.reserve == 30 && f.state.held_rounds == 0, "switch consumes old cancellation before new instance; held trigger cannot draw");
		f.trigger(false); f.trigger(true);
		check(f.native.reserve == 15 && f.state.held_rounds == 15, "new instance deliberately draws from its own reserve");
	}
	{
		fixture f;
		f.trigger(true); // outside both regions
		f.geometry.waist_distance = 0; f.step();
		check(f.state.held_rounds == 0, "holding trigger while entering waist is not a new draw");
		f.trigger(false); f.owner.support = vr::hand::left; f.trigger(true);
		check(f.state.held_rounds == 0, "support hand cannot concurrently own a magazine");
		f.owner.support = vr::hand::none; f.step();
		check(f.state.held_rounds == 0, "support release does not replay rejected draw");
	}
	{
		fixture f;
		f.writable = false; f.button(true);
		check(f.state.magazine_inserted && f.native.loaded == 15, "failed native compare publishes no mechanical change");
		f.writable = true; f.step();
		check(f.state.magazine_inserted, "failed eject is not queued");
	}
	for (int invalid = 0; invalid < 4; ++invalid)
	{
		fixture f;
		f.button(true); f.geometry.waist_distance = 0; f.trigger(true); f.step();
		if (invalid == 0) f.geometry.insertion_alignment = -.99f;
		if (invalid == 1) f.geometry.magazine_top_in_well[0] = .5f;
		if (invalid == 2) f.geometry.magazine_top_in_well[2] = -.8f;
		if (invalid == 3) f.geometry.magazine_top_in_well[0] = std::numeric_limits<float>::quiet_NaN();
		f.step(); f.geometry.magazine_top_in_well[2] = 0; f.step();
		check(!f.state.magazine_inserted, "wrong orientation/lateral entry/teleport/NaN cannot latch");
	}
	{
		fixture f;
		f.owner.rear = vr::hand::left; ++f.owner.rear_revision; f.step();
		f.button(true);
		check(!f.state.magazine_inserted, "left rear uses its secondary action (Y), not fixed physical right B");
		f.geometry.waist_distance = 0; f.trigger(true);
		check(f.state.magazine_hand == vr::hand::right, "right offhand draw after rear ownership swap");
	}
	check(p::valid(w::m9::reload_interaction), "M9 physical profile valid");
	{
		// Staging a new magazine before eject, including rotation in the capture
		// collar, must not invent a cooldown or require withdrawing a second time.
		fixture f;
		f.geometry.waist_distance=0; f.trigger(true);
		f.geometry.magazine_top_in_well[2]=-.04f; f.geometry.insertion_alignment=0; f.step();
		f.geometry.insertion_alignment=1; f.step();
		check(f.state.magazine_inserted && f.state.held_rounds==15, "occupied well preserves staged spare");
		f.button(true);
		check(f.state.magazine_inserted && f.state.held_rounds==0 && f.native.loaded==16 && f.commits==3,
			"B eject followed by already aligned spare inserts immediately without timed gate");
	}
	{
		fixture f;
		f.button(true); f.geometry.waist_distance=0; f.trigger(true);
		f.geometry.insertion_alignment=0; f.geometry.magazine_top_in_well[2]=-.04f; f.step();
		for (int i=0;i<30;++i) f.step();
		f.geometry.insertion_alignment=1; f.step();
		check(f.state.magazine_inserted && f.native.loaded==16, "alignment in expanded collar retains deliberate below approach");
	}
	{
		fixture f; f.geometry.slide_distance=0; f.trigger(true);
		const auto lease=f.control.slide_grip();
		float previous=-1;
		for (int render=0;render<9;++render)
		{
			const float travel=p::constrained_slide_travel(w::m9::reload_interaction,lease,{-.007f*render,0,0});
			check(travel>previous, "nine render poses advance slide without a simulation tick");
			previous=travel;
		}
		check(f.native.loaded==15 && f.spent==0 && f.control.slide_travel()==0,
			"visual full stroke cannot mutate authoritative ammo or gesture");
		f.geometry.hand_in_gun={-.06f,0,0}; f.step();
		check(f.spent==1, "next server tick remains sole extraction authority");
	}
	{
		fixture f; f.geometry.waist_distance=0; f.trigger(true);
		f.geometry.magazine_top_in_well={0,0,-.05f}; f.step(); // touch occupied well from below
		f.geometry.magazine_top_in_well={-.035f,.01f,.025f}; f.step(); // small tracked overshoot
		for (int i=0;i<120;++i) f.step();
		check(f.state.magazine_inserted && f.state.held_rounds==15, "staged spare never inserts through an occupied well");
		f.button(true);
		check(f.commits==3 && f.state.magazine_inserted && f.state.held_rounds==0 && f.native.loaded==16,
			"staged contact survives above-mouth/lateral jitter then ejects and inserts on same tick without waiting");
	}
	for (int reject=0;reject<4;++reject)
	{
		fixture f; f.geometry.waist_distance=0; f.trigger(true);
		f.geometry.magazine_top_in_well={0,0,-.05f}; f.step();
		if (reject==0) f.geometry.magazine_top_in_well={.08f,0,.025f}; // deliberate separation
		if (reject==1) f.geometry.magazine_top_in_well={0,0,.30f}; // teleport
		if (reject==2) f.geometry.insertion_alignment=-.2f;
		if (reject==3) { f.writable=false; f.button(true); f.writable=true; f.step(); }
		if (reject!=3) f.button(true);
		check(f.state.held_rounds==15 && f.commits<=2, "separation/teleport/wrong angle/rejected B cannot insert a staged spare");
		if (reject==2) { f.geometry.insertion_alignment=1; f.step();
			check(f.state.magazine_inserted && f.native.loaded==16, "realignment of still-near staged spare needs no timer"); }
	}
	{
		std::string report;
		for (int instance=0;instance<15;++instance) for (int attempt=0;attempt<32;++attempt)
			report += "attempt=" + std::to_string(attempt) + " decision=magazine outside mouth volume tip_m=0.025,0,-0.1 held=15 trigger/squeeze=1/0\n";
		std::string reassembled;
		for (const auto part : console::split_text_chunks(report))
		{
			check(part.size()<=console::max_text_chunk_size && part.size()<4096, "full status ring never enters fixed CRT buffer in one piece");
			reassembled.append(part);
		}
		check(report.size()>4096 && reassembled==report, "full inventory status survives bounded console output without loss");
	}
	{
		fixture f;
		f.adopt({49, 1, 1, true, false, 0, 60, 0, vr::hand::none, m::action_state::locked_open});
		f.geometry.slide_distance = 0;
		f.trigger(true);
		f.geometry.hand_in_gun[0] = -.012f; f.step();
		check(f.state.action == m::action_state::held_open, "small extra stroke beyond lock reaches full rearward travel");
		f.trigger(false);
		check(f.state.action == m::action_state::locked_open && f.spent == 0,
			"empty follower returns to lock after manual release");
	}
	{
		fixture f;
		f.input.secondary[1].active = false;
		f.geometry.slide_distance = 0;
		f.trigger(true);
		check(f.control.slide_held(), "unbound optional B action does not disable manual slide interaction");
		f.geometry.hand_in_gun[0] = -.06f;
		f.writable = false; f.step();
		check(f.native.loaded == 15 && f.state.chamber_loaded && !f.control.slide_held(), "failed full-stroke native commit cancels gesture without spending");
		f.writable = true; f.step();
		check(f.native.loaded == 15 && !f.control.slide_held(), "failed extraction is not replayed while trigger stays down");
	}
	{
		fixture f;
		f.button(true); f.geometry.waist_distance = 0;
		f.geometry.magazine_top_in_well = {};
		f.trigger(true); f.step(); f.step();
		check(!f.state.magazine_inserted, "starting inside well cannot auto-insert");
		f.geometry.magazine_top_in_well[2] = -.2f; f.step();
		f.geometry.magazine_top_in_well[2] = .015f;
		f.writable = false; f.step();
		check(!f.state.magazine_inserted && f.state.held_rounds == 15, "failed insertion retains held escrow");
		f.writable = true; f.step();
		check(!f.state.magazine_inserted, "failed insertion requires a fresh approach, not a deferred commit");
		f.geometry.magazine_top_in_well[2] = -.20f; f.step();
		f.geometry.magazine_top_in_well[2] = 0; f.step();
		check(f.state.magazine_inserted && f.native.loaded == 16, "fresh approach can retry insertion exactly once");
	}
	// Live M9 seq46888..46896: a fast waist draw crossed >25 cm between
	// simulation samples, but BOTH jump endpoints remained outside the well.
	// The next bounded approach must not inherit an invented second-exit latch.
	for (const auto* definition : w::reload_profiles)
	if (definition->ammunition.release==m::magazine_release::button)
	for (auto rear : {vr::hand::right,vr::hand::left})
	for (bool draw_first : {false,true})
	{
		fixture f(definition); f.owner.rear=rear; f.step();
		const auto initial_total=m::total_rounds(f.state);
		if (!draw_first)
		{
			const auto shot=m::plan(*f.rules,f.state,{m::operation::accepted_shot,f.state.weapon,
				f.state.instance_generation,f.state.revision,rear,rear});
			check(shot && f.commit(shot),"shot immediately before eject/fetch has an authoritative ammo transition");
			f.state=shot.next; f.button(true); f.button(false);
		}
		f.geometry.waist_distance=0;
		f.geometry.magazine_top_in_well={-.0593f,.4018f,.1711f}; f.trigger(true);
		f.geometry.magazine_top_in_well={-.0387f,.3540f,-.1456f}; f.step();
		f.geometry.magazine_top_in_well={.0285f,.0860f,-.2264f}; f.step();
		check(f.state.held_rounds==f.rules->magazine_capacity,"outside jumps never themselves insert a magazine");
		f.geometry.magazine_top_in_well={.0176f,.0279f,-.1518f}; f.step();
		f.geometry.magazine_top_in_well={.0090f,-.0022f,-.0952f}; f.geometry.insertion_alignment=.9863f; f.step();
		check(f.state.held_rounds==f.rules->magazine_capacity && !f.control.well_contact(),
			"retained trace endpoint below new capture bounds no longer causes remote insertion");
		// New bounded continuation into the requested region; retained trace
		// values above are unchanged and are not relabeled as new live evidence.
		f.geometry.magazine_top_in_well={.0090f,-.0022f,-.04f}; f.step();
		if (draw_first)
		{
			check(f.state.magazine_inserted && f.state.held_rounds==f.rules->magazine_capacity,
				"fast outside approach stages without inserting through the old magazine");
			f.button(true);
		}
		check(f.state.magazine_inserted && f.state.held_rounds==0 && f.native.loaded==f.rules->magazine_capacity+int(f.rules->plus_one),
			"outside jump followed by bounded contact inserts immediately, including staged-before-eject and shot-before-fetch");
		const auto commits=f.commits;
		for (int n=0;n<200;++n) f.step();
		check(f.commits==commits && m::total_rounds(f.state)+f.spent==initial_total,
			"repeated contact cannot repeat transfer and the shot is the only lost round");
	}
	for (bool spawn_in_contact : {false, true})
	{
		fixture f; f.button(true); f.geometry.waist_distance = 0;
		f.geometry.magazine_top_in_well = spawn_in_contact ? p::vec{} : p::vec{0,0,-.4f};
		f.trigger(true);
		f.geometry.magazine_top_in_well = {}; f.step(); // valid point, but spawned/teleported there
		for (int n = 0; n < 200; ++n) f.step();
		check(f.state.held_rounds == 15 && !f.state.magazine_inserted && f.commits == 2,
			"spawn or teleport at contact cannot turn into a delayed insertion while stationary");
		f.geometry.magazine_top_in_well = {.08f,0,0}; f.step(); // leave on any side, not a below-only ritual
		check(!f.state.magazine_inserted, "guard withdrawal sweep cannot itself insert");
		f.geometry.magazine_top_in_well = {}; f.step();
		check(f.state.magazine_inserted && f.commits == 3, "fresh bounded contact after guard exit inserts exactly once");
	}
	{
		fixture f;
		f.button(true); f.geometry.waist_distance=0; f.trigger(true); f.step();
		f.geometry.magazine_top_in_well[2]=-.04f; f.step();
		check(f.state.magazine_inserted && f.control.magazine_seated(), "capture near well seats magazine and keeps hand lease");
		const int commits=f.commits;
		for (int i=0;i<40;++i) f.step();
		check(f.control.offhand_busy(f.state) && f.commits==commits && f.native.loaded==16,
			"holding seated magazine beyond animation has no second insert or ammo spending");
		f.geometry.seated_hand_distance=.36f; f.step();
		check(!f.control.magazine_seated() && f.state.magazine_inserted && f.native.loaded==16,
			"raw wrist separation breaks visual grip without ejecting/refunding inserted ammo");
		f.geometry.slide_distance=0; f.step();
		check(!f.control.slide_held(), "held trigger after magazine breakaway cannot acquire slide");
	}
	for (int end=0;end<4;++end)
	{
		fixture f;
		f.button(true); f.geometry.waist_distance=0; f.trigger(true); f.step();
		f.geometry.magazine_top_in_well[2]=-.04f; f.step();
		if (end==0) f.trigger(false);
		if (end==1) { f.input.focused=false; f.step(); }
		if (end==2) { ++f.input.reference_generation; f.step(); }
		if (end==3) (void)f.interrupt();
		check(!f.control.magazine_seated() && f.state.magazine_inserted && f.native.loaded==16,
			"release/focus/recenter/switch clear seated hand without undoing committed insertion");
	}
	{
		fixture f;
		f.geometry.slide_distance=.07f; f.trigger(true);
		check(f.control.slide_held(), "expanded slide region accepts seven cm outside rear surfaces");
		f.geometry.hand_in_gun={-.06f,.12f,0}; f.step();
		check(f.control.slide_held() && f.spent==1, "side jitter under breakaway limit does not detach or alter axial stroke");
		f.geometry.hand_in_gun[1]=.19f; f.step();
		check(!f.control.slide_held() && f.spent==1, "deliberate sideways breakaway finishes stroke once");
	}
	{
		using namespace vr::gameplay::hands;
		const auto rest=w::m9::slide_wrist_rest;
		for (float t : {-.2f,0.f,.03f,.061f,.3f})
		{
			const auto wrist=p::part_wrist(rest,{-1,0,0},t,.061f);
			check(std::abs(wrist.position[0]-(rest.position[0]-std::clamp(t,0.f,.061f))) < .0001f &&
				wrist.position[1]==rest.position[1] && wrist.position[2]==rest.position[2] && wrist.rotation==rest.rotation,
				"part wrist constrained to axis, end stops and authored rotation");
		}
		check(p::sweep_well({0,0,-.2f},{0,0,-.1f},.025f,.012f,.12f), "asymmetric capture extends down twelve cm");
		check(!p::sweep_well({.05f,0,.025f},{0,0,.025f},.025f,.012f,.12f), "larger capture does not extend above well");
	}
	auto corrupt = w::m9::reload_interaction;
	{
		const auto& rules=w::m9::reload_rules;
		m::state before{49,9,1,true,true,11,0};
		auto grant=m::reconcile_native_grant(rules,before,{15,90},1000000);
		check(grant.valid && grant.after==m::ammo_projection{12,93} && grant.next.magazine_rounds==11 && grant.next.chamber_loaded,
			"observed ammo-box 12/0 to15/90 preserves feed and credits three clip rounds to reserve");
		check(m::total_rounds(grant.next)==105, "grant conserves native observed budget");
		check(!m::reconcile_native_grant(rules,grant.next,grant.after,1000000).valid, "normalized grant does not apply twice");
		before.magazine_rounds=14; before.reserve_rounds=90;
		check(!m::reconcile_native_grant(rules,before,{15,90},1000000).valid,
			"unchanged stock capacity is not a repeated grant");
		check(!m::reconcile_native_grant(rules,before,{15,95},1000000).valid,
			"reserve-only supply stays on ordinary reconciliation path");
		before.magazine_rounds=15; before.reserve_rounds=90;
		grant=m::reconcile_native_grant(rules,before,{15,90},1000000);
		check(grant.valid && grant.after==m::ammo_projection{16,89}, "stock full-capacity clamp pays from reserve without deleting chamber");
		for (int kind=0;kind<3;++kind)
		{
			before={49,9,1,kind!=0,false,0,5,15,vr::hand::left,kind==1 ? m::action_state::locked_open : m::action_state::closed};
			grant=m::reconcile_native_grant(rules,before,{15,90},1000000);
			check(grant.valid && grant.after==m::ammo_projection{0,105} && grant.next.held_rounds==15 &&
				grant.next.magazine_inserted==before.magazine_inserted && grant.next.action==before.action && !grant.next.chamber_loaded,
				"grant preserves no-mag/empty-follower/closed-empty states and escrow");
		}
		before={49,9,1,true,true,11,60};
		check(!m::reconcile_native_grant(rules,before,{15,57},1000000).valid, "native reserve-to-clip reload is not a grant");
		check(!m::reconcile_native_grant(rules,before,{11,60},1000000).valid, "unexplained loss stays rejected");
		check(!m::reconcile_native_grant(rules,before,{17,90},1000000).valid, "over-capacity credit rejected");
		check(!m::reconcile_native_grant(rules,before,{15,1000000},1000000).valid, "redirected reserve cannot exceed native bounded domain");
		check(!m::reconcile_native_grant(rules,before,{-1,90},1000000).valid, "negative native ammo rejected");
	}
	{
		p::part_return_transition spring;
		auto now = p::clock::time_point{1s};
		check(spring.update(1,1,true,.061f,now,.075f)==.061f, "held slide directly follows raw constrained travel");
		now+=10ms;
		check(spring.update(1,1,false,0,now,.075f)==.061f, "release starts from displayed slide position without snap");
		const float midway=spring.update(1,1,false,0,now+30ms,.075f);
		check(midway>0 && midway<.061f, "slide return interpolates between scene poses");
		check(spring.update(1,1,false,0,now+80ms,.075f)==0 && !spring.active(), "finite slide return ends exactly at closed stop");
		spring.update(1,1,true,.061f,now+90ms,.075f);
		spring.update(1,1,false,.047f,now+100ms,.075f);
		check(spring.update(1,1,false,.047f,now+180ms,.075f)==.047f, "empty follower return ends at locked stop");
		check(spring.update(1,1,false,0,now+190ms,.075f)==.047f, "B lock release also starts a cosmetic return");
		check(spring.update(1,1,true,.02f,now+200ms,.075f)==.02f && !spring.active(), "regrabbing immediately cancels cosmetic return");
		spring.update(1,1,false,0,now+210ms,.075f);
		check(spring.update(2,1,false,0,now+220ms,.075f)==0, "new weapon instance cannot inherit return animation");
	}
	{
		using scheduler::detail::execution_scope;
		using scheduler::pipeline;
		const auto server = [] { return scheduler::is_executing(pipeline::server); };
		check(!server() && !scheduler::is_executing(pipeline::count), "outside a callback has no write authority");
		{
			execution_scope startup(pipeline::server);
			check(server(), "startup server callback has scoped authority");
			{
				execution_scope nested(pipeline::renderer);
				check(!server(), "nested foreign pipeline cannot borrow server authority");
			}
			check(server(), "nested callback restores enclosing context");
			bool isolated{}, migrated{}, revoked{};
			std::thread level([&] {
				isolated = !server();
				{ execution_scope frame(pipeline::server); migrated = server(); }
				revoked = !server();
			});
			level.join();
			check(isolated && migrated && revoked && server(),
				"server callback can migrate threads without leaking or retaining authority");
		}
		check(!server(), "same OS thread loses authority after callback exits");
		try { execution_scope unwound(pipeline::server); throw 1; }
		catch (int) {}
		check(!server(), "exception also revokes callback authority");
		for (const auto type : {pipeline::async, pipeline::renderer, pipeline::lui, pipeline::main})
		{
			execution_scope foreign(type);
			check(!server(), "nonserver callbacks cannot commit physical ammunition");
		}
	}
	corrupt.slide_axis = {};
	check(!p::valid(corrupt), "zero slide direction rejected");
	std::cout << "physical reload failures=" << failures << '\n';
	return failures ? 1 : 0;
}
