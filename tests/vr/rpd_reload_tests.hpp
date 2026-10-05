#pragma once
#include "component/vr/gameplay/weapons/rpd/profile.hpp"
#include "component/vr/gameplay/weapons/rpd/reload_profile.hpp"

namespace rpd_reload_tests
{
 template<class Fixture,class Check>void run(Check check)
 {
  namespace w=vr::gameplay::weapons;namespace m=w::mechanics;using vr::hand;
  const auto* p=&w::rpd::physical;
  for(const auto* definition:{&w::rpd::physical,&w::rpd::digital_physical})for(auto rear:{hand::left,hand::right})
  {
   Fixture f(definition);f.owner.rear=rear;f.step();const auto total=m::total_rounds(f.state);
   f.manipulation=false;f.button(true);
   check(f.state.belt.bridge==1 && f.state.belt.cover==0 && f.last_effect==m::effect::bridge_open && m::total_rounds(f.state)==total,
    "RPD main-grip B/Y opens optic bridge with either physical hand without an offhand lease or ammo changes");
   const auto revision=f.state.revision;f.step();f.button(false);f.button(true);
   check(f.state.belt.bridge==1 && f.state.revision==revision,"held/repeated B/Y cannot close or retrigger an already open bridge");
   auto closed=f.state;closed.belt.bridge=0;f.adopt(closed);f.button(false);f.writable=false;f.button(true);f.writable=true;f.step();
   check(f.state.belt.bridge==0,"failed B/Y bridge write is not queued");f.button(false);f.button(true);
   check(f.state.belt.bridge==1,"fresh B/Y edge can retry the bridge release");
   m::request q{m::operation::release_bridge,f.state.weapon,f.state.instance_generation,f.state.revision,rear,hand(1-int(rear))};
   check(m::plan(*f.rules,f.state,q).error==m::rejection::wrong_hand,"bridge button operation cannot be requested by the offhand");
   Fixture support(definition);support.owner.rear=hand::none;support.owner.support=rear;support.step();support.button(true);
   check(support.state.belt.bridge==0,"foregrip-only carry does not gain a main-grip bridge button");
  }
  for(float age:{-1.f,0.f,.03f,.06f,1.f})
  {
   const auto expected=age<0?1.f:std::clamp(age/.06f,0.f,1.f);
   check(std::abs(w::fired_bolt_travel(*p->bolt,0,true,age)-expected*p->bolt->locked_m)<.00001f,"static RPD fire clip returns bolt to its retained sear");
   check(w::fired_bolt_travel(*p->bolt,0,false,age)==0,"last shot and empty release leave bolt forward");
   check(w::fired_bolt_travel(*p->bolt,p->interaction.slide_stroke,true,age)==p->interaction.slide_stroke,"manual bolt travel always overrides cosmetic shot motion");
  }
  for(auto rear:{hand::left,hand::right})for(bool empty:{false,true})for(int cock_stage:{0,1,2,3})
  {
   Fixture f(p);f.owner.rear=rear;auto supplied=f.state;supplied.reserve_rounds=300;f.adopt(supplied);f.step();
   const auto off=rear==hand::left?hand::right:hand::left;
   const auto query=[&](m::operation op,float amount=0.f){m::request q{op,f.state.weapon,f.state.instance_generation,f.state.revision,rear,
    (op==m::operation::accepted_shot || op==m::operation::dry_fire)?rear:off};q.cover=amount;return m::plan(*f.rules,f.state,q);};
   const auto apply=[&](m::operation op,float amount=0.f){const auto tx=query(op,amount);check(bool(tx),"RPD reload transaction admitted");if(tx && f.commit(tx))f.state=tx.next;};
   if(empty)for(int i=0;i<100;++i)apply(m::operation::accepted_shot);
   else {auto s=f.state;s.action=m::action_state::closed;f.adopt(s);}
   const auto total=m::total_rounds(f.state);
   check(!query(m::operation::move_cover,1) && !query(m::operation::pull_magazine),"closed bridge and cover prevent skipping physical access");
   if(cock_stage==0)apply(m::operation::cycle_action);
   apply(m::operation::move_bridge,.5f);
   check(!query(m::operation::move_cover,1),"half-open bridge still obstructs cover");
   const auto partial=f.state;f.interrupt();check(f.state.belt.bridge==partial.belt.bridge,"interrupt preserves partial bridge angle");
   apply(m::operation::move_bridge,1);apply(m::operation::move_cover,.5f);
   check(!query(m::operation::move_bridge,0) && !query(m::operation::pull_magazine),"partial cover blocks bridge closure and drum extraction");
   apply(m::operation::move_cover,1);check(!m::ready(*f.rules,f.state),"RPD open cover blocks shooting");
   apply(m::operation::pull_magazine);check(!f.state.belt.laid && f.state.held_rounds==(empty?0:100),"drum removal atomically clears belt with actual ammo");
   apply(m::operation::cancel_magazine);if(cock_stage==1)apply(m::operation::cycle_action);
   apply(m::operation::draw_magazine);apply(m::operation::insert_magazine);
   check(!f.state.belt.laid,"new drum needs separate belt placement");
   if(cock_stage==2)apply(m::operation::cycle_action);
   apply(m::operation::lay_belt);apply(m::operation::move_cover,0);
   if(cock_stage==3)apply(m::operation::cycle_action);
   check(m::ready(*f.rules,f.state),"folded optic bridge alone does not block an otherwise ready gun");
   apply(m::operation::move_bridge,0);
   check(m::ready(*f.rules,f.state) && m::total_rounds(f.state)==total && m::native_ammo(f.state).loaded==100,"all RPD cocking orders conserve ammo and restore ready state");
   apply(m::operation::extract_chamber);apply(m::operation::finish_stroke);
   check(m::total_rounds(f.state)==total && f.state.belt.laid,"recocking RPD never ejects a live chamber round");
   auto invalid=f.state;invalid.belt.bridge=std::numeric_limits<float>::quiet_NaN();
   check(!m::valid(*f.rules,invalid),"nonfinite bridge state is rejected");
   invalid=f.state;invalid.belt.cover=.5f;check(!m::valid(*f.rules,invalid),"intersecting bridge and cover state is rejected");
  }
  for(auto rear:{hand::left,hand::right})
  {
   Fixture f(p);f.owner.rear=rear;f.step();auto& c=f.geometry.belt;
   c.bridge_distance=0;c.cover_distance=0;f.trigger(true);
   check(f.control.belt_grip().part==w::belt_feed::lease::none && f.state.belt.bridge==0,"latched bridge requires its release control, not a bridge grasp");
   f.trigger(false);c.release_distance=0;f.trigger(true);
   check(f.control.belt_grip().part==w::belt_feed::lease::bridge_release && f.state.belt.bridge==1,"one release press frees bridge without manual swing");
   const auto revision=f.state.revision;c.bridge_angle=p->interaction.belt->bridge->angle*.5f;f.step();
   check(f.state.revision==revision && f.control.belt_grip().part==w::belt_feed::lease::bridge_release,"holding release does not retrigger or become a bridge grasp");
   f.trigger(false);c.release_distance=10;c.bridge_settled=false;f.trigger(true);
   check(f.control.belt_grip().part==w::belt_feed::lease::none,"automatic drop cannot acquire cover or bridge until visibly clear");
   f.trigger(false);c.bridge_settled=true;c.bridge_angle=p->interaction.belt->bridge->angle;
   c.bridge_distance=10;f.trigger(true);c.cover_angle=p->interaction.belt->cover_angle;f.step();f.trigger(false);
   check(f.state.belt.cover==1,"cleared bridge enables physical cover grasp");
   c.cover_distance=10;c.bridge_distance=0;f.trigger(true);c.bridge_angle=0;f.step();
   check(f.state.belt.bridge==w::belt_feed::bridge_clearance,"closing bridge stops at cover clearance");
   f.input.focused=false;f.step();check(f.control.belt_grip().part==w::belt_feed::lease::none && f.state.belt.cover==1,"tracking loss releases bridge lease without resetting mechanics");
   Fixture failed(p);failed.geometry.belt.release_distance=0;failed.writable=false;failed.trigger(true);failed.writable=true;failed.step();
   check(failed.state.belt.bridge==0,"failed release write cannot execute later from the same press");
   failed.trigger(false);failed.trigger(true);check(failed.state.belt.bridge==1,"new press can retry rejected bridge release");
   Fixture closing(p);closing.owner.rear=rear;auto open=closing.state;open.belt.bridge=1;closing.adopt(open);closing.step();
   auto& close=closing.geometry.belt;close.bridge_distance=0;close.bridge_angle=p->interaction.belt->bridge->angle;closing.trigger(true);
   close.bridge_angle=p->interaction.belt->bridge->angle*.5f;closing.step();closing.trigger(false);
   check(std::abs(closing.state.belt.bridge-.5f)<.001f,"released return grasp retains partial bridge closure");
   closing.trigger(true);close.bridge_angle=0;closing.step();closing.trigger(false);
   check(closing.state.belt.bridge==0 && closing.state.belt.cover==0,"late native return pose permits manual bridge closure and latch");
  }
  for(auto rear:{hand::left,hand::right})for(std::uint8_t style=0;style<p->slide_grips.size();++style)
  for(bool opened:{false,true})
  {
   Fixture f(p);f.owner.rear=rear;auto s=f.state;s.action=m::action_state::closed;if(opened){s.belt.bridge=1;s.belt.cover=1;}f.adopt(s);f.step();
   const auto total=m::total_rounds(f.state);f.geometry.slide_distance=0;f.geometry.slide_pose=style;f.trigger(true);
   check(f.control.slide_held(),"RPD index/pinky handles acquire with either hand, cover open or closed");
   const auto grip=f.control.slide_grip();f.move_slide(grip,p->interaction.slide_stroke);f.move_slide(grip,0);f.trigger(false);
   check(!f.control.slide_held() && f.state.action==m::action_state::cocked_open && m::total_rounds(f.state)==total,"RPD handle releases independently and retains cocked bolt");
  }
 }
}
