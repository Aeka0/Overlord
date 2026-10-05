#pragma once
#include "component/vr/gameplay/weapon_clip_projection.hpp"
#include "component/vr/gameplay/weapon_carry.hpp"

namespace weapon_clip_tests
{
	template<class Check> void run(Check check)
	{
		using namespace vr::gameplay::weapons;
		namespace p=native_ammunition::projection;
		namespace s=native_ammunition::storage;
		// Console give of an already-owned definition is a native no-op until
		// InitializeAmmo. Its entire sequence must share the pickup transaction.
		{
			std::array<std::byte,s::extent> memory{};clip_ledger given;
			const clip_ledger::definition gold[]{{21,2,21,0}};
			check(given.reconcile(gold) && s::commit(memory,21,21,0,0,2,14),"partly fired Desert Eagle exists before repeated give");
			const auto original=given.projected(21),copy=given.allocate(21);
			check(p::begin_pickup(given,memory,21,21,21) && s::observe(memory,21,21).clip.count==0,
				"give parks old physical clip before native already-owned success");
			// Model the observed native success-without-insertion return, followed
			// by the ordinary command's ammo initializer. Existing reserve is shared.
			check(given.count(21)==1 && s::commit(memory,21,21,0,14,7,21) &&
				p::finish_pickup(given,memory,copy,original,21,21,0,true) && given.count(21)==2 &&
				given.find(original)->loaded==2 && given.find(copy)->loaded==7 && s::observe(memory,21,21).clip.count==2 &&
				s::observe(memory,21,21).reserve.count==21,"repeated give creates a full independent clip and preserves original clip and native reserve result");
			carry::inventory carried;
			const carry::owned_instance first[]{{original,{true,true}}},pair[]{{original,{true,true}},{copy,{true,true}}};
			check(carried.reconcile_instances(first) && carried.equip(original,hand::right) && carried.reconcile_instances(pair) &&
				carried.in_hand(hand::right)->id==original && carried.find(copy)->at!=carry::location::held,
				"give while holding preserves the occupied hand and stows its new physical copy");
			check(carried.equip(copy,hand::left) && carried.in_hand(hand::left)->id==copy && carried.in_hand(hand::right)->id==original,
				"newly given same-model gun can be drawn into the second hand");
			carried.put_away();
			const auto third=given.allocate(21);
			check(p::begin_pickup(given,memory,21,21,21) && !p::finish_pickup(given,memory,third,original,21,21,0,false) &&
				given.count(21)==2 && s::observe(memory,21,21).clip.count==2,"rejected give restores the old projection without another copy");
			check(p::begin_pickup(given,memory,21,21,21) && s::commit(memory,21,21,0,21,7,21) &&
				p::finish_pickup(given,memory,third,original,21,21,0,true),"give while both copies are holstered admits a third independent clip");
			const carry::owned_instance holstered[]{{original,{true,true}},{copy,{true,true}},{third,{true,true}}};
			check(carried.reconcile_instances(holstered) && !carried.in_hand(hand::left) && !carried.in_hand(hand::right) &&
				carried.equip(third,hand::left),"give with all copies carried leaves hands empty until explicitly drawn");
			for (std::size_t i=given.count();i<clip_ledger::capacity;++i) check(given.add(given.allocate(21),7,21,0),"fill duplicate give capacity");
			const auto before=memory;
			check(!p::begin_pickup(given,memory,21,21,21) && before==memory && given.find(original)->loaded==2,
				"sixteenth give is rejected before native initialization or parking changes ammunition");
		}
		std::array<std::byte,s::extent> bytes{};
		clip_ledger clips;
		const clip_ledger::definition native[]{{7,11,70,0},{8,4,80,0}};
		check(clips.reconcile(native),"native definitions seed independent instance ledger");
		const auto a=clips.projected(7),other=clips.projected(8),b=clips.allocate(7);
		check(s::commit(bytes,70,90,0,0,11,40) && s::commit(bytes,80,90,0,40,4,40),"two definitions share one native reserve pool");
		check(p::begin_pickup(clips,bytes,7,70,90) && s::observe(bytes,70,90).clip.count==0 && clips.find(a)->loaded==11,
			"duplicate pickup parks existing clip before native payload installation");
		check(s::commit(bytes,70,90,0,40,3,45),"native pickup installs ground clip and credits shared reserve once");
		check(p::finish_pickup(clips,bytes,b,a,70,90,0,true) && clips.find(a)->loaded==11 && clips.find(b)->loaded==3 &&
			s::observe(bytes,70,90).clip.count==11 && s::observe(bytes,70,90).reserve.count==45,
			"duplicate pickup commits distinct counts and restores surviving projection");
		check(p::commit(clips,bytes,b,70,90,3,45,2,45) && p::commit(clips,bytes,a,70,90,11,45,10,45) &&
			clips.find(a)->loaded==10 && clips.find(b)->loaded==2 && clips.find(other)->loaded==4,
			"same-tick shots debit only the two requested physical clips");
		check(p::commit(clips,bytes,b,70,90,2,45,12,35) && !p::commit(clips,bytes,a,70,90,10,45,15,40) &&
			clips.find(a)->loaded==10 && clips.find(b)->loaded==12 && s::observe(bytes,80,90).reserve.count==35,
			"shared reserve comparison rejects the second stale reload without partial debit");
		check(p::select(clips,bytes,b,70,90) && s::observe(bytes,70,90).clip.count==12 && p::select(clips,bytes,a,70,90) &&
			s::observe(bytes,70,90).clip.count==10,"switching native projection preserves both clips");
		check(s::commit(bytes,70,90,10,35,9,35) && p::select(clips,bytes,b,70,90) && clips.find(a)->loaded==9,
			"native projected shot is reconciled only into its physical owner");
		const auto c=clips.allocate(7);
		check(p::begin_pickup(clips,bytes,7,70,90) && !p::finish_pickup(clips,bytes,c,b,70,90,0,false) &&
			!clips.find(c) && clips.find(b)->loaded==12 && s::observe(bytes,70,90).clip.count==12,
			"failed pickup preserves clips and ownership without admitting a phantom instance");
		check(p::retire(clips,bytes,b,70,90) && !clips.find(b) && clips.projected(7)==a &&
			s::observe(bytes,70,90).clip.count==9 && s::observe(bytes,70,90).reserve.count==35,
			"dropping projected copy retires only that copy and preserves shared reserve");
		check(p::begin_pickup(clips,bytes,7,70,90) && s::commit(bytes,70,90,0,35,12,35) &&
			p::finish_pickup(clips,bytes,b,a,70,90,0,true) && clips.find(b)->loaded==12,
			"recovered world identity retains its clip without cloning player reserve");
		const auto frozen=bytes;
		check(!p::commit(clips,bytes,c,70,90,12,35,11,35) && !p::commit(clips,bytes,b,80,90,12,35,11,35) &&
			!p::commit(clips,bytes,b,70,90,12,35,-1,35) && bytes==frozen && clips.find(b)->loaded==12,
			"retired identities, mismatched native keys and negative clips cannot mutate storage");
		const clip_ledger::definition regiven[]{{7,5,70,1},{8,4,80,0}};
		check(clips.reconcile(regiven) && !clips.find(a) && !clips.find(b) && clips.find(other) && clips.count(7)==1,
			"script take then give in one frame invalidates every old same-definition lifetime");
		const auto replacement=clips.projected(7);
		clips.clear();check(clips.reconcile(regiven) && clips.projected(7).generation>replacement.generation,
			"checkpoint reconstruction never reuses a previous physical lifetime");
		clip_ledger full;
		for (unsigned i=0;i<15;++i) check(full.add(full.allocate(7),int(i),70,0),"bounded equal-definition admission");
		const auto original=full.projected(7);
		check(!full.add(full.allocate(7),0,70,0) && full.count()==15 && full.find(original),"capacity rejection preserves all fifteen clips");
		const clip_ledger::definition extra[]{{7,0,70,0},{8,0,80,0}};
		check(!full.reconcile(extra) && full.count()==15 && full.find(original),"external overflow snapshot cannot partially erase inventory");
		bytes={};for (unsigned i=0;i<15;++i) check(s::commit(bytes,100+i,200+i,0,0,1,1),"fill native sparse tables");
		const auto crowded=bytes;
		check(!p::begin_pickup(clips,bytes,9,999,999) && bytes==crowded,"full native tables reject pickup before any unrelated cell can be overwritten");
		// Actual carry operations leave ammunition addressed by the same identities.
		carry::inventory bag;const auto left=full.entries()[0].id,right=full.entries()[1].id;
		const carry::owned_instance pair[]{{left,{true,true}},{right,{true,true}}};
		check(bag.reconcile_instances(pair) && bag.equip(left,hand::left) && bag.equip(right,hand::right),"two clips enter both physical hands");
		const auto left_clip=full.find(left)->loaded,right_clip=full.find(right)->loaded;
		bag.release(left,1,carry::location::back,true,[](const auto&){return false;});
		bag.release(right,2,carry::location::back,true,[](const auto&){return false;});
		check(bag.in_hand(hand::right)->id==left && full.find(left)->loaded==left_clip && full.find(right)->loaded==right_clip,
			"same-model holster exchange never exchanges clip contents");
	}
}
