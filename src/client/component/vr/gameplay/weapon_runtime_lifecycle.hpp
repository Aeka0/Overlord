#pragma once
#include "weapon_identity.hpp"
#include <array>

namespace vr::gameplay::weapons::runtime_lifecycle
{
	struct ownership_snapshot
	{
		std::array<weapon_identity,15> instances{};
		bool independent{};
		bool accepts(weapon_identity id)const noexcept
		{
			if(!id.weapon)return false;
			if(!independent)return !id.generation;
			for(const auto& current:instances)if(current==id)return true;
			return false;
		}
	};
	template<class Cache,class Scene>
	void publish_scene(Cache& cache,Scene& last,const Scene& value,const ownership_snapshot& ownership) noexcept
	{
		cache.retain([&](weapon_identity id){return ownership.accepts(id);});
		if(value.owner.weapon && (!ownership.independent || ownership.accepts(value.owner.id())))
			if(auto* slot=cache.acquire(value.owner.id()))*slot=value;
		last=value;
	}
	// Restoring the same physical instance preserves its event watermark. A
	// rejected native comparison retains a faulted identity instead of importing
	// a guessed chamber or creating a second escrow. The family owns ammo fields.
	template<class Inventory,class Presentation,class Restore>
	bool restore_transfer(Inventory& inventory,const Presentation& saved,bool compatible,Restore&& restore) noexcept
	{
		typename Inventory::value_type* destination{};bool found{};
		for(auto& item:inventory)if(item.view.active && item.view.owner.id()==saved.owner.id())
		{destination=&item;found=true;break;}
		if(!found)for(auto& item:inventory)if(!item.view.active){destination=&item;found=true;break;}
		if(!found)return false;
		*destination={};destination->view=saved;
		restore(*destination);destination->view.fault=!compatible;
		destination->view.resume_transfer();
		return compatible;
	}
}
