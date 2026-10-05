#include <std_include.hpp>
#include "component/scene_model_record.hpp"
#include "break_action_presenter.hpp"
#include "break_action_profiles.hpp"
#include "weapon_carry_runtime.hpp"
#include "weapon_instance_cache.hpp"
#include "weapon_render_pose.hpp"
#include "component/vr/gameplay/hand_pose_mirror.hpp"
#include "part_hand_constraint.hpp"
#include "ejection_scatter.hpp"
#include "viewmodel_visibility.hpp"
#include "body_supply_volume.hpp"
#include <utils/native_memory.hpp>
#include "component/scene_models.hpp"
#include "component/scene_rigid_part.hpp"
#include "component/fastfiles.hpp"
#include "component/scheduler.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <mutex>

namespace vr::gameplay::weapons::break_action
{
	namespace
	{
		using namespace hands;using namespace hands::pose_math;
		struct asset{scene_models::rigid_part shell,casing;game::XModel* source{};game::XSurface* surfaces{};const break_action_profile* definition{};std::uint64_t level{};};
		std::array<std::unique_ptr<asset>,32> retained;
		std::array<std::atomic<asset*>,break_action_definitions.capacity()> available{};
		std::atomic_uint64_t level{1};std::atomic_bool alive{true};std::mutex mutex;
		struct frame{presentation view{};asset* model{};std::uintptr_t object{},matrices{};std::uint32_t epoch{};anchor local{},world{};float units{};clock::time_point at{};};
		std::array<frame,15> latest{};std::array<frame,128> poses{};size_t cursor{};
		struct drop{asset* model{};bool casing{};anchor world{};float units{};clock::time_point at{};std::uint64_t reference{};ejection_scatter::motion scatter{};};
		std::array<drop,16> drops{};size_t drop_cursor{};std::array<unsigned short,31> lighting{};
		struct watermark{std::uint64_t instance{},sequence{};};instance_cache<watermark,512> events;
		anchor bind(const scene_models::rigid_part& p){const auto b=p.bind();return {{b[0],b[1],b[2]},normalize({b[3],b[4],b[5],b[6]})};}
		asset* model_for(const break_action_profile* p){for(size_t n=0;n<break_action_definitions.size();++n)if(break_action_definitions[n]==p)return available[n].load();return nullptr;}
		void refresh()
		{
			if(!alive || !game::CL_IsCgameInitialized() || !scene_models::ready() || !viewmodel_visibility::ready(part_visibility::rigid_groups))return;
			for(size_t n=0;n<break_action_definitions.size();++n)
			{
				const auto* p=break_action_definitions[n];auto* source=game::DB_FindXAssetHeader(game::ASSET_TYPE_XMODEL,p->receiver.data(),0).model;
				if(!source || !source->name || std::string_view(source->name)!=p->receiver || source->numBones!=p->receiver_bones || !source->boneNames){available[n]=nullptr;continue;}
				asset* found{};for(auto& entry:retained)if(entry && entry->source==source && entry->surfaces==source->lodInfo[0].surfs && entry->definition==p && entry->level==level){found=entry.get();break;}
				if(found){available[n]=found;continue;}
				int shell=-1,casing=-1;for(int b=0;b<source->numBones;++b){const char* name=game::SL_ConvertToString(source->boneNames[b]);if(!name)continue;
					if(std::string_view(name)==p->shell_bone)shell=b;if(std::string_view(name)==p->case_bone)casing=b;}
				if(shell<0 || casing<0)continue;
				for(auto& entry:retained)if(!entry)
				{
					auto a=std::make_unique<asset>();if(!a->shell.create(source,shell) || !a->casing.create(source,casing))break;
					const std::array<scene_models::runtime_model,2> identity{{{a->shell.model(),source},{a->casing.model(),source}}};
					if(!scene_models::register_runtime_models(identity))break;
					a->source=source;a->surfaces=source->lodInfo[0].surfs;a->definition=p;a->level=level;entry=std::move(a);available[n]=entry.get();break;
				}
			}
		}
		template<class T> bool read(const void* p,size_t offset,T& value)noexcept
		{return p && utils::native_memory::read_bytes(&value,static_cast<const std::byte*>(p)+offset,sizeof(value));}
		scene_models::placement_result prepare(const scene_models::preparation& preparing,const void* entry,game::GfxPlacement& placed,game::GfxPlacement& previous)noexcept
		{
			using result=scene_models::placement_result;std::uintptr_t handle{};const auto base=reinterpret_cast<std::uintptr_t>(lighting.data());
			if(!scene_models::native_entry::lighting(entry,handle) || handle<base || handle>=base+latest.size()*sizeof(unsigned short) || (handle-base)%sizeof(unsigned short))return result::unchanged;
			weapon_identity id;{const std::lock_guard lock(mutex);id=latest[(handle-base)/sizeof(unsigned short)].view.owner.id();}
			std::array<float,12> camera{};weapon_render_pose::snapshot skinned;
			if(!alive || !read(preparing.record,engine_stereo_view::h2_view_origin_offset,camera) || !weapon_render_pose::for_record(reinterpret_cast<std::uintptr_t>(preparing.record),camera,skinned,id))return result::omit;
			frame selected;bool found=false;{const std::lock_guard lock(mutex);for(size_t n=0;n<poses.size();++n){const auto& p=poses[(cursor+poses.size()-1-n)%poses.size()];
				if(p.object==skinned.object && p.matrices==skinned.matrices && p.epoch==skinned.epoch && p.view.owner.id()==id){selected=p;found=true;break;}}}
			const auto live=current(id);const auto now=clock::now();game::XModel* submitted{};vec origin{};
			if(!found || !selected.model || selected.model!=model_for(selected.view.definition) || !live.active || live.fault || !selected.view.active || selected.view.fault ||
				live.ammo.instance_generation!=selected.view.ammo.instance_generation || live.reference!=selected.view.reference || live.owner.rear_revision!=selected.view.owner.rear_revision ||
				live.ammo.loader_hand==hand::none || live.ammo.loader_hand!=selected.view.ammo.loader_hand || now<selected.at || now-selected.at>150ms ||
				!scene_models::native_entry::model(entry,submitted) || submitted!=selected.model->shell.model() || !read(preparing.record,engine_stereo_view::h2_current_model_placement_origin_offset,origin))return result::omit;
			for(float v:origin)if(!std::isfinite(v) || std::abs(v)>1e7f)return result::omit;
			auto pose=selected.local;pose.position=add(pose.position,origin);pose=compose(pose,inverse(bind(selected.model->shell)));
			std::copy(pose.position.begin(),pose.position.end(),placed.origin);std::copy(pose.rotation.begin(),pose.rotation.end(),placed.quat);previous=placed;return result::replace;
		}
		void submit()
		{
			if(!alive || !game::CL_IsCgameInitialized())return;
			std::array<frame,15> frames;std::array<drop,16> released;{const std::lock_guard lock(mutex);frames=latest;released=drops;}
			const auto input=controller_input::latest();const auto now=clock::now();
			const auto draw=[&](scene_models::rigid_part& model,anchor target,unsigned n,float radius){target=compose(target,inverse(bind(model)));game::GfxScaledPlacement place{};place.scale=1;
				std::copy(target.position.begin(),target.position.end(),place.base.origin);std::copy(target.rotation.begin(),target.rotation.end(),place.base.quat);
				float color[]{1,1,1,1};scene_models::submit(model.model(),&place,0,&lighting[n],color,color,color,radius);};
			for(unsigned n=0;n<frames.size();++n){const auto& f=frames[n];if(f.model && f.model->level==level && f.view.active && !f.view.fault && f.view.ammo.loader_hand!=hand::none &&
				f.view.reference==input.reference_generation && input.focused && now>=f.at && now-f.at<=150ms)draw(f.model->shell,f.world,n,.2f*f.units);}
			for(unsigned n=0;n<released.size();++n){const auto& d=released[n];const float age=std::chrono::duration<float>(now-d.at).count();
				if(!d.model || d.model->level!=level || d.reference!=input.reference_generation || age<0 || age>1.2f)continue;
				const auto velocity=d.casing ? rotate(d.world.rotation,{-1.2f*d.units,0,.2f*d.units}) : vec{0,0,-.15f*d.units};
				auto fallen=physical_reload::free_drop(d.world,velocity,age,d.units);
				fallen=ejection_scatter::apply(fallen,d.world.rotation,d.scatter,age,d.units);
				draw(d.casing ? d.model->casing : d.model->shell,fallen,unsigned(latest.size())+n,.2f*d.units);}
		}
	}
	part_presentation::result present(void* object,std::uint32_t epoch,const void* matrices,const part_rig& parts,const hands::rig& r,
		const hands::pose_library& library,const weapons::profile& grip,const controller_input::frame& input,const hold& owner,
		std::uint64_t assembly,bool gameplay,bool manipulation,const std::array<hands::anchor,2>& targets,const std::array<hands::vec,2>& shoulders,
		const std::array<hands::vec,3>& body_axis,hands::vec head,hands::vec offset,float units,std::span<hands::bone> solved,clock::time_point now,hands::part_hand_frame* hand_motion)noexcept
	{
		using namespace hands;using namespace hands::pose_math;
		if(!alive || !parts.valid || !grip.break_open || !valid_hand(owner.holding_hand()) || !std::isfinite(units) || units<=0 || solved.size()<size_t(r.count))return {};
		const auto& p=*grip.break_open;auto* model=model_for(&p);if(!model)return {};
		const auto v=current(owner.id());if(v.active && v.definition!=&p)return {};
		const int rear=int(owner.holding_hand()),off=1-rear;const auto gun=as_anchor(solved[r.gun]);const auto inverse_gun=inverse(gun);const auto basis=library.mirror_basis[r.arms[off].wrist];
		const anchor wrist{targets[off].position,normalize(multiply(targets[off].rotation,free_hand_rotation(grip,off)))};
		const auto raw=compose(inverse_gun,wrist);const auto grasp=off ? hands::pose_mirror::part(p.barrel_grip,basis) : p.barrel_grip;
		const auto shell_grip=off ? hands::pose_mirror::object_in_wrist({},p.shell_in_wrist,basis) : p.shell_in_wrist;
		const auto raw_shell=compose(raw,shell_grip);const auto shell_tip=compose(raw_shell,{p.shell_tip,{0,0,0,1}}).position;
		const float hinge=displayed_hinge(v,owner,input.reference_generation,now);
		const auto barrel_local=barrel_pose(p,hinge);const auto barrel=compose(gun,barrel_local);
		const auto delta=compose(barrel_local,inverse(p.barrel_closed));const auto barrel_wrist=compose(delta,grasp.wrist);
		const auto hand_contact=compose(raw,{grasp.contact_in_wrist,{0,0,0,1}}).position;
		const auto contact=compose(barrel_wrist,{grasp.contact_in_wrist,{0,0,0,1}}).position;
		const auto radial=sub(hand_contact,p.hinge_pivot);
		geometry g;g.valid=true;g.weapon=owner.weapon;g.instance_generation=v.active ? v.ammo.instance_generation : 0;g.reference_generation=input.reference_generation;g.input_sequence=input.sequence;
		g.barrel_hand=scale(hand_contact,1/units);g.barrel_distance=length(sub(hand_contact,contact))/units;g.barrel_angle=std::atan2(-radial[2],radial[0]);
		if(std::hypot(radial[0],radial[2])<.04f*units)g.barrel_distance=10; // A contact at the pivot cannot define hinge rotation.
		const auto supply=body_supply_volumes(head,body_axis,units,{},p.interaction.waist_radius);
		for(const auto& volume:supply)g.waist_distance=std::min(g.waist_distance,volume.distance(wrist.position)/units);
		for(unsigned n=0;n<p.ammunition.capacity;++n){const auto face=compose(barrel_local,p.mouth_in_barrel[n]);
			g.shell_in_chamber[n]=scale(compose(inverse(face),{shell_tip,{0,0,0,1}}).position,1/units);
			g.alignment[n]=dot(rotate(raw_shell.rotation,{1,0,0}),rotate(face.rotation,{0,0,1}));}
		const auto hidden=pose_parts(r,parts,p,v,hinge,solved);
		unsigned posed{};
		if(v.active && !v.fault && manipulation)
		{
			if(v.barrel_held){(void)constrain_part_hand(r,library,grip,targets,shoulders,body_axis,rear,compose(gun,barrel_wrist),solved);
				hands::pose_mirror::fingers(r,library,grip,p.barrel_grip.fingers,off,solved,off==1);posed=1u<<off;}
			else if(v.ammo.loader_hand==hand(off)){hands::pose_mirror::fingers(r,library,grip,p.shell_fingers,off,solved,off==1);posed=1u<<off;}
		}
		if(hand_motion)hand_motion->apply(off,!posed ? part_hand_attachment::free :
			v.barrel_held ? part_hand_attachment::barrel : part_hand_attachment::shell);
		const auto shell=compose(as_anchor(solved[r.arms[off].wrist]),shell_grip);
		scene_frame scene{input,owner,g,assembly,gameplay,shell,{},units,&p,manipulation,gun};scene.shell_world.position=add(scene.shell_world.position,offset);scene.gun_world.position=add(gun.position,offset);
		scene.binding={free_hand_rotation(grip,off),basis,true};
		for(unsigned n=0;n<p.ammunition.capacity;++n)scene.chambers_world[n]=compose(compose(scene.gun_world,barrel_local),p.chamber_in_barrel[n]);
		publish_scene(scene);
		const std::lock_guard lock(mutex);frame f{v,model,reinterpret_cast<std::uintptr_t>(object),reinterpret_cast<std::uintptr_t>(matrices),epoch,shell,scene.shell_world,units,input.sampled_at};f.view.owner=owner;
		auto* slot=&latest[0];for(auto& entry:latest)if(entry.view.owner.id()==owner.id()){slot=&entry;break;}else if(!entry.view.active || entry.at<slot->at)slot=&entry;
		*slot=f;poses[cursor++%poses.size()]=f;
		events.retain([](weapon_identity id){return carry::active() ? carry::contains(id) : !id.generation;});auto* water=events.acquire(owner.id());
		if(v.active && water)
		{
			if(water->instance!=v.ammo.instance_generation)*water={v.ammo.instance_generation,0};
			for(auto n=v.event_sequence>v.events.size() ? v.event_sequence-v.events.size()+1 : 1;n<=v.event_sequence;++n)
			{
				const auto& e=v.events[n%v.events.size()];if(e.sequence!=n || n<=water->sequence)continue;
				if(e.kind==effect::eject)for(unsigned b=0;b<p.ammunition.capacity;++b)if(e.ejected&(1u<<b))
					drops[drop_cursor++%drops.size()]={model,true,compose(e.chambers[b],p.case_in_shell),e.units,e.at,v.reference,
						p.scatter_ejected_rounds ? ejection_scatter::sample(v.ammo.instance_generation,e.sequence,b) : ejection_scatter::motion{}};
				if(e.kind==effect::discard)drops[drop_cursor++%drops.size()]={model,false,e.world,e.units,e.at,v.reference};
				water->sequence=n;
			}
		}
		return {true,hidden,posed};
	}
	class presenter_component final:public component_interface
	{
		void post_unpack()override
		{
			set_boundary_ready(4);scene_models::on_submit(submit);scene_models::on_prepare_placement(prepare);scheduler::loop(refresh,scheduler::pipeline::main,250ms);
			fastfiles::on_pre_unload([]{++level;for(auto& p:available)p=nullptr;{const std::lock_guard lock(mutex);latest={};poses={};drops={};events={};cursor=drop_cursor=0;}for(auto& p:retained)p.reset();});
		}
		void pre_destroy()override{alive=false;for(auto& p:available)p=nullptr;}
	};
}
REGISTER_COMPONENT(vr::gameplay::weapons::break_action::presenter_component)
