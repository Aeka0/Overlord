#include <std_include.hpp>
#include "component/scene_model_record.hpp"
#include "launcher_presenter.hpp"
#include "falling_item_presenter.hpp"
#include "component/vr/gameplay/hand_pose_mirror.hpp"
#include "physical_reload_geometry.hpp"
#include "weapon_render_pose.hpp"
#include "viewmodel_visibility.hpp"
#include <utils/native_memory.hpp>
#include "component/scene_models.hpp"
#include "component/scene_pose_match.hpp"
#include "component/scene_rigid_part.hpp"
#include "component/fastfiles.hpp"
#include "component/scheduler.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <mutex>

namespace vr::gameplay::weapons::launcher
{
	namespace
	{
		using namespace hands;using namespace hands::pose_math;
		struct asset {scene_models::rigid_part rocket;game::XModel* source{};game::XSurface* surfaces{};};
		std::array<std::unique_ptr<asset>,16> retained;std::atomic<asset*> model{};
		std::mutex mutex;std::atomic_bool alive{true};
		struct frame {presentation view{};asset* mesh{};std::uintptr_t object{},matrices{};std::uint32_t epoch{};anchor local{},world{};bool visible{},held{};};
		std::array<frame,15> latest{};std::array<frame,128> poses{};size_t cursor{};
		struct drop {asset* mesh{};anchor world{};controller_input::clock::time_point at{};std::uint64_t reference{};float units{};};
		std::array<drop,16> drops{};size_t drop_cursor{};
		std::array<unsigned short,15> lighting{};
		anchor bind(asset& a){auto b=a.rocket.bind();return {{b[0],b[1],b[2]},normalize({b[3],b[4],b[5],b[6]})};}
		void refresh()
		{
			if(!alive || !game::CL_IsCgameInitialized() || !scene_models::ready() || !viewmodel_visibility::ready(part_visibility::rigid_groups))return;
			auto* source=game::DB_FindXAssetHeader(game::ASSET_TYPE_XMODEL,"h2_viewmodel_rpg7_rocket",0).model;
			if(!source || !source->name || std::string_view(source->name)!="h2_viewmodel_rpg7_rocket" || source->numBones!=2 || !source->boneNames){model=nullptr;return;}
			const auto* root_name=game::SL_ConvertToString(source->boneNames[0]);
			if(!root_name || std::string_view(root_name)!="tag_clip"){model=nullptr;return;}
			for(auto& a:retained)if(a && a->source==source && a->surfaces==source->lodInfo[0].surfs){model=a.get();return;}
			for(auto& a:retained)if(!a)
			{
				auto next=std::make_unique<asset>();if(!next->rocket.create(source,0))return;
				const std::array<scene_models::runtime_model,1> identity{{{next->rocket.model(),source}}};
				if(!scene_models::register_runtime_models(identity))return;
				next->source=source;next->surfaces=source->lodInfo[0].surfs;a=std::move(next);model=a.get();return;
			}
		}
		template<class T> bool read(const void* p,size_t at,T& out)noexcept{return p && utils::native_memory::read_bytes(&out,static_cast<const std::byte*>(p)+at,sizeof(out));}
		scene_models::placement_result prepare(const scene_models::preparation& preparing,const void* entry,game::GfxPlacement& placed,game::GfxPlacement& previous)noexcept
		{
			using result=scene_models::placement_result;std::uintptr_t handle{};const auto base=reinterpret_cast<std::uintptr_t>(lighting.data());
			if(!scene_models::native_entry::lighting(entry,handle) || handle<base || handle>=base+latest.size()*sizeof(unsigned short) || (handle-base)%sizeof(unsigned short))return result::unchanged;
			weapon_identity id;{const std::lock_guard lock(mutex);id=latest[(handle-base)/sizeof(unsigned short)].view.owner.id();}
			std::array<float,12> camera{};weapon_render_pose::snapshot skin;
			if(!alive || !read(preparing.record,engine_stereo_view::h2_view_origin_offset,camera) || !weapon_render_pose::for_record(reinterpret_cast<std::uintptr_t>(preparing.record),camera,skin,id))return result::omit;
			frame f;bool found{};
			{const std::lock_guard lock(mutex);found=scene_models::latest_skeleton_pose(poses,cursor,skin,
				[&](const frame& value)noexcept{return value.view.owner.id()==id;},f);}
			const auto v=current(id);const auto now=controller_input::clock::now();vec origin{};game::XModel* submitted{};
			if(!found || !f.visible || f.mesh!=model.load() || !f.mesh || !v.active || v.fault || f.view.reference!=v.reference ||
				f.view.owner.rear_revision!=v.owner.rear_revision || f.held!=valid_hand(v.rocket.actor) || (!f.held && !v.loaded) ||
				now<f.view.at || now-f.view.at>150ms || !scene_models::native_entry::model(entry,submitted) || submitted!=f.mesh->rocket.model() ||
				!read(preparing.record,engine_stereo_view::h2_current_model_placement_origin_offset,origin))return result::omit;
			for(float x:origin)if(!std::isfinite(x) || std::abs(x)>1e7f)return result::omit;
			auto pose=f.local;pose.position=add(pose.position,origin);pose=compose(pose,inverse(bind(*f.mesh)));
			std::copy(pose.position.begin(),pose.position.end(),placed.origin);std::copy(pose.rotation.begin(),pose.rotation.end(),placed.quat);previous=placed;return result::replace;
		}
		void submit()
		{
			if(!alive || !game::CL_IsCgameInitialized())return;std::array<frame,15> frames;std::array<drop,16> released;{const std::lock_guard lock(mutex);frames=latest;released=drops;}
			const auto input=controller_input::latest();const auto now=controller_input::clock::now();
			for(unsigned n=0;n<frames.size();++n){const auto& f=frames[n];if(!f.visible || !f.mesh || f.mesh!=model.load() || !input.focused || f.view.reference!=input.reference_generation || now<f.view.at || now-f.view.at>150ms)continue;
				const auto pose=compose(f.world,inverse(bind(*f.mesh)));game::GfxScaledPlacement place{};place.scale=1;
				std::copy(pose.position.begin(),pose.position.end(),place.base.origin);std::copy(pose.rotation.begin(),pose.rotation.end(),place.base.quat);
				float color[]{1,1,1,1};scene_models::submit(f.mesh->rocket.model(),&place,0,&lighting[n],color,color,color,40.f);}
			for(unsigned n=0;n<released.size();++n){const auto& d=released[n];const float age=std::chrono::duration<float>(now-d.at).count();
				if(!d.mesh || d.mesh!=model.load() || d.reference!=input.reference_generation || !input.focused || age<0 || age>1.2f)continue;
				falling_item_presentation::motion falling;falling.path.start=d.world;falling.path.velocity={0,0,-.15f*d.units};
				falling.path.born=d.at;falling.path.units=d.units;falling.local=inverse(bind(*d.mesh));
				falling_item_presentation::submit(d.mesh->rocket.model(),falling,d.reference,40.f);}
		}
	}
	part_presentation::result present(void* object,std::uint32_t epoch,const void* matrices,const part_rig& parts,const hands::rig& r,
		const hands::pose_library& library,const profile& grip,const controller_input::frame& input,const hold& owner,std::uint64_t assembly,
		bool gameplay,bool manipulation,const std::array<hands::anchor,2>& targets,hands::vec offset,float units,std::span<hands::bone> solved,hands::part_hand_frame* hand_motion)noexcept
	{
		if(!alive || !ready() || !parts.valid || !grip.launcher || !valid_hand(owner.rear) || !std::isfinite(units) || units<=0 || solved.size()<size_t(r.count))return {};
		const auto& p=*grip.launcher;auto* mesh=model.load();if(p.manual_loading() && !mesh)return {};
		const int off=1-int(owner.rear);const auto basis=library.mirror_basis[r.arms[off].wrist];const auto gun=as_anchor(solved[r.gun]);
		if(gameplay)publish_scene({owner,&p,assembly,{free_hand_rotation(grip,off),basis,true}});
		const auto v=current(owner.id());if(!v.active)return {};if(!p.manual_loading())return {true,{},0};
		// Native rocket visibility/animation must not duplicate the instance-owned round.
		part_mask hidden{};for(int b=0;b<r.count;++b)if(descendant(b,parts.rocket,r))hidden[b/32]|=0x80000000u>>(b%32);
		unsigned posed{};const bool held=v.active && valid_hand(v.rocket.actor);
		const auto grasp=off?hands::pose_mirror::object_in_wrist({},p.rocket_in_wrist,basis):p.rocket_in_wrist;
		if(held && manipulation && v.rocket.actor==hand(off)){hands::pose_mirror::fingers(r,library,grip,p.rocket_fingers,off,solved,off==1);posed=1u<<off;}
		if(hand_motion)hand_motion->apply(off,posed ? hands::part_hand_attachment::rocket : hands::part_hand_attachment::free);
		const auto local=held?compose(as_anchor(solved[r.arms[off].wrist]),grasp):compose(gun,p.rocket_rest);
		auto world=local;world.position=add(world.position,offset);frame f{v,mesh,reinterpret_cast<std::uintptr_t>(object),reinterpret_cast<std::uintptr_t>(matrices),epoch,local,world,gameplay && v.active && !v.fault && (held || v.loaded),held};
		const std::lock_guard lock(mutex);auto* slot=&latest[0];for(auto& candidate:latest)if(candidate.view.owner.id()==owner.id()){slot=&candidate;break;}else if(!candidate.visible || candidate.view.at<slot->view.at)slot=&candidate;
		if(slot->view.owner.id()==owner.id() && slot->held && !f.held && !f.view.loaded && gameplay && slot->view.reference==input.reference_generation)
			drops[drop_cursor++%drops.size()]={slot->mesh,slot->world,controller_input::clock::now(),input.reference_generation,units};
		*slot=f;poses[cursor++%poses.size()]=f;return {true,hidden,posed};
	}
	class presenter_component final:public component_interface
	{
		void post_unpack()override{set_boundary_ready(4);scene_models::on_submit(submit);scene_models::on_prepare_placement(prepare);scheduler::loop(refresh,scheduler::pipeline::main,250ms);
			fastfiles::on_pre_unload([]{model=nullptr;{const std::lock_guard lock(mutex);latest={};poses={};drops={};cursor=drop_cursor=0;}for(auto& a:retained)a.reset();});}
		void pre_destroy()override{alive=false;model=nullptr;}
	};
}
REGISTER_COMPONENT(vr::gameplay::weapons::launcher::presenter_component)
