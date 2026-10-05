#pragma once
#include "component/vr/gameplay/world_interaction_policy.hpp"
#include "component/vr/gameplay/interaction_volume.hpp"
#include "component/vr/gameplay/scripted_use_proxy.hpp"
#include "component/vr/gameplay/body_supply_volume.hpp"
#include "component/vr/gameplay/pickup_visibility.hpp"
#include <limits>

namespace world_interaction_tests
{
	template<class Check> void run(Check&& check)
	{
		using namespace vr::gameplay::interaction;
		{
			pickup_surface_samples samples;
			const std::array mesh{vec{0,0,-2},vec{0,0,4},vec{5,0,2},vec{-4,0,3}};
			for(auto point:mesh)check(samples.add(point),"bounded rigid mesh vertices provide pickup visibility witnesses");
			for(auto point:samples.values())check(std::find(mesh.begin(),mesh.end(),point)!=mesh.end(),"visibility witnesses are actual mesh points rather than empty box corners");
			check(!samples.add({std::numeric_limits<float>::quiet_NaN(),0,0}) && !samples.add({10000,0,0}),"invalid and extreme mesh coordinates cannot extend pickup reach");
			const ray aim{{-12,0,3},{1,0,0},{-15,0,35},40,2.2f,12};
			unsigned budget=64,calls{};
			const auto floor=[&](const vec&,const vec& point){++calls;return point[2]>0;};
			const auto found=visible_pickup_surface(aim,{7,2},5,samples.values(),budget,floor);
			check(found && found.position[2]>0 && calls<=28,"a partly buried gun remains selectable through exposed mesh while the bottom point is obstructed");
			budget=64;
			check(!visible_pickup_surface(aim,{7,2},5,samples.values(),budget,[](const vec&,const vec&){return false;}),"fully buried weapons and solid walls still block every witness");
			budget=64;
			check(!visible_pickup_surface(aim,{7,2},5,samples.values(),budget,[&](const vec& eye,const vec& point){return eye==aim.head ? point[2]>3 : point[2]<=3;}),"head and hand seeing different points around cover cannot combine into a valid pickup");
			budget=1;calls=0;
			check(!visible_pickup_surface(aim,{7,2},5,samples.values(),budget,floor) && !calls,"exhausted query budget cannot waive a visibility test");
			auto away=aim;away.forward={-1,0,0};budget=64;
			check(!visible_pickup_surface(away,{7,2},5,samples.values(),budget,floor),"exposed geometry outside hand intent is not selected");
		}
		{
			// Read-only museum witness: cargo_belt and its tiny tag_origin use
			// model are separate entities. Coordinates are rebased for precision.
			scripted_use::snapshot crate;
			crate.trigger={970,0};crate.visual={590,0};crate.native_center={0,0,28};
			crate.volume.center={-.8010263f,.2313461f,20.8735905f};
			crate.volume.half={25.0574837f,32.7937355f,20.7623577f};
			crate.volume.valid=crate.enabled=true;crate.prompt=prompt_kind::resupply;
			const ray edge{{-55,30,15},{1,0,0},{-65,30,55},39.3701f,2.2f,12};
			const auto hit=scripted_use::score(crate,edge);
			check(!score(edge,crate.trigger,crate.native_center) && hit && !hit.weapon,
				"ammo cache body is selectable when the invisible native marker misses the hand ray");
			const auto accepted=scripted_use::finish(crate,hit,true,true);
			check(accepted.key==crate.trigger && accepted.prompt==prompt_kind::resupply &&
				std::abs(accepted.position[2]-41.6359482f)<.001f,
				"resupply uses the original trigger but anchors its specific prompt above the crate top");
			auto contact=edge;contact.origin={-26,30,15};contact.head={-50,30,50};contact.forward={-1,0,0};
			check(scripted_use::score(crate,contact).contact,"touching the crate edge does not require aiming back at its small marker");
			check(!scripted_use::finish(crate,hit,false,true) && !scripted_use::finish(crate,hit,true,false),
				"ammo visual binding cannot bypass native admission or world obstruction");
			auto disabled=crate;disabled.enabled=false;
			check(!scripted_use::score(disabled,edge),"native ammo prohibition and refill lock suppress selection and prompts");
			auto remote=edge;remote.head[0]-=150;remote.origin[0]-=150;
			check(!scripted_use::score(crate,remote),"large ammo cache does not extend head or hand reach");
			check(scripted_use::ammo_gaze_override(true,true,crate.trigger,crate.trigger) &&
				!scripted_use::ammo_gaze_override(false,true,crate.trigger,crate.trigger) &&
				!scripted_use::ammo_gaze_override(true,false,crate.trigger,crate.trigger) &&
				!scripted_use::ammo_gaze_override(true,true,{970,1},crate.trigger) &&
				!scripted_use::ammo_gaze_override(true,true,crate.visual,crate.trigger),
				"gaze replacement requires VR, exact helper callsite and the current linked use entity generation");
			constexpr std::array<std::byte,11> call{std::byte{0x1c},std::byte{0xfb},std::byte{0},std::byte{0x44},
				std::byte{0x6f},std::byte{0x75},std::byte{6},std::byte{0x6e},std::byte{0x4b},std::byte{0x0d},std::byte{0}};
			check(scripted_use::ammo_gaze_call(call)==3,"captured native dot/threshold sequence identifies its exact builtin return site");
			auto wrong=call;wrong[7]=std::byte{0};check(!scripted_use::ammo_gaze_call(wrong),"unknown gaze bytecode preserves native behavior");
			std::array<std::byte,22> ambiguous{};std::copy(call.begin(),call.end(),ambiguous.begin());std::copy(call.begin(),call.end(),ambiguous.begin()+11);
			check(!scripted_use::ammo_gaze_call(ambiguous),"ambiguous gaze bytecode is never intercepted");
			std::array<scripted_use::snapshot,6> many;many.fill(crate);
			for(unsigned i=0;i<many.size();++i){many[i].trigger.entity+=i;many[i].visual.entity+=i;}
			const auto ranked=scripted_use::candidates(many,edge,many.back().trigger);
			check(ranked.size()==4 && ranked[0].proposal.key==many.back().trigger && ranked[1].proposal.key==crate.trigger,
				"bounded native queries prioritize a held crate then deterministic hand intent across multiple caches");
			check(!prefer(hit,ranked[0].proposal,many.back().trigger),"a hovered crate cannot replace another held world target");
		}
		{
			scripted_use::snapshot dsm;
			dsm.trigger={736,2};dsm.visual={824,3};dsm.native_center={110,202,206};dsm.enabled=true;
			dsm.volume.origin={111.2507f,212.0593f,201.9033f};dsm.volume.center={0,-.03071284f,6.0338278f};
			dsm.volume.half={1.492975f,5.99845f,6.033845f};dsm.volume.valid=true;
			const float angle=9.979455f*.01745329252f;
			dsm.volume.axis={vec{std::cos(angle),std::sin(angle),0},vec{-std::sin(angle),std::cos(angle),0},vec{0,0,1}};
			for(float rotation:{0.f,1.570796327f})
			{
				auto moved=dsm;
				moved.volume.axis={vec{std::cos(rotation),std::sin(rotation),0},vec{-std::sin(rotation),std::cos(rotation),0},vec{0,0,1}};
				const auto center=moved.volume.world(moved.volume.center);
				ray aimed;aimed.units=39.3701f;aimed.head=aimed.origin={center[0]-28,center[1],center[2]};aimed.forward={1,0,0};
				const auto geometric=scripted_use::score(moved,aimed);
				const auto accepted=scripted_use::finish(moved,geometric,true,true);
				check(accepted && accepted.key==moved.trigger && !accepted.weapon && accepted.position==center,"DSM pointing and prompt anchor follow visual model while activation retains original trigger identity");
				check(!scripted_use::finish(moved,geometric,false,true) && !scripted_use::finish(moved,geometric,true,false),"visual proxy cannot bypass native eligibility or obstruction checks");
				moved.native_center[2]-=10000;
				check(!scripted_use::score(moved,aimed),"DSM trigger moved below map is never resurrected by the visible model");
				moved=dsm;moved.enabled=false;check(!scripted_use::score(moved,aimed),"download-in-progress phase cannot activate DSM proxy");
				moved=dsm;aimed.origin[0]-=120;aimed.head[0]-=120;
				check(!scripted_use::score(moved,aimed),"visual selection preserves configured head and hand reach");
			}
			check(scripted_use::for_map("estate") && !scripted_use::for_map("oilrig"),"DSM visual binding remains scoped to its native level");
		}
		// Captured estate DSM trigger and separate highlighted model origins.
		// Aim at the visible model instead of the offset native trigger centre.
		const vec dsm_center{110,202,206},dsm_half{3,7,5};
		const ray dsm_ray{{90,212.0593f,201.9033f},{1,0,0},{80,212,225},39.3701f,2.2f,12.f};
		check(!score(dsm_ray,{736,0},dsm_center) && bool(score_use_trigger(dsm_ray,{736,0},dsm_center,dsm_half)),
			"aiming toward the DSM highlight reaches its authored use volume without aiming at its offset centre");
		auto touch_dsm=dsm_ray;touch_dsm.origin={111.2507f,212.0593f,201.9033f};touch_dsm.forward={-1,0,0};
		check(score_use_trigger(touch_dsm,{736,0},dsm_center,dsm_half).contact,"contact near DSM highlight does not require centre-point aiming");
		check(use_trigger_class("trigger_use_flag_set") && use_trigger_class("trigger_use") &&
			!use_trigger_class("trigger_damage") && !use_trigger_class("trigger_multiple") && !use_trigger_class("trigger_useful"),
			"only native use trigger classes receive volume selection");
		check(!score_use_trigger(dsm_ray,{736,0},dsm_center,{-1,7,5}),"malformed trigger bounds cannot grant contact");
		auto distant=dsm_ray;distant.origin[0]-=150;distant.head[0]-=150;
		check(!score_use_trigger(distant,{736,0},dsm_center,dsm_half),"authored trigger volume retains head and hand reach limits");
		check(!score_use_trigger(dsm_ray,{736,0},dsm_center,{1000,1000,1000}),"oversized scene trigger cannot turn all nearby space into direct contact");
		const ray door_ray{{0,0,0},{1,0,0},{0,0,10},40,2.2f,12.f};
		check(!score(door_ray,{4,1},{35,16,0}) && bool(score_breach(door_ray,{4,1},{35,16,0},{1,1,1})),"breach handle is an interaction volume rather than a tiny point");
		auto close_door=door_ray;close_door.origin={36,0,0};close_door.head={20,0,10};
		check(score_breach(close_door,{4,1},{35,0,0},{1,1,1}).contact,"hand at or just past door marker remains a contact target");
		check(!score_breach(door_ray,{4,1},{200,0,0},{1,1,1}),"door enlargement cannot exceed hand/head reach");
		auto invalid_door=door_ray;invalid_door.units=-1;
		check(!score_breach(invalid_door,{4,1},{35,0,0},{1,1,1}),"invalid door units are rejected before range clamping");
		// Live USP geometry lies behind its entity origin. Rotation/translation
		// must move the entire native model box, never a fixed pickup sphere.
		oriented_volume pistol{{50,25,0},{-9,0,-3},{4,1,3},{{{0,1,0},{-1,0,0},{0,0,1}}},true};
		ray pistol_ray{{50,16,0},{0,0,-1},{50,16,20},39.3701f,2.2f,12.f};
		const auto touched=score_volume(pistol_ray,{10,1},pistol,23);
		check(touched && touched.contact,"rotated USP can be touched at its visible model behind the entity origin");
		pistol_ray.origin={50,25,0};pistol_ray.head={50,25,20};
		check(!score_volume(pistol_ray,{10,1},pistol,23),"empty space at a short weapon entity origin is not a pickup volume");
		auto rifle=pistol;rifle.half[0]=18;
		check(bool(score_volume(pistol_ray,{11,1},rifle,1)),"larger native gun bounds admit contact beyond the shorter pistol model");
		const ray edge_ray{{0,0,0},{1,0,0},{0,0,0},100,2.2f,12.f};
		oriented_volume edge_gun{{250,0,0},{-60,0,0},{20,2,2},{{{1,0,0},{0,1,0},{0,0,1}}},true};
		check(bool(score_volume(edge_ray,{11,1},edge_gun,1)),"gun inside reach remains eligible when its entity origin is beyond reach");
		edge_gun.origin={400,0,0};
		check(!score_volume(edge_ray,{11,1},edge_gun,1),"conservative native broadphase padding never grants extra model pickup reach");
		auto malformed=pistol;malformed.axis[1]=malformed.axis[0];
		check(!score_volume(pistol_ray,{10,1},malformed,23),"non-orthogonal model transform is rejected");
		malformed=pistol;malformed.axis[0][0]=std::numeric_limits<float>::quiet_NaN();
		check(!score_volume(pistol_ray,{10,1},malformed,23),"NaN model rotation cannot produce a selected candidate");
		// Cliffhanger's weapon_c4 is a script model, not a pickup token. Its
		// captured model bounds are much larger than its (1,1,1) entity box.
		oriented_volume c4{{35,10,0},{.3924177f,.2364075f,.3333100f},{6.0803127f,6.7407608f,2.6702561f},
			{{{1,0,0},{0,1,0},{0,0,1}}},true};
		ray c4_ray{{0,0,0},{1,0,0},{0,0,20},39.3701f,2.2f,12.f};
		const auto installation=score_volume(c4_ray,{2289,3},c4);
		check(!score(c4_ray,{2289,3},c4.origin) && installation && !installation.weapon && !installation.contact,
			"aim at the C4 body selects a native script target when entity-centre scoring rejects it");
		for (const float side:{-1.f,1.f})
		{
			auto contact_ray=c4_ray;
			contact_ray.head=c4.world({0,0,20});
			contact_ray.origin=c4.world({side*(c4.half[0]+3),0,0});contact_ray.forward={side,0,0};
			const auto contact=score_volume(contact_ray,{2289,3},c4);
			check(contact && contact.contact && !contact.weapon,
				"either side can touch the C4 while aiming past it without turning installation into a pickup");
		}
		auto remote_c4=c4;remote_c4.origin={150,0,0};
		check(!score_volume(c4_ray,{2289,3},remote_c4),"script model contact does not extend world interaction reach");
		auto malformed_c4=c4;malformed_c4.half[1]=-1;
		check(!score_volume(c4_ray,{2289,3},malformed_c4),"invalid native script bounds cannot become an installation target");
		malformed_c4=c4;malformed_c4.half[0]=10000;
		check(!score_volume(c4_ray,{2289,3},malformed_c4),"large script models cannot bypass the bounded model envelope");
		const auto supply=vr::gameplay::body_supply_volumes({0,0,100},{{{1,0,0},{0,1,0},{0,0,1}}},100,{},.22f);
		check(supply[0].contains({-25,45,12}) && supply[1].contains({-25,-45,12}),
			"both supply regions admit a low hand behind the outer hip");
		check(supply[0].contains({0,21,58}) && !supply[0].contains({25,21,38}) && !supply[0].contains({-45,65,0}),
			"supply keeps upward reach and forward limit without unbounded diagonal expansion");
		ray aim{{0,0,0},{1,0,0},{0,0,20},40,2.2f,12};
		const auto direct=score(aim,{10,1},{80,0,0},3);
		const auto near=score(aim,{11,1},{30,4,0},4);
		check(direct && near && better(direct,near),"farther centered weapon wins over nearby off-axis weapon");
		check(!score(aim,{12,1},{90,0,0}),"reach bounded from hand");
		check(!score(aim,{12,1},{-10,0,0}),"behind-hand objects rejected");
		check(!score(aim,{12,1},{20,6,0}),"objects outside interaction cone rejected");
		check(better(score(aim,{9,1},{80,0,0}),direct),"overlap has deterministic entity tie break");
		check(better(score(aim,{15,1},{60,0,0}),direct),"equal angular aim selects closest");
		auto extended=aim;extended.head={-30,0,0};
		check(!score(extended,{10,1},{80,0,0}),"extending hand cannot bypass head-relative reach cap");
		auto floor=aim;floor.origin={0,0,45};floor.head={0,0,65};
		floor.forward=vr::gameplay::hands::scale(vec{35,0,-45},1/std::sqrt(35.f*35+45.f*45));
		check(bool(score(floor,{4,1},{35,0,0})),"standing player can aim at floor without bending to touch it");
		const auto box_hit=score_weapon(aim,{20,1},{40,14,0},{12,15,2},7);
		check(!score(aim,{20,1},{40,14,0},7) && box_hit && box_hit.cosine>.999f,
			"aiming at the body of a long weapon succeeds even when its centre misses the cone");
		check(better(box_hit,near),"direct ray/body hit wins over a nearer off-axis weapon");
		const auto touch=score_weapon(aim,{21,1},{0,0,0},{6,2,2},8);
		check(touch && touch.contact && std::isfinite(touch.cosine) && better(touch,box_hit),
			"hand inside weapon has stable direct-contact priority without dividing by zero");
		check(bool(score_weapon(aim,{21,1},{-4,0,0},{1,1,1},8)),"close contact does not require pointing back at the gun centre");
		check(!score_weapon(aim,{21,1},{-20,0,0},{1,1,1},8),"contact tolerance does not admit distant weapons behind hand");
		check(!score_weapon(aim,{21,1},{120,0,0},{2,2,2},8),"weapon volume stays within reach");
		check(!score_weapon(aim,{21,1},{20,0,0},{-1,2,2},8),"malformed weapon bounds rejected");
		check(!score_weapon(aim,{21,1},{20,0,0},{1000,2,2},8),"oversized bounds cannot turn into an unlimited pickup volume");
		const auto end_point=ray_box_point(aim,{80,0,0},{2,2,2});
		check(std::abs(end_point[0]-78)<.001f && end_point[1]==0,"parallel slab axes and first box entry remain deterministic");
		check(!score(aim,{22,1},{0,0,0}),"ordinary scene interactions do not inherit weapon contact pickup");
		for (int i=0;i<3;++i)
		{
			auto invalid=aim;invalid.forward[i]=std::numeric_limits<float>::quiet_NaN();
			check(!score(invalid,{2,1},{20,0,0}),"invalid aim never reaches native query");
		}
		auto invalid=aim;invalid.origin={500,0,0};check(!valid(invalid),"implausible tracked-hand displacement rejected");
		use_lease lease;
		check(lease.begin(0,direct,1) && !lease.begin(1,near,1),"two grips cannot steal one native use hold");
		check(lease.owns_hand(),"an actual world object excludes simultaneous belt pickup");
		check(lease.retain(3,1,1,true) && lease.value().key==direct.key,"hold retains exact target identity");
		check(!lease.retain(3,1,1,false),"occlusion or recycled entity cancels hold");
		check(lease.begin(1,near,1) && !lease.retain(3,2,2,true),"recenter cancels held interaction");
		check(lease.begin(0,{},2) && lease.retain(3,1,2,true),"notify-only scene supports empty target");
		check(!lease.owns_hand(),"empty Grip script notification cannot swallow the first belt Trigger");
		check(!lease.begin(0,installation,2) && !lease.value(),"aiming at C4 after pressing does not retarget a notify-only hold");
		check(!lease.retain(3,0,2,true) && lease.begin(0,installation,2),"release and a fresh press acquire the C4 object");
		check(!lease.begin(1,near,2) && lease.value().key==installation.key,"other hand cannot replace a held script target");
		check(!lease.retain(3,1,2,false),"native script rejection cancels an object-backed installation hold");
		check(lease.begin(0,{},2),"notify-only use remains available after object cancellation");
		check(!lease.retain(2,1,2,true),"parts or holsters claiming hand cancel world use");
		notification_gate gate;
		auto edge=gate.consume(0,false,true);check(!edge.press && !edge.down,"startup emits no synthetic F");
		edge=gate.consume(1,true,true);check(edge.press && edge.down && !edge.release_after,"grip emits activate press and held level");
		edge=gate.consume(1,true,true);check(!edge.press && edge.down,"held grip never repeats reliable notification");
		edge=gate.consume(1,false,true);check(edge.release_after && !edge.down,"release is paired with activate press");
		edge=gate.consume(2,false,true);check(edge.press && edge.release_after,"coalesced tap or native pickup preserves notify pair");
		gate.consume(3,true,true);edge=gate.consume(4,true,true);
		check(edge.release_before && edge.press && edge.down,"new gesture closes previous notification before opening next");
		edge=gate.consume(4,true,false);check(edge.release_before && !edge.down,"pause or tracking loss releases notification");
		edge=gate.consume(4,true,true);check(!edge.press && !edge.down,"resume while held does not replay gesture");
	}
}
