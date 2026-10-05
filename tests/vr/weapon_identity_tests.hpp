#pragma once
#include "component/vr/gameplay/weapon_carry.hpp"
#include "component/vr/gameplay/weapon_instance_cache.hpp"
#include "component/vr/gameplay/weapon_feedback.hpp"
#include "component/vr/gameplay/weapon_hud_lifetime.hpp"
#include "component/vr/gameplay/weapon_render_pose.hpp"

namespace weapon_identity_tests
{
	template<class Check> void run(Check check)
	{
		using namespace vr::gameplay::weapons;
		using namespace carry;
		constexpr identity a{7,101},b{7,102},replacement{7,103};
		const owned_instance pair[]{{a,{true,true}},{b,{true,true}}};
		inventory bag;
		check(bag.reconcile_instances(pair) && bag.invariant(),"two physical copies of one definition admitted");
		check(!bag.find_definition(7) && !bag.equip_definition(7,hand::right),"definition-only lookup never selects an arbitrary copy");
		check(bag.equip(a,hand::left) && bag.equip(b,hand::right),"identical guns independently occupy both hands");
		const auto owner_a=bag.find(a)->owner,owner_b=bag.find(b)->owner;
		check(owner_a.id()==a && owner_b.id()==b,"published holds carry their stable physical identities");
		using vr::gameplay::weapon_render_pose::drives_native_muzzle;
		for (const auto projected:{a,b})
		{
			unsigned native_publications{},accepted_objects{};
			for (const auto& gun:{owner_a,owner_b})
			{
				// The actual native mailbox rejects nonprojected lifetimes. A second
				// same-model DObj must bypass it and reach its own skin/arm acceptance.
				if (drives_native_muzzle(true,gun.id(),projected))
				{
					++native_publications;
					if (gun.id()!=projected) continue;
				}
				++accepted_objects;
			}
			check(native_publications==1 && accepted_objects==2,"same-model dual skins and arms both commit while only the projected gun publishes the native muzzle");
			const auto other=projected==a ? b : a;
			check(!drives_native_muzzle(true,other,projected),"failed secondary skin cannot clear the projected same-model muzzle");
		}
		check(!drives_native_muzzle(true,replacement,a) && !drives_native_muzzle(true,{7,0},a) &&
			!drives_native_muzzle(true,{},{}),"retired, legacy and missing object identities cannot drive an independent native projection");
		check(drives_native_muzzle(false,{7,0},{7,0}),"ordinary native viewmodel retains muzzle publication");
		const owned_instance reversed[]{pair[1],pair[0]};
		check(bag.reconcile_instances(reversed) && bag.find(a)->owner.revision==owner_a.revision &&
			bag.find(b)->owner.revision==owner_b.revision,"observation reorder preserves both grips and revisions");
		const auto reject=[](const instance&){return false;};
		check(bag.release(a,1,location::absent,true,reject).action==outcome::rejected &&
			bag.in_hand(hand::left)->id==a && bag.in_hand(hand::right)->id==b,"failed identical-gun drop retains only its own transaction");
		check(bag.release(a,1,location::back,true,reject).action==outcome::stowed &&
			bag.find(b)->owner.revision==owner_b.revision,"stowing a copy does not revise the other trigger lease");
		check(bag.release(b,2,location::back,true,reject).action==outcome::exchanged &&
			bag.in_hand(hand::right)->id==a && bag.in_slot(location::back)->id==b,"same-definition holster exchange preserves both identities");
		check(bag.support(a,hand::left) && bag.release(a,2,location::absent,false,reject).action==outcome::promoted &&
			bag.in_hand(hand::left)->id==a,"handover targets the exact same-definition instance");
		identity dropped{};
		check(bag.release(a,1,location::absent,true,[&](const instance& gun){dropped=gun.id;return true;}).action==outcome::dropped &&
			dropped==a && !bag.find(a) && bag.find(b),"drop callback and retirement address one copy only");
		const owned_instance recovered[]{{replacement,{true,true}},pair[1]};
		check(bag.reconcile_instances(recovered) && !bag.restore_identity(replacement,b) &&
			bag.restore_identity(replacement,a) && bag.find(a)->owner.id()==a && bag.invariant(),"recovery preserves the world identity and rejects collision with another gun");
		check(!bag.support(replacement,hand::right) && !bag.equip(replacement,hand::right),"retired provisional identity cannot manipulate recovered gun");
		const owned_instance malformed[]{{a,{}},{a,{}}};
		check(!bag.reconcile_instances(malformed) && bag.find(a) && bag.find(b) && bag.invariant(),"duplicate instance snapshot rejected before mutation");
		const owned_instance missing[]{{{7,0},{}}};
		check(!bag.reconcile_instances(missing) && bag.find(a) && bag.find(b),"explicit instance admission rejects missing lifetime");
		const owned_weapon native[]{{7,{}}};
		check(!bag.reconcile(native) && bag.find(a) && bag.find(b),"native definition projection cannot silently collapse copies");
		check(bag.reconcile_instances({pair+1,1}) && !bag.find(a) && bag.find(b),"external instance removal retains another same-model gun");
		check(projection_rebind({7,0},a) && projection_rebind(a,{7,0}) && !projection_rebind(a,b) &&
			!projection_rebind(a,{8,0}),"carry mode changes can rebind only a unique legacy projection");
		holding_state legacy;
		auto stale=legacy.equipped(7);stale.instance_generation=a.generation;
		check(!legacy.grip(stale,grip_role::rear,hand::left) && legacy.current().rear==hand::right,
			"physical grip cannot mutate a legacy projection with matching token and revision");

		std::array<owned_instance,inventory::capacity+1> many{};
		for (std::size_t i=0;i<many.size();++i) many[i]={{7,1000+i},{true,true}};
		check(bag.reconcile_instances({many.data(),inventory::capacity}) && bag.invariant(),"fifteen equal definitions occupy bounded independent slots");
		check(!bag.reconcile_instances(many) && bag.invariant(),"over-capacity admission leaves all live copies intact");
		std::uint32_t random=9421;
		for (unsigned i=0;i<4000;++i)
		{
			random=random*1664525u+1013904223u;const auto h=static_cast<hand>((random>>5)&1);
			constexpr location slots[]{location::left_waist,location::right_waist,location::back,location::absent};
			if (const auto* gun=bag.in_hand(h)) bag.release(gun->id,1u<<unsigned(h),slots[random%4],bool(random&7),reject);
			else bag.draw(slots[random%4],h);
			check(bag.invariant(),"adversarial identical-gun actions preserve hand and slot uniqueness");
		}
		instance_cache<int,2> cache;
		*cache.acquire(a)=3;*cache.acquire(b)=11;
		check(*cache.find(a)==3 && *cache.find(b)==11 && !cache.acquire(replacement),"full cache never evicts or aliases a same-model gun");
		cache.retain([&](identity id){return id==b;});
		check(!cache.find(a) && *cache.find(b)==11 && *cache.acquire(replacement)==0,"retired slot starts clean without overwriting surviving copy");
		auto other=owner_a;++other.instance_generation;
		check(!vr::gameplay::weapon_hud::same_presentation_owner(owner_a,other),"identical hand and revision cannot alias another instance HUD");
		vr::controller_input::frame input;input.focused=true;input.sequence=1;input.reference_generation=1;
		input.sampled_at=vr::controller_input::clock::now();
		for (int h=0;h<2;++h) {input.grip[h].valid=true;input.aim[h].valid=true;}
		feedback::event shot{mechanics::effect::shot,owner_a,1,input.sampled_at};
		check(feedback::fresh(shot,owner_a,input,true,input.sampled_at) && !feedback::fresh(shot,other,input,true,input.sampled_at),
			"queued shot audio and effects cannot migrate to a same-model replacement");
	}
}
