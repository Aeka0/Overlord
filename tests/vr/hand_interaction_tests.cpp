#include "interaction_schedule_tests.hpp"
#include "component/vr/hand.hpp"
using vr::hand;
#include "component/vr/gameplay/hand_interaction/core.hpp"
#include "component/vr/gameplay/weapons/m16/profile.hpp"
#include "component/vr/gameplay/weapons/m4/profile.hpp"
#include "component/vr/gameplay/weapons/m9/profile.hpp"
#include "component/vr/gameplay/weapons/scar/profile.hpp"
#include "component/vr/gameplay/world_interaction_policy.hpp"
#include "component/vr/gameplay/physical_reload_contact_sample.hpp"
#include "component/vr/gameplay/hand_interaction/mechanical_contacts.hpp"
#include "component/vr/gameplay/hand_interaction/constraints.hpp"
#include "component/vr/gameplay/hand_interaction/pose_plan.hpp"
#include "component/vr/gameplay/weapons/m9/reload_profile.hpp"
#include "component/vr/gameplay/weapons/m4/reload_profile.hpp"
#include "component/vr/gameplay/weapons/m16/reload_profile.hpp"
#include "component/vr/gameplay/weapons/scar/reload_profile.hpp"
#include "hand_interaction_lifecycle_tests.hpp"
#include "support_magazine_transfer_tests.hpp"
#include "scripted_use_input_tests.hpp"
#include "signal_flare_tests.hpp"
#include "cliffhanger_tests.hpp"
#include <iostream>
#include <limits>
#include <random>

