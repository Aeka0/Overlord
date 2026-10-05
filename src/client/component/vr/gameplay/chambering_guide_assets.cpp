#include <std_include.hpp>
#include "chambering_guide_assets.hpp"
#include "chambering_guide.hpp"
#include "weapon_registry.hpp"
#include "native_partition_assets.hpp"
#include "viewmodel_visibility.hpp"
#include "component/scene_models.hpp"
#include "component/scene_rigid_part.hpp"
#include "component/scheduler_context.hpp"
#include "component/vr/settings.hpp"
#include "game/game.hpp"
#include "game/dvars.hpp"

namespace vr::gameplay::weapons::chambering_guide
{
	namespace
	{
		struct prepared
		{
			assets value;
			const profile* definition{};
			std::string_view receiver;
			game::XModel* source{};
			game::XSurface* surfaces{};
		};
		std::array<std::unique_ptr<prepared>,registration_capacity*2> retained;
		std::array<std::atomic<const prepared*>,registration_capacity> selected{};
		// Resolve posed stack copies back to an immutable catalog entry. Geometry
		// is shared across grips with the same receiver and mechanical capability.
		std::array<std::atomic<const profile*>,registration_capacity> requested{};
		std::array<std::atomic<const char*>,registration_capacity> reasons{};
		game::dvar_t* setting{};
		physical_reload::clock::time_point attempted{};
	}
	bool enabled() noexcept{return setting && setting->current.enabled;}
	void initialize()
	{
		setting=dvars::register_bool(settings::chambering_guide.name,settings::chambering_guide.default_value,
			game::DVAR_FLAG_SAVED,"Highlight chambering controls in opaque yellow while a loaded weapon needs chambering");
	}
	assets request(const profile& p) noexcept
	{
		if(!enabled() || (!p.reload && (!p.tube || !supported(*p.tube))) || registered_profiles.size()>registration_capacity)return {};
		for(size_t i=0;i<registered_profiles.size();++i)
		{
			const auto* entry=registered_profiles[i].value;
			if(!entry || entry->receiver!=p.receiver || entry->reload!=p.reload || entry->tube!=p.tube)continue;
			requested[i]=entry;const auto* value=selected[i].load();
			return value && value->definition==entry?value->value:assets{};
		}
		return {};
	}
	void clear() noexcept {for(auto& p:selected)p=nullptr;for(auto& p:requested)p=nullptr;}
	void retire_after_drain() noexcept
	{
		clear();for(auto& p:retained)p.reset();for(auto& p:reasons)p=nullptr;attempted={};
	}
	std::string status()
	{
		std::string out=std::format("chambering_guide={} renderer=opaque_depth_tested ",enabled());
		for(size_t i=0;i<std::min(registered_profiles.size(),registration_capacity);++i)if(const auto* why=reasons[i].load())
			out+=std::format("guide_asset({})=[{}] ",registered_profiles[i].value->receiver,why);
		return out;
	}
	void refresh()
	{
		if(!enabled() || !scheduler::is_executing(scheduler::pipeline::main) || !game::CL_IsCgameInitialized() ||
			!scene_models::ready() || !viewmodel_visibility::ready(part_visibility::rigid_groups))return;
		const auto now=physical_reload::clock::now();
		if(now>=attempted && now-attempted<std::chrono::seconds(1))return;attempted=now;
		for(size_t i=0;i<std::min(registered_profiles.size(),registration_capacity);++i)
		{
			const auto* grip=requested[i].load();if(!grip)continue;
			const auto* receiver=grip->receiver.data();const auto* reload=grip->reload;
			auto* source=game::DB_FindXAssetHeader(game::ASSET_TYPE_XMODEL,receiver,0).model;
			if(!source || !source->name || std::string_view(source->name)!=receiver || !source->boneNames ||
				!source->numBones || source->numBones>256 || source->numLods!=1 || !source->lodInfo[0].surfs)
			{selected[i]=nullptr;reasons[i]="exact receiver unavailable";continue;}
			const prepared* cached{};
			for(const auto& old:retained)if(old && old->definition==grip && old->source==source &&
				old->receiver==receiver && old->surfaces==source->lodInfo[0].surfs){cached=old.get();break;}
			selected[i]=cached;if(cached)continue;
			auto slot=std::find_if(retained.begin(),retained.end(),[](const auto& v){return !v;});
			if(slot==retained.end()){reasons[i]="retained guide capacity reached";continue;}
			auto next=std::make_unique<prepared>();
			reasons[i]="guide geometry preparation rejected";
			next->definition=grip;next->receiver=receiver;next->source=source;next->surfaces=source->lodInfo[0].surfs;
			const auto make_bone=[&](unsigned index,std::string_view name) {
				if(name.empty())return true;
				unsigned bone=256;
				for(unsigned b=0;b<source->numBones;++b)
				{
					const auto* text=game::SL_ConvertToString(source->boneNames[b]);
					if(text && name==text){if(bone!=256)return false;bone=b;}
				}
				scene_models::rigid_part mesh;
				if(bone==256){reasons[i]="guide bone missing or ambiguous";return false;}
				if(!mesh.create_rigid_bone(source,bone)){reasons[i]=mesh.status();return false;}
				auto geometry=opaque_mesh::mesh::create(mesh.model());if(!geometry)return false;
				const auto bind=mesh.bind();
				next->value.bones[index]={std::move(geometry),inverse_reload({{bind[0],bind[1],bind[2]},{bind[3],bind[4],bind[5],bind[6]}})};
				return true;
			};
			const bool split_handle=reload && reload->handle_fold && reload->handle_fold->mesh;
			const bool split_carrier=reload && reload->bolt_partition && reload->bolt_partition->motion.bone==reload->slide_bone;
			bool valid=true;
			if(split_handle || split_carrier)
			{
				const auto split=physical_reload::partition_assets::get(reload);
				valid=bool(split);
				for(unsigned piece=0;piece<(split_handle?2u:1u) && valid;++piece)
				{
					auto geometry=opaque_mesh::mesh::create(split.models[piece]);valid=bool(geometry);
					if(valid)next->value.partition[piece]={std::move(geometry),split.in_part[piece]};
				}
			}
			else valid=make_bone(0,reload?reload->slide_bone:grip->tube->bolt_bone);
			if(valid && reload && reload->handle_fold && !reload->handle_fold->end_bone.empty())valid=make_bone(1,reload->handle_fold->end_bone);
			if(valid && reload)valid=make_bone(3,reload->action_detail_bone);
			if(!valid){if(!reasons[i].load())reasons[i]="action geometry not prepared; original geometry retained";continue;}
			// Optional separately bound paddle. A shared receiver group must never
			// turn the entire gun yellow; its charging handle remains an exact cue.
			const bool catch_ready=!reload || !reload->interaction.receiver_release || make_bone(2,reload->interaction.receiver_release->visual_bone);
			*slot=std::move(next);selected[i]=slot->get();
			reasons[i]=catch_ready?"opaque chambering controls ready":"opaque action ready; paddle geometry unavailable";
		}
	}
}
