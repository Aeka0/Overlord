#include <std_include.hpp>
#include "native_partition_assets.hpp"
#include "weapon_reload_profiles.hpp"
#include "component/scene_models.hpp"
#include "component/scene_rigid_part.hpp"
#include "component/scheduler_context.hpp"
#include <utils/native_memory.hpp>
#include "game/game.hpp"

namespace vr::gameplay::weapons::physical_reload::partition_assets
{
	namespace
	{
		struct prepared_parts
		{
			std::array<scene_models::rigid_part,2> parts;
			asset value;
			const reload_profile* definition{};game::XModel* source{};game::XSurface* surfaces{};
		};
		std::array<std::unique_ptr<prepared_parts>,reload_profiles.capacity()> retained;
		std::array<std::atomic<const prepared_parts*>,reload_profiles.capacity()> selected{};
		std::array<std::atomic<const char*>,reload_profiles.capacity()> reasons{};
		std::array<clock::time_point,reload_profiles.capacity()> attempts{};
		template<class T> bool read(const T* from,T& to) noexcept
		{return utils::native_memory::read_bytes(&to,from,sizeof(T));}
	}
	asset get(const reload_profile* p) noexcept
	{
		const auto i=reload_profile_index(p);const auto* v=i<reload_profiles.size()?selected[i].load():nullptr;
		asset out;if(v){out.models=v->value.models;out.in_part=v->value.in_part;}return out;
	}
	const char* status(const reload_profile* p) noexcept
	{
		const auto i=reload_profile_index(p);const auto* reason=i<reload_profiles.size()?reasons[i].load():nullptr;
		return reason?reason:"partition mesh not prepared";
	}
	void clear() noexcept{for(auto& p:selected)p=nullptr;}
	void retire_after_drain() noexcept
	{clear();for(auto& p:retained)p.reset();attempts={};for(auto& p:reasons)p=nullptr;}
	void refresh()
	{
		if(!scheduler::is_executing(scheduler::pipeline::main))return;
		if(!game::CL_IsCgameInitialized()){clear();return;}
		for(size_t i=0;i<reload_profiles.size();++i)
		{
			const auto* p=reload_profiles[i];const auto* fold=p->handle_fold;
			const auto* mesh=partition_mesh(*p);if(!mesh)continue;
			if(!valid_partition_profile(*p)){selected[i]=nullptr;reasons[i]="invalid partition mesh descriptor";continue;}
			const auto& recipe=*mesh;
			auto* source=game::DB_FindXAssetHeader(game::ASSET_TYPE_XMODEL,recipe.source,0).model;
			game::XModel model;
			if(!source || !read(source,model) || !model.name || std::string_view(model.name)!=recipe.source ||
				model.numBones!=recipe.bones || model.numsurfs!=recipe.surfaces.size() || model.numLods!=1 ||
				!model.lodInfo[0].surfs || !model.boneNames || !model.baseMat)
			{selected[i]=nullptr;reasons[i]="partition mesh identity/topology unavailable";continue;}
			const prepared_parts* cached{};for(const auto& old:retained)
				if(old && old->definition==p && old->source==source && old->surfaces==model.lodInfo[0].surfs){cached=old.get();break;}
			selected[i]=cached;if(cached){reasons[i]="source remainder and moving piece ready";continue;}
			const auto now=clock::now();if(now>=attempts[i] && now-attempts[i]<std::chrono::seconds(1))continue;attempts[i]=now;
			bool valid=true;unsigned bone=256;
			for(unsigned b=0;b<model.numBones;++b)
			{
				const auto* name=game::SL_ConvertToString(model.boneNames[b]);
				if(!name){valid=false;break;}
				if((p->bolt_partition?p->bolt_partition->motion.bone:p->slide_bone)==name){if(bone!=256)valid=false;bone=b;}
			}
			for(unsigned n=0;n<model.numsurfs && valid;++n)
			{
				game::XSurface s;valid=read(model.lodInfo[0].surfs+n,s) && s.vertCount==recipe.surfaces[n][0] && s.triCount==recipe.surfaces[n][1];
			}
			if(!valid || bone==256){reasons[i]="partition mesh surface/bone contract rejected";continue;}
			auto slot=std::find_if(retained.begin(),retained.end(),[](const auto& p){return !p;});
			if(slot==retained.end()){reasons[i]="partition mesh retained capacity reached";continue;}
			auto next=std::make_unique<prepared_parts>();
			for(unsigned part=0;part<2;++part)
			{
				if(!next->parts[part].create_face_partition(source,bone,recipe.moving_faces,part==1))
				{valid=false;reasons[i]=next->parts[part].status();break;}
				next->value.models[part]=next->parts[part].model();
				const auto bind=next->parts[part].bind();hands::anchor parent{{bind[0],bind[1],bind[2]},{bind[3],bind[4],bind[5],bind[6]}};
				if(part && fold)parent=compose_reload(parent,fold->pivot);
				next->value.in_part[part]=inverse_reload(parent);
			}
			if(!valid)continue;
			const auto& bounds=next->value.models[1]->bounds;
			for(unsigned axis=0;axis<3;++axis)
				valid=valid && std::abs(bounds.midPoint[axis]-bounds.halfSize[axis]-recipe.moving_low[axis])<.02f &&
					std::abs(bounds.midPoint[axis]+bounds.halfSize[axis]-recipe.moving_high[axis])<.02f;
			if(!valid){reasons[i]="partition moving-piece geometry bounds rejected";continue;}
			std::array<scene_models::runtime_model,2> identities;
			for(unsigned part=0;part<2;++part)identities[part]={next->value.models[part],source};
			if(!scene_models::register_runtime_models(identities)){reasons[i]="partition mesh identity registration rejected";continue;}
			next->definition=p;next->source=source;next->surfaces=model.lodInfo[0].surfs;
			*slot=std::move(next);selected[i]=slot->get();reasons[i]="source remainder and moving piece ready";
		}
	}
}
