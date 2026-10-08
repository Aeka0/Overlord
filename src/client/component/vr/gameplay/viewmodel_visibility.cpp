#include <std_include.hpp>
#include "component/fastfiles.hpp"
#include "viewmodel_visibility.hpp"
#include "rigid_part_visibility.hpp"
#include "immutable_surface_cache.hpp"
#include "skinned_part_visibility.hpp"
#include "component/scene_surface_indices.hpp"
#include "component/scheduler_context.hpp"
#include <utils/native_memory.hpp>
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>
#include <mutex>

namespace vr::gameplay::weapons::viewmodel_visibility
{
	namespace
	{
		constexpr std::uintptr_t hidden_site=0x14071F434, hidden_target=0x140658AB0;
		constexpr std::uintptr_t rigid_site=0x14071F7C1, rigid_target=0x14071E680;
		constexpr std::uintptr_t skin_site=0x14071F879, skin_target=0x14071E020;
		std::atomic_bool alive{true}, hidden_ready{}, rigid_ready{}, skin_ready{};
		thread_local const void* skin_owner{};
		std::atomic_uint64_t filtered{}, rejected{};
		struct frame
		{
			const void* object{}; const void* matrices{}; std::uint32_t epoch{};
			part_mask bits{}; part_visibility mode{};
			int omitted_arm_root{-1};
			std::array<const game::XSurface*,8> hidden_surfaces{};
		};
		std::mutex frames_mutex, variants_mutex;
		std::array<frame,128> frames{};
		size_t sequence{};
		// Immutable copies of METADATA only, never vertices/index buffers/materials.
		// Scene packets retain these pointers across render workers and both eyes.
		// Never recycle an entry while native work could still reference it.
		struct variant
		{
			const game::XSurface* source{};
			game::XSurface original{}, surface{};
			std::array<game::XRigidVertList,max_visibility_groups> original_groups{}, groups{};
			std::uint32_t mask{};
		};
		immutable_surface_cache<variant> variants;
		struct skin_variant
		{
			const game::XSurface* source{}; unsigned bone{};
			unsigned second_bone{256};
			bool subtree{};
			game::XSurface original{}, surface{};
			std::vector<std::uint16_t> indices;
			Microsoft::WRL::ComPtr<ID3D11Buffer> index_buffer,vertex_buffer,blend_buffer;
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> index_view,vertex_view,blend_view;
		};
		// No eviction: native asynchronous skin jobs and stereo draw packets may
		// retain descriptors. Full cache rejects preparation, not in-flight work.
		std::array<std::unique_ptr<skin_variant>,64> skin_variants{};
		size_t skin_variant_count{};
		template<class T> bool read(const void* base, size_t offset, T& out) noexcept
		{
			return base && utils::native_memory::read_bytes(&out,
				static_cast<const std::byte*>(base)+offset,sizeof(out));
		}
		bool find(const void* object, frame& out) noexcept
		{
			const void* matrices{}; std::uint32_t epoch{};
			if (!alive.load() || !object ||
				!read(object,0xa8,matrices) || !read(object,0xb0,epoch)) return false;
			const std::lock_guard lock(frames_mutex);
			for (size_t n=0;n<frames.size();++n)
			{
				const auto& value=frames[(sequence+frames.size()-1-n)%frames.size()];
				if (value.object==object && value.matrices==matrices && value.epoch==epoch)
				{ out=value; return true; }
			}
			return false;
		}
		void hidden_stub(const void* object, std::uint32_t* bits)
		{
			utils::hook::invoke<void>(hidden_target,object,bits);
			frame value;
			if (!bits || !find(object,value) || value.mode!=part_visibility::surface) return;
			for (size_t i=0;i<value.bits.size();++i) bits[i] |= value.bits[i];
		}
		const game::XSurface* filtered_surface(const game::XSurface* source, unsigned bone_base,
			const part_mask& bits) noexcept
		{
			game::XSurface original{};
			if (!read(source,0,original)) { ++rejected; return nullptr; }
			part_mask local{};
			std::memcpy(local.data(),original.partBits,sizeof(local));
			// Always-suppressed cosmetic props must not cause unrelated surfaces
			// to trigger rigid-list reads or count as unsupported every frame.
			if (!surface_intersects(local,bone_base,bits)) return nullptr;
			if ((original.flags & 4) || original.subdivLevelCount ||
				!original.rigidVertLists || !original.rigidVertListCount ||
				original.rigidVertListCount > max_visibility_groups) { ++rejected; return nullptr; }
			std::array<game::XRigidVertList,max_visibility_groups> groups{};
			const size_t bytes=original.rigidVertListCount*sizeof(groups[0]);
			if (!utils::native_memory::read_bytes(groups.data(),original.rigidVertLists,bytes))
			{ ++rejected; return nullptr; }
			std::array<rigid_group_range,max_visibility_groups> ranges{};
			for (size_t i=0;i<original.rigidVertListCount;++i)
				ranges[i]={groups[i].boneOffset,groups[i].vertCount,groups[i].triOffset,groups[i].triCount};
			const auto plan=plan_rigid_visibility({ranges.data(),original.rigidVertListCount},bone_base,
				original.vertCount,original.triCount,bits);
			if (!plan.valid) { ++rejected; return nullptr; }
			if (!plan.hidden_groups) return nullptr;
			const std::lock_guard lock(variants_mutex);
			const auto* cached=variants.get(source,plan.hidden_groups,[&](const variant& v) {
				return std::memcmp(&v.original,&original,sizeof(original))==0 &&
					std::memcmp(v.original_groups.data(),groups.data(),bytes)==0;
			},[&](variant& v) {
				v.source=source;v.original=original;v.surface=original;v.mask=plan.hidden_groups;
				v.original_groups=groups;v.groups=groups;
				for(size_t i=0;i<original.rigidVertListCount;++i)
					if(plan.hidden_groups&(1u<<i))v.groups[i].triCount=0;
				// Preserve source vertex indices and bone transforms of visible groups.
				v.surface.rigidVertLists=v.groups.data();
			});
			if(!cached){++rejected;return nullptr;}return &cached->surface;
		}
		void rigid_stub(const void* object, void* packet, void* history)
		{
			// The original builder owns this packet. No MOD lock across native work.
			utils::hook::invoke<void>(rigid_target,object,packet,history);
			frame value;
			if (!find(object,value)) return;
			const game::XSurface* source{}; std::uint8_t bone_base{};
			if (!read(packet,0x28,source) || !read(packet,0x10,bone_base)) return;
			const bool hidden=source && std::find(value.hidden_surfaces.begin(),value.hidden_surfaces.end(),source)!=value.hidden_surfaces.end();
			if (!hidden && (value.mode==part_visibility::surface ||
				std::all_of(value.bits.begin(),value.bits.end(),[](auto b) { return b==0; }))) return;
			auto bits=value.bits;if (hidden) bits.fill(UINT_MAX);
			if (const auto* replacement=filtered_surface(source,bone_base,bits))
			{
				std::memcpy(static_cast<std::byte*>(packet)+0x28,&replacement,sizeof(replacement));
				++filtered;
			}
		}
		bool skin_stub(void* begin,void* end,unsigned surfaces,unsigned vertices,void* history)
		{
			// Called before CPU/GPU skin-job publication, while SkinSceneDObj owns
			// its local packet builder. Do not race a queued worker by patching the
			// returned scene buffer later. Geometry/skin vertices remain identical.
			frame value;
			const auto first=reinterpret_cast<std::uintptr_t>(begin), last=reinterpret_cast<std::uintptr_t>(end);
			if (find(skin_owner,value) && last>first && last-first<=0x4000)
			{
				std::array<std::byte,0x4000> copy;
				if (utils::native_memory::read_bytes(copy.data(),begin,last-first))
				{
					const auto plan=plan_skin_packets({copy.data(),last-first},surfaces);
					if (plan.valid)
					{
						const std::lock_guard lock(variants_mutex);
						for (size_t i=0;i<plan.count;++i)
						{
							const auto offset=plan.skinned[i]; const game::XSurface* source{};
							std::memcpy(&source,copy.data()+offset+0x28,sizeof(source));
							const auto base=std::to_integer<unsigned>(copy[offset+0x10]);
							for (size_t n=0;n<skin_variant_count;++n)
							{
								const auto& v=*skin_variants[n]; const auto bone=base+v.bone;
								if (!v.subtree && value.mode!=part_visibility::skinned_groups) continue;
								const auto selected=[&](unsigned b){return b<256 && (value.bits[b/32]&(0x80000000u>>(b%32)));};
								const bool root_selected=v.subtree ? value.omitted_arm_root==int(bone) || (value.mode==part_visibility::skinned_groups && selected(bone)) : selected(bone);
								if (v.source!=source || bone>=256 || !root_selected || (v.second_bone<256 && !selected(base+v.second_bone))) continue;
								game::XSurface current{};
								if (!read(source,0,current) || std::memcmp(&current,&v.original,sizeof(current))) continue;
								const auto* replacement=&v.surface;
								std::memcpy(static_cast<std::byte*>(begin)+offset+0x28,&replacement,sizeof(replacement));
								++filtered; break;
							}
						}
					}
					else ++rejected;
				}
			}
			return utils::hook::invoke<bool>(skin_target,begin,end,surfaces,vertices,history);
		}
		bool verify_call(std::uintptr_t site, std::uintptr_t target)
		{
			std::array<std::uint8_t,5> call{0xe8}, mask{}; mask.fill(0xff);
			const auto relative=static_cast<std::int32_t>(target-site-5);
			std::memcpy(call.data()+1,&relative,4);
			return static_cast<bool>(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(site),{call.data(),mask.data(),5}));
		}
	}
	const void* skin_object(const void* object) noexcept
	{ const auto old=skin_owner; skin_owner=object; return old; }
	bool prepare_skinned_part(game::XModel* source,std::string_view name,bool subtree)
	{return prepare_skinned_parts(source,std::span(&name,1),subtree);}
	bool prepare_skinned_parts(game::XModel* source,std::span<const std::string_view> names,bool subtree)
	{
		if (!alive.load() || !skin_ready.load() || names.empty() || names.size()>2 ||
			(names.size()==2 && (!subtree || names[0]==names[1])) || !scheduler::is_executing(scheduler::pipeline::main)) return false;
		game::XModel model{};
		if (!read(source,0,model) || !model.numBones || !model.boneNames || model.numLods!=1 ||
			!model.numsurfs || model.numsurfs>32 || model.lodInfo[0].numsurfs!=model.numsurfs || !model.lodInfo[0].surfs) return false;
		std::array<int,2> selected{-1,-1};
		for (unsigned i=0;i<model.numBones;++i)
		{
			game::scr_string_t id{}; if (!read(model.boneNames+i,0,id)) return false;
			const char* bone=game::SL_ConvertToString(id);
			for(unsigned n=0;n<names.size();++n)if (bone && names[n]==bone) { if (selected[n]>=0) return false; selected[n]=i; }
		}
		std::array<bool,256> members{};
		for(unsigned n=0;n<names.size();++n){if(selected[n]<0)return false;members[selected[n]]=true;}
		const unsigned second=names.size()==2?unsigned(selected[1]):256;
		if (subtree)
		{
			if (!model.parentList || !model.numRootBones) return false;
			for (unsigned i=model.numRootBones;i<model.numBones;++i)
			{
				const auto distance=model.parentList[i-model.numRootBones];
				if (!distance || distance>i) return false;
				members[i]=members[i] || members[i-distance];
			}
		}
		bool found=false;
		for (unsigned i=0;i<model.numsurfs;++i)
		{
			const auto* src=model.lodInfo[0].surfs+i; game::XSurface s{};
			if (!read(src,0,s)) return false;
			bool intersects=false;
			for (unsigned b=0;b<model.numBones;++b) if (members[b] && (static_cast<unsigned>(s.partBits[b/32])&(0x80000000u>>(b%32)))) intersects=true;
			if (!(s.flags&4) || !intersects) continue;
			found=true;
			bool cached=false;
			{ const std::lock_guard lock(variants_mutex);
				for (size_t n=0;n<skin_variant_count;++n) if (skin_variants[n]->source==src &&
					skin_variants[n]->bone==unsigned(selected[0]) && skin_variants[n]->second_bone==second && skin_variants[n]->subtree==subtree && !std::memcmp(&skin_variants[n]->original,&s,sizeof(s))) { cached=true; break; }
				if (!cached && skin_variant_count==skin_variants.size()) return false;
			}
			if (cached) continue;
			if ((s.flags&~7u) || s.subdivLevelCount || s.subdiv || s.blendShapesCount || !s.vertCount || !s.triCount ||
				!s.blendVerts || !s.triIndices || !s.vb0 || !s.indexBuffer) return false;
			size_t words{},vertices{};
			for (size_t n=0;n<8;++n) { if (s.blendVertCounts[n]<0) return false; words+=s.blendVertCounts[n]*(2*n+1); vertices+=s.blendVertCounts[n]; }
			if (vertices!=s.vertCount || words>65535*15) return false;
			std::vector<std::uint16_t> blends(words),indices(size_t(s.triCount)*3);
			if (!utils::native_memory::read_bytes(blends.data(),s.blendVerts,blends.size()*2) ||
				!utils::native_memory::read_bytes(indices.data(),s.triIndices,indices.size()*2)) return false;
			auto part=partition_skin(s.blendVertCounts,blends,indices,s.vertCount,model.numBones,members);
			if (!part.valid || !part.removed || (!subtree && part.retained.empty())) return false;
			// A surface belonging wholly to one arm keeps a legal zero-area
			// triangle so native packet counts/strides remain unchanged.
			if (part.retained.empty()) part.retained={0,0,0};
			auto v=std::make_unique<skin_variant>(); v->source=src; v->bone=selected[0];v->second_bone=second; v->original=v->surface=s;
			v->subtree=subtree;
			v->indices=std::move(part.retained);
			Microsoft::WRL::ComPtr<ID3D11Device> device; s.vb0->GetDevice(&device);
			if (!scene_models::create_surface_indices(device.Get(),s.indexBufferView,std::as_bytes(std::span(v->indices)),v->index_buffer,v->index_view)) return false;
			v->vertex_buffer=s.vb0; v->vertex_view=s.vb0View; v->blend_buffer=s.blendVertsBuffer; v->blend_view=s.blendVertsView;
			v->surface.triCount=static_cast<unsigned short>(v->indices.size()/3);
			v->surface.triIndices=v->surface.triIndices2=reinterpret_cast<game::Face*>(v->indices.data());
			v->surface.indexBuffer=v->index_buffer.Get(); v->surface.indexBufferView=v->index_view.Get();
			const std::lock_guard lock(variants_mutex);
			if (skin_variant_count==skin_variants.size()) return false;
			skin_variants[skin_variant_count++]=std::move(v);
		}
		return found;
	}
	bool ready(part_visibility mode) noexcept
	{ return alive.load() && hidden_ready.load() && (mode==part_visibility::surface || rigid_ready.load()) &&
		(mode!=part_visibility::skinned_groups || skin_ready.load()); }
	void publish(const void* object, const void* matrices, std::uint32_t epoch,
		const part_mask& bits, part_visibility mode, int omitted_arm_root,
		const std::array<const game::XSurface*,8>& hidden_surfaces) noexcept
	{
		const std::lock_guard lock(frames_mutex);
		frames[sequence++ % frames.size()]={object,matrices,epoch,bits,mode,omitted_arm_root,hidden_surfaces};
	}
	std::string status()
	{
		const std::unique_lock lock(variants_mutex,std::try_to_lock);
		if(!lock.owns_lock())return "part_visibility snapshot=busy\n";
		return std::format("part_visibility=surface/rigid_groups/skinned_groups hooks={}/{}/{} variants={}/{} skin_variants={}/{} filtered={} rejected={}\n",
			hidden_ready.load(),rigid_ready.load(),skin_ready.load(),variants.size(),variants.capacity,skin_variant_count,skin_variants.size(),filtered.load(),rejected.load());
	}
	class visibility_component final : public component_interface
	{
		void post_unpack() override
		{
			fastfiles::on_pre_unload([] {
				// The native unload boundary has drained consumers of these borrowed
				// descriptors. New zones must not exhaust the immutable subset cache.
				{ const std::lock_guard lock(frames_mutex); frames={};sequence=0; }
				const std::lock_guard lock(variants_mutex);
				variants.clear();
				for (auto& v:skin_variants) v.reset();
				skin_variant_count=0;
			});
			constexpr std::uint8_t getter[]{0x0f,0x10,0x81,0xb8,0x00,0x00,0x00,0x0f,0x11,0x02};
			std::array<std::uint8_t,sizeof(getter)> mask{}; mask.fill(0xff);
			if (verify_call(hidden_site,hidden_target) &&
				utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(hidden_target),{getter,mask.data(),mask.size()}))
			{ utils::hook::call(hidden_site,hidden_stub); hidden_ready=true; }
			constexpr std::uint8_t prologue[]{0x4c,0x8b,0xdc,0x53,0x55,0x41,0x57,0x48,0x81,0xec,0x90,0x01,0x00,0x00};
			std::array<std::uint8_t,sizeof(prologue)> rigid_mask{}; rigid_mask.fill(0xff);
			if (verify_call(rigid_site,rigid_target) &&
				utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(rigid_target),{prologue,rigid_mask.data(),rigid_mask.size()}))
			{ utils::hook::call(rigid_site,rigid_stub); rigid_ready=true; }
			// The existing SkinSceneDObj boundary provides the scoped object. This
			// native call occurs before either CPU or GPU skin-job publication.
			// Independent held objects require these arm subsets in every build.
			// Omitting the hook prevents prepare_hands(), bringing back the native
			// foreground depth, equip transitions and scope-ADS model visibility.
			if (verify_call(skin_site,skin_target) && !utils::hook::is_relatively_far(
				reinterpret_cast<void*>(skin_site),reinterpret_cast<void*>(skin_stub)))
			{ utils::hook::call(skin_site,skin_stub); skin_ready=true; }
		}
		void pre_destroy() override { alive=false; }
	};
}
REGISTER_COMPONENT(vr::gameplay::weapons::viewmodel_visibility::visibility_component)
