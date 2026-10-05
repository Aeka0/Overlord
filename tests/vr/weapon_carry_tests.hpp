#pragma once
#include "component/vr/gameplay/weapons/m4/profile.hpp"
#include "component/vr/gameplay/weapon_carry.hpp"
#include "component/vr/gameplay/native_viewmodel_policy.hpp"
#include "component/vr/gameplay/native_carry_selection_policy.hpp"
#include "component/vr/gameplay/grip_edges.hpp"
#include "component/vr/gameplay/weapon_holsters.hpp"
#include "component/vr/gameplay/weapon_carry_grip.hpp"
#include "component/vr/gameplay/weapon_carry_profiles.hpp"
#include "component/vr/gameplay/weapon_carry_pose.hpp"
#include "component/vr/gameplay/weapon_carry_render_cache.hpp"
#include "component/vr/gameplay/weapon_render_owner.hpp"
#include <memory>
#include "weapon_identity_tests.hpp"
#include "holster_presentation_tests.hpp"

namespace weapon_carry_tests
{
	template<class Check> void run(Check check)
	{
		weapon_identity_tests::run(check);
		holster_presentation_tests::run(check);
		using namespace vr::gameplay::weapons;
		using namespace carry;
		namespace hands=vr::gameplay::hands;
		{
			using native_carry_selection::admit;using native_carry_selection::admission;
			// Captured Exodus draw: immediate selection clears actual weapon,
			// then the old gun's command precedes the requested device command.
			for(auto gun:{8u,26u})
			{
				check(admit(gun,gun,64,0,0x08000800,3,false)==admission::defer,
					"pending device preserves immediate selection until the old gun command drains");
				check(admit(64,gun,64,0,0x08000800,3,false)==admission::physical,
					"arriving device command completes natively and expires its raise before grip handoff");
			}
			check(admit(64,0,64,0,0x08000800,3,false)==admission::physical &&
				admit(8,8,0,0,0x08000000,3,false)==admission::physical,
				"empty-hand device draws and ordinary physical gun acquisition stay immediate");
			check(admit(8,0,0,0,0x08000800,3,false)==admission::native &&
				admit(8,0,64,8,0x08000800,3,false)==admission::native &&
				admit(8,0,64,0,0x800,3,false)==admission::native &&
				admit(8,0,64,0,0x08000000,3,false)==admission::native,
				"absent reservation, selected weapon, and missing native transition flags never stall a switch");
			check(admit(64,8,64,0,0x08000800,9,false)==admission::native &&
				admit(8,8,64,0,0x08000800,3,true)==admission::native &&
				admit(26,8,64,64,0,4,false)==admission::native,
				"reload, alternate mode and unrelated native selection keep native timing");
		}
		check(vr::gameplay::hands::personal_firearm_definition(1,11,0,"usp_laserdesignator") && !vr::gameplay::hands::personal_firearm_definition(1,11,0,"unrelated_item"),
			"captured class-11 designator replaces its flat viewmodel without claiming every mission item");
		for(auto h:{hand::left,hand::right})
		{
			inventory device;const owned_weapon items[]{{64,profile_for("usp_laserdesignator")}};
			check(device.reconcile(items) && device.find_definition(64)->at==location::abdominal,"designator starts at abdomen instead of taking a pistol holster");
			const auto id=device.find_definition(64)->id;check(device.equip(id,h),"mission device accepts either controlling hand");
			const auto other=hand(1-unsigned(h));check(device.support(id,other),"mission device admits ordinary pistol support grip");
			check(device.release(id,1u<<unsigned(h),location::absent,false,[](const auto&){return false;}).action==outcome::promoted,"remaining hand retains bilateral mission device");
			int drops{};const auto result=device.release(id,3,location::right_waist,false,[&](const auto&){++drops;return true;});
			check(result.action==outcome::stowed && device.find(id)->at==location::abdominal && !drops && device.invariant(),"last release always returns mission device without clearance, hip exchange or world spawn");
		}
		check(vr::gameplay::hands::personal_firearm_definition(1,0,0) && vr::gameplay::hands::personal_firearm_definition(1,2,0),"captured native AK and M240 classes are firearm definitions even with zero selected weapon");
		check(!vr::gameplay::hands::personal_firearm_definition(2,9,1) && !vr::gameplay::hands::personal_firearm_definition(1,8,0) && !vr::gameplay::hands::personal_firearm_definition(3,6,3),"grenades knives and alternate-mode-only props are not generic firearm fallbacks");
		check(vr::gameplay::hands::suppress_legacy_viewmodel(true,false,true),"cinematic or unavailable VR owner cannot expose a legacy firearm");
		check(!vr::gameplay::hands::suppress_legacy_viewmodel(true,false,false),"native bomb and cinematic props remain allowed during scripted ownership");
		check(!vr::gameplay::hands::suppress_legacy_viewmodel(false,true,true) && vr::gameplay::hands::suppress_legacy_viewmodel(true,true,false),"flat mode retains native models and active VR replacement owns its normal viewmodel pass");
		for(auto rear:{hand::left,hand::right})
		{
			inventory held;const owned_weapon items[]{{101,{true,true}}};held.reconcile(items);held.equip_definition(101,rear);
			const auto id=held.find_definition(101)->id;unsigned drops{};
			const auto result=held.release(id,1u<<unsigned(rear),location::absent,true,[&](const auto&){++drops;return true;},false);
			check(result.action==outcome::retained && !drops && held.in_hand(rear) && held.invariant(),"scripted retention consumes release without dropping or corrupting hand ownership");
			const auto released=held.release(id,1u<<unsigned(rear),location::absent,true,[&](const auto&){++drops;return true;});
			check(released.action==outcome::dropped && drops==1 && !held.in_hand(rear),"new ordinary release can drop once after scripted retention ends");
		}
		for(auto rear:{hand::left,hand::right})
		{
			const auto other=hand(1-unsigned(rear));
			inventory held;const owned_weapon items[]{{101,{true,true}},{102,{true,true}}};
			check(held.reconcile(items) && held.equip_definition(101,rear),"scripted slot fixture owns two weapons");
			const auto id=held.find_definition(101)->id,stored=held.find_definition(102)->id;
			const auto slot=held.find(stored)->at;unsigned drops{};
			const auto drop=[&](const auto&){++drops;return true;};
			check(held.support(id,other),"scripted retention permits support acquisition");
			check(held.release(id,1u<<unsigned(other),location::absent,true,drop,false).action==outcome::support_released,
				"scripted retention permits independent support release");
			check(held.release(id,1u<<unsigned(rear),slot,true,drop,false).action==outcome::exchanged && held.in_hand(rear)->id==stored,
				"scripted retention permits occupied-slot exchange");
			check(held.support(stored,other) && held.release(stored,1u<<unsigned(rear),location::absent,true,drop,false).action==outcome::promoted,
				"scripted retention permits hand transfer");
			check(held.release(stored,3,location::absent,true,drop,false).action==outcome::retained && !drops,
				"exchanged weapon is also protected from an empty-space drop");
			const auto empty=slot==location::left_waist?location::right_waist:location::left_waist;
			check(held.release(stored,1u<<unsigned(other),empty,true,drop,false).action==outcome::stowed,
				"scripted retention permits explicit stow into an empty slot");
			check(held.draw(empty,rear).action==outcome::drawn && held.in_hand(rear)->id==stored && !drops && held.invariant(),
				"scripted stow and draw preserve exact identities without native world drops");
		}
		for (auto rear:{hand::left,hand::right})
		{
			const auto other=static_cast<hand>(1-static_cast<int>(rear));
			inventory initial;const owned_weapon items[]{{101,{}},{102,{true,true}}};
			check(initial.reconcile(items) && initial.equip_definition(101,rear),"priority fixture owns rifle and holstered sidearm");
			const auto rifle=initial.find_definition(101)->id;
			for (int choice=0;choice<3;++choice)
			{
				auto state=initial;
				const auto claim = claim_grip(state, {
					.actor = other,
					.pressed = true,
					.available = true,
					.support = choice == 0 ? rifle : identity{},
					.slot = choice < 2 ? location::right_waist : location::absent
				});
				check(claim.consumed==(choice<2),"support and occupied holster consume the press before ground pickup or F");
				if(choice==0)check(state.in_hand(other)->id==rifle && state.in_slot(location::right_waist)->id.weapon==102,
					"support wins a simultaneous support and holster contact without drawing the sidearm");
				if(choice==1)check(claim.change.action==outcome::drawn && state.in_hand(other)->id.weapon==102,
					"holster draw wins when no support contact matches");
				if(choice==2)check(!state.in_hand(other),"unclaimed press remains available to world interaction");
			}
			for (int blocked=0;blocked<3;++blocked)
			{
				auto state = initial;
				const auto claim = claim_grip(state, {
					.actor = other,
					.pressed = blocked != 0,
					.released = blocked == 1,
					.available = blocked != 2,
					.support = rifle,
					.slot = location::right_waist
				});
				check(!claim.consumed && !state.in_hand(other),"no edge, release or leased/busy hand cannot acquire support or draw");
			}
			auto state = initial;
			const auto stale = claim_grip(state, {
				.actor = other,
				.pressed = true,
				.available = true,
				.support = {101, rifle.generation + 1},
				.slot = location::right_waist
			});
			check(stale.consumed && !stale.pose_changed && !state.in_hand(other),"failed high-priority claim never falls through to another operation");
			state=initial;state.support(rifle,other);state.release(rifle,1u<<static_cast<unsigned>(rear),location::absent,true,[](const auto&){return false;});
			const auto control = claim_grip(state, {
				.actor = rear,
				.pressed = true,
				.available = true,
				.control = rifle,
				.slot = location::right_waist
			});
			check(control.consumed && control.pose_changed && state.in_hand(rear)->id==rifle,"rear-grip reacquisition precedes holster and world actions");
		}
		{
			auto p=m4::foregrip;const vr::gameplay::hands::anchor gun{{10,20,30},{0,0,0,1}},local{{20,0,0},{0,0,0,1}};
			check(support_contact(p,gun,local,{10,20,30},{30,20,30},100),"same-frame support contact uses authored position before any render proposal");
			check(!support_contact(p,gun,local,{10,20,30},{50,20,30},100),"distant hand cannot reserve support priority");
			check(!support_contact(p,gun,local,{29,20,30},{30,20,30},100),"coincident rifle hands retain existing eight-centimetre exclusion");
			const auto rotation=vr::gameplay::hands::from_axis({{{0,1,0},{-1,0,0},{0,0,1}}});
			check(support_contact(p,{{10,20,30},rotation},local,{10,20,30},{10,40,30},100),"support priority follows current receiver rotation");
			check(!support_contact(p,gun,local,{10,20,30},{30,20,30},0),"invalid world scale cannot reserve a grip");
		}
		// Native queued records may consume an older epoch after a newer hand
		// solve. Neither pointer reuse nor asset equality permits latest-pose reuse.
		auto cache=std::make_unique<render_cache>();
		render_sample a;a.object=11;a.matrices=22;a.epoch=3;a.reference=7;a.count=1;
		a.parts[0].model=33;a.parts[0].slot=64;a.parts[0].id={2,1};a.parts[0].relative.position={1,2,3};
		cache->publish(a);auto b=a;b.epoch=4;b.parts[0].relative.position={20,30,40};cache->publish(b);
		render_part part;render_sample metadata;
		check(cache->find(11,22,3,64,33,part,metadata) && part.relative.position==vr::gameplay::hands::vec{1,2,3},
			"queued secondary gun uses its skinned epoch, not latest hand");
		check(!cache->find(11,23,3,64,33,part,metadata) && !cache->find(12,22,3,64,33,part,metadata) &&
			!cache->find(11,22,3,65,33,part,metadata) && !cache->find(11,22,3,64,34,part,metadata),
			"secondary pose rejects recycled buffers, other part and changed model");
		for(auto rear:{hand::left,hand::right})
		{
			const hand off=hand(1-int(rear));auto packet=a;
			packet.parts[0].owner={2,5,rear,hand::none,hold_source::interaction,5,rear,1};cache->publish(packet);
			auto current_owner=packet.parts[0].owner;current_owner.support=off;++current_owner.revision;
			check(cache->find(11,22,3,64,33,part,metadata) && same_render_carrier(part.owner,current_owner),
				"held whole-weapon fallback survives support acquisition while keeping the exact queued pose");
			for(int change=0;change<6;++change)
			{
				auto invalid=current_owner;
				if(change==0)++invalid.instance_generation;
				if(change==1)invalid.rear=off;
				if(change==2)++invalid.rear_revision;
				if(change==3)invalid.attachment=control_attachment::moving;
				if(change==4)invalid.rear=invalid.support=hand::none;
				if(change==5)invalid.pose_rear=off;
				check(!same_render_carrier(part.owner,invalid),"whole-weapon fallback rejects real carrier, contact and lifetime changes");
			}
			auto support_only=current_owner;support_only.rear=hand::none;
			check(same_render_carrier(support_only,support_only),"unchanged support-only weapon remains renderable");
			auto new_lease=support_only;++new_lease.revision;
			check(!same_render_carrier(support_only,new_lease),"sole-support regrasp cannot reuse an earlier carrier lease");
		}
		b.count=0;cache->publish(b);
		check(!cache->find(11,22,4,64,33,part,metadata),"removed part never falls back to an earlier publication");
		for (unsigned n=0;n<128;++n) {b.epoch=10+n;cache->publish(b);}
		check(!cache->find(11,22,3,64,33,part,metadata),"expired queued epoch is omitted rather than substituted");
		const auto no_drop=[](const instance&){return false;};
		{
			grip_edge_gate independent;
			vr::controller_input::frame f;f.focused=true;f.sequence=1;f.reference_generation=1;
			f.sampled_at=vr::controller_input::clock::now();
			for (int h=0;h<2;++h) {f.grip[h].valid=true;f.squeeze[h].active=true;f.squeeze[h].generation=1;}
			independent.consume(f,true,f.sampled_at);
			++f.sequence;++f.squeeze[0].presses;f.squeeze[0].down=true;
			independent.consume(f,true,f.sampled_at);
			++f.sequence;f.grip[1].valid=false;++f.squeeze[0].releases;f.squeeze[0].down=false;
			check(independent.consume(f,true,f.sampled_at).released==1,"valid left release survives opposite tracking loss");
			++f.sequence;f.grip[1].valid=true;
			check(independent.consume(f,true,f.sampled_at).released==0,"tracking recovery cannot replay consumed release");
		}
		// Both physical hands must retain real release intent across pose-only
		// gaps, without turning focus/input loss or tracking recovery into a grab.
		for (unsigned h=0;h<2;++h)
		{
			using namespace std::chrono_literals;
			grip_edge_gate edges;vr::controller_input::digital_sampler buttons;
			vr::controller_input::frame f;f.focused=true;f.reference_generation=1;
			f.sampled_at=vr::controller_input::clock::time_point{}+1s;
			const auto tick=[&](bool down,bool pose=true,bool active=true) {
				++f.sequence;f.sampled_at+=10ms;f.grip[h].valid=pose;
				f.squeeze[h]=buttons.sample(active,down,f.sampled_at);
				return edges.consume(f,true,f.sampled_at);
			};
			const auto bit=1u<<h;
			check(tick(true).pressed==0,"initial held input cannot acquire a new grip");
			check(tick(false).released==bit,"initial held weapon accepts next real release");
			tick(true);tick(true,false);tick(true);
			check(tick(false).released==bit,"pose gap before release does not disarm the grip");
			tick(true);
			const auto lost=tick(false,false);
			check(lost.released==0 && lost.deferred==bit,"real release waits for its own hand geometry");
			check(tick(false).released==bit && tick(false).released==0,"deferred release is delivered exactly once");
			tick(true);tick(false,false);tick(true,false);
			check(tick(true).released==0 && tick(false).released==bit,"new press cancels older deferred release");
			tick(true);tick(false,false);++f.reference_generation;
			check(tick(false).released==0,"recenter cancels deferred release from old coordinates");
			tick(true);tick(true,true,false);
			check(tick(true).pressed==0 && tick(false).released==bit,"input recovery while held admits only the next real release");
			tick(true);f.sampled_at+=200ms;
			check(tick(true).pressed==0 && tick(false).released==bit,"slow-frame generation change cannot strand a held grip");
			tick(true);
			// Runtime samples continue while the server consumer stalls. Its
			// cumulative release remains evidence in the same input generation.
			for (unsigned i=0;i<30;++i)
			{
				++f.sequence;f.sampled_at+=10ms;
				f.squeeze[h]=buttons.sample(true,i<20,f.sampled_at);
			}
			check(edges.consume(f,true,f.sampled_at).released==bit,
				"consumer stall cannot discard a proven same-generation release");
			tick(true);tick(false,false);edges.latch(static_cast<hand>(h));
			check(tick(false).released==0,"exchange latch cancels pending release");
			tick(true);tick(false,false);f.focused=false;tick(false);f.focused=true;
			check(tick(false).released==0,"focus suspension cannot replay pending drops");
		}
		const std::array<owned_weapon,6> items{{{1,{true,true}},{2,{true,true}},{3,{}},{4,{}},{5,{}},{6,{true,true}}}};
		inventory bag;check(bag.reconcile(items),"carry native admission");
		check(bag.invariant() && bag.next_back()->id.weapon==3,"normal back precedes overflow");
		check(bag.draw(location::back,hand::right).action==outcome::drawn,"back draw");
		check(bag.draw(location::back,hand::left).subject.weapon==4,"second hand draws overflow");
		check(bag.next_back()->id.weapon==5,"stable overflow order");
		check(bag.reconcile(items) && bag.in_hand(hand::left)->id.weapon==4,"reconcile never requeues extracted weapon");
		auto id=bag.in_hand(hand::right)->id;
		check(bag.release(id,2,location::right_waist,true,no_drop).action==outcome::rejected,"long gun rejected by occupied waist");
		check(bag.in_hand(hand::right)->id==id && bag.in_slot(location::right_waist)->id.weapon==1,"rejected stow loses neither weapon");
		check(bag.release(id,2,location::back,true,no_drop).action==outcome::stowed,"empty back stow");
		check(!bag.in_hand(hand::right) && bag.in_hand(hand::left)->id.weapon==4,"stow leaves hand empty without equipping another weapon");
		check(bag.draw(location::right_waist,hand::right).subject.weapon==1,"waist draw");
		id=bag.in_hand(hand::right)->id;
		check(bag.release(id,2,location::left_waist,true,no_drop).action==outcome::exchanged,"occupied compatible holster swaps atomically");
		check(bag.in_hand(hand::right)->id.weapon==2 && bag.in_slot(location::left_waist)->id==id && bag.invariant(),"exchange conserves identities");
		check(bag.release(bag.in_hand(hand::left)->id,1,location::overflow,true,no_drop).action==outcome::rejected,"overflow cannot accept deposits");
		check(bag.release(bag.in_hand(hand::left)->id,1,location::absent,true,no_drop).action==outcome::rejected,"native drop failure retains grip");
		unsigned drops{};
		auto accept=[&](const instance&){++drops;return true;};
		check(bag.release(bag.in_hand(hand::left)->id,1,location::absent,false,accept).action==outcome::rejected && drops==0,"clipped release never calls native drop");
		check(bag.release(bag.in_hand(hand::left)->id,1,location::absent,true,accept).action==outcome::dropped && drops==1,"successful native drop clears only one hand");
		inventory pistol;const std::array<owned_weapon,1> p{{items[0]}};pistol.reconcile(p);pistol.equip_definition(1,hand::right);
		id=pistol.find_definition(1)->id;check(pistol.support(id,hand::left),"support hand attached");
		const auto old=pistol.find(id)->owner;
		check(pistol.release(id,2,location::absent,false,accept).action==outcome::promoted,"pistol transfers control before clearance checks");
		check(pistol.find(id)->owner.rear==hand::left && pistol.find(id)->owner.support==hand::none &&
			pistol.find(id)->owner.rear_revision!=old.rear_revision && drops==1,"handover retains instance and invalidates old trigger lease");
		pistol.equip_definition(1,hand::right);pistol.support(id,hand::left);
		check(pistol.release(id,3,location::absent,true,accept).action==outcome::dropped && drops==2,"simultaneous release drops once without transient handover");
		inventory rifle;const std::array<owned_weapon,1> r{{items[2]}};rifle.reconcile(r);rifle.equip_definition(3,hand::right);
		id=rifle.find_definition(3)->id;rifle.support(id,hand::left);
		check(rifle.release(id,2,location::absent,false,accept).action==outcome::carry_only,"long gun stays at remaining foregrip");
		check(!rifle.find(id)->owner.can_fire() && rifle.control(id,hand::right) && rifle.find(id)->owner.can_fire(),"carry-only requires control reacquisition");
		// Native capacity, not a baked-in number of ordinary physical weapons.
		inventory many;std::array<owned_weapon,15> full{};
		for (unsigned i=0;i<15;++i) full[i]={i+1,{true,true}};
		check(many.reconcile(full) && many.invariant(),"fifteen native records admitted without a three-weapon cap");
		many.draw(location::left_waist,hand::left);many.draw(location::right_waist,hand::right);
		many.release(many.in_hand(hand::left)->id,1,location::absent,true,accept);
		many.draw(location::back,hand::left);many.release(many.in_hand(hand::left)->id,1,location::left_waist,true,no_drop);
		many.draw(location::back,hand::left);
		check(many.in_hand(hand::left)->id.weapon==4,"overflow extraction follows ordinary back");
		check(many.release(many.in_hand(hand::left)->id,1,location::right_waist,true,no_drop).action==outcome::stowed,"extracted pistol may enter compatible ordinary empty slot");
		// Rejected/malformed snapshots leave existing data unchanged.
		const std::array<owned_weapon,2> duplicate{{{2,{}},{2,{}}}};
		check(!many.reconcile(duplicate) && many.invariant(),"duplicate native tokens rejected atomically");
		std::uint32_t random=7321;
		for (unsigned n=0;n<4000;++n)
		{
			random=random*1664525u+1013904223u;const auto h=static_cast<hand>((random>>5)&1);
			constexpr std::array slots{location::left_waist,location::right_waist,location::back,location::overflow,location::absent};
			const auto target=slots[random%slots.size()];
			if (const auto* v=many.in_hand(h)) many.release(v->id,1u<<static_cast<unsigned>(h),target,(random&7)!=0,no_drop);
			else many.draw(target,h);
			check(many.invariant(),"adversarial carry sequence conserves slot/hand uniqueness");
		}
		using vr::controller_input::clock;using namespace std::chrono_literals;
		vr::controller_input::frame frame;frame.sequence=1;frame.reference_generation=1;frame.focused=true;
		frame.sampled_at=clock::time_point{}+1s;frame.grip[0].valid=frame.grip[1].valid=true;
		vr::controller_input::digital_sampler sampler;grip_edge_gate gate;
		const auto sample=[&](bool active,bool down) {frame.sampled_at+=10ms;++frame.sequence;frame.squeeze[1]=sampler.sample(active,down,frame.sampled_at);return gate.consume(frame,true,frame.sampled_at);};
		check(sample(true,false).released==0 && sample(true,true).pressed==2,"grip edge requires actual press");
		check(sample(true,false).released==2 && sample(true,false).released==0,"release once; no retry while button remains up");
		sample(true,true);check(sample(false,false).released==0 && sample(true,false).released==0,"tracking loss is not release");
		sample(true,true);frame.focused=false;check(sample(true,false).released==0,"focus loss is not release");
		frame.focused=true;sample(true,false);sample(true,true);gate.latch(hand::right);
		check(sample(true,false).released==0,"new exchanged grip cannot inherit old release");
		check(sample(true,true).pressed==2 && sample(true,false).released==2,"fresh regrip retries a latched release");
		sample(true,true);gate.adopt(hand::right,frame,frame.sampled_at);sample(true,true);
		check(sample(true,false).released==2 && sample(true,false).released==0,"delayed abdominal pickup preserves its first physical release exactly once");
		check((neutral_grips(frame,frame.sampled_at)&2)!=0,"fresh neutral grip can settle abdominal equipment without a second press");
		frame.squeeze[1].active=false;check(!(neutral_grips(frame,frame.sampled_at)&2),"inactive tracking input is not a proven physical release");frame.squeeze[1].active=true;
		check(profile_for("glock").waist && profile_for("glock").promote_support && !profile_for("ump45").waist &&
			profile_for("tmp").waist && !profile_for("tmp").promote_support,"carry eligibility is explicitly authored");
		for (auto name:{"ranger","ranger_akimbo"})for(auto slot:{location::left_waist,location::right_waist})
		{
			inventory carried;const owned_weapon items[]{{301,profile_for(name)}};
			check(carried.reconcile(items) && carried.equip_definition(301,hand::right),"Ranger carry fixture owns the native instance");
			const auto id=carried.find_definition(301)->id;
			check(!carried.find(id)->policy.promote_support,"Ranger waist eligibility preserves long-gun support ownership");
			check(carried.release(id,2,slot,true,[](const auto&){return false;}).action==outcome::stowed && carried.in_slot(slot)->id==id,
				"Ranger stows in either waist without a native world drop");
			check(carried.draw(slot,hand::left).action==outcome::drawn && carried.in_hand(hand::left)->id==id && carried.invariant(),
				"Ranger waist draw retains the same instance and slot uniqueness");
		}
		check(!profile_for("ranger2").waist && !profile_for("m79").waist,"Ranger waist exception does not admit unrelated weapons");
		vr::head_pose_bridge::spatial_frame body{};body.units_per_meter=40;body.head_position={10,20,70};
		body.head_yaw_axis={{{1,0,0},{0,1,0},{0,0,1}}};
		const auto holsters=locate_holsters(body);
		{
			inventory stored;const owned_weapon weapons[]{{201,{}},{202,{}}};stored.reconcile(weapons);
			auto overlapping=holsters;overlapping.centers[0]=holsters.centers[2];overlapping.volumes[0]=holsters.volumes[2];
			check(draw_contact(stored,overlapping,holsters.centers[2])==location::back,"empty overlapping waist cannot conceal occupied back draw priority");
			stored.draw(location::back,hand::right);
			check(draw_contact(stored,overlapping,holsters.centers[2])==location::back,"overflow-only back still reserves draw priority");
			stored.draw(location::back,hand::left);
			check(draw_contact(stored,overlapping,holsters.centers[2])==location::absent,"empty holsters do not suppress ground pickup or F");
		}
		for (const auto where:{location::left_waist,location::right_waist})
		{
			const auto q=holster_rotation(body.head_yaw_axis,where);
			const auto barrel=vr::gameplay::hands::rotate(q,{1,0,0}),slide=vr::gameplay::hands::rotate(q,{0,0,1});
			check(barrel[2]<-.98f && (where==location::left_waist ? barrel[1]<-.1f : barrel[1]>.1f) && slide[0]>.999f,
				"waist gun points down with inward cant and slide facing body forward");
		}
		check(holsters.valid && hit(holsters,holsters.centers[0])==location::left_waist &&
			hit(holsters,holsters.centers[2])==location::back,"body slots use gravity-aligned head-yaw frame");
		const auto body_point=[&](vr::gameplay::hands::vec meters) {return vr::gameplay::hands::add(body.head_position,vr::gameplay::hands::scale(meters,body.units_per_meter));};
		check(hit(holsters,body_point({-.25f,.47f,-.86f}))==location::left_waist &&
			hit(holsters,body_point({-.25f,-.47f,-.86f}))==location::right_waist,"waist slots reach behind, outside and below both hips");
		check(hit(holsters,body_point({-.30f,.35f,-.18f}))==location::back &&
			hit(holsters,body_point({-.30f,-.35f,-.18f}))==location::back &&
			hit(holsters,body_point({-.35f,0,-.56f}))==location::back,"back slot covers both shoulder approaches and lower back");
		check(hit(holsters,body_point({.25f,.23f,-.6f}))==location::absent,"forward hand motion is outside body storage");
		auto rotated=body;rotated.head_yaw_axis={{{0,1,0},{-1,0,0},{0,0,1}}};
		const auto rotated_slots=locate_holsters(rotated);
		const auto turned_slide=vr::gameplay::hands::rotate(holster_rotation(rotated.head_yaw_axis,location::left_waist),{0,0,1});
		check(turned_slide[1]>.999f && std::abs(turned_slide[0])<.001f,"holstered gun orientation follows body yaw");
		check(hit(rotated_slots,body_point({-.47f,-.25f,-.86f}))==location::left_waist,"extended storage rotates with body yaw");
		body.head_yaw_axis[0][2]=.5f;check(!locate_holsters(body).valid,"invalid body basis cannot accept a slot");
	}
}
