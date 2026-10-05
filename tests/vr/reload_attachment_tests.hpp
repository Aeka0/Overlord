#pragma once
#include "component/vr/gameplay/reload_attachment_state.hpp"
#include "component/vr/gameplay/weapon_carry.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"

namespace reload_attachment_tests
{
	template<class Check> void run(Check check)
	{
		namespace w=vr::gameplay::weapons;namespace p=w::physical_reload;
		using kind=p::attachment_kind;using vr::hand;
		using namespace std::chrono_literals;
		const auto at=p::clock::time_point{1s};
		unsigned profiles{};
		for(const auto* definition:w::reload_profiles)if(definition->magazine_fill())
		{
			++profiles;
			for(auto rear:{hand::left,hand::right})
			{
				const auto off=hand(1-int(rear));w::carry::inventory carry;
				const w::carry::owned_instance owned[]{{{49,7},{false,false}}};
				check(carry.reconcile_instances(owned) && carry.equip(owned[0].id,rear),"attachment fixture owns the actual rifle instance");
				p::presentation before;before.active=true;before.definition=definition;before.reference_generation=3;
				before.owner=carry.find(owned[0].id)->owner;before.ammo={49,9,1,true,true,2,60};
				check(carry.support(owned[0].id,off),"empty hand acquires the real carry support grip");
				auto supported=before;supported.owner=carry.find(owned[0].id)->owner;
				check(before.owner.support!=supported.owner.support && before.owner.revision!=supported.owner.revision &&
					before.owner.rear_revision==supported.owner.rear_revision,"support changes precede mechanical ownership without changing the rear lease");
				// present() stamps the new carry owner onto its exact skinned frame;
				// current() still contains the earlier reload view until the next tick.
				for(auto receiver_part:{kind::inserted_magazine,kind::chamber_round,kind::action_partition})
				{
					check(p::attachment_state_matches(receiver_part,supported,before,at,at+10ms),
						"receiver attachment survives new support skin ahead of mechanical ownership");
					check(p::attachment_state_matches(receiver_part,before,supported,at,at+10ms),
						"receiver attachment survives newer support state during native preparation");
				}
				const auto release=carry.release(owned[0].id,1u<<int(off),w::carry::location::absent,false,[](const auto&){return false;});
				check(release.action==w::carry::outcome::support_released,"support release retains the controlling hand");
				auto released=before;released.owner=carry.find(owned[0].id)->owner;
				check(p::attachment_state_matches(kind::inserted_magazine,released,supported,at,at+10ms) &&
					p::attachment_state_matches(kind::inserted_magazine,supported,released,at,at+10ms),"support release has no empty magazine frame in either scheduling order");
				auto pose_change=before;pose_change.magazine_pose=1;pose_change.knife_magazine_grasp=true;
				check(p::attachment_state_matches(kind::inserted_magazine,before,pose_change,at,at) &&
					p::attachment_state_matches(kind::chamber_round,before,pose_change,at,at),"offhand magazine pose selection does not own receiver-mounted geometry");
				for(int invalid=0;invalid<12;++invalid)
				{
					auto live=before;auto now=at;
					switch(invalid)
					{
					case 0:live.ammo.magazine_inserted=false;break;
					case 1:live.ammo.magazine_rounds=3;break;
					case 2:++live.ammo.instance_generation;break;
					case 3:++live.owner.instance_generation;break;
					case 4:live.owner.rear=off;break;
					case 5:++live.owner.rear_revision;break;
					case 6:++live.reference_generation;break;
					case 7:live.fault=true;break;
					case 8:live.active=false;break;
					case 9:live.definition=nullptr;break;
					case 10:now=at+151ms;break;
					case 11:now=at-1ms;break;
					}
					check(!p::attachment_state_matches(kind::inserted_magazine,before,live,at,now),
						"removal, population change, foreign lifetimes, rear changes and stale snapshots still reject");
				}
				auto saturated=before;saturated.ammo.magazine_rounds=30;auto fewer=saturated;fewer.ammo.magazine_rounds=4;
				check(p::attachment_state_matches(kind::inserted_magazine,saturated,fewer,at,at+150ms),"unchanged three-round asset remains valid at the freshness boundary");
				auto held=before;held.ammo.magazine_hand=off;held.ammo.held_rounds=2;
				check(p::attachment_state_matches(kind::held_magazine,held,held,at,at),"unchanged offhand magazine remains eligible");
				for(int change=0;change<6;++change)
				{
					auto live=held;
					switch(change)
					{
					case 0:live.owner.support=off;break;
					case 1:live.ammo.magazine_hand=hand::none;break;
					case 2:live.ammo.magazine_hand=rear;break;
					case 3:++live.magazine_pose;break;
					case 4:live.knife_magazine_grasp=true;break;
					case 5:live.ammo.held_rounds=3;break;
					}
					check(!p::attachment_state_matches(kind::held_magazine,held,live,at,at),"detached magazines still require the actual hand, grasp and payload");
				}
				auto carried=before;carried.owner.rear=hand::none;carried.owner.support=off;
				check(p::attachment_state_matches(kind::inserted_magazine,carried,carried,at,at),"foregrip-only carry retains its own attached magazine");
				auto foreign_support=carried;foreign_support.owner.support=rear;
				check(!p::attachment_state_matches(kind::inserted_magazine,carried,foreign_support,at,at),"when support is the sole carrier its identity must still match");
				for(int change=0;change<3;++change)
				{
					auto live=before;
					if(change==0)live.ammo.chamber_loaded=false;
					if(change==1)live.ammo.bolt.spent_case=true;
					if(change==2)live.ammo.bolt.feeding=true;
					check(!p::attachment_state_matches(kind::chamber_round,before,live,at,at),"extraction, spent cases and feeding do not resurrect a live chamber round");
				}
			}
		}
		check(profiles==15,"support-transition coverage includes all counted-magazine receiver variants");
		unsigned partitions{},chambers{};
		for(const auto* definition:w::reload_profiles)
		{
			const bool partition=w::partition_mesh(*definition)!=nullptr;
			const bool chamber=definition->interaction.manual_bolt && definition->feeding_path;
			if(!partition && !chamber)continue;
			partitions+=partition;chambers+=chamber;
			for(auto rear:{hand::left,hand::right})
			{
				p::presentation frame;frame.active=true;frame.definition=definition;frame.reference_generation=3;
				frame.owner={49,1,rear,hand::none,w::hold_source::interaction,1,rear,7};frame.ammo={49,9,1,true,true,2,60};
				auto supported=frame;supported.owner.support=hand(1-int(rear));++supported.owner.revision;
				if(partition)check(p::attachment_state_matches(kind::action_partition,frame,supported,at,at) &&
					p::attachment_state_matches(kind::action_partition,supported,frame,at,at),"every authored bolt/folding-handle partition survives support state ordering");
				if(chamber)check(p::attachment_state_matches(kind::chamber_round,frame,supported,at,at) &&
					p::attachment_state_matches(kind::chamber_round,supported,frame,at,at),"every independently drawn chamber round survives support state ordering");
			}
		}
		check(partitions>0 && chambers==2,"extended audit exercises partitioned actions and both M200 chamber variants");
	}
}
