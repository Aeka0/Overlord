#include <std_include.hpp>
#include "native_carry_model.hpp"
#include "native_carry.hpp"
#include "component/vr/gameplay/hands/pose_math.hpp"
#include "special_melee.hpp"
#include "pickup_visibility.hpp"
#include <utils/native_memory.hpp>
#include "component/scheduler.hpp"
#include "component/scene_rigid_part.hpp"
#include "component/scene_models.hpp"
#include "component/fastfiles.hpp"
#include "loader/component_loader.hpp"
#include "game/game.hpp"

namespace vr::gameplay::weapons::native_carry
{
	namespace
	{
		struct cached {game::XModel* root{};model_geometry value{};interaction::pickup_surface_samples surface{};bool sampled{};};
		std::array<cached,512> cache{};
		constexpr std::array knife_names{"viewmodel_commando_knife","viewmodel_commando_knife_bloody","wpn_h1_melee_rifle_bayonet_vm"};
		std::array<std::unique_ptr<scene_models::rigid_part>,3> knife_parts;
		std::array<std::atomic<game::XModel*>,3> knife_models{};
		std::chrono::steady_clock::time_point knife_attempt{};
		void prepare_knives()
		{
			const auto now=std::chrono::steady_clock::now();
			if(!game::CL_IsCgameInitialized() || !scene_models::ready() || now<knife_attempt)return;
			knife_attempt=now+1s;
			for(std::size_t i=0;i<knife_names.size();++i)
			{
				if(knife_models[i])continue;
				auto* source=game::DB_FindXAssetHeader(game::ASSET_TYPE_XMODEL,knife_names[i],0).model;
				if(!source || !source->name || std::string_view(source->name)!=knife_names[i] ||
					source->numBones!=special_melee::bone_count(knife_names[i]) || !source->boneNames)continue;
				const auto* root=game::SL_ConvertToString(source->boneNames[0]);
				if(!root || std::string_view(root)!=special_melee::root(knife_names[i]))continue;
				auto part=std::make_unique<scene_models::rigid_part>();
				// The bayonet's tag_clip is its sheath, moved away by the stock idle.
				// Every blade vertex belongs to the root; keep only that rigid group.
				if(!part->create(source,0))continue;
				const std::array<scene_models::runtime_model,1> identity{{{part->model(),source}}};
				if(!scene_models::register_runtime_models(identity))continue;
				knife_parts[i]=std::move(part);knife_models[i]=knife_parts[i]->model();
			}
		}
		bool finite(hands::vec p) noexcept
		{return std::all_of(p.begin(),p.end(),[](float x){return std::isfinite(x) && std::abs(x)<10000;});}
		hands::anchor bind(const game::DObjAnimMat& p) noexcept
		{return {{p.trans[0],p.trans[1],p.trans[2]},hands::normalize({p.quat[0],p.quat[1],p.quat[2],p.quat[3]})};}
		model_geometry expand(game::XModel* root)
		{
			using namespace hands;using namespace hands::pose_math;
			model_geometry out;
			std::array<game::XModel*,64> pending{},seen{};std::size_t todo=1,visited{};
			pending[0]=root;
			while (todo)
			{
				auto* model=pending[--todo];if (!model || visited==seen.size()) return {};
				for (std::size_t i=0;i<visited;++i) if (seen[i]==model) return {}; // cycles/duplicate geometry
				seen[visited++]=model;
				if (model->numCompositeModels)
				{
					if (!model->compositeModels || model->numCompositeModels>32 || todo+model->numCompositeModels>pending.size()) return {};
					// Native composite order supplies the receiver before attachments.
					for (int i=model->numCompositeModels-1;i>=0;--i) pending[todo++]=model->compositeModels[i];
					if (!model->numBones) continue;
				}
				if (!model->numBones || !model->numRootBones || !model->baseMat || !model->boneNames || out.count==out.parts.size()) return {};
				anchor local{};
				const auto root_name=model->boneNames[0];
				bool attached=false;
				for (std::size_t p=0;p<out.count && !attached;++p)
					for (int b=0;b<out.parts[p].model->numBones;++b) if (out.parts[p].model->boneNames[b]==root_name)
					{
						local=compose(compose(out.parts[p].local,bind(out.parts[p].model->baseMat[b])),inverse(bind(model->baseMat[0])));
						attached=true;break;
					}
				if (out.count && !attached) return {};
				const auto& bounds=model->bounds;
				const vec center{bounds.midPoint[0],bounds.midPoint[1],bounds.midPoint[2]},extent{bounds.halfSize[0],bounds.halfSize[1],bounds.halfSize[2]};
				if (!finite(center) || !finite(extent) || std::any_of(extent.begin(),extent.end(),[](float x){return x<=0 || x>200;})) return {};
				for (unsigned corner=0;corner<8;++corner)
				{
					vec v=center;for (int axis=0;axis<3;++axis) v[axis]+=((corner>>axis)&1 ? 1 : -1)*extent[axis];
					v=add(local.position,rotate(local.rotation,v));if (!finite(v)) return {};
					if (!out.count && !corner) out.low=out.high=v;
					else for (int axis=0;axis<3;++axis) {out.low[axis]=std::min(out.low[axis],v[axis]);out.high[axis]=std::max(out.high[axis],v[axis]);}
				}
				for (int b=0;b<model->numBones;++b)
				{
					const auto* name=game::SL_ConvertToString(model->boneNames[b]);
					if (name && std::string_view(name)=="tag_flash") {out.muzzle=compose(local,bind(model->baseMat[b]));out.has_muzzle=true;}
					if (name && std::string_view(name)=="tag_brass") {out.brass=compose(local,bind(model->baseMat[b]));out.has_brass=true;}
				}
				out.parts[out.count++]={model,local};
			}
			out.valid=out.count>0;out.native_root=out.count ? out.parts[0].model : nullptr;return out;
		}
	}
	model_geometry geometry(std::uint32_t token) noexcept
	{
		if (!scheduler::is_executing(scheduler::pipeline::server) || token>=cache.size()) return {};
		auto* source=world_model(token);if (!source) return {};
		auto* root=source;
		if(source->name)for(std::size_t i=0;i<knife_names.size();++i)if(std::string_view(source->name)==knife_names[i])
		{root=knife_models[i].load();if(!root)return {};break;}
		auto& value=cache[token];if (value.root!=root)
		{
			value={};value.root=root;value.value=expand(root);
			// Composite DObjs begin with their flattened receiver, not the outer
			// composite asset. Only a replacement subset needs a distinct source.
			if(root!=source)value.value.native_root=source;
		}
		return value.value;
	}
	std::span<const hands::vec> pickup_surface(std::uint32_t token) try
	{
		const auto model=geometry(token);if(!model.valid || token>=cache.size())return {};
		auto& entry=cache[token];if(entry.sampled)return entry.surface.values();entry.sampled=true;
		interaction::pickup_surface_samples points;std::size_t visited{},triangles{},surfaces{};
		for(std::size_t p=0;p<model.count;++p)
		{
			const auto& part=model.parts[p];const auto& lod=part.model->lodInfo[0];
			if(!lod.surfs || lod.numsurfs>256)return {};
			for(unsigned s=0;s<lod.numsurfs;++s)
			{
				if(++surfaces>256)return {};
				const auto& surface=lod.surfs[s];
				// Dynamic/skinned straps are not stable pickup witnesses. Rigid
				// weapon vertices retain the same bind-space placement as the model.
				if(surface.flags&~3u || surface.subdiv || surface.blendShapesCount)continue;
				if(!surface.vertCount || !surface.triCount || !surface.verts0.packedVerts0 || !surface.triIndices)return {};
				visited+=surface.vertCount;triangles+=surface.triCount;
				if(visited>131072 || triangles>262144)return {};
				std::vector<game::GfxPackedVertex> vertices(surface.vertCount);
				std::vector<game::Face> faces(surface.triCount);
				for(std::size_t first=0;first<vertices.size();first+=16384)
					if(!utils::native_memory::read_bytes(vertices.data()+first,surface.verts0.packedVerts0+first,
						std::min<std::size_t>(16384,vertices.size()-first)*sizeof(vertices[0])))return {};
				if(!utils::native_memory::read_bytes(faces.data(),surface.triIndices,faces.size()*sizeof(faces[0])))return {};
				std::vector<bool> used(vertices.size());
				for(const auto& face:faces)for(const auto index:{face.v1,face.v2,face.v3})
				{
					if(index>=vertices.size())return {};
					if(used[index])continue;used[index]=true;
					const auto& xyz=vertices[index].xyz;
					const auto point=hands::add(part.local.position,hands::rotate(part.local.rotation,{xyz[0],xyz[1],xyz[2]}));
					if(!points.add(point))return {};
				}
			}
		}
		entry.surface=points;return entry.surface.values();
	}
	catch(const std::exception&) {return {};}
	void reset_model_cache() noexcept {for(auto& entry:cache)entry={};}
	class model_component final:public component_interface
	{
		void post_unpack() override
		{
			scheduler::loop(prepare_knives,scheduler::pipeline::main);
			fastfiles::on_pre_unload([] {
				// Native unload has drained readers/render jobs; never free a subset
				// from the server's ordinary inventory/cache reset.
				for(auto& model:knife_models)model=nullptr;
				for(auto& part:knife_parts)part.reset();
				knife_attempt={};reset_model_cache();
			});
		}
	};
}
REGISTER_COMPONENT(vr::gameplay::weapons::native_carry::model_component)
