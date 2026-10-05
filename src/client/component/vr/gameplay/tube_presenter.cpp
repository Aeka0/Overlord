#include <std_include.hpp>
#include "tube_presenter.hpp"
#include "falling_item_presenter.hpp"
#include "weapon_instance_cache.hpp"
#include "weapon_carry_runtime.hpp"
#include "component/vr/gameplay/hand_pose_mirror.hpp"
#include "part_hand_constraint.hpp"
#include "part_return_transition.hpp"
#include "tube_profiles.hpp"
#include "chambering_guide.hpp"
#include "chambering_guide_presenter.hpp"
#include "interaction_debug.hpp"
#include "viewmodel_visibility.hpp"
#include "component/scene_models.hpp"
#include "component/scene_rigid_part.hpp"
#include "component/fastfiles.hpp"
#include "component/scheduler.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <mutex>

namespace vr::gameplay::weapons::tube
{
	namespace
	{
		using namespace hands;using namespace hands::pose_math;
		struct asset{scene_models::rigid_part shell;game::XModel* source{};game::XSurface* surfaces{};std::uint64_t level{};};
		std::array<std::unique_ptr<asset>,32> retained{};std::array<std::atomic<asset*>,tube_definitions.capacity()> available{};
		std::atomic_uint64_t level{1};std::atomic_bool alive{true};
		std::mutex mutex;
		struct released{presentation::event event{};asset* model{};std::uint64_t level{},reference{};};
		std::array<released,16> drops{};size_t drop_cursor{};
		struct watermark{std::uint64_t generation{},sequence{};};instance_cache<watermark,512> events;
		instance_cache<physical_reload::part_return_transition,512> bolt_returns;
		void refresh()
		{
			if(!alive || !game::CL_IsCgameInitialized())return;
			for(size_t skin=0;skin<tube_definitions.size();++skin)
			{
				const auto& p=*tube_definitions[skin];
				auto* source=game::DB_FindXAssetHeader(game::ASSET_TYPE_XMODEL,p.receiver.data(),0).model;
				if(!source || !source->name || std::string_view(source->name)!=p.receiver || source->numBones!=p.receiver_bones){available[skin]=nullptr;continue;}
				asset* found{};
				for(auto& entry:retained)if(entry && entry->source==source && entry->surfaces==source->lodInfo[0].surfs && entry->level==level){found=entry.get();break;}
				if(found){available[skin]=found;continue;}
				if(!scene_models::ready() || !viewmodel_visibility::ready(part_visibility::rigid_groups))continue;
				int shell=-1;for(int n=0;n<source->numBones;++n){const char* name=game::SL_ConvertToString(source->boneNames[n]);if(name && std::string_view(name)==p.shell_bone)shell=n;}
				if(shell<0)continue;
				for(auto& entry:retained)if(!entry)
				{
					auto model=std::make_unique<asset>();if(!model->shell.create(source,shell))break;
					const std::array<scene_models::runtime_model,1> identities{{{model->shell.model(),model->shell.source()}}};
					if(!scene_models::register_runtime_models(identities))break;
					model->source=source;model->surfaces=source->lodInfo[0].surfs;model->level=level;
					entry=std::move(model);available[skin]=entry.get();break;
				}
			}
		}
		void submit()
		{
			if(!alive || !game::CL_IsCgameInitialized())return;
			std::array<released,16> copy;{const std::lock_guard lock(mutex);copy=drops;}
			const auto now=clock::now();const auto reference=controller_input::latest().reference_generation;
			for(size_t i=0;i<copy.size();++i)
			{
				const auto& d=copy[i];const float age=std::chrono::duration<float>(now-d.event.at).count();
				if(!d.model || d.level!=level || d.model->level!=d.level || d.reference!=reference || age<0 || age>1.2f || d.event.units<=0)continue;
				const auto velocity=d.event.kind==effect::rack_open ? rotate(d.event.world.rotation,{0,-1.2f*d.event.units,.4f*d.event.units}) : vec{0,0,-.15f*d.event.units};
				const auto bind=d.model->shell.bind();const anchor rest{{bind[0],bind[1],bind[2]},{bind[3],bind[4],bind[5],bind[6]}};
				falling_item_presentation::motion falling;falling.path.start=d.event.world;falling.path.velocity=velocity;
				falling.path.born=d.event.at;falling.path.units=d.event.units;falling.local=inverse(rest);
				falling_item_presentation::submit(d.model->shell.model(),falling,d.reference,.25f*d.event.units);
			}
		}
	}
	part_presentation::result present(const void* object,std::uint32_t epoch,const void* matrices,
		const part_rig& parts,const hands::rig& r,const hands::pose_library& library,const weapons::profile& grip,
		const controller_input::frame& input,const hold& owner,std::uint64_t assembly,bool gameplay,bool manipulation,
		const std::array<hands::anchor,2>& targets,const std::array<hands::vec,2>& shoulders,const std::array<hands::vec,3>& body_axis,
		hands::vec head,hands::vec offset,float units,std::span<hands::bone> solved,clock::time_point now,hands::part_hand_frame* hand_motion)noexcept
	{
		using namespace hands;using namespace hands::pose_math;
		if(!alive || !parts.valid || !grip.tube || !valid_hand(owner.holding_hand()) || !std::isfinite(units) || units<=0 || solved.size()<size_t(r.count))return {};
		const auto& p=*grip.tube;auto v=current(owner.id());v.lever=displayed_lever_pose(v,owner,input.reference_generation,now);const int rear=int(owner.holding_hand()),off=1-rear;
		unsigned posed_hands{};
		auto gun=as_anchor(solved[r.gun]);
		if(p.lever && v.active && owner.can_fire())
		{
			const auto motion=lever::gun_offset(*p.lever,v.lever,owner.rear);
			gun=lever::apply_gun_pose(gun,motion,grip.wrists[rear],owner.support==hand(off)?&grip.wrists[off]:nullptr);
			move_part(r,r.gun,gun,solved);
			move_part(r,r.arms[rear].wrist,compose(gun,compose(inverse(motion),grip.wrists[rear])),solved);
			if(v.lever.open>0 || v.lever.spinning)
			{
				std::array<joint_pose,64> fingers{};size_t count{};
				for(const auto& joint:p.lever->open_fingers)if(count<fingers.size())
				{
					auto value=joint;
					value.rotation=lever::finger_rotation(*p.lever,count,v.lever);
					fingers[count++]=value;
				}
				hands::pose_mirror::fingers(r,library,grip,{fingers.data(),count},rear,solved,rear==0);
			}
			if(owner.support==hand(off))
				(void)constrain_part_hand(r,library,grip,targets,shoulders,body_axis,rear,compose(gun,grip.wrists[off]),solved,1.f);
		}
		const auto inverse_gun=inverse(gun);
		const auto shell_in_wrist=off ? hands::pose_mirror::object_in_wrist({},p.shell_in_wrist,library.mirror_basis[r.arms[off].wrist]) : p.shell_in_wrist;
		if(!rotary(p.ammunition) && !levered(p.ammunition) && (p.rack_grips.empty() || p.rack_grips.size()>max_part_grips || p.rack_grips.size()!=p.interaction.rack.slide_pose_count))return {};
		std::array<part_grip_pose,max_part_grips> rack_poses{};
		for(size_t i=0;i<p.rack_grips.size();++i)rack_poses[i]=off ? hands::pose_mirror::part(p.rack_grips[i],library.mirror_basis[r.arms[off].wrist]) : p.rack_grips[i];
		const anchor raw{targets[off].position,normalize(multiply(targets[off].rotation,free_hand_rotation(grip,off)))};
		const auto raw_local=compose(inverse_gun,raw);const auto shell_local=compose(raw_local,shell_in_wrist);
		const auto centre=compose(shell_local,{p.shell_center,{0,0,0,1}}).position;
		float travel=v.active && v.ammo.phase==action::locked_open ? p.interaction.rack.locked_travel : 0;
		if(manual(p.ammunition) && v.active)travel=v.travel;
		if(v.active && v.rack_held && input.focused && gameplay && input.grip[off].valid && input.aim[off].valid &&
			v.reference==input.reference_generation && v.owner.rear_revision==owner.rear_revision)
			travel=physical_reload::constrained_slide_travel(p.interaction.rack,v.rack_grip,scale(raw_local.position,1/units));
		else if(!manual(p.ammunition) && v.active && v.ammo.phase==action::held_open)travel=p.interaction.rack.slide_stroke;
		float bolt_travel=travel;
		if(v.active && p.bolt_return_seconds>0 && !manual(p.ammunition) && !rotary(p.ammunition))
		{
			const std::lock_guard lock(mutex);
			bolt_returns.retain([](weapon_identity id){return carry::active()?carry::contains(id):!id.generation;});
			if(auto* motion=bolt_returns.acquire(owner.id()))
				bolt_travel=motion->update(v.ammo.instance_generation,input.reference_generation,v.rack_held,travel,now,p.bolt_return_seconds);
		}
		// Reciprocation is a visual overlay, never the return spring's mechanical target.
		if(!manual(p.ammunition) && v.active && !v.rack_held && v.ammo.phase==action::closed && now>=v.shot_at)
		{const float age=std::chrono::duration<float>(now-v.shot_at).count();if(age<.10f)bolt_travel=std::max(bolt_travel,.1097324f*std::max(0.f,1.f-std::abs(age-.025f)/.075f));}
		const auto rack_offset=scale(p.interaction.rack.slide_axis,travel*units);
		const auto candidate=choose_part_grip({rack_poses.data(),p.rack_grips.size()},raw_local,rack_offset,p.rack_low,p.rack_high,units);
		geometry g{true,owner.weapon,v.active ? v.ammo.instance_generation : 0,input.reference_generation,input.sequence};
		const auto supply=body_supply_volumes(head,body_axis,units,{},p.interaction.rack.waist_radius);
		for(const auto& volume:supply)g.waist_distance=std::min(g.waist_distance,volume.distance(raw.position)/units);
		g.rack_pose=candidate.pose;g.rack_distance=(rotary(p.ammunition) || levered(p.ammunition)) ? 10.f : candidate.distance_meters;g.hand_in_gun=scale(raw_local.position,1/units);
		g.port_delta=scale(sub(centre,p.port_center),1/units);g.tube_delta=scale(sub(centre,p.tube_center),1/units);
		const auto forward=rotate(shell_local.rotation,{1,0,0});g.port_alignment=dot(forward,p.port_forward);g.tube_alignment=dot(forward,p.tube_forward);
		if(v.active && !v.fault && v.rack_held && v.rack_grip.pose<p.rack_grips.size() && (manipulation || (manual(p.ammunition) && owner.support==hand(off))))
		{
			const auto& rack_pose=rack_poses[v.rack_grip.pose];
			auto wrist=rack_pose.wrist;wrist.position=add(wrist.position,rack_offset);
			if(constrain_part_hand(r,library,grip,targets,shoulders,body_axis,rear,compose(gun,wrist),solved))
			{hands::pose_mirror::fingers(r,library,grip,rack_pose.fingers,off,solved,off==1);posed_hands|=1u<<off;}
		}
		auto bolt=p.bolt_rest;
		if(pumped(p.ammunition))
		{
			const float fraction=std::clamp(travel/p.interaction.rack.slide_stroke,0.f,1.f);
			bolt.position=add(bolt.position,scale(sub(p.bolt_open.position,p.bolt_rest.position),fraction));
			auto pump=p.pump_rest;pump.position=add(pump.position,rack_offset);move_part(r,parts.pump,compose(gun,pump),solved);
		}
		else bolt.position=add(bolt.position,scale(p.interaction.rack.slide_axis,bolt_travel*units));
		if(!p.lever && parts.bolt>=0)move_part(r,parts.bolt,compose(gun,bolt),solved);
		if(rotary(p.ammunition) && parts.drum>=0)
		{
			auto drum=p.drum_rest;
			const float half=-3.14159265359f*float(v.active ? v.ammo.drum_index : 0)/float(p.ammunition.capacity);
			const auto axis=scale(p.drum_axis,std::sin(half));
			drum.rotation=normalize(multiply(drum.rotation,{axis[0],axis[1],axis[2],std::cos(half)}));
			move_part(r,parts.drum,compose(gun,drum),solved);
		}
		const bool loading=v.active && now>=v.load_at && now-v.load_at<std::chrono::milliseconds(160);
		const bool cover_open=rotary(p.ammunition) && v.active && v.ammo.loader_hand!=hand::none;
		if(p.lever)
		{
			for(size_t n=0;n<p.lever->parts.size();++n)
				move_part(r,parts.lever_parts[n],compose(gun,lever::blend(p.lever->parts[n].closed,p.lever->parts[n].open,v.active?v.lever.open:0)),solved);
		}
		else move_part(r,parts.lifter,compose(gun,loading || cover_open ? p.lifter_loaded : p.lifter_rest),solved);
		if(hand_motion)hand_motion->apply(off,!v.active || v.fault ? part_hand_attachment::free :
			v.rack_held ? part_hand_attachment::rack : v.ammo.loader_hand==hand(off) ? part_hand_attachment::shell : part_hand_attachment::free);
		const auto visual_shell=compose(as_anchor(solved[r.arms[off].wrist]),shell_in_wrist);
		part_mask hidden{};
		// Source animations contain duplicate display rounds. The feed owns one
		// live shell entity; neither duplicate may leak from a native reload pose.
		if(p.lever)for(const int root:parts.hidden_rounds)if(root>=0)
			for(int i=0;i<r.count;++i)if(descendant(i,root,r))hidden[i/32]|=0x80000000u>>(i%32);
		if(v.active && !v.fault && v.ammo.loader_hand==hand(off))
		{move_part(r,parts.shell,visual_shell,solved);hands::pose_mirror::fingers(r,library,grip,p.shell_fingers,off,solved,off==1);posed_hands|=1u<<off;}
		else if(manual(p.ammunition) && v.active && v.ammo.phase==action::held_open && v.ammo.chamber)move_part(r,parts.shell,compose(gun,p.port_shell),solved);
		else for(int i=0;i<r.count;++i)if(descendant(i,parts.shell,r))hidden[i/32]|=0x80000000u>>(i%32);
		const auto ejection=compose(gun,tube_ejection_pose(p));
		scene_frame scene{input,owner,g,assembly,gameplay,ejection,visual_shell,units,&p,manipulation};
		scene.binding={free_hand_rotation(grip,off),library.mirror_basis[r.arms[off].wrist],true};
		for(int h=0;h<2;++h)scene.shell_bindings[h]={free_hand_rotation(grip,h),library.mirror_basis[r.arms[h].wrist],true};
		scene.has_shell_bindings=true;
		scene.ejection_world.position=add(scene.ejection_world.position,offset);scene.shell_world.position=add(scene.shell_world.position,offset);publish_scene(scene);
		if(chambering_guide::enabled() && chambering_guide::supported(p))
		{
			const auto guide=chambering_guide::request(grip);
			chambering_guide::sample sample;
			sample.object=reinterpret_cast<std::uintptr_t>(object);sample.matrices=reinterpret_cast<std::uintptr_t>(matrices);sample.epoch=epoch;
			sample.owner=owner;sample.instance=v.ammo.instance_generation;sample.reference=input.reference_generation;sample.at=input.sampled_at;sample.tube=&p;
			if(v.active && !v.fault && gameplay && input.focused && v.reference==input.reference_generation &&
				parts.bolt>=0 && chambering_guide::needed(p,v.ammo,v.rack_held))sample.add(guide.bones[0],as_anchor(solved[parts.bolt]));
			chambering_guide::publish(sample);
		}
		if(interaction::debug::body_enabled() && gameplay && input.focused)
		{
			interaction::debug::supply_sample diagnostic{supply,add(raw.position,offset),owner,units,off,input.reference_generation,input.sampled_at};
			for(auto& volume:diagnostic.volumes)volume.origin=add(volume.origin,offset);interaction::debug::publish_supply(diagnostic);
		}
		asset* model{};for(size_t skin=0;skin<tube_definitions.size();++skin)if(tube_definitions[skin]==&p){model=available[skin].load();break;}
		if(v.active)
		{
			const std::lock_guard lock(mutex);
			events.retain([](weapon_identity id){return carry::active() ? carry::contains(id) : !id.generation;});
			auto* event_ptr=events.acquire(owner.id());if (!event_ptr) return {true,hidden,posed_hands};
			auto& e=*event_ptr;if(e.generation!=v.ammo.instance_generation)e={v.ammo.instance_generation,0};
			for(auto n=v.event_sequence>v.events.size() ? v.event_sequence-v.events.size()+1 : 1;n<=v.event_sequence;++n)
			{
				const auto& event=v.events[n%v.events.size()];if(n<=e.sequence || event.sequence!=n)continue;
				if(model && event.live && (event.kind==effect::discard || event.kind==effect::rack_open))drops[drop_cursor++%drops.size()]={event,model,level.load(),v.reference};
				e.sequence=n;
			}
		}
		return {true,hidden,posed_hands};
	}
	class presenter_component final:public component_interface
	{
		void post_unpack()override
		{
			set_boundary_ready(4);scene_models::on_submit(submit);scheduler::loop(refresh,scheduler::pipeline::main,250ms);
			fastfiles::on_pre_unload([] {
				++level;for(auto& p:available)p=nullptr;
				{const std::lock_guard lock(mutex);drops={};drop_cursor=0;events={};bolt_returns={};}
				for(auto& model:retained)model.reset();
			});
		}
		void pre_destroy()override{alive=false;for(auto& p:available)p=nullptr;}
	};
}
REGISTER_COMPONENT(vr::gameplay::weapons::tube::presenter_component)
