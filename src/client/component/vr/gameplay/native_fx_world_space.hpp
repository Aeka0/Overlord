#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <vector>
#include "game/assets.hpp"

namespace vr::gameplay::native_fx
{
	// Private immutable descriptors for world-space presentations of authored FX.
	// Native FX still owns simulation, visuals and child spawning. The caller
	// serializes construction and retires the cache only after native asset drain.
	class world_space_cache
	{
		static constexpr std::size_t capacity=128;
		static constexpr int max_elements=64; // Native element indices are six bits.
		struct entry
		{
			const game::FxEffectDef* source{};
			game::FxEffectDef definition{};
			std::vector<game::FxElemDef> elements;
			std::array<std::vector<game::FxElemVisuals>,max_elements> runners;
		};
		using storage=std::vector<std::unique_ptr<entry>>;
		storage entries_;

		static game::FxEffectDef* find(const storage& entries,const game::FxEffectDef* source) noexcept
		{
			for(const auto& e:entries)if(e->source==source)return &e->definition;
			return nullptr;
		}
		game::FxEffectDef* copy(const game::FxEffectDef* source,storage& pending,unsigned depth)
		{
			if(!source)return nullptr;
			if(auto* existing=find(entries_,source))return existing;
			if(auto* existing=find(pending,source))return existing; // Shared/cyclic child graph.
			if(entries_.size()+pending.size()>=capacity || depth>=16)return nullptr;
			const auto loops=source->elemDefCountLooping,emissions=source->elemDefCountEmission,
				shots=source->elemDefCountOneShot;
			if(loops<0 || loops>max_elements || emissions<0 || emissions>max_elements ||
				shots<0 || shots>max_elements)return nullptr;
			const int count=loops+emissions+shots;
			if(count>max_elements || (count && !source->elemDefs))return nullptr;
			auto next=std::make_unique<entry>();auto& e=*next;
			e.source=source;e.definition=*source;
			if(count)e.elements.assign(source->elemDefs,source->elemDefs+count);
			e.definition.elemDefs=count?e.elements.data():nullptr;
			pending.push_back(std::move(next));
			const auto link=[&](game::FxEffectDefRef& ref)
			{
				if(!ref.handle)return true;
				auto* child=copy(ref.handle,pending,depth+1);
				if(!child)return false;ref.handle=child;return true;
			};
			for(int i=0;i<count;++i)
			{
				auto& elem=e.elements[i];
				// H2's model-FX consumer 0x14042EC71..EC81 maps this bit
				// to scene flag 1 (depth hack), even for an oriented world spawn.
				elem.flags&=~game::FX_ELEM_DRAW_WITH_VIEWMODEL;
				if(!link(elem.effectOnImpact) || !link(elem.effectOnDeath) || !link(elem.effectEmitted))return nullptr;
				if(elem.elemType!=game::FX_ELEM_TYPE_RUNNER)continue;
				if(elem.visualCount==1)
				{
					if(!link(elem.visuals.instance.effectDef))return nullptr;
				}
				else if(elem.visualCount)
				{
					if(!elem.visuals.array)return nullptr;
					auto& visuals=e.runners[i];
					visuals.assign(elem.visuals.array,elem.visuals.array+elem.visualCount);
					for(auto& visual:visuals)if(!link(visual.effectDef))return nullptr;
					elem.visuals.array=visuals.data();
				}
			}
			return &e.definition;
		}
	public:
		game::FxEffectDef* get(const game::FxEffectDef* source) noexcept
		{
			// Never publish a partial graph or evict a descriptor retained by FX.
			try
			{
				storage pending;
				auto* result=copy(source,pending,0);
				if(!result)return nullptr;
				entries_.reserve(entries_.size()+pending.size());
				for(auto& e:pending)entries_.push_back(std::move(e));
				return result;
			}
			catch(...){return nullptr;}
		}
		void clear() noexcept {entries_.clear();}
		std::size_t size() const noexcept {return entries_.size();}
	};
}
