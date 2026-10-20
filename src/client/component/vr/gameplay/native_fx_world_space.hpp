#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <vector>
#include "game/assets.hpp"
#include "native_fx_checkpoint_policy.hpp"

namespace vr::gameplay::native_fx
{
	// Private immutable descriptors for world-space presentations of authored FX.
	// Native FX still owns simulation, visuals and child spawning. The caller
	// serializes construction and retires the cache only after native asset drain.
	class world_space_cache
	{
	  public:
		static constexpr std::size_t capacity = 512;
		static constexpr unsigned surface_count = 53;
		using surface_resolver = game::FxEffectDef* (*)(const game::FxEffectDef*, unsigned);
		struct model_variant
		{
			unsigned identity{};
			game::XModel* model{};
		};

	  private:
		static constexpr int max_elements=64; // Native element indices are six bits.
		struct entry
		{
			const game::FxEffectDef* source{};
			model_variant replacement{};
			game::FxEffectDef definition{};
			std::vector<game::FxElemDef> elements;
			std::array<std::vector<game::FxElemVisuals>,max_elements> runners;
			std::array<std::vector<game::FxElemVisStateSample>, max_elements> samples;
			std::string checkpoint_name;
		};
		using storage=std::vector<std::unique_ptr<entry>>;
		storage entries_;

		static game::FxEffectDef* find(const storage& entries,
		                               const game::FxEffectDef* source,
		                               model_variant replacement) noexcept
		{
			for (const auto& e : entries)
				if (e->source == source && e->replacement.identity == replacement.identity &&
				    e->replacement.model == replacement.model)
					return &e->definition;
			return nullptr;
		}
		game::FxEffectDef* copy(const game::FxEffectDef* source,
		                        storage& pending,
		                        unsigned depth,
		                        model_variant replacement,
		                        surface_resolver surfaces)
		{
			if(!source || !source->name)return nullptr;
			if (auto* existing = find(entries_, source, replacement))
				return existing;
			if (auto* existing = find(pending, source, replacement))
				return existing; // Shared/cyclic child graph.
			if(entries_.size()+pending.size()>=capacity || depth>=16)return nullptr;
			const auto loops=source->elemDefCountLooping,emissions=source->elemDefCountEmission,
				shots=source->elemDefCountOneShot;
			if(loops<0 || loops>max_elements || emissions<0 || emissions>max_elements ||
				shots<0 || shots>max_elements)return nullptr;
			const int count=loops+emissions+shots;
			if(count>max_elements || (count && !source->elemDefs))return nullptr;
			auto next=std::make_unique<entry>();auto& e=*next;
			e.source = source;
			e.definition = *source;
			e.replacement = replacement;
			e.checkpoint_name = checkpoint::name(
			    replacement.identity ? checkpoint::variant::casing : checkpoint::variant::world,
			    {source->name, strnlen_s(source->name, checkpoint::source_name_limit + 1)},
			    replacement.identity);
			if(e.checkpoint_name.empty())return nullptr;
			e.definition.name=e.checkpoint_name.c_str();
			if(count)e.elements.assign(source->elemDefs,source->elemDefs+count);
			e.definition.elemDefs=count?e.elements.data():nullptr;
			pending.push_back(std::move(next));
			const auto link=[&](game::FxEffectDefRef& ref)
			{
				if(!ref.handle)return true;
				auto* child = copy(ref.handle, pending, depth + 1, replacement, surfaces);
				if(!child)return false;ref.handle=child;return true;
			};
			for(int i=0;i<count;++i)
			{
				auto& elem=e.elements[i];
				// H2's model-FX consumer 0x14042EC71..EC81 maps this bit
				// to scene flag 1 (depth hack), even for an oriented world spawn.
				elem.flags&=~game::FX_ELEM_DRAW_WITH_VIEWMODEL;
				if (surfaces && (elem.flags & game::FX_ELEM_SPAWN_IMPACT_FX_WITH_SURFACE_NAME) &&
				    elem.effectOnImpact.handle)
				{
					// H2 selects these through a separate 8 x 53 material table,
					// not through the ordinary linked child graph. Prepare them
					// before any particle or checkpoint worker can consume them.
					for (unsigned surface = 0; surface < surface_count; ++surface)
						if (auto* child = surfaces(elem.effectOnImpact.handle, surface);
						    child && !copy(child, pending, depth + 1, replacement, surfaces))
							return nullptr;
				}
				if (replacement.model && elem.elemType == game::FX_ELEM_TYPE_MODEL)
				{
					// Reviewed shell FX use particle collision, not an XModel rigid
					// body. Never borrow the receiver's physics metadata.
					if (!elem.visualCount || !elem.visSamples ||
					    (elem.flags & game::FX_ELEM_USE_MODEL_PHYSICS))
						return nullptr;
					if (elem.visualCount == 1)
						elem.visuals.instance.model = replacement.model;
					else
					{
						if (!elem.visuals.array)
							return nullptr;
						auto& visuals = e.runners[i];
						visuals.assign(elem.visuals.array, elem.visuals.array + elem.visualCount);
						for (auto& visual : visuals)
							visual.model = replacement.model;
						elem.visuals.array = visuals.data();
					}
					auto& samples = e.samples[i];
					samples.assign(elem.visSamples,
					               elem.visSamples + unsigned(elem.visStateIntervalCount) + 1);
					for (auto& sample : samples)
					{
						sample.base.scale = 1;
						sample.amplitude.scale = 0;
						for (unsigned a = 0; a < 2; ++a)
						{
							sample.base.size[a] = 1;
							sample.amplitude.size[a] = 0;
						}
					}
					elem.visSamples = samples.data();
					elem.flags &= ~game::FX_ELEM_NONUNIFORM_SCALE;
					elem.collBounds = replacement.model->bounds;
					elem.spawnFrustumCullRadius =
					    std::max(elem.spawnFrustumCullRadius, replacement.model->radius);
				}
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
	  game::FxEffectDef* get(const game::FxEffectDef* source,
		                     model_variant replacement = {},
		                     surface_resolver surfaces = nullptr) noexcept
	  {
		  if (bool(replacement.identity) != bool(replacement.model))
			  return nullptr;
		  // Never publish a partial graph or evict a descriptor retained by FX.
		  try
		  {
			  storage pending;
			  auto* result = copy(source, pending, 0, replacement, surfaces);
			  if (!result)
				  return nullptr;
			  entries_.reserve(entries_.size() + pending.size());
			  for (auto& e : pending)
				  entries_.push_back(std::move(e));
			  return result;
		  }
		  catch (...)
		  {
			  return nullptr;
		  }
	  }
		void clear() noexcept {entries_.clear();}
		std::size_t size() const noexcept {return entries_.size();}
		std::array<game::FxEffectDef*,capacity> checkpoint_definitions() const noexcept
		{
			std::array<game::FxEffectDef*,capacity> result{};
			for(std::size_t i=0;i<entries_.size();++i)result[i]=&entries_[i]->definition;
			return result;
		}
	};
}
