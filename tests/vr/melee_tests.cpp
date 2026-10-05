#include "component/vr/hand.hpp"
using vr::hand;
#include "component/vr/gameplay/body_equipment.hpp"
#include "component/vr/gameplay/chest_equipment.hpp"
#include "component/vr/gameplay/melee_motion.hpp"
#include "component/vr/gameplay/knife_profile.hpp"
#include "component/vr/gameplay/npc_collision_policy.hpp"
#include "component/vr/gameplay/muzzle_clearance.hpp"
#include "component/vr/gameplay/native_melee_events.hpp"
#include "grenade_tests.hpp"
#include "special_equipment_tests.hpp"
#include "trainer_tests.hpp"
#include "melee_motion_tests.hpp"
#include "enemy_combat_tests.hpp"
#include <iostream>
#include <limits>

int main()
{
	using namespace vr::gameplay;
	using namespace hands;
	using vr::hand;
	using namespace std::chrono_literals;
	int failures{};const auto check=[&](bool value,const char* message){if(!value){++failures;std::cerr<<"FAIL "<<message<<'\n';}};
	grenade_tests::run(check);
	special_equipment_tests::run(check);
	trainer_tests::run(check);
	melee_motion_tests::run(check);
	{
		melee::motion motion;melee::sample sample;sample.count=1;sample.sequence=1;sample.reference=1;sample.identity=3;
		sample.weapon=80;sample.kind=melee::tool::pickaxe;sample.at=melee::clock::now();sample.points[0]={0,0,.45f};
		motion.advance(sample);sample.sequence++;sample.at+=20ms;
		sample.hand.rotation={0,std::sin(.3f),0,std::cos(.3f)};sample.points[0]=rotate(sample.hand.rotation,{0,0,.45f});
		const auto swing=motion.advance(sample);
		check(swing.valid && swing.armed && swing.speed>12.f,"fast pick head rotation uses hand continuity rather than the short knife tip-speed ceiling");
		sample.sequence++;sample.at+=20ms;sample.identity++;
		check(!motion.advance(sample).valid,"return and regrip cannot bridge a pickaxe swing");
		sample.sequence++;sample.at+=20ms;sample.hand.position={2,0,0};sample.points[0]={2,0,.45f};
		check(motion.advance(sample).rejected==melee::rejection::tracking,"tracking teleport cannot become a pickaxe strike");
		check(melee::scaled_damage(135,melee::tool::pickaxe)==135 && melee::scaled_damage(135,melee::tool::pickaxe,true,220)==220,
			"pickaxes keep native blade damage and the existing Ragdoll Impact policy");
	}
	enemy_combat_tests::run(check);
	vr::head_pose_bridge::spatial_frame body;body.head_position={100,200,300};body.units_per_meter=40;
		body.head_yaw_axis={vec{1,0,0},vec{0,1,0},vec{0,0,1}};
	const auto chest=equipment::locate_chest(body);
	check(chest.valid && chest.anchors[unsigned(equipment::slot::knife)].position[1]>chest.anchors[unsigned(equipment::slot::tactical)].position[1] &&
		chest.anchors[unsigned(equipment::slot::tactical)].position[1]>chest.anchors[unsigned(equipment::slot::lethal)].position[1],"left knife, central tactical and right lethal anchors are separate");
	check(std::abs(chest.radius/body.units_per_meter-.10f)<.00001f && length(sub(chest.anchors[0].position,chest.anchors[1].position))>chest.radius,
		"existing chest contact tolerance remains available to abdominal equipment");
	for(const quat yaw:{quat{0,0,0,1},quat{0,0,.70710678f,.70710678f}})
	{
		auto turned=body;turned.head_yaw_axis={rotate(yaw,{1,0,0}),rotate(yaw,{0,1,0}),vec{0,0,1}};
		const auto slots=equipment::locate_chest(turned);
		for(unsigned i=0;i<3;++i)
		{
			const auto& box=slots.grabs[i];const auto half=scale(sub(box.high,box.low),.5f);
			check(half[2]>half[1] && equipment::chest_grab_distance(slots,equipment::slot(i),box.origin)==0,"each pickup is centered in its tall body-relative box");
			for(float sign:{-1.f,1.f})
			{
				const auto point=box.world(scale(half,sign*.9f));
				check(equipment::chest_grab_distance(slots,equipment::slot(i),point)<1,"box corners and their symmetric opposites admit palms");
				for(unsigned other=0;other<3;++other)if(other!=i)check(equipment::chest_grab_distance(slots,equipment::slot(other),point)>1,"tall pickup boxes never overlap neighboring equipment");
			}
		}
		check(std::abs(dot(sub(slots.grabs[0].origin,slots.anchors[0].position),turned.head_yaw_axis[2])-1.6f)<.0001f &&
			slots.grabs[0].high[2]>slots.grabs[1].high[2] && slots.grabs[0].high[0]>slots.grabs[1].high[0],"knife pickup is raised four centimeters with extra height and depth tolerance");
	}
	{
		std::array<std::byte,0x3bc> native{};const std::uint32_t lethal=50,tactical=145,primed=9;
		std::memcpy(native.data()+0x3b0,&primed,4);std::memcpy(native.data()+0x3b4,&lethal,4);std::memcpy(native.data()+0x3b8,&tactical,4);
		const auto before=native;
		check(equipment::selected_chest_items(native)==std::array<std::uint32_t,2>{145,50} && native==before,
			"chest displays native selected secondary/primary equipment without consuming or selecting it");
		check(equipment::selected_chest_items({native.data(),0x3b8})==std::array<std::uint32_t,2>{},"truncated native inventory cannot supply chest items");
		const std::uint32_t invalid=512;std::memcpy(native.data()+0x3b8,&invalid,4);
		check(equipment::selected_chest_items(native)==std::array<std::uint32_t,2>{0,50},"unsupported tactical token cannot alias a different weapon");
		check(!equipment::show_chest_item(0,0) && equipment::show_chest_item(1,0) && equipment::show_chest_item(4,0) && equipment::show_chest_item(0,4),
			"zero hides while one or many native rounds yield the same single-item visibility");
		check(!equipment::show_chest_item(-1,4) && !equipment::show_chest_item(4,-1) && !equipment::show_chest_item(1002,0),"invalid native counts do not fabricate equipment");
		for(auto where:equipment::item_slots)for(auto yaw:{quat{0,0,0,1},quat{0,0,.70710678f,.70710678f}})
		{
			auto rotated=body;rotated.head_yaw_axis={rotate(yaw,{1,0,0}),rotate(yaw,{0,1,0}),vec{0,0,1}};
			const auto slots=equipment::locate_chest(rotated);const vec center{-.3f,.8f,-2.6f};
			const auto model=equipment::stowed_chest_item(slots,where,center);
			check(length(sub(add(model.position,rotate(model.rotation,center)),slots.anchors[unsigned(where)].position))<.0001f &&
				length(sub(rotate(model.rotation,{0,0,1}),vec{0,0,1}))<.0001f,"native world equipment stays upright and centered through body yaw, without changing its scale");
		}
	}
	const vec local{.35f,.15f,-.3f};const auto world=equipment::body_world(body,local);
	check(length(sub(equipment::body_local(body,world),local))<.00001f,"body coordinate roundtrip");
	const auto old_body=body;body.head_position={500,-70,140};body.head_yaw_axis={vec{0,1,0},vec{-1,0,0},vec{0,0,1}};
	check(length(sub(equipment::body_local(body,equipment::body_world(body,local)),equipment::body_local(old_body,world)))<.00001f,"locomotion and snap turn cannot produce a hand swing");
	body.head_yaw_axis[1]=body.head_yaw_axis[0];check(!equipment::locate_chest(body).valid,"corrupt body basis rejected");
	equipment::knife_state knife;
	check(knife.take(hand::left,equipment::knife_grip::forward) && !knife.take(hand::right,equipment::knife_grip::reverse),"two hands cannot duplicate the body knife");
	const auto revision=knife.revision;
	check(!knife.release(2) && knife.release(1) && knife.holder==hand::none && knife.revision>revision,"only real owning-hand release returns the knife");
	check(knife.take(hand::right,equipment::knife_grip::reverse),"returned knife can be drawn reverse by the other hand");
	knife.return_to_chest();check(knife.holder==hand::none,"context teardown returns equipment without a world drop");
	using namespace vr::gameplay::hands::pose_math;
	// Independent common-animation witness: middle/ring knuckle midpoint
	// projected onto the knife's handle axis, expressed in the left wrist.
	// Merely preserving an arbitrary pivot allowed the old displaced grip to pass.
	const vec authored_palm{2.778192701f,-.414296988f,1.365333401f};
	for (const quat mirror:{quat{0,0,0,1},normalize(quat{.31f,-.27f,.52f,.73f})})
		for (bool right:{false,true})
			for (auto mode:{equipment::knife_grip::forward,equipment::knife_grip::reverse})
			{
				const auto object=equipment::knife_profile::attachment(mode,mirror,right);
				const auto contact=compose(object,{equipment::knife_profile::grip_center,{0,0,0,1}}).position;
				const auto expected=right ? vr::gameplay::hands::pose_mirror::local_point(authored_palm,mirror) : authored_palm;
				check(length(sub(contact,expected))<.0001f,"both grip directions stay in the source hand's actual palm, including anatomical mirroring");
				// In the authored hand, thumb/index is on the negative knife-X side.
				const auto source_thumb=rotate(equipment::knife_profile::left_reverse_attachment.rotation,{-1,0,0});
				const auto thumb=right ? vr::gameplay::hands::pose_mirror::local_point(source_thumb,mirror) : source_thumb;
				const auto direction=dot(rotate(object.rotation,{1,0,0}),thumb);
				check(mode==equipment::knife_grip::forward ? direction>.999f : direction<-.999f,"forward points toward the thumb and reverse toward the pinky in either hand");
			}
	for (const quat mirror:{quat{0,0,0,1},normalize(quat{.31f,-.27f,.52f,.73f})})
		for (bool right:{false,true})
			for (auto pose:{equipment::knife_hand_pose::grip,equipment::knife_hand_pose::magazine,equipment::knife_hand_pose::slide})
			{
				const auto forward=equipment::knife_profile::attachment(equipment::knife_grip::forward,mirror,right,pose);
				const auto reverse=equipment::knife_profile::attachment(equipment::knife_grip::reverse,mirror,right,pose);
				check(dot(rotate(forward.rotation,{0,0,1}),rotate(reverse.rotation,{0,0,1}))>.999f,
					"forward grip preserves cutting-edge facing in either hand and every co-grasp");
				check(dot(rotate(forward.rotation,{1,0,0}),rotate(reverse.rotation,{1,0,0}))<-.999f &&
					length(sub(compose(forward,{equipment::knife_profile::grip_center,{0,0,0,1}}).position,
					compose(reverse,{equipment::knife_profile::grip_center,{0,0,0,1}}).position))<.0001f,
					"correcting the edge retains the opposite tip direction and the same palm contact");
			}
	for (bool right:{false,true})
	{
		const auto a=equipment::knife_profile::attachment(equipment::knife_grip::forward,{0,0,0,1},right);
		const auto b=equipment::knife_profile::attachment(equipment::knife_grip::reverse,{0,0,0,1},right);
		check(length(sub(compose(a,{equipment::knife_profile::grip_center,{0,0,0,1}}).position,compose(b,{equipment::knife_profile::grip_center,{0,0,0,1}}).position))<.0001f,"forward/reverse grip keeps the handle inside the hand");
		check(dot(rotate(a.rotation,{1,0,0}),rotate(b.rotation,{1,0,0}))<-.999f,"forward/reverse changes blade direction");
		const anchor wrist{{4,7,12},{0,0,.70710678f,.70710678f}};
		const auto palm=equipment::knife_profile::palm_contact(wrist,{0,0,0,1},{0,0,0,1},right);
		check(length(sub(palm,compose(compose(wrist,a),{equipment::knife_profile::grip_center,{0,0,0,1}}).position))<.0001f,
			"draw query uses the visible gripping palm instead of the offset wrist");
		check(length(sub(palm,compose(compose(wrist,b),{equipment::knife_profile::grip_center,{0,0,0,1}}).position))<.0001f,
			"reverse grip uses the same acquisition contact in either hand");
	}
	const auto stowed=equipment::knife_profile::stowed(chest);
	check(length(sub(compose(stowed,{equipment::knife_profile::grip_center,{0,0,0,1}}).position,chest.anchors[0].position))<.0001f,
		"chest draw region is centered on the displayed knife handle");
	equipment::grab_intent intent;vr::controller_input::frame input;input.focused=true;input.reference_generation=1;input.sequence=1;
	input.sampled_at=melee::clock::time_point{}+1s;input.squeeze[0].active=input.squeeze[0].down=true;input.trigger[0].down=true;
	check(intent.consume(input,1,1,0)==1,"an available palm may request a knife while its index finger is closed");
	input.sampled_at+=100ms;check(intent.consume(input,1,0,0)==1,"recent real squeeze survives until the palm reaches the handle");
	input.sampled_at+=101ms;check(!intent.consume(input,1,0,0),"old held squeeze cannot acquire by drifting over the chest");
	check(!intent.consume(input,0,1,0) && !intent.consume(input,1,0,0),"occupied-hand rejection does not turn into a delayed draw when its lease ends");
	check(!intent.consume(input,1,1,1),"same-sample release wins over draw intent");
	intent.consume(input,1,1,0);++input.reference_generation;
	check(!intent.consume(input,1,0,0),"recenter clears pending acquisition without inventing a new squeeze");
	intent.consume(input,1,1,0);input.focused=false;
	check(!intent.consume(input,1,0,0),"focus loss cancels pending acquisition");
	input.focused=true;check(!intent.consume(input,1,0,0),"focus restoration does not replay a held squeeze");
	intent.consume(input,1,1,0);input.sampled_at-=1s;
	check(!intent.consume(input,1,0,0),"clock rollback cannot extend a previous acquisition window");
	melee::motion motion;melee::sample s;s.count=2;s.sequence=1;s.reference=1;s.identity=1;s.kind=melee::tool::knife;s.at=melee::clock::time_point{}+1s;
	s.points[0]={.4f,.1f,0};s.points[1]={.5f,.1f,0};
	check(!motion.advance(s).valid,"first pose is not a swing");
	auto advance=[&](float distance,int ms){++s.sequence;s.at+=std::chrono::milliseconds(ms);for(unsigned i=0;i<s.count;++i)s.points[i][1]+=distance;return motion.advance(s);};
	check(!advance(.005f,10).armed,"slow hand contact cannot attack");
	check(advance(.05f,20).armed,"tracked useful knife stroke arms the swept contact");
	check(!motion.advance(s).valid,"same sample cannot replay a swing");
	check(!advance(.8f,10).valid,"tracking jump rejected instead of clamped to an attacking speed");
	check(!advance(.05f,200).valid,"stale history rejected");
	++s.reference;check(!advance(.05f,20).valid,"recenter changes clear history");
	++s.identity;check(!advance(.05f,20).valid,"changing tool ownership clears history");
	s.points[0][0]=std::numeric_limits<float>::quiet_NaN();check(!advance(.05f,20).valid,"nonfinite controller data rejected");
	melee::hit_gate gate;const auto t=melee::clock::time_point{}+2s;
	check(gate.contact(0,101,true,t),"first valid native contact is accepted");
	check(!gate.contact(1,202,true,t) && !gate.contact(1,303,true,t+399ms),"shared cooldown blocks other hand and other target");
	check(!gate.contact(0,101,true,t+800ms),"persistent overlap cannot hit again after cooldown");
	check(!gate.contact(0,0,false,t+810ms) && gate.contact(0,101,true,t+820ms),"withdrawal permits a new swing");
	gate.clear_contacts();check(!gate.contact(1,101,true,t+900ms),"tracking suspension does not erase the damage cooldown");
	check(!gate.contact(2,101,true,t+2s),"invalid hand cannot commit");
	check(melee::scaled_damage(200,melee::tool::knife)==200 && melee::scaled_damage(200,melee::tool::fist)==67 && melee::scaled_damage(200,melee::tool::firearm)==67,"knife and blunt damage use the shared native base");
	check(!melee::scaled_damage(-1,melee::tool::knife) && !melee::scaled_damage(std::numeric_limits<int>::max(),melee::tool::fist),"malformed native damage does not overflow");
	for(const auto kind:{melee::tool::fist,melee::tool::firearm,melee::tool::knife,melee::tool::shield})
	for(const int native:{100,200,450})
	{
		const int normal=melee::scaled_damage(native,kind);
		check(melee::scaled_damage(native,kind,true,200)==200,"Ragdoll Impact gives every melee tool the same tactical knife damage");
		check(melee::scaled_damage(native,kind,false,200)==normal,"disabling Ragdoll Impact immediately restores each tool's ordinary damage");
		check(melee::scaled_damage(native,kind,true,350)==350,"re-enabling reads the current knife damage instead of a cached or hardcoded value");
		check(melee::scaled_damage(native,kind,true,0)==normal && melee::scaled_damage(native,kind,true,std::numeric_limits<int>::max())==normal,
			"missing or malformed knife evidence retains ordinary damage");
		check(!melee::scaled_damage(-1,kind,true,200) && !melee::scaled_damage(std::numeric_limits<int>::max(),kind,true,200),
			"cheat cannot turn an invalid native melee definition into a strike");
	}
	{
		std::array<std::byte,760> event;event.fill(std::byte{0xa5});const auto original=event;
		check(melee::native::events::knife_hit(event,42,0,6,0),"native knife hit accepts a player-to-actor contact");
		const auto read=[&]<class T>(std::size_t at) {T value;std::memcpy(&value,event.data()+at,sizeof(value));return value;};
		check(read.operator()<std::uint32_t>(8)==9 && read.operator()<std::uint16_t>(0x7c)==0 &&
			read.operator()<std::uint16_t>(0x8e)==42 && read.operator()<std::uint32_t>(0x80)==6 &&
			read.operator()<std::uint32_t>(0x84)==0 && event[2]==std::byte{0},"stock knife/player flags, attacker, target and weapon use the native event ABI");
		bool preserved=true;
		for (std::size_t i=0;i<event.size();++i)
			if (!(i==2 || (i>=8 && i<12) || (i>=0x7c && i<0x7e) || (i>=0x80 && i<0x88) || (i>=0x8e && i<0x90)))
				preserved=preserved && event[i]==original[i];
		check(preserved,"native event type, trajectory, own entity number and allocator lifecycle are preserved");
		check(melee::native::events::knife_hit(event,42,0,6,0x10000) && read.operator()<std::uint32_t>(8)==0x19,
			"native player stance flag participates in impact feedback");
		const auto populated=event;
		check(!melee::native::events::knife_hit(std::span(event).first(0x8f),42,0,6,0) && event==populated,"short native state is rejected before any write");
		for (unsigned target:{0u,3998u,65535u})
			check(!melee::native::events::knife_hit(event,std::uint16_t(target),0,6,0) && event==populated,"non-actor event targets cannot mutate a native entity");
		check(!melee::native::events::knife_hit(event,42,1,6,0) && !melee::native::events::knife_hit(event,42,0,512,0) &&
			!melee::native::events::knife_hit(event,42,0,0,0) && event==populated,"foreign attacker or invalid weapon cannot populate a knife event");
	}
	{
		std::array<std::byte,0x90> event{};std::uint32_t flags{},weapon{};
		check(melee::native::events::weapon_hit(event,42,0,45,0,false),"shield emits native non-knife melee contact with its own weapon");
		std::memcpy(&flags,event.data()+8,4);std::memcpy(&weapon,event.data()+0x80,4);
		check(flags==8 && weapon==45,"shield contact has player flag but no knife flag and retains shield identity");
		check(melee::scaled_damage(100,melee::tool::shield)==100 && melee::scaled_damage(100,melee::tool::firearm)==33,
			"shield uses full native shield damage while firearm blunt scaling stays unchanged");
	}
	for (const auto kind:{melee::tool::fist,melee::tool::firearm,melee::tool::knife,melee::tool::shield})
	{
		melee::motion m;melee::sample pose;pose.count=1;pose.sequence=1;pose.reference=1;pose.identity=1;pose.kind=kind;pose.at=t;
		m.advance(pose);
		const auto step=[&](float speed) {++pose.sequence;pose.at+=30ms;pose.points[0][0]+=speed*.03f;return m.advance(pose);};
		check(!step(melee::minimum_speed(kind)-.1f).armed,"contact below its tool speed does not arm melee");
		step(melee::minimum_speed(kind)+.1f);
		check(step(melee::minimum_speed(kind)+.1f).armed,"deliberate stroke above its tool speed arms melee");
		if (kind!=melee::tool::knife) check(!step(1.3f).armed,"former blunt swing speed now rejects incidental contact");
	}
	struct bounds {float midPoint[3],halfSize[3];};
	{
		melee::motion m;melee::sample pose;pose.count=1;pose.sequence=1;pose.reference=1;pose.identity=1;pose.kind=melee::tool::knife;pose.at=t;
		m.advance(pose);++pose.sequence;pose.at+=30ms;pose.points[0][0]+=.06f;
		check(m.advance(pose).armed,"chest knife stroke arms before weapon handoff");
		++pose.sequence;pose.at+=30ms;pose.points[0][0]+=.06f;pose.weapon=137;
		check(!m.advance(pose).valid,"carried knife cannot inherit chest knife swing with the same lease counter");
	}
	bounds standing{{0,0,35},{15,15,35}};
	check(npc_collision::narrow(standing) && standing.halfSize[0]==9 && standing.halfSize[1]==9 && standing.halfSize[2]==35 && standing.midPoint[2]==35,"NPC query narrows only player horizontal bounds");
	bounds narrow{{0,0,20},{6,6,20}};
	check(!npc_collision::narrow(narrow) && narrow.halfSize[0]==6,"narrow native bounds never grow");
	bounds ray{};check(!npc_collision::narrow(ray),"point traces cannot acquire a collision capsule");
	bounds invalid{{0,0,35},{15,std::numeric_limits<float>::quiet_NaN(),35}};
	check(!npc_collision::narrow(invalid),"invalid native bounds remain untouched");
	struct trace {float fraction;bool startsolid{},allsolid{};};
	const trace original{.2f},clear{1},npc{.7f},wall{.5f};
	check(npc_collision::merge(original,clear,npc).fraction==.7f,"NPC keeps a smaller positive collision core");
	check(npc_collision::merge(original,wall,npc).fraction==.5f,"original-sized wall sweep stops movement before the reduced NPC contact");
	check(npc_collision::merge(original,clear,clear).fraction==1,"extra NPC clearance can permit a previously blocked step");
	check(npc_collision::merge(original,{0,true,true},npc).allsolid,"environment initial overlap never becomes passable");
	check(npc_collision::merge(original,clear,{0,true,true}).allsolid,"NPC core initial overlap remains blocked");
	check(npc_collision::merge(original,{std::numeric_limits<float>::quiet_NaN()},npc).fraction==original.fraction,"invalid supplemental collision results retain the native answer");
	check((weapons::muzzle_clearance_mask&int(npc_collision::actor_contents))==0 &&
		(weapons::muzzle_clearance_mask&0x2000000)!=0,"muzzle clearance ignores NPC contents but retains the cover layer");
	std::cout<<"melee failures="<<failures<<'\n';return failures ? 1 : 0;
}
