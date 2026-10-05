#include <std_include.hpp>
#include "native_magazine_assets.hpp"
#include "weapon_reload_profiles.hpp"
#include "viewmodel_visibility.hpp"
#include "component/scene_models.hpp"
#include "component/scene_rigid_part.hpp"
#include "component/scheduler_context.hpp"
#include <utils/native_memory.hpp>
#include "game/game.hpp"

namespace vr::gameplay::weapons::physical_reload::magazine_assets
{
	namespace
	{
		struct subset
		{
			std::array<scene_models::rigid_part,12> parts;
			std::array<asset,12> views;
			scene_models::rigid_part round;
			asset round_view{};
			const reload_profile* definition{};
			game::XModel* source{};
			game::XSurface* surfaces{};
		};
		// Native queued packets borrow immutable descriptors. No in-flight cache
		// eviction or descriptor recycling before the native asset-unload barrier.
		std::array<std::unique_ptr<subset>,reload_profiles.capacity()*2> retained;
		std::array<std::atomic<const subset*>,reload_profiles.capacity()> selected{};
		std::array<std::atomic<game::XModel*>,reload_profiles.capacity()> ordinary{};
		std::array<std::atomic<const char*>,reload_profiles.capacity()> reasons{};
		std::array<clock::time_point,reload_profiles.capacity()> last_attempt{};
		bool fill_topology(game::XModel* source,const magazine_fill_recipe& recipe)
		{
			game::XModel m;
			if(!valid_magazine_fill(recipe) || !utils::native_memory::read_bytes(&m,source,sizeof(m)) ||
				m.numBones!=recipe.bones || m.numsurfs!=recipe.surfaces.size() || m.numLods!=1 || !m.lodInfo[0].surfs)return false;
			for(size_t n=0;n<recipe.surfaces.size();++n)
			{
				game::XSurface s;
				if(!utils::native_memory::read_bytes(&s,m.lodInfo[0].surfs+n,sizeof(s)) ||
					s.vertCount!=recipe.surfaces[n][0] || s.triCount!=recipe.surfaces[n][1])return false;
			}
			return true;
		}
	}
	asset get(const reload_profile* definition,int rounds) noexcept
	{
		const auto i=reload_profile_index(definition);
		if (i>=reload_profiles.size() || rounds<0) return {};
		if (!definition->rigid_magazine_source) return {ordinary[i].load(),definition->rigid_in_magazine};
		const auto* value=selected[i].load();
		return value ? value->views[definition->magazine_subset(rounds)] : asset{};
	}
	const char* status(const reload_profile* definition) noexcept
	{
		const auto i=reload_profile_index(definition);
		const auto* reason=i<reload_profiles.size() ? reasons[i].load() : nullptr;
		return reason ? reason : "not prepared";
	}
	asset cartridge(const reload_profile* definition) noexcept
	{
		const auto i=reload_profile_index(definition);
		const auto* value=i<reload_profiles.size() ? selected[i].load() : nullptr;
		return value ? value->round_view : asset{};
	}
	void clear() noexcept
	{ for (auto& value:selected) value=nullptr; for (auto& value:ordinary) value=nullptr; }
	void retire_after_drain() noexcept
	{
		clear();
		for (auto& value:retained) value.reset();
		for (auto& reason:reasons) reason="waiting for loaded assets after retirement";
		last_attempt={};
	}
	void refresh()
	{
		if (!scheduler::is_executing(scheduler::pipeline::main)) return;
		// Checkpoints reset player/time, not necessarily the native assets. Reuse
		// immutable subsets until the actual asset owner retires their resources.
		if (!game::CL_IsCgameInitialized() || !game::g_entities[0].client) { clear(); return; }
		for (size_t i=0;i<reload_profiles.size();++i)
		{
			const auto& definition=*reload_profiles[i];
			const char* name=definition.rigid_magazine_source ? definition.rigid_magazine_source : definition.magazine_model;
			auto* source=name ? game::DB_FindXAssetHeader(game::ASSET_TYPE_XMODEL,name,0).model : nullptr;
			if (!source || !source->name || std::string_view(source->name)!=name)
			{ selected[i]=nullptr; ordinary[i]=nullptr; reasons[i]="required native asset not loaded"; continue; }
			if (!definition.rigid_magazine_source)
			{
				auto* model=source->numBones==1 ? source : nullptr;
				if (model && definition.skinned_receiver)
				{
					auto* receiver=game::DB_FindXAssetHeader(game::ASSET_TYPE_XMODEL,definition.skinned_receiver,0).model;
					if (!receiver || !receiver->name || std::string_view(receiver->name)!=definition.skinned_receiver ||
						!viewmodel_visibility::prepare_skinned_part(receiver,definition.magazine_bone)) model=nullptr;
				}
				ordinary[i]=model; reasons[i]=model ? "native independent magazine ready" : "native magazine/visibility rejected";
				continue;
			}
			const subset* cached{};
			for (const auto& value:retained)
				if (value && value->source==source && value->surfaces==source->lodInfo[0].surfs &&
					value->definition==&definition) { cached=value.get(); break; }
			selected[i]=cached;
			if (cached) { reasons[i]=definition.magazine_fill()?"counted magazine 0/1/2/3 ready":"exact receiver magazine/round subsets ready"; continue; }
			if (!viewmodel_visibility::ready(part_visibility::rigid_groups) || !scene_models::ready()) continue;
			const auto now=clock::now();
			if (now>=last_attempt[i] && now-last_attempt[i]<std::chrono::seconds(1)) continue;
			last_attempt[i]=now;
			const auto recipe=definition.magazine_mesh();
			if (!recipe || !source->boneNames || source->numBones>256) continue;
			const auto* fill=definition.magazine_fill();
			if(fill && !fill_topology(source,*fill))
			{reasons[i]="counted magazine topology rejected";continue;}
			const auto count=recipe.subsets;
			std::array<unsigned,16> bones; bones.fill(256);
			bool valid=true;
			for (unsigned b=0;b<source->numBones;++b)
			{
				const auto* bone_name=game::SL_ConvertToString(source->boneNames[b]);
				if (!bone_name) { valid=false; break; }
				for (size_t k=0;k<recipe.count;++k) if (recipe.names[k]==bone_name)
				{ if (bones[k]!=256) valid=false; bones[k]=b; }
			}
			if (!valid || std::find(bones.begin(),bones.begin()+recipe.count,256)!=bones.begin()+recipe.count)
			{ reasons[i]="magazine/round skeleton rejected"; continue; }
			auto slot=std::find_if(retained.begin(),retained.end(),[](const auto& value) { return !value; });
			if (slot==retained.end()) { reasons[i]="retained magazine asset capacity reached"; continue; }
			auto value=std::make_unique<subset>();
			std::array<scene_models::runtime_model,13> identities;
			size_t identity_count=count;
			for (size_t k=0;k<count;++k)
			{
				auto& part=value->parts[k];
				std::array<unsigned,16> selection{};
				for(size_t n=0;n<recipe.selected_count(k);++n)selection[n]=bones[recipe.selected_index(k,n)];
				const bool created=fill ? part.create_face_partition(source,bones[0],std::span(bones).first(recipe.count),fill->faces[k]) :
					part.create(source,bones[0],std::span(selection).first(recipe.selected_count(k)));
				if (!created)
				{ reasons[i]=part.status(); valid=false; break; }
				if(fill)
				{
					const auto& b=part.model()->bounds;
					for(unsigned axis=0;axis<3;++axis)
						valid=valid && std::abs(b.midPoint[axis]-b.halfSize[axis]-fill->low[k][axis])<.02f &&
							std::abs(b.midPoint[axis]+b.halfSize[axis]-fill->high[k][axis])<.02f;
					if(!valid){reasons[i]="counted magazine geometry bounds rejected";break;}
				}
				const auto bind=part.bind();
				value->views[k]={part.model(),inverse_reload({{bind[0],bind[1],bind[2]},{bind[3],bind[4],bind[5],bind[6]}})};
				identities[k]={part.model(),part.source()};
			}
			if (!valid) continue;
			if (definition.interaction.manual_bolt)
			{
				auto& round=value->round;
				if (!round.create(source,bones[recipe.body_count],std::span(bones).subspan(recipe.body_count,1))) {reasons[i]=round.status();continue;}
				const auto bind=round.bind();
				value->round_view={round.model(),inverse_reload({{bind[0],bind[1],bind[2]},{bind[3],bind[4],bind[5],bind[6]}})};
				identities[identity_count++]={round.model(),round.source()};
			}
			for (size_t k=count;k<value->views.size();++k) value->views[k]=value->views[count-1];
			if (!scene_models::register_runtime_models(std::span(identities).first(identity_count)))
			{ reasons[i]="native magazine subset identity registration rejected"; continue; }
			value->definition=&definition; value->source=source; value->surfaces=source->lodInfo[0].surfs;
			*slot=std::move(value); selected[i]=slot->get(); reasons[i]=fill ? "counted magazine 0/1/2/3 ready" : "exact receiver magazine/round subsets ready";
		}
	}
}