namespace hi=vr::gameplay::hand_interaction;
using hi::hand;
int main()
{
	int checks=0,failures=0;const auto check=[&](bool ok,const char* text){++checks;if(!ok){++failures;std::cerr<<text<<'\n';}};
	interaction_schedule_tests::run(check);
	support_magazine_transfer_tests::run(check);
	scripted_use_input_tests(check);
	signal_flare_tests(check);
	cliffhanger_tests(check);
	const auto request=[](hand h,hi::domain d,hi::role role,hi::button button,unsigned component=0){
		return hi::candidate{h,{{d,{53,7},component,9},role,button,hi::recipe::single,hi::capability::none},1,10,.01f,1,true,true};};
	for(auto h:{hand::left,hand::right})
	{
		// Map display/script entities start at generation zero. NPC drops often
		// reuse an entity slot and have a nonzero generation. Both are world IDs,
		// whereas a carried firearm still needs a physical instance generation.
		for(auto generation:{0ull,7ull})
		{
			hi::arbiter world;world.begin(1,1);
			auto use=request(h,hi::domain::world,hi::role::world,hi::button::grip);
			use.desired.destination.object={1042,generation};
			int commits=0;world.offer(use);world.resolve([&](const auto&){++commits;return true;});
			check(commits==1 && world.decisions()[0].result==hi::rejection::none,"initial display and recycled drop both reach world-use commit");
			vr::gameplay::interaction::target chosen;chosen.key={1042,generation};
			vr::gameplay::interaction::use_lease lease;
			check(lease.begin(int(h),chosen,1) && lease.owns_hand() && lease.value().key==chosen.key,"world lease retains exact native generation including zero");
			auto replacement=use.desired.destination;++replacement.object.generation;
			check(!world.find(h,replacement),"recycled entity generation cannot match stale session");
			world.begin(2,1);world.offer(use);world.resolve([&](const auto&){++commits;return true;});
			check(commits==1,"held world-use press cannot repeat a pickup");
		}
		for(auto id:{0u,4000u,UINT32_MAX})
		{
			hi::arbiter world;world.begin(1,1);
			auto use=request(h,hi::domain::world,hi::role::world,hi::button::grip);use.desired.destination.object={id,1};
			world.offer(use);world.resolve([](const auto&){return true;});
			check(world.decisions()[0].result==hi::rejection::invalid,"invalid world entity IDs remain rejected");
		}
		for(auto domain:{hi::domain::carry,hi::domain::magazine,hi::domain::underbarrel,hi::domain::knife})
		{
			auto physical=request(h,domain,hi::role::control,hi::button::grip);physical.desired.destination.object.generation=0;
			hi::arbiter a;a.begin(1,1);a.offer(physical);a.resolve([](const auto&){return true;});
			check(a.decisions()[0].result==hi::rejection::invalid,"physical instance generation validation is unchanged");
		}
		// Replay the actual failure: a held notify-only world Grip, followed by
		// the first belt Trigger. The notification has no physical hand session.
		vr::gameplay::interaction::use_lease notify;check(notify.begin(int(h),{},1),"script notification begins");
		check(notify.active() && !notify.owns_hand(),"notify-only use cannot manufacture hand ownership");
		hi::arbiter a;a.begin(1,1);auto grenade=request(h,hi::domain::underbarrel,hi::role::supply,hi::button::trigger);
		int reserve=9,held=0,commits=0;a.offer(grenade);a.resolve([&](const auto&){--reserve;++held;++commits;return true;});
		check(reserve==8 && held==1 && commits==1 && a.find(h,grenade.desired.destination),"first belt press commits one grenade");
		a.begin(2,1);a.offer(grenade);a.resolve([&](const auto&){++commits;return true;});
		check(commits==1,"existing session cannot duplicate its native acquisition");
		const auto id=a.find(h,grenade.desired.destination)->id;
		check(!a.release(id,false) && !a.empty(h),"rejected refund keeps occupied settling session");
		a.begin(3,1);auto next=request(h,hi::domain::magazine,hi::role::supply,hi::button::trigger);next.event=2;a.offer(next);a.resolve([](const auto&){return true;});
		check(a.decisions()[0].result==hi::rejection::incompatible,"pending settlement excludes new item");
		check(a.release(id,true) && a.empty(h),"successful settlement frees hand exactly once");
		for(bool release_knife_first:{false,true})
		{
			hi::arbiter c;c.begin(1,1);auto knife=request(h,hi::domain::knife,hi::role::knife,hi::button::grip);
			knife.desired.abilities=hi::capability::melee;c.offer(knife);c.resolve([](const auto&){return true;});
			auto magazine=request(h,hi::domain::magazine,hi::role::supply,hi::button::trigger);
			magazine.desired.pose=hi::recipe::knife_magazine;c.begin(2,1);c.offer(magazine);c.resolve([](const auto&){return true;});
			const auto* k=c.find(h,knife.desired.destination);const auto* m=c.find(h,magazine.desired.destination);
			check(k && m,"approved knife and magazine coexist with separate inputs");
			const auto plan=hi::compose_pose(h,c.sessions());
			check(plan.driver.provider==hi::domain::magazine && plan.knife_attachment && plan.melee && plan.grasp==hi::recipe::knife_magazine,"knife and magazine co-grasp retains knife melee while the magazine drives the wrist");
			{
				auto sessions=c.sessions();
				std::array<hi::session,8> copy{};std::copy(sessions.begin(),sessions.end(),copy.begin());
				for(auto& s:copy)if(s.id==m->id)s.settling=true;
				check(!hi::compose_pose(h,copy).melee,"pending magazine settlement still blocks knife melee");
				for(auto& s:copy)if(s.id==m->id){s.settling=false;s.held.pose=hi::recipe::knife_part;s.held.purpose=hi::role::part;}
				check(!hi::compose_pose(h,copy).melee,"slide co-grasp still blocks knife melee");
			}
			const auto kid=k?k->id:0,mid=m?m->id:0;
			check(c.release(release_knife_first?kid:mid,true),"release only the chosen physical input owner");
			const auto* remaining=c.find(h,release_knife_first?magazine.desired.destination:knife.desired.destination);
			check(remaining && (!release_knife_first || remaining->held.pose==hi::recipe::knife_magazine),"remaining object retains original recipe");
			check(hi::compose_pose(h,c.sessions()).melee==!release_knife_first,"magazine alone cannot retain the released knife's melee permission");
		}
		check(!hi::compatible(request(h,hi::domain::knife,hi::role::knife,hi::button::grip).desired,grenade.desired),"knife does not grant unauthored grenade co-grasp");
		auto fore=request(h,hi::domain::underbarrel,hi::role::foregrip,hi::button::grip);
		fore.desired.abilities=hi::capability::aim|hi::capability::action;
		check(hi::has(fore.desired.abilities,hi::capability::aim) && hi::has(fore.desired.abilities,hi::capability::action),"one M203 grasp can support aim and slide without replacing session");
	}
	std::mt19937 random(73);
	{
		namespace w=vr::gameplay::weapons;namespace pr=w::physical_reload;
		using namespace vr::gameplay::hands;using namespace vr::gameplay::hands::pose_math;
		for(const auto* d:{&w::m4::physical,&w::m16::physical,&w::scar::physical})
		for(auto rear:{hand::left,hand::right})for(float angle:{0.f,1.57f,-2.1f})
		{
			hi::frame f;f.input.sequence=101;f.input.reference_generation=2;f.valid_hands=3;f.body.units_per_meter=39.37f;
			f.body.head_yaw_axis={{{1,0,0},{0,1,0},{0,0,1}}};
			w::hold owner;owner.weapon=49;owner.instance_generation=7;owner.rear=rear;
			const anchor gun{{700,-400,50},{0,0,std::sin(angle*.5f),std::cos(angle*.5f)}};
			f.objects[0].owner=owner;f.objects[0].assembly=19;f.objects[0].gun=gun;
			const int off=1-int(rear);const vec offset{0,.12f,0};
			f.wrists[off]=compose(gun,{add(d->interaction.receiver_release->centre,scale(offset,f.body.units_per_meter)),{0,0,0,1}});
			pr::scene_frame sample;sample.owner=owner;sample.assembly=19;sample.definition=d;sample.binding.valid=true;
			sample.contact.catch_input.valid=true;sample.contact_in_wrist.fill({});
			pr::presentation view;view.active=true;view.ammo.instance_generation=7;
			check(pr::sample_contact(sample,view,f) && sample.contact.catch_input.valid &&
				length(sub(sample.contact.catch_input.slap_points[0],offset))<.00001f,
				"receiver paddle sampling follows gun rotation/translation and keeps physical left side for either holding hand");
		}
	}
	for(int repeat=0;repeat<128;++repeat)
	{
		hi::arbiter a;a.begin(1,1);auto main=request(hand::left,hi::domain::magazine,hi::role::supply,hi::button::trigger);
		auto secondary=request(hand::left,hi::domain::underbarrel,hi::role::supply,hi::button::trigger);main.distance=0;
		std::array choices{main,secondary};std::shuffle(choices.begin(),choices.end(),random);for(auto c:choices)a.offer(c);
		a.select_supply(hand::left,secondary.desired.destination);
		int commits=0;a.resolve([&](const auto& c){++commits;return c.desired.destination.provider!=hi::domain::underbarrel;});
		check(commits==1 && a.empty(hand::left) && std::count_if(a.decisions().begin(),a.decisions().end(),[](const auto& d){return d.result==hi::rejection::commit_failed;})==1,"supply selection beats distance and enumeration order; refused secondary never falls through to main magazine");
		a.begin(2,1);a.offer(main);a.resolve([&](const auto&){++commits;return true;});check(commits==1,"same refused event cannot acquire a different item on a later frame");
	}
	{
		hi::arbiter a;a.begin(1,1);auto c=request(hand::left,hi::domain::magazine,hi::role::supply,hi::button::trigger);
		for(int i=0;i<65;++i){c.desired.destination.component=unsigned(i);a.offer(c);}
		int commits=0;a.resolve([&](const auto&){++commits;return true;});check(a.overflow() && !commits,"capacity overflow never silently selects a truncated candidate");
		a.begin(2,1);c.distance=std::numeric_limits<float>::quiet_NaN();a.offer(c);a.resolve([&](const auto&){++commits;return true;});check(!commits,"non-finite candidate rejected before native execution");
	}
	{
		vr::controller_input::frame f;f.sequence=1;f.reference_generation=1;f.focused=true;f.sampled_at=hi::clock::now();
		for(int h=0;h<2;++h){f.squeeze[h]={true,false,0,1,0};f.trigger[h]={true,false,0,1,0};f.grip[h].valid=f.aim[h].valid=true;}
		hi::input_history history;history.update(f,true,f.sampled_at);++f.sequence;f.squeeze[0].down=true;++f.squeeze[0].presses;
		history.update(f,true,f.sampled_at);check(history.get(hand::left,hi::button::grip).press,"fresh Grip reported once");
		check(!history.update(f,true,f.sampled_at),"duplicate frame does not advance history");
		++f.sequence;f.trigger[0].down=true;++f.trigger[0].presses;history.update(f,true,f.sampled_at);
		check(history.get(hand::left,hi::button::trigger).press && history.get(hand::left,hi::button::grip).down,"held Grip modifies first fresh Trigger");
		++f.sequence;f.grip[0].valid=false;f.squeeze[0].down=false;++f.squeeze[0].releases;history.update(f,true,f.sampled_at);
		check(history.get(hand::left,hi::button::grip).release,"proven release survives missing pose");
		++f.sequence;++f.reference_generation;history.update(f,true,f.sampled_at);
		check(!history.get(hand::left,hi::button::trigger).press,"held trigger cannot replay after recenter");
	}
	{
		using namespace vr::gameplay::hands;
		const vec rest{.32f,.015f,-.045f},axis{1,0,0};const float stroke=.09170237f,start=length(rest);
		for(int degrees=-170;degrees<=170;degrees+=10)for(int step=0;step<=10;++step)
		{
			const float angle=float(degrees)*3.14159265359f/180,travel=stroke*float(step)/10;
			const quat turn{0,0,std::sin(angle*.5f),std::cos(angle*.5f)};
			const auto actual=rotate(turn,add(rest,scale(axis,travel)));
			const auto solved=hi::shared_slider(rest,axis,start,start,length(actual),0,stroke,.12f,.25f);
			check(solved.valid && std::abs(solved.travel-travel)<.00001f,"M203 steering and sliding are independent under full rigid rotation");
		}
		const auto closed=hi::shared_slider(rest,axis,length(add(rest,scale(axis,stroke))),start,start,stroke,stroke,.12f,.25f);
		check(closed.valid && closed.travel<.00001f,"open foregrip closes through the same length constraint");
		check(!hi::shared_slider(rest,axis,start,start,start+.6f,0,stroke,.12f,.25f).valid,"tracking jump cannot create a mechanical stroke");
		check(!hi::shared_slider({0,1,0},axis,start,start,start,0,stroke,.12f,.25f).valid,"ambiguous perpendicular layout is explicitly rejected");
	}
	{
		namespace w=vr::gameplay::weapons;namespace pr=w::physical_reload;
		hi::frame f;f.input.sequence=71;f.input.reference_generation=3;f.valid_hands=3;f.body.units_per_meter=100;
		f.body.head_position={0,0,170};f.body.head_yaw_axis={{{1,0,0},{0,1,0},{0,0,1}}};
		w::hold owner;owner.weapon=49;owner.instance_generation=7;owner.rear=hand::right;
		f.objects[0].owner=owner;f.objects[0].assembly=19;f.objects[0].gun={{10,20,30},{0,0,0,1}};
		f.wrists[0]={{21,0,108},{0,0,0,1}};f.wrists[1]={{10,20,30},{0,0,0,1}};
		pr::scene_frame source;source.owner=owner;source.assembly=19;source.definition=&w::m9::physical;source.binding.valid=true;
		pr::presentation view;view.active=true;view.ammo.weapon=49;view.ammo.instance_generation=11;
		auto sample=source;check(pr::sample_contact(sample,view,f) && sample.input.sequence==71 && sample.contact.input_sequence==71 && sample.contact.instance_generation==11,"current input rebuilds primary contact from immutable binding despite an unrendered old scene");
		const auto original=sample.contact;const vr::gameplay::hands::vec shift{500,-300,200};
		f.body.head_position=vr::gameplay::hands::add(f.body.head_position,shift);f.objects[0].gun.position=vr::gameplay::hands::add(f.objects[0].gun.position,shift);
		for(auto& wrist:f.wrists)wrist.position=vr::gameplay::hands::add(wrist.position,shift);
		check(pr::sample_contact(sample,view,f) && std::abs(sample.contact.waist_distance-original.waist_distance)<.00001f && std::abs(sample.contact.slide_distance-original.slide_distance)<.00001f,"world translation cannot change supply or part contact");
		++f.objects[0].assembly;check(!pr::sample_contact(sample,view,f),"changed assembly rejects obsolete local bindings");
		--f.objects[0].assembly;
		source.binding.paired_valid=true;source.binding.paired_wrist={0,0,.70710678f,.70710678f};source.contact.knife_held=true;
		auto paired=source;check(pr::sample_contact(paired,view,f),"paired grasp samples its own immutable wrist basis");
		const auto held=paired.held_world;source.contact.knife_held=false;view.ammo.magazine_hand=hand::left;view.knife_magazine_grasp=true;
		paired=source;check(pr::sample_contact(paired,view,f) && vr::gameplay::hands::length(vr::gameplay::hands::sub(paired.held_world.position,held.position))<.00001f,"returning knife preserves magazine pose without waiting for a render refresh");
	}
	for(auto actor:{hand::left,hand::right})
	{
		namespace w=vr::gameplay::weapons;namespace pr=w::physical_reload;namespace m=w::mechanics;
		pr::controller controller;m::state state{49,1,1,true,true,14,60};w::hold owner;owner.weapon=49;owner.rear=hand(1-int(actor));owner.rear_revision=1;
		vr::controller_input::frame input;input.sequence=input.reference_generation=1;input.focused=true;input.sampled_at=hi::clock::time_point{std::chrono::seconds(1)};
		for(int h=0;h<2;++h){input.grip[h].valid=input.aim[h].valid=true;input.trigger[h]=input.secondary[h]={true,false,0,1};}
		pr::geometry geometry{true,49,1,1,1,.01f,1,1,{},{0,0,-.2f}};
		auto native=m::native_ammo(state);int commits=0;
		const auto commit=[&](const m::transaction& tx){if(native!=tx.before)return false;native=tx.after;++commits;return true;};
		controller.update(w::m9::physical.interaction,w::m9::physical.ammunition,state,input,owner,geometry,true,input.sampled_at,commit,{false,false,true,false});
		++input.sequence;++geometry.input_sequence;input.sampled_at+=std::chrono::milliseconds(10);input.trigger[int(actor)]={true,true,1,1};
		controller.update(w::m9::physical.interaction,w::m9::physical.ammunition,state,input,owner,geometry,true,input.sampled_at,commit,{true,true,true,true});
		check(commits==1 && state.magazine_hand==actor && state.held_rounds>0,"real mechanical controller honors centrally armed first Trigger after previous hand exclusion");
		const auto before=commits;controller.update(w::m9::physical.interaction,w::m9::physical.ammunition,state,input,owner,geometry,true,input.sampled_at,commit,{true,true,true,true});
		check(commits==before,"duplicate coordinator input cannot repeat native magazine draw");
		++input.sequence;++geometry.input_sequence;input.sampled_at+=std::chrono::milliseconds(10);++input.trigger[int(actor)].releases;++input.trigger[int(actor)].presses;
		controller.update(w::m9::physical.interaction,w::m9::physical.ammunition,state,input,owner,geometry,true,input.sampled_at,commit,{true,false,false,true,false,true});
		check(state.magazine_hand==hand::none && state.held_rounds==0,"coalesced release and repress must end the old pinch even when the raw button is down again");
	}
	{
		hi::arbiter a;a.begin(1,1);int native_count=1;
		auto tactical=request(hand::left,hi::domain::tactical,hi::role::tactical,hi::button::grip);
		a.offer(tactical);a.resolve([&](const auto&){--native_count;return true;});
		const auto* held=a.find(hand::left,tactical.desired.destination);check(held && native_count==0,"future tactical provider uses the same exclusive grasp contract");
		if(held)a.release(held->id,true);
		check(native_count==0 && !hi::compose_pose(hand::left,a.sessions()).driver,"generic hand release cannot refund an activated tactical item; empty gestures have no grasp owner");
	}
	{
		hi::arbiter a;a.begin(1,1);
		auto body=request(hand::right,hi::domain::grenade,hi::role::tactical,hi::button::grip);
		auto pin=request(hand::left,hi::domain::grenade,hi::role::part,hi::button::trigger,1);
		a.offer(body);a.offer(pin);a.resolve([](const auto&){return true;});
		check(a.find(hand::right,body.desired.destination) && a.find(hand::left,pin.desired.destination),"grenade body and pin own independent hands on one object");
		check(!hi::compose_pose(hand::right,a.sessions()).melee && !hi::compose_pose(hand::left,a.sessions()).melee,"grenade holder and pin puller cannot also fist-strike");
		check(!a.permits(hand::right,request(hand::right,hi::domain::carry,hi::role::control,hi::button::grip).desired),"grenade grip cannot coexist with a firearm grip");
		const auto* pulling=a.find(hand::left,pin.desired.destination);if(pulling)a.release(pulling->id,true);
		check(a.empty(hand::left) && a.find(hand::right,body.desired.destination),"pin extraction releases only the pulling hand");
		a.begin(2,1);pin.event=2;
		auto transfer=request(hand::left,hi::domain::grenade,hi::role::control,hi::button::grip,2);transfer.event=2;transfer.priority=12;
		a.offer(pin);a.offer(transfer);a.resolve([](const auto&){return true;});
		check(a.find(hand::left,pin.desired.destination) && !a.find(hand::left,transfer.desired.destination),"simultaneous trigger pin grab blocks side-button handoff");
	}
	hand_interaction_lifecycle_tests::run(check);
	std::cout<<"hand interaction: "<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
