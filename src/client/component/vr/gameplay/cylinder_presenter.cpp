#include <std_include.hpp>
#include "component/scene_model_record.hpp"
#include "component/vr/gameplay/hand_pose_mirror.hpp"
#include "cylinder_presenter.hpp"
#include "falling_item_presenter.hpp"
#include "weapon_instance_cache.hpp"
#include "weapon_carry_runtime.hpp"
#include "interaction_debug.hpp"
#include "viewmodel_visibility.hpp"
#include "weapon_render_pose.hpp"
#include "physical_reload_geometry.hpp"
#include "ejection_scatter.hpp"
#include "cylinder_profiles.hpp"
#include <utils/native_memory.hpp>
#include "component/scene_models.hpp"
#include "component/scene_pose_match.hpp"
#include "component/fastfiles.hpp"
#include "component/scene_rigid_part.hpp"
#include "component/scheduler.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <mutex>

namespace vr::gameplay::weapons::cylinder
{
	namespace
	{
		using namespace hands;
		using namespace hands::pose_math;
		std::atomic_bool alive{true};
		std::atomic_uint64_t scene_generation{1};
		struct assets
		{
			scene_models::rigid_part loader, casing, tip;
			game::XModel* source{}; game::XSurface* surfaces{};const cylinder_profile* definition{};
		};
		// Queued native surface packets borrow these immutable descriptors.
		// Retain until the drained native asset-unload barrier.
		std::array<std::unique_ptr<assets>,32> retained{};
		std::array<std::atomic<assets*>,cylinder_definitions.capacity()> available{};
		assets* find_assets(const cylinder_profile* definition) noexcept
		{for(size_t n=0;n<cylinder_definitions.size();++n)if(cylinder_definitions[n]==definition)return available[n].load();return nullptr;}
		void clear_assets() noexcept {for(auto& entry:available)entry=nullptr;}
		std::atomic<const char*> asset_reason{"waiting for native loaded model"};
		std::array<clock::time_point,cylinder_definitions.capacity()> last_attempt{};
		struct pose_frame
		{
			presentation view{}; assets* models{}; part_rig parts{};
			std::uintptr_t object{}, matrices{}; std::uint32_t epoch{};
			anchor loader_model{}, loader_world{}; float units{}; clock::time_point at{};size_t slot{};
		};
		std::mutex mutex;
		std::array<pose_frame,15> latest_frames{};
		std::array<pose_frame,128> poses{}; size_t cursor{};
		std::array<unsigned short,15*13> lighting{}; // 13 held parts; falling tickets have their own handles.
		struct drop { bool active{}; presentation::event event{}; assets* models{}; part_rig parts{}; std::uint64_t instance{}; std::uint32_t weapon{}; std::uint64_t reference{}, scene{}; std::array<ejection_scatter::motion,6> scatter{}; };
		std::array<drop,4> drops{};
		struct event_cursor { std::uint64_t instance{}, sequence{}; };
		instance_cache<event_cursor,512> events;
		anchor bind_pose(const scene_models::rigid_part& part)
		{ const auto v=part.bind(); return {{v[0],v[1],v[2]},normalize({v[3],v[4],v[5],v[6]})}; }
		anchor place(const scene_models::rigid_part& part,anchor target)
		{ return compose(target,inverse(bind_pose(part))); }
		// n=0 loader; n=1..6 cases; n=7..12 tips. Bind positions are source
		// coordinates, while loader_rounds describe the authored left-hand grasp.
		std::pair<scene_models::rigid_part*,anchor> held_part(assets& models,anchor loader,const part_rig& parts,size_t n)
		{
			if (!n) return {&models.loader,place(models.loader,loader)};
			const size_t round=(n-1)%6;
			auto pose=compose(loader,models.definition->loader_rounds[round]);
			if (n<=6) return {&models.casing,place(models.casing,pose)};
			return {&models.tip,place(models.tip,compose(pose,parts.tip_in_case[0]))};
		}
		template<class T> bool read(const void* source,size_t offset,T& value) noexcept
		{ return source && utils::native_memory::read_bytes(&value,static_cast<const std::byte*>(source)+offset,sizeof(value)); }
		scene_models::placement_result prepare(const scene_models::preparation& preparing,const void* entry,
			game::GfxPlacement& placement,game::GfxPlacement& previous) noexcept
		{
			using result=scene_models::placement_result;
			const void* handle{};
			if (!scene_models::native_entry::lighting(entry,handle)) return result::unchanged;
			const auto address=reinterpret_cast<std::uintptr_t>(handle),base=reinterpret_cast<std::uintptr_t>(lighting.data());
			if (address<base || address>=base+15*13*sizeof(unsigned short) || (address-base)%sizeof(unsigned short)) return result::unchanged;
			const auto index=(address-base)/sizeof(unsigned short),part=index%13;
			weapon_identity id;
			{const std::lock_guard lock(mutex);id=latest_frames[index/13].view.owner.id();}
			if (!alive.load()) return result::omit;
			std::array<float,12> camera{}; weapon_render_pose::snapshot rendered;
			if (!read(preparing.record,engine_stereo_view::h2_view_origin_offset,camera) ||
				!weapon_render_pose::for_record(reinterpret_cast<std::uintptr_t>(preparing.record),camera,rendered,id)) return result::omit;
			pose_frame selected; bool found=false;
			{
				const std::lock_guard lock(mutex);
				found=scene_models::latest_skeleton_pose(poses,cursor,rendered,
					[&](const pose_frame& value)noexcept{return value.view.owner.id()==id;},selected);
			}
			const auto now=clock::now(); const auto live=current(selected.view.owner.id());
			if (!found || !selected.models || selected.models!=find_assets(selected.view.definition) || !live.active || live.fault ||
				!selected.view.active || selected.view.fault || selected.view.ammo.instance_generation!=live.ammo.instance_generation ||
				selected.view.reference!=live.reference || selected.view.owner.rear_revision!=live.owner.rear_revision ||
				selected.view.ammo.loader_hand==hand::none || live.ammo.loader_hand==hand::none ||
				now<selected.at || now-selected.at>150ms ||
				(part && ((part-1)%6>=size_t(selected.view.ammo.held_rounds) || (part-1)%6>=size_t(live.ammo.held_rounds)))) return result::omit;
			vec origin{};
			if (!read(preparing.record,engine_stereo_view::h2_current_model_placement_origin_offset,origin) || !finite(origin)) return result::omit;
			auto loader=selected.loader_model; loader.position=add(loader.position,origin);
			const auto [asset,world]=held_part(*selected.models,loader,selected.parts,part);
			game::XModel* submitted{};
			if (!scene_models::native_entry::model(entry,submitted) || submitted!=asset->model()) return result::omit;
			std::copy(world.position.begin(),world.position.end(),placement.origin);
			std::copy(world.rotation.begin(),world.rotation.end(),placement.quat);
			previous=placement; return result::replace;
		}
		void draw(scene_models::rigid_part& part,anchor world,size_t handle,float units)
		{
			game::GfxScaledPlacement p{}; p.scale=1;
			std::copy(world.position.begin(),world.position.end(),p.base.origin);
			std::copy(world.rotation.begin(),world.rotation.end(),p.base.quat);
			float color[4]{1,1,1,1};
			// Match independent-hand depth for loaders, loose rounds and released parts.
			scene_models::submit(part.model(),&p,0,&lighting[handle],color,color,color,.25f*units);
		}
		void submit()
		{
			if (!alive.load()) return;
			if (!game::CL_IsCgameInitialized()) { drops={}; events={}; return; }
			std::array<pose_frame,15> frames; {const std::lock_guard lock(mutex);frames=latest_frames;}
			const auto now=clock::now();
			for (const auto& p:frames)
			{
			if (!p.models || p.models!=find_assets(p.view.definition)) continue;
			const auto live=current(p.view.owner.id());
			const bool coherent=live.active && !live.fault && live.ammo.instance_generation==p.view.ammo.instance_generation;
			if (coherent && now>=p.at && now-p.at<=150ms && p.view.ammo.loader_hand!=hand::none && live.ammo.loader_hand!=hand::none)
				for (size_t n=0;n<13;++n)
				{
					if (n && (n-1)%6>=size_t(p.view.ammo.held_rounds)) continue;
					const auto [asset,world]=held_part(*p.models,p.loader_world,p.parts,n);
					draw(*asset,world,p.slot*13+n,p.units);
				}
			events.retain([](weapon_identity id){return carry::active() ? carry::contains(id) : !id.generation;});
			if (auto* event_ptr=coherent ? events.acquire(live.owner.id()) : nullptr)
			{
				auto& e=*event_ptr;
				if (e.instance!=live.ammo.instance_generation) e={live.ammo.instance_generation,0};
				for (auto n=live.event_sequence>live.events.size() ? live.event_sequence-live.events.size()+1 : 1;n<=live.event_sequence;++n)
				{
					const auto event=live.events[n%live.events.size()];
					if (n<=e.sequence || event.sequence!=n || now<event.at) continue;
					if (!event.recoverable && (event.kind==effect::clear || event.kind==effect::discard) && now-event.at<1200ms && event.units>0)
					{
						auto* target=&drops[0];
						for (auto& d : drops) if (!d.active) { target=&d; break; } else if (d.event.at<target->event.at) target=&d;
						*target={true,event,p.models,p.parts,live.ammo.instance_generation,live.ammo.weapon,live.reference,scene_generation.load()};
						if(event.kind==effect::clear)for(unsigned round=0;round<target->scatter.size();++round)
							target->scatter[round]=ejection_scatter::sample(live.ammo.instance_generation,event.sequence,round);
					}
					e.sequence=n;
				}
			}
			}
			for (size_t n=0;n<drops.size();++n)
			{
				auto& d=drops[n]; if (!d.active) continue;
				const float age=std::chrono::duration<float>(now-d.event.at).count();
				if (age<0 || age>=1.2f || d.reference!=controller_input::latest().reference_generation || d.scene!=scene_generation.load() || d.models!=find_assets(d.models?d.models->definition:nullptr))
				{ d.active=false; continue; }
				for (size_t k=0;k<13;++k)
				{
					falling_item_presentation::motion falling;falling.path.start=d.event.world;
					falling.path.velocity={0,0,-.15f*d.event.units};falling.path.units=d.event.units;falling.path.born=d.event.at;
					if (d.event.kind==effect::discard)
					{
						if (k && (k-1)%6>=size_t(d.event.live)) continue;
						const auto [asset,local]=held_part(*d.models,{},d.parts,k);falling.local=local;
						falling_item_presentation::submit(asset->model(),falling,d.reference,.25f*d.event.units);
					}
					else if (k)
					{
						const auto round=(k-1)%6;
						if (round>=size_t(d.event.live+d.event.spent) || (k>6 && round>=size_t(d.event.live))) continue;
						falling.path.start=compose(compose(d.event.world,d.models->definition->ammo_in_cylinder),d.parts.case_in_ammo[round]);
						falling.scattered=true;falling.scatter=d.scatter[round];falling.scatter_basis=falling.path.start.rotation;
						auto& asset=k<=6 ? d.models->casing : d.models->tip;
						falling.local=k<=6?inverse(bind_pose(asset)):compose(d.parts.tip_in_case[round],inverse(bind_pose(asset)));
						falling_item_presentation::submit(asset.model(),falling,d.reference,.25f*d.event.units);
					}
				}
			}
		}
		void refresh_assets()
		{
			if (!alive.load()) return;
			const bool gameplay=game::CL_IsCgameInitialized();
			const auto* ps=gameplay ? game::g_entities[0].client : nullptr;
			if (!ps) { clear_assets(); asset_reason="no initialized scene"; return; }
			for(size_t n=0;n<cylinder_definitions.size();++n)
			{
			const auto& definition=*cylinder_definitions[n];const auto& recipe=definition.assets;
			if(recipe.receiver.empty() || !recipe.receiver_bones)continue;
			auto* source=game::DB_FindXAssetHeader(game::ASSET_TYPE_XMODEL,recipe.receiver.data(),0).model;
			if (!source || !source->name || std::string_view(source->name)!=recipe.receiver || source->numBones!=recipe.receiver_bones)
			{ available[n]=nullptr; asset_reason="native receiver absent or unsupported skeleton"; continue; }
			bool found{};
			for (const auto& set : retained)
				if (set && set->definition==&definition && set->source==source && set->surfaces==source->lodInfo[0].surfs)
				{ available[n]=set.get(); asset_reason="ready: separate loader/cases/tips";found=true;break; }
			if(found)continue;
			available[n]=nullptr;
			if (!viewmodel_visibility::ready(part_visibility::rigid_groups) || !scene_models::ready())
			{ asset_reason="native visibility/scene submission unavailable"; continue; }
			const auto now=clock::now();
			if (now>=last_attempt[n] && now-last_attempt[n]<1s) continue;
			last_attempt[n]=now;
			// Bounded exact named roles; no assumptions about a 25-bone asset's
			// numeric bone order beyond the live checked grouping contract.
			int loader=-1,casing=-1,tip=-1;
			for (int b=0;b<source->numBones;++b)
			{
				const auto* name=game::SL_ConvertToString(source->boneNames[b]); if (!name) {loader=casing=tip=-1;break;}
				if (std::string_view(name)==recipe.loader) loader=b;
				if (std::string_view(name)==recipe.case_body) casing=b;
				if (std::string_view(name)==recipe.case_tip) tip=b;
			}
			if (loader<0 || casing<0 || tip<0) { asset_reason="required named part absent"; continue; }
			for (auto& slot : retained) if (!slot)
			{
				auto a=std::make_unique<assets>();
				if (!a->loader.create(source,loader)) { asset_reason=a->loader.status(); break; }
				if (!a->casing.create(source,casing)) { asset_reason=a->casing.status(); break; }
				if (!a->tip.create(source,tip)) { asset_reason=a->tip.status(); break; }
				const std::array<scene_models::runtime_model,3> identities{{
					{a->loader.model(),a->loader.source()},{a->casing.model(),a->casing.source()},
					{a->tip.model(),a->tip.source()}}};
				if (!scene_models::register_runtime_models(identities))
				{ asset_reason="native subset resource identity rejected or registry full"; break; }
				a->definition=&definition;a->source=source; a->surfaces=source->lodInfo[0].surfs;
				slot=std::move(a); available[n]=slot.get(); asset_reason="ready: separate loader/cases/tips"; set_boundary_ready(4);break;
			}
			if(!available[n])asset_reason="retained rigid-part asset limit reached or part creation rejected";
			}
		}
	}
	const char* presentation_status() noexcept { return asset_reason.load(); }
	reload_items::visual loader_visual(const cylinder_profile* definition,int rounds)noexcept
	{
		reload_items::visual out;auto* models=find_assets(definition);if(!alive || !models || !definition || rounds<0 || rounds>definition->ammunition.capacity)return out;
		part_rig parts;parts.tip_in_case[0]=hands::pose_math::compose(hands::pose_math::inverse(bind_pose(models->casing)),bind_pose(models->tip));
		for(size_t n=0;n<13;++n)
		{
			if(n && (n-1)%6>=size_t(rounds))continue;
			const auto [model,pose]=held_part(*models,{},parts,n);
			out.pieces[out.count++]={model->model(),pose};
		}
		return out;
	}
	part_presentation::result present(void* object,std::uint32_t epoch,const void* matrices,const part_rig& parts,
		const hands::rig& r,const hands::pose_library& library,const weapons::profile& grip,
		const controller_input::frame& input,const hold& owner,std::uint64_t assembly,bool gameplay, bool manipulation,
		const std::array<hands::anchor,2>& targets,const std::array<hands::vec,2>& shoulders,
		const std::array<hands::vec,3>& body_axis,hands::vec head,hands::vec offset,float units,
		std::span<hands::bone> solved,clock::time_point now,hands::part_hand_frame* hand_motion) noexcept
	{
		(void)shoulders;
		using namespace hands::pose_math;
		auto* models=find_assets(grip.cylinder);
		if (!alive.load() || !parts.valid || !grip.cylinder || !valid_hand(owner.holding_hand()) ||
			!std::isfinite(units) || units<=0 || solved.size()<size_t(r.count)) return {};
		const auto& p=*grip.cylinder; const auto v=current(owner.id());
		const auto off=1-static_cast<int>(owner.holding_hand());
		const auto in_wrist=off ? hands::pose_mirror::object_in_wrist({},p.loader_in_wrist,library.mirror_basis[r.arms[off].wrist]) : p.loader_in_wrist;
		float progress=0;
		if (v.active)
		{
			const float age=std::max(0.f,std::chrono::duration<float>(now-v.action_at).count());
			progress=v.ammo.phase==action::open ? 1.f : v.ammo.phase==action::opening ? std::clamp(age/p.interaction.opening_seconds,0.f,1.f) :
				v.ammo.phase==action::closing ? 1.f-std::clamp(age/p.interaction.closing_seconds,0.f,1.f) : 0;
			if (v.ammo.loader_hand!=hand::none) hands::pose_mirror::fingers(r,library,grip,p.loader_fingers,off,solved,off==1);
		}
		pose_parts(r,parts,p,progress,solved);
		const auto barrel=as_anchor(solved[parts.barrel]);
		const auto face=compose(barrel,p.face_in_cylinder);
		const anchor raw_wrist{targets[off].position,normalize(multiply(targets[off].rotation,free_hand_rotation(grip,off)))};
		const auto raw_loader=compose(raw_wrist,in_wrist);
		if(hand_motion)hand_motion->apply(off,v.active && !v.fault && manipulation && v.ammo.loader_hand==hand(off) ? hands::part_hand_attachment::loader : hands::part_hand_attachment::free);
		const auto visual_loader=compose(as_anchor(solved[r.arms[off].wrist]),in_wrist);
		const auto tip=compose(raw_loader,{p.loader_tip,{0,0,0,1}});
		const auto contact_point=scale(compose(inverse(face),tip).position,1/units);
		float waist=10;
		const auto supply=body_supply_volumes(head,body_axis,units,{},p.interaction.waist_radius);
		for (const auto& volume:supply) waist=std::min(waist,volume.distance(raw_wrist.position)/units);
		if (interaction::debug::body_enabled() && gameplay && input.focused)
		{
			interaction::debug::supply_sample diagnostic{supply,add(raw_wrist.position,offset),owner,units,off,input.reference_generation,input.sampled_at};
			for (auto& volume:diagnostic.volumes) volume.origin=add(volume.origin,offset);
			interaction::debug::publish_supply(diagnostic);
		}
		scene_frame s{input,owner,{true,owner.weapon,v.active ? v.ammo.instance_generation : 0,input.reference_generation,input.sequence,
			waist,rotate(barrel.rotation,{-1,0,0})[2],dot(rotate(raw_loader.rotation,{1,0,0}),rotate(barrel.rotation,{1,0,0})),contact_point},assembly,gameplay};
		s.definition=&p; s.cylinder_world=barrel; s.cylinder_world.position=add(barrel.position,offset);
		s.loader_world=visual_loader; s.loader_world.position=add(visual_loader.position,offset); s.units=units;
		s.manipulation=manipulation && models;s.binding={free_hand_rotation(grip,off),library.mirror_basis[r.arms[off].wrist],models!=nullptr};s.cylinder_in_swing=parts.cylinder_in_swing;publish_scene(s);
		const auto hidden=hidden_parts(parts,v);
		const std::lock_guard lock(mutex);
		pose_frame latest={v,models,parts,reinterpret_cast<std::uintptr_t>(object),reinterpret_cast<std::uintptr_t>(matrices),epoch,
			visual_loader,s.loader_world,units,input.sampled_at};
		auto* slot=&latest_frames[0];
		for (auto& frame:latest_frames) if (frame.view.owner.id()==owner.id()) {slot=&frame;break;} else if (!frame.view.ammo.weapon || frame.at<slot->at) slot=&frame;
		latest.view.owner=owner;latest.slot=size_t(slot-latest_frames.data());
		*slot=latest;
		poses[cursor++%poses.size()]=latest;
		return {true,hidden};
	}
	class presenter_component final : public component_interface
	{
		void post_unpack() override
		{
			scene_models::on_submit(submit);scene_models::on_prepare_placement(prepare);
			scheduler::loop(refresh_assets,scheduler::pipeline::main,250ms);
			fastfiles::on_pre_unload([] {
				++scene_generation;clear_assets();
				{ const std::lock_guard lock(mutex);latest_frames={};poses={};cursor=0;drops={};events={}; }
				for (auto& set:retained) set.reset();
				last_attempt={};asset_reason="waiting for loaded assets after retirement";
			});
		}
		void pre_destroy() override { alive=false; clear_assets(); }
	};
}
REGISTER_COMPONENT(vr::gameplay::weapons::cylinder::presenter_component)
