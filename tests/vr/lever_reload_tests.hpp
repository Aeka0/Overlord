#pragma once
#include "component/vr/gameplay/weapons/model1887/profile.hpp"

namespace lever_reload_tests
{
	template<class Check> void run(Check check)
	{
		namespace w=vr::gameplay::weapons;namespace t=w::tube;using namespace std::chrono_literals;
		struct fixture
		{
			t::state state=t::import_native(w::model1887::feed.ammunition,40,1,{5,20});
			w::hold owner{40,1,vr::hand::right,vr::hand::none,w::hold_source::interaction,1};
			t::controller control;vr::controller_input::frame input{};t::geometry geometry{true,40,1,1,1};
			t::clock::time_point now{1s};w::ammunition::projection native{5,20};bool writable{true};int cases{},live{};float pitch{};std::chrono::nanoseconds cadence{10ms};vr::hand physical{vr::hand::right};
			fixture(vr::hand rear,std::chrono::nanoseconds interval=10ms)
			{
				cadence=interval;physical=rear;owner.rear=rear;input.focused=true;input.sequence=input.reference_generation=input.continuity_generation=1;
				for(int h=0;h<2;++h){input.grip[h].valid=input.aim[h].valid=true;input.aim[h].tracking.orientation={{{1,0,0},{0,1,0},{0,0,1}}};input.trigger[h]=input.secondary[h]={true,false,0,1};input.squeeze[h]={true,true,0,1};}
				geometry.port_delta=geometry.tube_delta={0,0,1};geometry.port_alignment=geometry.tube_alignment=1;step();
			}
			int rear()const{return int(physical);}int off()const{return 1-rear();}
			bool commit(const t::transaction& tx){if(!writable || native!=tx.before)return false;native=tx.after;cases+=tx.case_ejected;live+=tx.ejected;return true;}
			void step()
			{
				now+=cadence;++input.sequence;input.sampled_at=now;geometry.input_sequence=input.sequence;
				input.aim[rear()].tracking.orientation={{{1,0,0},{0,std::cos(pitch),-std::sin(pitch)},{0,std::sin(pitch),std::cos(pitch)}}};
				control.update(w::model1887::feed.interaction,w::model1887::feed.ammunition,state,input,owner,geometry,true,now,[&](const auto& tx){return commit(tx);});
			}
			void key(bool down){auto& b=input.secondary[rear()];if(down && !b.down)++b.presses;if(!down && b.down)++b.releases;b.down=down;step();}
			void move(float angle,float increment=.02f){while(std::abs(angle-pitch)>.001f){pitch+=std::clamp(angle-pitch,-increment,increment);step();}}
			bool shot(){const auto tx=t::plan(w::model1887::feed.ammunition,state,{t::operation::accepted_shot,40,1,state.revision,owner.rear,owner.rear});if(!tx || !commit(tx))return false;state=tx.next;return true;}
			void pinch_hand(int h,bool down){auto& b=input.trigger[h];if(down && !b.down)++b.presses;b.down=down;step();}
			void pinch(bool down){pinch_hand(off(),down);}
			void support_only(){owner.support=vr::hand(off());owner.rear=vr::hand::none;input.squeeze[rear()].down=false;++owner.rear_revision;step();}
			void fixed_grip(){owner.rear=physical;owner.attachment=w::control_attachment::fixed;input.squeeze[rear()].down=true;++owner.rear_revision;step();}
		};
		for(auto rear:{vr::hand::left,vr::hand::right})
		{
			fixture f(rear);f.move(.4f);check(f.control.travel()==0 && f.live==0,"closed fireable chamber locks ordinary wrist motion without Y");f.move(0);
			f.key(true);f.key(false);f.move(.3f);check(f.control.travel()==0,"Y released before the stroke leaves a fireable chamber locked");f.move(0);
			check(f.shot() && f.state.spent_case && f.native.loaded==4,"lever shot retains case and consumes chamber only");
			f.move(.35f);check(f.control.travel()>0 && f.cases==0,"spent chamber unlocks automatically without Y");
			f.move(.60f);check(f.cases==1 && f.state.phase==t::action::held_open && f.control.travel()<.095f,"usable opening extracts once before the full mechanical stop");
			const float opening=f.control.travel();for(int i=0;i<10;++i)f.step();check(f.cases==1 && f.control.travel()==opening,"holding the partial opening cannot repeat extraction");
			f.geometry.waist_distance=0;f.pinch(true);f.step();check(f.state.held_rounds==1,"shell can be drawn while unlocked lever is held still");
			f.geometry.tube_delta={};f.step();check(!f.state.held_rounds && f.state.stored==5,"tube insertion succeeds at usable partial opening");f.pinch(false);
			f.move(-.1f);f.step();check(f.state.chamber && f.state.stored==4 && f.native.loaded==5 && f.control.fire_armed(),"return without Y closes and feeds once, then relocks");
			f.move(.4f);check(f.control.travel()<=.002f && f.live==0,"completed loaded cycle remains locked without Y");
			f.key(true);f.move(.65f);f.key(false);const auto partial=f.control.travel();f.move(1.f);
			check(partial>0 && f.live==1,"once motion starts Y can be released and live extraction completes");f.move(.1f);f.step();
			check(f.state.chamber && f.control.travel()==0 && f.control.lever_pose().open==0,"released unlock does not lock a partially open lever");
			check(!t::plan(w::model1887::feed.ammunition,f.state,{t::operation::rack_open,40,1,f.state.revision,rear,vr::hand(f.off()),true}),"auxiliary hand cannot impersonate lever operator");
			fixture rejected(rear);rejected.key(true);rejected.writable=false;rejected.move(.7f);check(rejected.state.chamber && rejected.live==0,"failed extraction preserves live chamber and committed travel");
			rejected.writable=true;rejected.key(false);rejected.move(.9f);check(rejected.live==1,"fresh movement can complete an unlocked interrupted stroke");
			fixture grip(rear);grip.shot();grip.move(.7f);const float retained=grip.control.travel();
			grip.owner.support=vr::hand(grip.off());grip.owner.rear=vr::hand::none;++grip.owner.rear_revision;grip.step();
			check(grip.control.travel()==retained,"support-only carry preserves released lever angle");
			grip.owner.rear=rear;grip.owner.attachment=w::control_attachment::fixed;++grip.owner.rear_revision;grip.step();grip.move(0);
			check(!grip.control.lever_pose().grasped && grip.control.travel()==retained,"reacquiring original grip cannot close or move open lever");
			grip.move(.7f);check(grip.control.travel()==retained,"ordinary aiming from a fixed grip leaves the lever unchanged");
			grip.owner.rear=vr::hand::none;++grip.owner.rear_revision;grip.step();grip.owner.rear=rear;grip.owner.attachment=w::control_attachment::moving;++grip.owner.rear_revision;grip.step();
			grip.move(-.1f);grip.step();check(grip.control.lever_pose().grasped && grip.state.chamber && grip.control.travel()<=.002f,"explicit lever regrasp resumes and closes without Y");
			fixture spin(rear);spin.shot();spin.move(.5f,.1f);
			check(spin.control.lever_pose().spinning,"spent chamber admits deliberate assisted spin without Y");
			for(int i=0;i<100;++i)spin.step();check(spin.control.lever_pose().spinning && spin.control.fire_armed() && spin.cases==1,"closed loaded mechanism is fireable during the cosmetic catch tail");
			spin.move(0);for(int i=0;i<30;++i)spin.step();check(!spin.control.lever_pose().spinning && spin.state.chamber && spin.native.loaded==4,"unmodified wrist return finishes conserved spin");
			fixture two(rear);two.owner.support=vr::hand(two.off());two.shot();two.move(.5f,.1f);check(!two.control.lever_pose().spinning,"supported weapon cannot enter spin");
			fixture lost(rear);lost.shot();lost.move(.5f,.1f);lost.input.aim[lost.rear()].valid=false;lost.step();check(!lost.control.lever_pose().spinning && !lost.control.fire_armed(),"tracking loss cancels spin and firing authority");
			fixture early(rear);early.shot();early.move(.3f);early.geometry.waist_distance=0;early.pinch(true);early.step();early.geometry.tube_delta={};early.step();
			check(early.state.held_rounds==1 && early.state.spent_case,"too-small opening cannot insert a shell or eject a case");
			fixture jump(rear);jump.shot();jump.step();jump.pitch=2;jump.step();check(jump.cases==0 && jump.control.travel()==0,"tracking jump cannot create extraction");
		}

		for(auto rear:{vr::hand::left,vr::hand::right})
		{
			fixture free(rear);free.shot();free.move(.7f);const auto opening=free.control.travel();free.support_only();
			free.geometry.waist_distance=0;free.pinch_hand(free.rear(),true);free.step();
			check(free.state.loader_hand==rear && free.state.held_rounds==1 && !free.owner.can_fire() && !free.control.fire_armed(),"released lever hand draws ammunition while opposite foregrip carries gun without firing authority");
			free.geometry.port_delta={0,0,1};free.geometry.tube_delta={.065f,0,0};free.geometry.tube_alignment=-.25f;free.step();
			check(free.state.held_rounds==0 && free.state.stored==5 && free.control.travel()==opening && free.native.loaded==5,"wide oblique shell insertion works from former lever hand and preserves angle/budget");
			free.pinch_hand(free.rear(),false);free.fixed_grip();free.move(0);free.step();free.key(true);
			check(free.control.lever_pose().returning && !free.control.lever_pose().grasped && !free.control.fire_armed(),"fresh Y/B at original grip starts rapid return without regripping lever");
			for(int i=0;i<30;++i)free.step();
			check(free.control.travel()==0 && free.state.chamber && free.state.stored==4 && free.native.loaded==5,"rapid return chambers once and preserves loaded budget");
			free.move(.4f);check(free.control.travel()==0,"held return button cannot immediately unlock the completed loaded action");
			free.key(false);free.step();check(free.control.fire_armed(),"neutral trigger rearms after successful rapid return");
			fixture fail(rear);fail.shot();fail.move(.7f);fail.support_only();fail.fixed_grip();fail.writable=false;fail.key(true);for(int i=0;i<30;++i)fail.step();
			check(fail.state.phase==t::action::held_open && !fail.state.chamber && fail.control.travel()>0 && fail.native.loaded==4,"failed native closure retains partial return and cannot visually snap closed or feed");
			fail.writable=true;fail.key(false);fail.key(true);for(int i=0;i<30;++i)fail.step();check(fail.state.chamber && fail.control.travel()==0,"fresh return press retries failed closure exactly once");
			fixture interrupted(rear);interrupted.shot();interrupted.move(.7f);interrupted.support_only();interrupted.fixed_grip();interrupted.key(true);interrupted.input.focused=false;interrupted.step();const auto stopped=interrupted.control.travel();interrupted.input.focused=true;for(int i=0;i<30;++i)interrupted.step();
			check(interrupted.control.travel()==stopped && !interrupted.state.chamber && !interrupted.control.lever_pose().returning,"focus loss cancels return without delayed automatic completion");
			fixture held_key(rear);held_key.shot();held_key.move(.7f);held_key.support_only();held_key.input.secondary[held_key.rear()].down=true;held_key.fixed_grip();for(int i=0;i<30;++i)held_key.step();
			check(held_key.control.travel()>0 && !held_key.control.lever_pose().returning,"holding Y/B while reacquiring original grip cannot queue a return");
			fixture angle(rear);angle.shot();angle.move(.7f);angle.support_only();angle.geometry.waist_distance=0;angle.pinch_hand(angle.rear(),true);angle.step();angle.geometry.tube_delta={};angle.geometry.tube_alignment=-.8f;angle.step();
			check(angle.state.held_rounds==1,"reversed shell outside expanded angle tolerance remains rejected");angle.input.grip[angle.rear()].valid=false;angle.step();check(angle.state.held_rounds==0 && angle.native.reserve==20,"lost loading hand refunds its held shell during foregrip-only carry");
		}

		for(int hz:{60,72,90,120,144})for(auto rear:{vr::hand::left,vr::hand::right})
		{
			fixture f(rear,std::chrono::nanoseconds{1000000000/hz});f.shot();f.key(true);f.move(.5f,.1f);
			for(int i=0;i<hz;++i)f.step();check(f.control.lever_pose().spinning && f.cases==1,"spin extraction and catch gate agree across XR frame rates");
			f.move(0);for(int i=0;i<hz/2;++i)f.step();
			check(!f.control.lever_pose().spinning && f.state.chamber && f.native.loaded==4,"spin completes one conserved cycle across XR frame rates");
		}
		// Once the catch has been accepted, ordinary aiming and a premature
		// firing attempt must not leave the shared interaction lock latched.
		for(int hz:{60,90,144})for(auto rear:{vr::hand::left,vr::hand::right})
		{
			fixture moving(rear,std::chrono::nanoseconds{1000000000/hz});moving.shot();moving.move(.5f,.1f);moving.move(0);
			for(int i=0;i<hz*2 && moving.control.lever_pose().spin<.90f;++i)moving.step();
			check(moving.control.lever_pose().spinning && moving.control.lever_pose().spin>=.90f,"spin fixture reaches accepted catch tail");
			moving.move(.7f,.04f);
			for(int i=0;i<hz/3;++i)moving.step();
			check(!moving.control.lever_pose().spinning && moving.control.fire_armed(),"aiming away after accepted catch cannot re-lock the completion tail");
			fixture firing(rear,std::chrono::nanoseconds{1000000000/hz});firing.shot();firing.move(.5f,.1f);
			for(int i=0;i<hz;++i)firing.step();
			check(firing.state.chamber && firing.control.lever_pose().spin>=.85f,"early fire fixture has a closed action in the visual catch tail");
			firing.input.trigger[firing.rear()].down=true;
			for(int i=0;i<hz/3;++i)firing.step();
			check(!firing.control.lever_pose().spinning && firing.control.fire_armed() && firing.native.loaded==4,"fresh Trigger after mechanical closure finishes the cosmetic spin without retaining a fire lock");
			firing.input.trigger[firing.rear()].down=false;firing.step();
			check(firing.control.fire_armed(),"neutral Trigger rearms normally after early-fire catch completion");
		}

		for(auto rear:{vr::hand::left,vr::hand::right})
		{
			fixture support(rear);support.shot();support.move(.5f,.1f);
			check(w::lever::blocks_support(support.control.lever_pose()),"active opening/spinning still excludes premature fore-end grasp");
			for(int i=0;i<100;++i)support.step();
			check(support.state.chamber && !w::lever::blocks_support(support.control.lever_pose()),"closed catch tail exposes fore-end to the shared grip arbiter");
			support.owner.support=vr::hand(support.off());support.step();support.step();support.step();
			check(!support.control.lever_pose().spinning && support.control.fire_armed() && support.native.loaded==4,
				"accepted fore-end catch clears flourish state without feeding again or retaining the fire lock");
			fixture early(rear);early.shot();early.move(.5f,.1f);early.input.trigger[early.rear()].down=true;
			const float open=early.control.travel();for(int i=0;i<40;++i)early.step();
			check(early.control.travel()==open && !early.state.chamber,"Trigger during an open spin still cannot finish or feed the mechanism");
		}

		for(int hz:{60,90,144})for(auto rear:{vr::hand::left,vr::hand::right})for(bool spent:{false,true})
		{
			fixture empty_spin(rear,std::chrono::nanoseconds{1000000000/hz});
			empty_spin.state=t::import_native(w::model1887::feed.ammunition,40,1,{spent?1:0,20});empty_spin.native={spent?1:0,20};
			if(spent)empty_spin.shot();empty_spin.move(.5f,.1f);
			check(empty_spin.control.lever_pose().spinning,"empty weapon admits opening flick");
			for(int i=0;i<hz && empty_spin.control.lever_pose().spinning;++i)empty_spin.step();
			check(empty_spin.control.travel()==.1f && empty_spin.state.phase==t::action::held_open && !empty_spin.control.lever_pose().spinning && empty_spin.cases==int(spent),
				"empty tube and chamber stop at full opening and eject the last case once without a complete rotation");
			empty_spin.move(0,.04f);check(empty_spin.control.travel()==.1f,"empty-flick wrist recovery cannot inadvertently close the action");
			for(int i=0;i<hz/5;++i)empty_spin.step();
			empty_spin.geometry.waist_distance=0;empty_spin.pinch(true);empty_spin.step();empty_spin.geometry.tube_delta={};empty_spin.step();empty_spin.pinch(false);
			check(empty_spin.state.stored==1 && !empty_spin.state.chamber && empty_spin.control.travel()==.1f,"empty-flick stop remains loadable and cannot auto chamber");
			empty_spin.move(-1.f);empty_spin.step();check(empty_spin.state.chamber && empty_spin.control.travel()==0,"loaded stopped action can be deliberately closed by the existing manual stroke");
		}

		for(auto rear:{vr::hand::left,vr::hand::right})for(auto interval:{200ms,250ms})
		{
			fixture slow(rear,interval);slow.owner.support=vr::hand(slow.off());slow.shot();slow.step();slow.move(.95f,.15f);
			check(slow.cases==1 && slow.state.phase==t::action::held_open,"slow simulation permits M1887 manual opening and one case ejection");
			slow.move(0,.15f);slow.step();
			check(slow.state.chamber && slow.native.loaded==4 && slow.control.fire_armed(),"slow simulation closes feeds and rearms M1887 without duplicating ammunition");
			slow.key(true);slow.move(.75f,.15f);slow.key(false);slow.support_only();slow.fixed_grip();slow.key(true);
			for(int i=0;i<10;++i)slow.step();
			check(slow.control.travel()==0 && slow.state.chamber && !slow.control.lever_pose().returning,"fixed-grip rapid return is not cancelled by slow simulation");
			fixture spin(rear,interval);spin.shot();spin.step();spin.step();spin.pitch=1.4f;spin.step();
			check(spin.control.lever_pose().spinning,"observed fast motion can start a spin at slow consumer cadence");
			for(int i=0;i<30;++i)spin.step();spin.pitch=0;spin.step();for(int i=0;i<30;++i)spin.step();
			check(spin.cases==1 && spin.state.chamber && !spin.control.lever_pose().spinning,"slow simulation completes spin catch with exactly one ejection and feed");
			fixture reset(rear,interval);reset.shot();reset.step();reset.move(.3f,.1f);const auto retained=reset.control.travel();
			++reset.input.continuity_generation;reset.pitch=.9f;reset.step();
			check(reset.control.travel()==retained && reset.cases==0,"producer discontinuity cannot fund a lever stroke even during slow motion");
			reset.input.focused=false;reset.step();reset.input.focused=true;reset.pitch=0;reset.step();
			check(reset.control.travel()==retained,"focus loss retains mechanical travel without catch-up motion");
		}

		const auto rules=w::model1887::feed.ammunition;
		check(t::loaded_capacity(rules)==5 && !t::valid(rules,t::import_native(rules,40,1,{6,20})),"M1887 never silently inherits an unreviewed sixth round");
		fixture empty(vr::hand::right);empty.state=t::import_native(rules,40,1,{0,20});empty.native={0,20};empty.key(true);empty.move(.95f);empty.move(0);empty.key(false);
		check(empty.native.loaded==0 && empty.cases==0 && !empty.state.chamber,"empty lever cycle creates neither cartridges nor spent cases");
		fixture trigger(vr::hand::right);trigger.shot();trigger.key(true);trigger.move(.95f);trigger.input.trigger[trigger.rear()].down=true;trigger.move(0);trigger.key(false);
		check(!trigger.control.fire_armed() && !trigger.state.chamber,"holding Trigger pauses mechanism and cannot queue a completion shot");
		fixture offlost(vr::hand::right);offlost.input.aim[0].valid=offlost.input.grip[0].valid=false;offlost.shot();offlost.key(true);offlost.move(.95f);offlost.move(0);offlost.key(false);
		check(offlost.state.chamber && offlost.cases==1,"single-hand lever does not depend on opposite controller tracking");
		fixture gap(vr::hand::right);gap.shot();gap.key(true);gap.move(.4f);const auto before=gap.control.travel();gap.now+=1s;gap.step();
		check(gap.control.travel()==before && gap.cases==0,"long sample gap cannot create catch-up travel");

	}
}
