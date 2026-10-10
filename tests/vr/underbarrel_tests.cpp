#include "support_handoff_tests.hpp"
#include "component/vr/hand.hpp"
using vr::hand;
#include "component/vr/gameplay/underbarrel_feed.hpp"
#include "component/vr/gameplay/underbarrel_supply.hpp"
#include "component/vr/gameplay/underbarrel_binding.hpp"
#include "component/vr/gameplay/underbarrel_rig.hpp"
#include "component/vr/gameplay/underbarrel_runtime.hpp"
#include "component/vr/gameplay/underbarrel_feedback.hpp"
#include "component/vr/gameplay/native_ammunition_storage.hpp"
#include "component/vr/digital_button_gate.hpp"
#include "component/vr/gameplay/weapon_carry.hpp"
#include "component/vr/gameplay/weapons/scar/profile.hpp"
#include "component/vr/gameplay/weapon_carry_grip.hpp"
#include "underbarrel_data.hpp"
#include <iostream>
#include <limits>

int main()
{
	namespace w=vr::gameplay::weapons;namespace u=w::underbarrel;
	int failures{},checks{};auto check=[&](bool ok,const char* label){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<label<<'\n';}};
	support_handoff_tests::run(check);
	for(auto type:{u::kind::m203,u::kind::gp25,u::kind::shotgun})
	{
		auto ammo=u::import_native({{53,7},54,type},0,9);
		if(type==u::kind::m203)ammo.open=true;
		check(u::prefer_secondary_supply(ammo,.1f,.1f,false),"loadable empty modules prefer their own ammunition");
		check(!u::prefer_secondary_supply(ammo,.1f,.1f,true),"missing primary magazine or exhausted primary feed takes priority for every module");
		auto empty=ammo;empty.reserve=0;
		check(!u::prefer_secondary_supply(empty,.1f,.1f,false),"empty secondary reserve falls back to the primary magazine");
		auto held=ammo;held.held=1;held.loader=vr::hand::left;
		check(!u::prefer_secondary_supply(held,.1f,.1f,false),"existing secondary escrow never becomes a new draw preference");
		for(int loaded=1;loaded<=u::capacity(type);++loaded)
		{
			auto full=u::import_native(ammo.id,loaded,9);
			check(u::prefer_secondary_supply(full,.1f,.1f,false)==(type==u::kind::shotgun && loaded<4),
				"launchers require an empty chamber; chambered shotgun states top up their underfilled tube");
		}
	}
	{
		auto ammo=u::import_native({{53,7},54,u::kind::m203},0,9);ammo.open=true;
		const float threshold=.1f*u::action_open_fraction;
		check(!u::prefer_secondary_supply(ammo,std::nextafter(threshold,0.f),.1f,false) &&
			u::prefer_secondary_supply(ammo,threshold,.1f,false),"M203 needs the actual opening threshold, including a partially closing action");
		ammo.open=false;
		check(!u::prefer_secondary_supply(ammo,.1f,.1f,false),"empty closed M203 keeps primary selection");
		ammo.open=true;
		for(float travel:{-.1f,.11f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()})
			check(!u::prefer_secondary_supply(ammo,travel,.1f,false),"invalid M203 travel cannot prefer a grenade");
		for(float stroke:{0.f,-.1f,1.f,std::numeric_limits<float>::quiet_NaN()})
			check(!u::prefer_secondary_supply(ammo,.1f,stroke,false),"invalid M203 stroke cannot admit loading preference");
		check(!u::prefer_secondary_supply({},.1f,.1f,false),"unadmitted module state retains primary priority");
	}
	{
		auto ammo=u::import_native({{53,7},54,u::kind::shotgun},4,9);
		const auto shot=u::plan(ammo,{u::operation::shot,ammo.id,ammo.revision,vr::hand::right,vr::hand::left,true});
		check(shot && !u::prefer_secondary_supply(shot.next,0,.1f,false),"an empty shotgun chamber does not imply room in its still-full tube");
		if(shot)
		{
			const auto opened=u::plan(shot.next,{u::operation::open,ammo.id,shot.next.revision,vr::hand::right,vr::hand::left});
			check(opened && !u::prefer_secondary_supply(opened.next,.1f,.1f,false),"opening a full shotgun tube keeps primary priority");
			if(opened)
			{
				const auto closed=u::plan(opened.next,{u::operation::close,ammo.id,opened.next.revision,vr::hand::right,vr::hand::left});
				check(closed && u::prefer_secondary_supply(closed.next,0,.1f,false),"pump chambering makes one tube slot available for smart shell selection");
			}
		}
	}
	for(auto actor:{vr::hand::left,vr::hand::right})for(bool smart:{false,true})for(bool preferred:{false,true})for(bool grip:{false,true})
	{
		namespace hi=vr::gameplay::hand_interaction;
		const auto selected=u::select_secondary_supply(smart,preferred,grip)?hi::domain::underbarrel:hi::domain::magazine;
		check(selected==((smart?preferred!=grip:grip)?hi::domain::underbarrel:hi::domain::magazine),
			"Grip chooses the other smart default; disabled policy retains the original modifier");
		hi::arbiter arbiter;arbiter.begin(1,3);arbiter.select_supply(actor,{selected,{53,7}});
		for(auto provider:{hi::domain::magazine,hi::domain::underbarrel})
			arbiter.offer({actor,{{provider,{53,7},0,17},hi::role::supply,hi::button::trigger,hi::recipe::single,{}},1,30,.5f,1,true,true});
		int draws{};arbiter.resolve([&](const auto& candidate){++draws;return candidate.desired.destination.provider==selected;});
		check(draws==1 && arbiter.find(actor,{selected,{53,7},0,17}),"one fresh waist Trigger grants exactly the selected supply in either hand");
		check(u::route(true,false,false,false,true,selected==hi::domain::underbarrel,true)==
			(selected==hi::domain::underbarrel?u::trigger_route::secondary_supply:u::trigger_route::primary_supply),
			"secondary draw execution follows its grant even without Grip");
	}
	for(auto rear:{vr::hand::left,vr::hand::right})for(auto type:{u::kind::m203,u::kind::shotgun})for(float units:{1.f,39.3701f})
	{
		using namespace vr::gameplay::hands;
		const int off=1-int(rear);const float sign=type==u::kind::m203?1.f:-1.f;
		const auto stamp=u::clock::time_point{}+std::chrono::seconds(1);
		w::hold owner{};owner.weapon=53;owner.instance_generation=7;owner.rear=rear;owner.rear_revision=9;
		u::presentation v;v.active=v.owns_support=true;v.owner=owner;v.reference=3;v.grip=u::lease::action;v.travel=.02f;
		v.ammo=u::import_native({owner.id(),54,type},0,9);
		auto& m=v.motion;m.sequence=10;m.assembly=17;m.grip_generation=5;m.sampled_at=stamp;m.units=units;
		m.axis={sign,0,0};m.rest={.3f,0,0};m.stroke=.1f;m.tolerance=u::limits(type);
		m.start={.3f,0,0};m.previous=m.hand=m.span={.3f+sign*.02f,0,0};
		m.start_distance=.3f;m.previous_distance=m.distance=m.span[0];
		vr::controller_input::frame input;input.focused=true;input.sequence=11;input.reference_generation=3;input.sampled_at=stamp;
		for(int h=0;h<2;++h){input.grip[h].valid=input.aim[h].valid=true;input.squeeze[h]={true,true,1,5};}
		std::array<anchor,2> wrists{};for(auto& wrist:wrists)wrist.rotation={0,0,0,1};
		const auto render=u::displayed_travel;
		for(int frame=1;frame<=5;++frame)
		{
			const float travel=.02f+frame*.01f;wrists[off].position={units*(.3f+sign*travel),0,0};
			input.sampled_at=stamp+std::chrono::milliseconds(frame*10);++input.sequence;
			check(std::abs(render(v,owner,input,wrists,units,17,true,input.sampled_at)-travel)<.00001f,
				"M203 and pump follow every rendered controller frame between unchanged server samples");
		}
		check(v.travel==.02f && v.ammo.loaded==0 && v.ammo.reserve==9 && !v.ammo.open,
			"rendered travel cannot mutate authoritative motion, chamber or ammunition");
		const auto expected=render(v,owner,input,wrists,units,17,true,input.sampled_at);
		auto moved=wrists;const quat rotation{0,0,.70710678f,.70710678f};
		for(auto& wrist:moved){wrist.position=add(rotate(rotation,wrist.position),{10*units,20*units,30*units});wrist.rotation=rotation;}
		check(std::abs(render(v,owner,input,moved,units,17,true,input.sampled_at)-expected)<.00001f,
			"display stroke is invariant under shared movement and rotation in either hand");
		auto changed=input;changed.squeeze[off].down=false;
		check(render(v,owner,changed,wrists,units,17,true,changed.sampled_at)==v.travel,"release immediately stops render projection");
		changed=input;++changed.squeeze[off].generation;
		check(render(v,owner,changed,wrists,units,17,true,changed.sampled_at)==v.travel,"reconnected grip cannot reuse an old stroke");
		changed=input;++changed.reference_generation;
		check(render(v,owner,changed,wrists,units,17,true,changed.sampled_at)==v.travel,"recenter rejects prior motion basis");
		changed=input;changed.sequence=9;
		check(render(v,owner,changed,wrists,units,17,true,changed.sampled_at)==v.travel,"older render input cannot project a later server sample");
		changed=input;changed.grip[off].valid=false;
		check(render(v,owner,changed,wrists,units,17,true,changed.sampled_at)==v.travel,"tracking loss retains confirmed travel");
		auto other=owner;++other.instance_generation;
		check(render(v,other,input,wrists,units,17,true,input.sampled_at)==v.travel,"another copy of the same weapon cannot borrow motion");
		other=owner;++other.rear_revision;
		check(render(v,other,input,wrists,units,17,true,input.sampled_at)==v.travel,"handover invalidates the previous grasp");
		check(render(v,owner,input,wrists,units,18,true,input.sampled_at)==v.travel &&
			render(v,owner,input,wrists,units*2,17,true,input.sampled_at)==v.travel,"assembly and world scale changes cannot reuse local motion");
		check(render(v,owner,input,wrists,units,17,false,input.sampled_at)==v.travel &&
			render(v,owner,input,wrists,units,17,true,stamp+std::chrono::milliseconds(151))==v.travel,
			"pause and stale server samples never extrapolate action travel");
		moved=wrists;moved[off].position[0]+=units;
		check(render(v,owner,input,moved,units,17,true,input.sampled_at)==v.travel,"tracking jumps are bounded by the shared mechanical projection");
		moved=wrists;moved[off].position[0]=std::numeric_limits<float>::quiet_NaN();
		check(render(v,owner,input,moved,units,17,true,input.sampled_at)==v.travel,"nonfinite render targets cannot corrupt part placement");
		auto open=v;open.ammo.open=true;wrists[off].position={.3f*units,0,0};
		check(render(open,owner,input,wrists,units,17,true,input.sampled_at)>=.001f && open.ammo.open,
			"visual closing waits short of the server-confirmed closure");
		open=v;open.grip=u::lease::support;open.ammo.chamber=true;
		check(render(open,owner,input,wrists,units,17,true,input.sampled_at)==v.travel,"loaded locked support cannot open an action visually");
	}
	{
		// Captured live mappings differ by host: SCAR uses Winchester keys,
		// while AK/FAL use the destination alias as their notetrack key.
		for(auto op:{u::operation::open,u::operation::close,u::operation::insert})
		{
			const auto sound=u::interaction_sound(u::kind::shotgun,op);
			check(sound.kind==w::sound_reference_kind::alias && std::string_view(sound.name)==
				(op==u::operation::insert?"weap_shotattach_clipin_plr":"weap_shotattach_chamber_plr"),
				"all shotgun hosts request the verified alias explicitly instead of looking it up as a SCAR notetrack");
			check(u::interaction_sound(u::kind::m203,op).kind==w::sound_reference_kind::notetrack,"M203 retains its native notetrack mapping");
		}
		check(!u::interaction_sound(u::kind::shotgun,u::operation::shot).name &&
			!u::interaction_sound(u::kind::shotgun,u::operation::cleanup).name,"shots and cleanup cannot replay mechanical sound");
	}
	for(auto rear:{vr::hand::left,vr::hand::right})for(auto kind:{u::kind::m203,u::kind::gp25,u::kind::shotgun})
	{
		const auto off=vr::hand(1-int(rear));const u::identity id{{53,99},54,kind};
		auto s=u::import_native(id,u::capacity(kind),9);const auto initial=u::total(s);
		auto apply=[&](u::operation op,bool contact=false,bool unlock=false){auto tx=u::plan(s,{op,id,s.revision,rear,off,contact,unlock});if(tx)s=tx.next;return tx;};
		check(u::valid(s)&&u::ready(s),"native import ready");
		const auto dry=u::import_native(id,1,0);check(!u::plan(dry,{u::operation::draw,id,dry.revision,rear,off}),"zero secondary reserve never manufactures a belt round");
		check(!u::plan(s,{static_cast<u::operation>(255),id,s.revision,rear,off}),"unknown operation rejected");
		check(!apply(u::operation::shot),"no firing contact cannot emit");
		check(!u::plan(s,{u::operation::shot,id,s.revision,rear,rear,true}),"main trigger cannot spend secondary ammunition");
		check(!u::plan(s,{u::operation::shot,{{53,100},54,kind},s.revision,rear,off,true}),"stale host generation rejected");
		check(!u::plan(s,{u::operation::shot,{{53,99},55,kind},s.revision,rear,off,true}),"different secondary definition rejected");
		const auto before=s;auto tx=apply(u::operation::shot,true);check(bool(tx)&&tx.spent==1,"support shot spends one");
		check(!u::ready(s)&&s.loaded==before.loaded-1,"shot does not auto cycle");
		check(!apply(u::operation::shot,true),"held repeat cannot fire before reloading or pump");
		check(!u::plan(s,{u::operation::draw,id,before.revision,rear,off}),"stale revision rejected");
		if(kind==u::kind::gp25)
		{
			check(!s.spent&&!s.open,"GP25 does not invent a spent case or sliding action");
			check(!apply(u::operation::open),"GP25 has no M203 opening transition");
			check(bool(apply(u::operation::draw))&&bool(apply(u::operation::insert)),"muzzle loading completes GP25 feed");
		}
		else
		{
			check(s.spent,"shot retains case until extraction");
			check(bool(apply(u::operation::open)),"fired action unlocks");
			check(!s.spent&&s.open,"open removes fired case");
			check(!apply(u::operation::open),"duplicate extraction rejected");
			if(kind==u::kind::m203)check(bool(apply(u::operation::draw))&&bool(apply(u::operation::insert)),"open M203 accepts one round");
			check(!u::ready(s),"open action cannot fire even with a round inserted");
			check(bool(apply(u::operation::close))&&u::ready(s),"complete close chambers and locks");
			if(kind==u::kind::shotgun)check(bool(apply(u::operation::draw))&&bool(apply(u::operation::insert)),"shotgun tops up one after cycling");
			check(!apply(u::operation::open),"loaded action remains locked without manual release");
		}
		check(u::total(s)==initial-1,"cycle conserves all unspent ammunition");
		check(s.loaded==u::capacity(kind),"secondary capacity does not acquire an implicit extra round");
		check(bool(apply(u::operation::draw))&&!apply(u::operation::insert),"overfill retains held ammo");
		check(!apply(u::operation::draw),"one hand cannot take a second round");
		check(bool(apply(u::operation::cleanup))&&u::total(s)==initial-1,"interruption returns escrow once");
		check(!apply(u::operation::cleanup),"cleanup replay cannot duplicate ammunition");
		check(bool(apply(u::operation::draw))&&bool(apply(u::operation::discard))&&u::total(s)==initial-1,"released round follows shared no-penalty disposition policy");
	}
	check(u::route(true,true,true,true,true,true,true)==u::trigger_route::existing_lease,"held grenade is not retargeted when gun and waist overlap");
	check(u::route(true,false,true,true,true,true,true)==u::trigger_route::fire,"confirmed firing grip beats overlapping magazine and waist");
	check(u::route(true,false,false,true,true,true,true)==u::trigger_route::part,"action grasp beats a nearby waist volume");
	check(u::route(true,false,false,false,true,true,true)==u::trigger_route::secondary_supply,"empty hand grip modifier draws secondary without visiting firing grip");
	check(u::route(true,false,false,false,true,false,true)==u::trigger_route::primary_supply,"released modifier selects primary supply");
	check(u::route(false,false,true,true,true,true,true)==u::trigger_route::none,"held trigger moving between regions cannot replay");
	check(u::route(true,false,false,false,false,true,true)==u::trigger_route::none,"modifier outside contact volumes does nothing");
	vr::controller_input::digital_press_gate edge;
	check(!edge.consume({true,true,4,2}),"first sample held trigger is unarmed");
	check(!edge.consume({true,false,4,2})&&edge.consume({true,true,5,2}),"neutral then fresh trigger accepted");
	check(!edge.consume({true,true,5,2}),"duplicate frame cannot repeat");
	check(!edge.consume({true,true,6,3}),"tracking generation requires neutral");
	check(u::route(true,false,false,false,true,true,false)==u::trigger_route::primary_supply,"ordinary rifle retains its belt behavior");
	for(int hand=0;hand<2;++hand)
	{
		using namespace vr::gameplay::hands;
		const vec palm{0,0,1};const quat up{0,0,0,1},inward{hand?-.70710678f:.70710678f,0,0,.70710678f};
		check(u::support_facing(u::facing(up,palm,hand))&&!u::firing_facing(u::facing(up,palm,hand)),"upward palm only acquires ordinary support");
		check(u::firing_facing(u::facing(inward,palm,hand))&&!u::support_facing(u::facing(inward,palm,hand)),"inward palm cannot be stolen by nearby ordinary support");
		const quat diagonal{hand?-.38268343f:.38268343f,0,0,.92387953f};
		check(u::support_facing(u::facing(diagonal,palm,hand))&&!u::firing_facing(u::facing(diagonal,palm,hand)),"diagonal palm now admits support while the distinct firing gate stays strict");
		u::scene source;source.type=u::kind::m203;source.contact_hand=hand;source.axis={1,0,0};source.stroke=.0917f;source.input.sequence=10;
		source.input.squeeze[hand]={true,false,1,1};source.input.trigger[hand]={true,false,1,1};
		auto live=source.input;live.sequence=11;live.squeeze[hand]={true,true,2,1};live.trigger[hand]={true,true,2,1};
		w::hold host{};host.weapon=53;host.instance_generation=99;host.rear=vr::hand(1-hand);
		const std::array<vec,3> axes{{{1,0,0},{0,1,0},{0,0,1}}};
		const anchor gun{{0,0,0},{0,0,0,1}},raw{{.065f,0,0},{0,0,0,1}};
		const auto now=u::sample_contact(source,live,host,gun,raw,{0,0,1.7f},axes,1,0);
		check(now.input.sequence==11 && now.input.squeeze[hand].down && now.firing_distance<u::firing_acquire && u::firing_facing({now.support_facing,now.facing}),"secondary grab uses current frame and geometry before ordinary carry election");
		vr::controller_input::digital_press_gate gate;gate.consume(source.input.trigger[hand]);
		const anchor belt{{0,hand?-.21f:.21f,1.08f},up};
		const auto at_belt=u::sample_contact(source,live,host,gun,belt,{0,0,1.7f},axes,1,0);
		check(gate.consume(at_belt.input.trigger[hand]) && at_belt.waist_distance<=at_belt.waist_radius &&
			u::route(true,false,false,false,true,at_belt.input.squeeze[hand].down,true)==u::trigger_route::secondary_supply,
			"empty-hand held Grip plus fresh belt Trigger survives one-frame-old render input");
		const quat roll{.70710678f,0,0,.70710678f};
		const auto rolled=u::sample_contact(source,live,host,{{},roll},{rotate(roll,raw.position),multiply(roll,raw.rotation)},
			{0,0,1.7f},axes,1,0);
		check(u::firing_facing({rolled.support_facing,rolled.facing}),"palm classification follows weapon roll, not world-up");
		// The same real controller pose must not become palm-up merely because
		// SCAR's visual idle wrist is palm-up and M4's is a vertical foregrip.
		for(auto basis:std::array<quat,2>{{{.7879048f,-.0542386f,.0827816f,.6077922f},{.10471560f,-.11177909f,-.26729761f,.95136327f}}})
		{
			auto authored=source;authored.wrist_basis=basis;
			const auto contact=u::sample_contact(authored,live,host,gun,raw,{0,0,1.7f},axes,1,0);
			check(u::firing_facing({contact.support_facing,contact.facing}) && !u::support_facing({contact.support_facing,contact.facing}),"physical inward controller is independent of SCAR/M4 visual wrist bases");
		}
		w::profile profile{};w::carry::scene current;current.owner=host;current.authored=&profile;current.assembly=17;current.sequence=live.sequence;current.reference=live.reference_generation;
		auto cached=source;cached.owner=host;cached.assembly=17;cached.input.sequence=1;cached.input.sampled_at={};
		cached.gameplay=false;cached.input.reference_generation=999;
		check(u::binding_current(cached,current,live),"unchanged local contacts survive culled/paused render data and use current tracking space");
		current.assembly=18;check(!u::binding_current(cached,current,live),"different assembly cannot reuse stale secondary contacts");
		current.assembly=17;current.owner.instance_generation++;check(!u::binding_current(cached,current,live),"different physical gun cannot reuse old local contacts");
	}
	check(u::firing_distance({.11f,0,0},.05f)<u::firing_acquire,"M4 forward extension admits a hand eleven centimetres ahead");
	check(u::firing_distance({-.08f,0,0},.05f)>u::firing_acquire,"M4 extension does not enlarge rear admission");
	check(u::firing_distance({.03f,.08f,0},.05f)>u::firing_acquire,"M4 extension does not enlarge sideways admission");
	{
		const u::identity id{{62,2},63,u::kind::m203};auto empty=u::import_native(id,0,10);
		check(u::choose_grip(empty,0,1,.01f,{1,0})==u::contact_role::support,"empty picked-up M203 retains aim through its shared foregrip");
		check(u::choose_grip(empty,0,1,.01f,{.6f,.6f})==u::contact_role::support,"ordinary foregrip outside firing contact retains the original positional capture");
		check(u::choose_grip(empty,0,.02f,.03f,{.66867f,.69105f},.10f)==u::contact_role::support,"recorded left SCAR approach three centimetres from support is not rejected by a global palm cone");
		check(u::choose_grip(empty,0,.02f,.03f,{0,1},.10f)==u::contact_role::firing,"clear inward firing intent in the overlap still beats ordinary support");
		check(u::support_intent(.2f,{.4f,.9f}) && !u::support_intent(.02f,{.4f,.9f}),"clear firing exclusion is local to its own contact volume");
		check(!u::support_intent(1,{.1f,0}) && u::support_intent(1,{.3f,.6f}) && u::support_facing({-.3f,0},true),"M203 keeps directional acquisition but ordinary positional support retention");
		check(!u::support_facing({-.3f,-.4f},true,u::kind::shotgun),"M203 support adjustment preserves the separate shotgun policy");
		check(!u::support_facing({},true) && !u::support_facing({NAN,0},true) && !u::support_facing({0,INFINITY},true),"invalid tracking orientation still fails closed");
		const auto opened=u::plan(empty,{u::operation::open,id,empty.revision,vr::hand::right,vr::hand::left});check(bool(opened),"empty closed action immediately permits opening");
		check(u::choose_grip(opened.next,u::authored::m203_stroke,.01f,1,{0,1})==u::contact_role::firing && !u::ready(opened.next),"open M203 permits firing-position grasp but refuses emission");
		check(u::choose_grip(opened.next,u::authored::m203_stroke,1,.01f,{.55f,.7f})==u::contact_role::action,"open sliding barrel can be regrasped from its side");
		const auto closed=u::plan(opened.next,{u::operation::close,id,opened.next.revision,vr::hand::right,vr::hand::left});check(bool(closed),"empty M203 closes");
		check(u::choose_grip(closed.next,0,1,.01f,{1,0})==u::contact_role::support,"closing empty retains shared grasp without a fired-once requirement");
		const auto loaded=u::import_native(id,1,10);
		check(u::choose_grip(loaded,0,1,.09f,{1,0},.12f)==u::contact_role::support,"loaded SCAR admits the same authored support region while its action is locked");
		check(u::choose_grip(loaded,0,1,.099f,{.3f,.4f},.10f)==u::contact_role::support && u::choose_grip(loaded,0,1,.101f,{1,0},.10f)==u::contact_role::none,"support capture keeps the original ten centimetre authored sphere");
		const auto pump=u::import_native({{71,3},72,u::kind::shotgun},0,8);
		check(u::choose_grip(pump,0,1,.09f,{.2f,.4f},.10f)==u::contact_role::action,"empty pump regrasp uses host support capture rather than a separate five centimetre sphere");
		const auto tolerance=u::support_limits(u::kind::m203,.22f);
		check(tolerance.retention==.22f && tolerance.step==u::limits(u::kind::m203).step,"support adopts host breakaway while retaining tracking discontinuity protection");
		const vr::gameplay::hands::vec rest{.32108486f,.09792749f,.00464360f};const float span=vr::gameplay::hands::length(rest);
		check(u::retained_support_span(rest,.27769533f,.28f,.22f,tolerance.step),"recorded SCAR closure span retains loaded support after mechanical-to-support handoff");
		check(u::retained_support_span(rest,span+.20f,span+.19f,.22f,tolerance.step) && !u::retained_support_span(rest,span+.23f,span+.22f,.22f,tolerance.step),"legacy support hysteresis retains twenty centimetres and releases beyond twenty-two");
		check(!u::retained_support_span(rest,span,span+.4f,.22f,tolerance.step) && !u::retained_support_span(rest,.02f,.1f,.22f,tolerance.step),"jumps and coincident hands cannot retain support");
		for(int hand:{0,1})for(int degrees=-180;degrees<=180;degrees+=15)
		{
			const float angle=degrees*.0174532925199433f;
			const vr::gameplay::hands::quat rotation{std::sin(angle*.5f),0,0,std::cos(angle*.5f)};
			const auto scores=u::controller_facing(rotation,hand);
			check((u::choose_grip(loaded,0,1,.09f,scores,.10f)==u::contact_role::support)==(scores.support>=.258819f),
				"both hands use the same broad support angle across the full wrist-roll range");
			check(u::support_facing(scores,true) && u::retained_support_span(rest,span+.20f,span+.19f,.22f,tolerance.step),
				"held M203 support tolerates full wrist roll inside the host foregrip breakaway region");
			check(!u::retained_support_span(rest,span+.23f,span+.22f,.22f,tolerance.step),
				"wrist freedom cannot retain M203 beyond the host foregrip breakaway distance");
		}
		auto cycle=empty;
		for(auto operation:{u::operation::open,u::operation::draw,u::operation::insert,u::operation::close})
		{
			const auto tx=u::plan(cycle,{operation,id,cycle.revision,vr::hand::right,vr::hand::left});
			check(bool(tx),"SCAR open/draw/insert/close cycle remains admitted");if(tx)cycle=tx.next;
		}
		check(cycle.chamber && !cycle.open && u::retained_support_span(rest,.27769533f,.28f,.22f,tolerance.step) &&
			u::choose_grip(cycle,0,1,.09f,{.66867f,.69105f},.10f)==u::contact_role::support,
			"loaded closure retains support and permits regrasp at the recorded left-hand orientation");
		const auto gp=u::import_native({{61,4},62,u::kind::gp25},1,3);
		check(u::choose_grip(gp,0,.06f,.06f,{0,0},.12f)==u::contact_role::support,"GP25 keeps a single ordinary support/fire grasp");
		const float stroke=u::authored::m203_stroke;
		check(u::project_stroke({},{},{stroke,0,0},{1,0,0},0,stroke).travel==stroke,"full forward M203 stroke opens");
		check(u::project_stroke({stroke,0,0},{stroke,0,0},{},{1,0,0},stroke,stroke).travel==0,"reverse stroke returns the opened M203 fully closed");
		check(u::project_stroke({},{},{stroke*.5f,0,0},{1,0,0},0,stroke).travel<stroke*.9f,"partial pull cannot complete opening");
	}
	check(u::project_stroke({},{},{-.10f,.14f,0},{-1,0,0},0,.105f,u::limits(u::kind::shotgun)).valid,"held underbarrel pump tolerates fourteen centimetres lateral drift");
	check(!u::project_stroke({},{},{-.10f,.14f,0},{-1,0,0},0,.105f,u::limits(u::kind::m203)).valid,"shotgun retention relaxation does not widen M203 manipulation");
	check(!u::project_stroke({},{},{-.60f,0,0},{-1,0,0},0,.105f,u::limits(u::kind::shotgun)).valid,"large tracking jump still releases the pump");
	check(u::project_stroke({},{},{-.10f,.20f,0},{-1,0,0},0,.105f,u::support_limits(u::kind::shotgun,.22f)).valid,"pump travel preserves host support breakaway rather than narrow mechanical lateral range");
	{
		w::carry::scene s;s.owner.rear=vr::hand::right;s.wrists[1].position={-.05f,0,0};
		const std::array<vr::gameplay::hands::anchor,2> wrists{{{{0,0,0},{0,0,0,1}},{{1,2,3},{0,0,0,1}}}};
		const auto before=u::pump_frame(s,wrists);s.gun.rotation={.70710678f,0,0,.70710678f};
		const auto after=u::pump_frame(s,wrists);
		check(before.position==after.position && before.rotation==after.rotation,"restoring support IK cannot change the retained action's measurement frame");
	}
	check(w::underbarrel::authored::m203_axis[0]>.99f&&w::underbarrel::authored::shotgun_axis[0]<-.99f,"captured sliding directions are opposite");
	check(w::underbarrel::authored::m203_stroke>.08f&&w::underbarrel::authored::m203_stroke<.11f,"M203 source travel validated");
	check(w::underbarrel::authored::shotgun_stroke>.09f&&w::underbarrel::authored::shotgun_stroke<.12f,"pump source travel validated");
	u::scene contact;contact.units=39.3701f;contact.stroke=.1f;contact.axis={1,0,0};
	check(u::finite_contact(contact),"finite contact admitted");
	contact.stroke_hand[0]=std::numeric_limits<float>::quiet_NaN();check(!u::finite_contact(contact),"NaN cannot enter physical travel");
	contact.stroke_hand={};contact.units=0;check(!u::finite_contact(contact),"zero scale rejected");
	contact.units=39.3701f;contact.axis={0,0,0};check(!u::finite_contact(contact),"degenerate action axis rejected");
	for(auto pair:std::array<std::pair<const char*,const char*>,11>{{{"m16_grenadier","m203_m16"},{"m4_grenadier_airport","m203_m4_airport"},
		{"scar_h_grenadier","scar_h_m203"},{"masada_digital_grenadier_eotech","gl_masada_digital_eotech"},{"m4_grenadier_acog","m203_m4_acog"},
		{"m4_grenadier","m203_m4"},{"m4m203","m203_m4"},{"m4m203_acog","m203_m4_acog"},{"m4m203_eotech","m203_m4_eotech"},{"m4m203_reflex","m203_m4_reflex"},
		{"m4m203_reflex_arctic","m203_m4_reflex_arctic"}}})
	{
		check(u::classify(pair.first,pair.second,3,6,1)==u::kind::m203,"captured host/launcher families classify");
		check(u::classify(pair.first,pair.second,1,6,1)==u::kind::none,"name alone cannot authorize wrong delivery");
		check(u::classify(pair.second,pair.first,3,6,1)==u::kind::none,"reverse links do not resolve host as a module");
	}
	check(u::classify("ak47_arctic_grenadier","gl_ak47_arctic",3,6,1)==u::kind::gp25,"GP25 own policy");
	check(u::classify("ak47_digital_grenadier","gl_ak47_digital",3,6,1)==u::kind::gp25,"captured digital GP25 pair retains its own family");
	for(auto pair:std::array<std::pair<const char*,const char*>,7>{{{"m4m203_eotech","m203_m16"},{"scar_h_grenadier","m203_m4"},
		{"masada_grenadier","scar_h_m203"},{"m16_grenadier","gl_masada"},{"m4m203x_eotech","m203_m4_eotech"},
		{"m4m203_eotech","m203_m4x_eotech"},{"ak47_arctic_grenadier","gl_ak47x_arctic"}}})
		check(u::classify(pair.first,pair.second,3,6,1)==u::kind::none,"cross-host and prefix-lookalike secondary families reject even with launcher metadata");
	check(u::classify("m4m203__eotech","m203_m4_eotech",3,6,1)==u::kind::none &&
		u::classify("m4m203_eotech_","m203_m4_eotech",3,6,1)==u::kind::none &&
		u::classify(std::string(10000,'a'),"m203_m4",3,6,1)==u::kind::none,"malformed or oversized names fail bounded module admission");
	check(u::classify("ak47_shotgun","ak47_shotgun_attach",1,4,4)==u::kind::shotgun,"captured shotgun family");
	for(auto host:{"scar_h_shotgun","scar_h_reflex_shotgun","ak47_acog_shotgun","fal_shotgun","fal_reflex_shotgun"})
	{
		const auto child=std::string(host)+"_attach";
		check(u::classify(host,child,1,4,4)==u::kind::shotgun,"matching optic variant and exact linked shotgun child admit the native four-round module");
		check(u::classify(host,child,3,6,4)==u::kind::none && u::classify(host,child,1,4,5)==u::kind::none,"variant names cannot bypass delivery or capacity admission");
	}
	for(auto host:{"scar_h_reflex_notshotgun","scar_h_reflex_shotgunx","scar_h__shotgun","scar_h_shotgun_","SCAR_h_shotgun","m4_shotgun"})
		check(u::classify(host,std::string(host)+"_attach",1,4,4)==u::kind::none,"malformed names and unreviewed host families do not inherit shotgun support");
	check(u::classify("scar_h_reflex_shotgun","scar_h_shotgun_attach",1,4,4)==u::kind::none &&
		u::classify("scar_h_reflex_shotgun","ak47_shotgun_attach",1,4,4)==u::kind::none,"SCAR optic variant cannot borrow another definition's secondary identity");
	{
		const u::identity id{{68,1},69,u::kind::shotgun};const auto loaded=u::import_native(id,4,10);
		// Live SCAR+reflex samples: correct firing geometry was rejected only
		// because its variant pair had not entered the module authority.
		for(const auto sample:std::array<std::array<float,4>,3>{{{.03069f,.32113f,-.22066f,.95045f},{.04588f,.30653f,-.27957f,.95988f},{.05255f,.30079f,-.05910f,.95061f}}})
		{
			check(u::choose_grip(loaded,0,sample[0],sample[1],{sample[2],sample[3]},.13f)==u::contact_role::firing,"captured SCAR secondary trigger grasps are admitted after exact variant binding");
			check(!u::support_intent(sample[0],{sample[2],sample[3]},u::kind::shotgun),"broader shotgun support cannot steal a clear trigger-grip contact");
		}
		for(const auto palm:{u::palm_facing{-.55845f,.77485f},u::palm_facing{-.42064f,.87442f},u::palm_facing{-.15178f,.97534f}})
			check(u::support_intent(.17f,palm,u::kind::shotgun) && u::support_facing(palm,true,u::kind::shotgun),"recorded inward/down-tilted ordinary shotgun grasps acquire and retain support away from the trigger grip");
		check(!u::support_intent(.17f,{-.6f,-.7f},u::kind::shotgun),"outward/downward palm remains outside the support region");
		const auto empty=u::import_native(id,0,10);
		check(u::choose_grip(empty,0,.379f,.0412f,{-.26314f,.91367f},.13f)==u::contact_role::action,"captured pump position remains a valid empty-shotgun action grasp");
		using namespace vr::gameplay::hands;
		const anchor gun{{},{0,0,0,1}};const auto& profile=w::scar::shotgun;const auto& support_anchor=profile.wrists[0];const auto rear=profile.wrists[1].position;
		check(w::carry::support_contact(profile,gun,support_anchor,rear,add(support_anchor.position,{.12f*40,0,0}),40) &&
			!w::carry::support_contact(profile,gun,support_anchor,rear,add(support_anchor.position,{.14f*40,0,0}),40),"SCAR shotgun support admits a 12 cm approach but retains a bounded acquisition volume");
	}
	check(u::classify("fal_shotgun","fal_shotgun_attach",1,4,5)==u::kind::none,"modified native capacity requires review");
	check(u::classify("masada_mt_eotech","masada_mt_eotech",1,0,30)==u::kind::none,"heartbeat is not a second feed");
	check(!u::identity{{53,1},54,static_cast<u::kind>(255)},"unknown module kind rejected");
	namespace storage=w::native_ammunition::storage;
	std::array<std::byte,storage::extent> cells{};
	check(storage::commit(cells,53,31,0,0,30,300)&&storage::commit(cells,54,54,0,0,1,9),"host and secondary cells allocated independently");
	const auto primary=storage::observe(cells,53,31);
	check(storage::commit(cells,54,54,1,9,0,9),"one secondary shot debits only its cell");
	check(storage::observe(cells,53,31).clip.count==primary.clip.count&&storage::observe(cells,53,31).reserve.count==primary.reserve.count,"primary ammunition unchanged");
	check(!storage::commit(cells,54,54,1,9,0,9),"replayed native debit rejected");
	check(storage::commit(cells,54,54,0,9,0,8)&&storage::commit(cells,54,54,0,8,1,8),"draw then insert uses secondary reserve");
	check(w::ammunition::granted_reserve({0,8},{1,9},1,1,1000000)==10,"native refill grants reserve budget without auto-loading an open launcher");
	check(!w::ammunition::granted_reserve({4,8},{3,8},4,4,1000000),"external debit is distinct from a grant");
	for(auto k:{u::kind::m203,u::kind::gp25,u::kind::shotgun})
	{
		const u::identity id{{57,10},58,k};const auto before=u::import_native(id,u::capacity(k),10);
		const auto synced=u::reconcile(before,id,{u::capacity(k)-1,10});
		check(bool(synced) && synced.change==u::observed_change::debit,"witnessed native clip reduction does not permanently fault the module");
		check(synced.next.loaded==u::capacity(k)-1 && !synced.next.chamber && !u::ready(synced.next),"external reduction never refunds ammo or invents a loaded chamber");
		check(u::total(synced.next)==u::total(before)-1 && !synced.next.spent,"budget synchronizes without fabricating a VR shot or case");
		check(bool(u::plan(synced.next,{u::operation::draw,id,synced.next.revision,vr::hand::right,vr::hand::left})),"belt pickup remains available after native debit");
		check(!u::reconcile(before,{{130,9},131,k},{0,10}),"airport M4 identity cannot overwrite another M4's module");
		const auto empty=u::import_native(id,0,10);const auto refill=u::reconcile(empty,id,{1,9});
		check(refill && refill.next.loaded==0 && refill.next.reserve==10 && !refill.next.chamber,"native reload preserves total budget without bypassing physical loading");
		check(!u::reconcile(before,id,{u::capacity(k)+1,10}),"out-of-range native module counts remain rejected");
		const auto take=u::plan(before,{u::operation::draw,id,before.revision,vr::hand::right,vr::hand::left});
		const auto with_escrow=u::reconcile(take.next,id,{u::capacity(k)-1,9});
		check(with_escrow && with_escrow.next.held==1 && with_escrow.next.loader==vr::hand::left,"external debit preserves the already owned belt round");
		const auto cleanup=u::plan(with_escrow.next,{u::operation::cleanup,id,with_escrow.next.revision,vr::hand::right,vr::hand::left});
		check(cleanup && cleanup.after.reserve==10 && cleanup.after.loaded==u::capacity(k)-1,"transfer cleanup returns escrow without refunding the external debit");
	}
	for(int variant=0;variant<5;++variant)
	{
		using namespace vr::gameplay::hands;namespace data=underbarrel_test_data;
		const auto kind=variant==1?u::kind::gp25:variant==2?u::kind::shotgun:u::kind::m203;
		const std::span<const bone_definition> receiver=variant==3?std::span<const bone_definition>(data::scar):variant==4?std::span<const bone_definition>(data::m4):kind==u::kind::m203?std::span<const bone_definition>(data::m16):std::span<const bone_definition>(data::ak);
		const std::span<const bone_definition> attachment=kind==u::kind::m203?std::span<const bone_definition>(data::m203):kind==u::kind::gp25?std::span<const bone_definition>(data::gp25):std::span<const bone_definition>(data::shotgun);
		rig r{};r.gun=0;const int hand_begin=int(receiver.size()+attachment.size());r.count=hand_begin+6;r.parent.fill(-1);std::array<bone_definition,256> b{};
		std::copy(receiver.begin(),receiver.end(),b.begin());std::copy(attachment.begin(),attachment.end(),b.begin()+receiver.size());
		std::copy(data::palms.begin(),data::palms.end(),b.begin()+hand_begin);r.arms[0].wrist=hand_begin;r.arms[1].wrist=hand_begin+3;
		int parent=-1;for(size_t i=0;i<receiver.size();++i)if(receiver[i].name==attachment[0].name)parent=int(i);
		for(int i=0;i<hand_begin;++i){r.parent[i]=i<int(receiver.size())?b[i].parent:i==int(receiver.size())?parent:int(receiver.size())+b[i].parent;r.weapon_bones[i]=true;}
		std::array<model_definition,2> models{{{variant==3?"h2_viewmodel_scar_h_base":variant==4?"h2_viewmodel_m4_base":"captured receiver",0,int(receiver.size())},{kind==u::kind::m203?"attach_h2_m203_vm":kind==u::kind::gp25?"attach_h2_gp25_vm":"attach_h2_shotgun_vm",int(receiver.size()),int(attachment.size())}}};
		const auto parts=u::bind(models,r,{b.data(),size_t(r.count)});check(bool(parts)&&parts.type==kind,"all three captured native hierarchies bind");
		if(variant==3)check(length(sub(parts.firing_pose.position,u::authored::scar_fire_grip.position))<.0001f && parts.firing_fingers.data()==u::authored::scar_fire_fingers.data(),"SCAR uses its own captured grenade firing posture");
		check(parts.firing_forward_m==(variant==4?.05f:0.f),"only M4 receives the requested forward contact extension");
		check(parts.palms_valid,"both palms derive from actual glove anatomy");
		if(kind==u::kind::m203)
		{
			const auto palm=u::facing(u::authored::fire_grip.rotation,parts.palm[0],0);
			check(u::firing_facing(palm)&&!u::support_facing(palm),"native M203 inward palm is firing, never ordinary support");
			const auto left=normalize(b[hand_begin].bind.rotation),right=normalize(b[hand_begin+3].bind.rotation);
			const auto basis=normalize(multiply(conjugate({-left[0],left[1],-left[2],left[3]}),right));
			const auto mirrored=vr::gameplay::hands::pose_mirror::wrist(u::authored::fire_grip,basis);
			const auto right_palm=u::facing(mirrored.rotation,parts.palm[1],1);
			check(u::firing_facing(right_palm)&&!u::support_facing(right_palm),"native mirrored palm retains inward classification");
		}
		check(length(sub(parts.mount.position,b[parent].bind.position))<.001f,"attachment origin aliases its real receiver mount");
		check(parts.muzzle.position[0]>parts.mount.position[0],"module muzzle lies forward of actual attachment mount");
		for (auto rear : {vr::hand::left, vr::hand::right})
		{
			using namespace vr::gameplay::hands::pose_math;
			w::carry::scene live;
			live.owner.rear = rear;
			live.gun = {{20, -10, 7}, normalize({.1f, .2f, .3f, .9f})};
			std::array<anchor, 2> wrists{};
			wrists[int(rear)] = {{2, 3, 4}, {0, 0, 0, 1}};
			u::scene barrel;
			barrel.type = kind;
			barrel.contact_hand = 1 - int(rear);
			barrel.muzzle_local = parts.muzzle;
			const auto visible_contact = u::sample_contact(barrel,
			                                               {},
			                                               live.owner,
			                                               live.gun,
			                                               wrists[1 - int(rear)],
			                                               {},
			                                               {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}},
			                                               39.37007874f,
			                                               0);
			const auto visible = compose(live.gun, parts.muzzle);
			for (const auto grasp : {u::lease::none, u::lease::firing, u::lease::support, u::lease::action})
			{
				auto sampled = visible_contact;
				if (u::uses_pump_frame(kind, grasp))
					u::sample_pump_motion(sampled, live, wrists, 0);
				check(
				    length(sub(sampled.muzzle.position, visible.position)) < .00001f &&
				        dot(rotate(sampled.muzzle.rotation, {1, 0, 0}), rotate(visible.rotation, {1, 0, 0})) >
				            .99999f,
				    "every underbarrel grip state uses the visible barrel for firing, including a pump-to-fire transition");
				check(
				    sampled.firing_distance == visible_contact.firing_distance &&
				        sampled.facing == visible_contact.facing &&
				        sampled.support_facing == visible_contact.support_facing &&
				        sampled.action_distance == visible_contact.action_distance &&
				        sampled.load_distance == visible_contact.load_distance &&
				        sampled.load_alignment == visible_contact.load_alignment,
				    "stroke sampling never moves new-grab, firing or loading contacts away from the visible attachment");
				if (kind == u::kind::shotgun && (grasp == u::lease::action || grasp == u::lease::support))
				{
					const auto pump = u::pump_frame(live, wrists);
					const auto expected =
					    scale(compose(inverse(pump), wrists[1 - int(rear)]).position, 1 / 39.37007874f);
					check(length(sub(sampled.stroke_hand, expected)) < .00001f &&
					          length(sub(sampled.stroke_hand, visible_contact.stroke_hand)) > .01f &&
					          std::abs(sampled.action_retention_distance - length(expected)) < .00001f,
					      "held shotgun pump stroke and retention stay in the separate rear-driven frame");
				}
				else
					check(sampled.stroke_hand == visible_contact.stroke_hand &&
					          sampled.action_retention_distance == sampled.action_distance,
					      "launcher and unheld shotgun contacts retain the visible weapon frame");
			}
		}
		check((parts.bolt>=0)==(kind==u::kind::shotgun),"only the shotgun binds its independent window bolt");
		for(float units:{1.f,39.37007874f})for(float amount:{0.f,.5f,1.f,2.f,-1.f})
		for(const auto gun:{anchor{{},{0,0,0,1}},anchor{{12,-3,7},normalize({.2f,-.3f,.1f,.8f})}})
		{
			std::array<bone,256> posed{};
			for(int i=0;i<r.count;++i)posed[i]=b[i].bind;
			const auto before=posed;const auto span=std::span(posed).first(r.count);
			const auto stroke=kind==u::kind::shotgun?u::authored::shotgun_stroke:u::authored::m203_stroke;
			check(u::pose_action(parts,r,gun,amount*stroke,units,span),"captured attachment action poses without an animation tree");
			if(kind!=u::kind::gp25)
			{
				const auto axis=rotate(parts.mount.rotation,kind==u::kind::shotgun?u::authored::shotgun_axis:u::authored::m203_axis);
				const auto actual=std::clamp(amount,0.f,1.f);
				auto target=parts.motion_rest;target.position=add(target.position,scale(axis,actual*stroke*units));
				check(length(sub(posed[parts.motion].position,vr::gameplay::hands::pose_math::compose(gun,target).position))<.0001f,
					"module slider retains its captured axis and clamp under transformed host poses");
				if(kind==u::kind::shotgun)
				{
					target=parts.bolt_rest;target.position=add(target.position,scale(axis,actual*.078f*units));
					check(length(sub(posed[parts.bolt].position,vr::gameplay::hands::pose_math::compose(gun,target).position))<.0001f,
						"shotgun sibling bolt follows the same partial pump with its own window-clearing stroke");
				}
			}
			for(int i=0;i<r.count;++i)if(i!=parts.motion && i!=parts.bolt)
				check(posed[i].position==before[i].position && posed[i].rotation==before[i].rotation,
					"action overlay preserves host, muzzle, shell, lifter and both hands");
			const auto once=posed;u::pose_action(parts,r,gun,amount*stroke,units,span);
			for(int i=0;i<r.count;++i)check(length(sub(posed[i].position,once[i].position))<.0001f,
				"repeated eye/render application cannot accumulate pump or bolt travel");
		}
		{
			std::array<bone,256> posed{};for(int i=0;i<r.count;++i)posed[i]=b[i].bind;
			const auto before=posed;auto span=std::span(posed).first(r.count);
			check(!u::pose_action(parts,r,{},std::numeric_limits<float>::quiet_NaN(),1,span) &&
				!u::pose_action(parts,r,{},0,0,span) && !u::pose_action(parts,r,{},0,1,span.first(1)),
				"invalid action input and short matrix storage reject before writes");
			if(kind==u::kind::shotgun){auto bad=parts;bad.bolt=r.count;check(!u::pose_action(bad,r,{},.1f,1,span),"invalid bolt index cannot partially move the pump");}
			for(int i=0;i<r.count;++i)check(posed[i].position==before[i].position,"rejected action pose leaves matrices unchanged");
		}
		auto invalid=r;invalid.parent[receiver.size()]=r.gun;check(!u::bind(models,invalid,{b.data(),size_t(r.count)}),"wrong root parent rejected");
		invalid=r;invalid.parent[receiver.size()+1]=r.gun;check(!u::bind(models,invalid,{b.data(),size_t(r.count)}),"part outside attachment hierarchy rejected");
		auto duplicate=std::array{models[0],models[1],models[1]};check(!u::bind(duplicate,r,{b.data(),size_t(r.count)}),"duplicate firing attachments rejected");
	}
	for(auto rear:{vr::hand::left,vr::hand::right})
	{
		w::carry::inventory inventory;const w::weapon_identity id{53,99};const w::carry::owned_instance item{id,{false,false}};
		check(inventory.reconcile_instances({&item,1})&&inventory.equip(id,rear),"module host equipped through ordinary inventory");
		const auto before=inventory.find(id)->owner;const auto off=vr::hand(1-int(rear));
		check(inventory.support(id,off),"firing grasp joins host two-hand support");
		check(inventory.find(id)->owner.rear==rear && inventory.find(id)->owner.rear_revision==before.rear_revision,"secondary grasp preserves primary trigger owner and edge generation");
		const auto released=inventory.release(id,1u<<int(off),w::carry::location::absent,false,[](const auto&){return false;});
		check(released.action==w::carry::outcome::support_released && inventory.find(id)->owner.rear==rear && inventory.find(id)->owner.support==vr::hand::none,"leaving firing grip releases only support, not host weapon");
	}
	{
		using namespace vr::gameplay::hands;using namespace vr::gameplay::hands::pose_math;
		// Independent source mesh centres, not the animated parent node. The
		// first import omitted j_grenade_main's displacement and floated in air.
		const auto m203=compose(u::authored::m203_round_in_wrist,{scale({-.086209953f,-.061625525f,.002109427f},1/2.54f),{0,0,0,1}}).position;
		const auto gp25=compose(u::authored::gp25_round_in_wrist,{scale({-.156783581f,-.186463922f,-.144487932f},1/2.54f),{0,0,0,1}}).position;
		check(length(sub(m203,scale({10.295550591f,2.624030162f,4.676950572f},1/2.54f)))<.0001f,"M203 mesh remains at its captured frame-38 hand contact");
		check(length(sub(gp25,scale({9.482472700f,2.458138723f,4.627520406f},1/2.54f)))<.0001f,"GP25 mesh uses its own captured frame-24 hand contact");
	}
	std::cout<<"underbarrel: "<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
