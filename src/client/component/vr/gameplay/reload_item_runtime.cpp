#include <std_include.hpp>
#include "reload_item_runtime.hpp"
#include "reload_item_compatibility.hpp"
#include "native_magazine_assets.hpp"
#include "cylinder_presenter.hpp"
#include "physical_reload_runtime.hpp"
#include "physical_reload_presenter.hpp"
#include "falling_rail_presentation.hpp"
#include "magazine_grip_selection.hpp"
#include "magazine_well_contact.hpp"
#include "equipment_runtime.hpp"
#include "knife_profile.hpp"
#include "component/vr/gameplay/hands/pose_mirror.hpp"
#include "hands/attachment_pose.hpp"
#include "hand_interaction/runtime.hpp"
#include "native_scripted_control.hpp"
#include "native_carry.hpp"
#include "weapon_feedback.hpp"
#include <utils/native_memory.hpp>
#include "../engine_stereo_view.hpp"
#include "component/scene_models.hpp"
#include "component/scene_submission_pool.hpp"
#include "component/scene_pose_match.hpp"
#include "component/scheduler.hpp"
#include "component/fastfiles.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "game/game.hpp"
#include "game/dvars.hpp"
#include "../settings.hpp"
#include "loader/component_loader.hpp"
#include <mutex>

namespace vr::gameplay::reload_items
{
	namespace
	{
		using namespace hands;using namespace hands::pose_math;
		namespace w=weapons;namespace hi=hand_interaction;namespace pr=w::physical_reload;
		struct metadata
		{
			const w::reload_profile* magazine{};const w::cylinder_profile* loader{};
			std::uint64_t reserve_key{};
			w::magazine_grip_selection grasp{};quat basis{0,0,0,1};
			std::array<catch_probe,2> probes{};
			w::weapon_identity target{};pr::well_motion well{};
			vec previous_center{};bool traced{};
		};
		struct displayed {item value{};metadata source{};anchor held_world{};};
		struct snapshot
		{
			std::array<displayed,capacity> items{};clock::time_point at{};std::uint64_t reference{};
			std::array<quat,2> basis{},mirror{};bool hands_ready{},enabled{};
		};
		inventory owned;std::array<metadata,capacity> sources{};
		std::array<key,2> proposed{};std::array<w::magazine_grip_selection,2> proposed_grasps{};std::array<quat,2> proposed_basis{};
		std::mutex publication; snapshot published;
		// Native scene entries retain lighting pointers. Freeze each submission's
		// identity and coarse placement until its queued preparation consumes it.
		struct render_ticket {displayed record{};anchor world{};};
		scene_models::submission_pool<render_ticket,2048,13> submissions;
		struct rail_state {key id{};std::uint64_t revision{},reference{};motion::rail_presentation motion{};};
		std::array<rail_state,capacity> displayed_rails{};
		std::atomic_bool alive{true};const void* player{};std::uint64_t timeline{},reference{};int game_time{};
		game::dvar_t* penalty{};
		std::uint64_t catches{},releases{},insertions{},refunds{},rejected{},lost_rounds{},waist_returns{};
		bool server()noexcept{return scheduler::is_executing(scheduler::pipeline::server);}
		bool running()noexcept{return alive && w::carry::active() && player && scripted_control::allowed(player);}
		visual models(const metadata& m,int rounds)noexcept
		{
			if(m.magazine)
			{
				visual out;const auto a=pr::magazine_assets::get(m.magazine,rounds);
				if(a.model){out.pieces[0]={a.model,a.in_magazine};out.count=1;}return out;
			}
			return m.loader?w::cylinder::loader_visual(m.loader,rounds):visual{};
		}
		bool bounds(const visual& model,float units,pr::contact_box& out)noexcept
		{
			if(!model || !std::isfinite(units) || units<=0)return false;
			const auto& a=model.pieces[0];const auto& b=a.model->bounds;
			vec mid{b.midPoint[0],b.midPoint[1],b.midPoint[2]},half{b.halfSize[0],b.halfSize[1],b.halfSize[2]};
			for(float v:half)if(!std::isfinite(v) || v<=0 || v>units)return false;
			for(float v:mid)if(!std::isfinite(v) || std::abs(v)>units*4)return false;
			out.pose=compose(a.in_item,{mid,{0,0,0,1}});out.pose.position=scale(out.pose.position,1/units);out.half=scale(half,1/units);return true;
		}
		hi::target target(key id)noexcept{return hi::object(hi::domain::reload_item,{id.slot+1,id.generation});}
		anchor held_pose(const metadata& m,const anchor& raw)noexcept
		{return compose({raw.position,normalize(multiply(raw.rotation,m.basis))},m.grasp.in_wrist);}
		void publish(const hi::frame* frame=nullptr)noexcept
		{
			const std::lock_guard lock(publication);published.at=clock::now();published.reference=reference;published.enabled=running();
			for(size_t n=0;n<capacity;++n)
			{
				auto& p=published.items[n];p.value=owned.items()[n];p.source=sources[n];
				if(frame && p.value.state==phase::held && vr::valid_hand(p.value.holder) && (frame->valid_hands&(1u<<int(p.value.holder))))
					p.held_world=held_pose(p.source,frame->wrists[int(p.value.holder)]);
			}
		}
		bool refund(w::weapon_identity origin,std::uint64_t reserve_key,int rounds)noexcept
		{
			auto observed=w::native_ammunition::observe_carried(player,origin);
			if(!observed.valid || w::native_ammunition::reserve_identity(player,origin)!=reserve_key)
			{
				// Return through an owned weapon with the proven SAME native ammo
				// pool, never by guessing caliber or magazine capacity. If none is
				// owned, retain settlement until it is; reset invalidates old escrow.
				observed={};
				const auto inventory=w::native_carry::observe();
				for(size_t i=0;inventory.valid && i<inventory.count;++i)if(w::native_ammunition::reserve_identity(player,inventory.owned[i].id)==reserve_key)
				{observed=w::native_ammunition::observe_carried(player,inventory.owned[i].id);if(observed.valid)break;}
			}
			if(!reserve_key || !observed.valid || rounds>1000000-observed.reserve)return false;
			if(!w::native_ammunition::commit_carried(observed,observed.loaded,observed.reserve+rounds))return false;
			++refunds;return true;
		}
		bool settle_item(key id)noexcept
		{
			const auto* live=owned.find(id);if(!live || live->state!=phase::settling)return false;
			const bool enabled=discard_penalty();
			const auto disposed=w::ammunition::dispose(live->rounds,live->disposition,enabled);
			const bool waist=live->disposition==w::ammunition::disposition_reason::waist_return;
			if(!owned.settle(id,[&](auto origin,int rounds){return refund(origin,sources[id.slot].reserve_key,rounds);},enabled))return false;
			lost_rounds+=disposed.lost;if(waist)++waist_returns;return true;
		}
		bool at_waist(const metadata& m,const hi::frame& frame,hand actor)noexcept
		{
			if(m.magazine)
			{
				const auto& p=*m.magazine;
				return frame.waist(actor,p.supply,p.interaction.waist_radius)<=p.interaction.waist_radius;
			}
			return m.loader && frame.waist(actor,{},m.loader->interaction.waist_radius)<=m.loader->interaction.waist_radius;
		}
		void synchronize()noexcept
		{
			const bool initialized=game::CL_IsCgameInitialized();const auto* ps=initialized?game::g_entities[0].client:nullptr;
			const int now=initialized?game::CG_GetGameTime(0):0;const auto next=w::native_ammunition::timeline();
			if(ps!=player || next!=timeline || now<game_time){owned.clear();sources={};proposed={};player=ps;timeline=next;}
			game_time=now;const auto current=controller_input::latest().reference_generation;
			if(current && current!=reference){owned.rebase(current);reference=current;for(auto& m:sources){m.probes={};m.target={};m.traced=false;}}
		}
		void physics(clock::time_point now,const hi::frame* frame)noexcept
		{
			for(const auto copy:owned.items())
			{
				if(copy.state==phase::falling)
				{
					auto& source=sources[copy.id.slot];anchor attached;const anchor* rail=nullptr;
					if(frame && source.magazine && copy.motion.rail_seconds>0)if(const auto* gun=frame->find(copy.origin))
					{attached=compose(gun->gun,source.magazine->magazine_rest);rail=&attached;}
					owned.advance(copy.id,now,rail);const auto* live=owned.find(copy.id);
					if(live && live->state==phase::falling && (live->motion.rail_seconds==0 || live->motion.detached))
					{
						pr::contact_box box;
						if(bounds(models(source,0),live->motion.units,box))
						{
							const auto root=live->motion.pose(now);auto local=box.pose;local.position=scale(local.position,live->motion.units);
							const auto placed=compose(root,local);const auto centre=placed.position;
							if(!std::all_of(centre.begin(),centre.end(),[](float x){return std::isfinite(x) && std::abs(x)<1e7f;}))
							{owned.expire(copy.id,w::ammunition::disposition_reason::forced_cleanup);continue;}
							game::trace_t hit{};game::Bounds volume{};
							for(int axis=0;axis<3;++axis)
							{
								vec side{};side[axis]=box.half[axis]*live->motion.units;const auto projected=rotate(placed.rotation,side);
								for(int n=0;n<3;++n)volume.halfSize[n]+=std::abs(projected[n]);
							}
							const auto previous=source.traced?source.previous_center:compose(copy.motion.start,local).position;
							game::G_TraceCapsule(&hit,previous.data(),centre.data(),&volume,0,0x280e831);
							if(!std::isfinite(hit.fraction) || hit.fraction<1 || hit.startsolid || hit.allsolid)owned.expire(copy.id);
							source.previous_center=centre;source.traced=true;
						}
					}
				}
				if(const auto* live=owned.find(copy.id);live && live->state==phase::settling)
					(void)settle_item(copy.id);
			}
		}
		w::magazine_grip_selection grasp_for(const metadata& m,const snapshot& binding,int actor,const anchor& raw,
			const anchor& falling,const body_pose::estimate& body)noexcept
		{
			if(m.magazine)
			{
				const auto gun=compose(falling,inverse(m.magazine->magazine_rest));
				const auto wrist=compose(inverse(gun),{raw.position,normalize(multiply(raw.rotation,binding.basis[actor]))});
				return w::select_magazine_grip(*m.magazine,wrist.rotation,actor,binding.mirror[actor],false,false,0,controller_in_body(raw.rotation,body));
			}
			const auto pose=actor?vr::gameplay::hands::pose_mirror::object_in_wrist({},m.loader->loader_in_wrist,binding.mirror[actor]):m.loader->loader_in_wrist;
			return {pose,{},0,m.loader->loader_fingers};
		}
		bool fill_item(const item& item,metadata& m,const hi::frame& frame,const anchor& world)noexcept
		{
			for(const auto& gun:frame.objects)if(gun.authored && gun.owner.can_fire() && gun.owner.rear!=item.holder && gun.owner.support!=item.holder)
			{
				if(m.magazine && gun.authored->reload && compatible(*m.magazine,*gun.authored->reload))
				{
					const auto& p=*gun.authored->reload;const auto live=pr::current(gun.owner.id());if(!live.active || live.fault || live.part_leased())continue;
					const auto tip=w::magazine_tip_in_well(p,gun.gun,world,frame.body.units_per_meter);
					if(m.target!=gun.owner.id())
					{
						m.target=gun.owner.id();m.well={tip,false,pr::sweep_well(tip,tip,p.interaction.well_radius+p.interaction.well_withdraw_margin,
							p.interaction.well_contact_depth+p.interaction.well_withdraw_margin,p.interaction.well_capture_below+p.interaction.well_withdraw_margin)};
						return false;
					}
					const auto step=pr::advance_well(p.interaction,m.well,tip,w::magazine_alignment(p,gun.gun,world),live.ammo.magazine_inserted,
						frame.input.sampled_at<live.insert_after);
					if(!step.insert)return false;
					if(pr::insert_recovered(gun.owner.id(),item.holder,item.rounds,*m.magazine,m.grasp.index,world))return true;
					m.well.contact=false;m.well.withdraw=true;++rejected;return false;
				}
				if(m.loader && item.rounds && gun.authored->cylinder && compatible(*m.loader,*gun.authored->cylinder))
					return w::cylinder::fill_recovered(gun.owner.id(),item.holder,item.rounds,*m.loader,world);
			}
			m.target={};return false;
		}
	}
	bool discard_penalty()noexcept{return penalty && penalty->current.enabled;}
	bool can_release(const w::reload_profile* p)noexcept
	{return server() && running() && owned.room() && p && bool(pr::magazine_assets::get(p,0).model);}
	bool can_release(const w::cylinder_profile* p)noexcept
	{return server() && running() && owned.room() && p && bool(w::cylinder::loader_visual(p,0));}
	key reserve(w::weapon_identity origin,const w::reload_profile* p,int rounds,const anchor& world,float units,std::uint64_t ref,clock::time_point at,bool ejected)noexcept
	{
		if(!can_release(p) || ref!=reference)return {};
		const auto ammo_key=w::native_ammunition::reserve_identity(player,origin);if(!ammo_key)return {};
		flight motion;motion.start=world;motion.units=units;motion.born=at;
		if(ejected){motion.rail=w::magazine_exit_translation(*p,units);motion.rail_seconds=p->presentation.magazine_exit_seconds;motion.advance(at);}
		const auto id=owned.reserve(kind::magazine,origin,rounds,p->ammunition.magazine_capacity,ref,motion);
		if(id){sources[id.slot]={};sources[id.slot].magazine=p;sources[id.slot].reserve_key=ammo_key;}return id;
	}
	key reserve(w::weapon_identity origin,const w::cylinder_profile* p,int rounds,const anchor& world,float units,std::uint64_t ref,clock::time_point at)noexcept
	{
		if(!can_release(p) || ref!=reference)return {};flight motion;motion.start=world;motion.units=units;motion.born=at;
		const auto ammo_key=w::native_ammunition::reserve_identity(player,origin);if(!ammo_key)return {};
		const auto id=owned.reserve(kind::speedloader,origin,rounds,p->ammunition.capacity,ref,motion);
		if(id){sources[id.slot]={};sources[id.slot].loader=p;sources[id.slot].reserve_key=ammo_key;}return id;
	}
	void complete_release(key id,bool committed)noexcept
	{if(server() && owned.activate(id,committed) && committed)++releases;}
	void collect_interactions(const hi::frame& frame)noexcept
	{
		if(!server())return;synchronize();proposed={};if(!running())return;
		physics(frame.input.sampled_at,&frame);snapshot binding;{const std::lock_guard lock(publication);binding=published;}
		if(!binding.hands_ready)return;
		for(int h=0;h<2;++h)
		{
			const auto actor=hand(h);const auto trigger=hi::input(actor,hi::button::trigger);
			if(!trigger.down || !trigger.armed || trigger.release || !(frame.valid_hands&(1u<<h)) || !hi::free(actor))
			{for(auto& m:sources)m.probes[h]={};continue;}
			if(trigger.press)for(auto& m:sources)m.probes[h]={};
			float best=INFINITY;
			for(const auto& item:owned.items())if(item.state==phase::falling && item.reference==frame.input.reference_generation && item.motion.alive(frame.input.sampled_at))
			{
				auto& m=sources[item.id.slot];pr::contact_box box;if(!bounds(models(m,0),item.motion.units,box))continue;
				const auto root=item.motion.pose(frame.input.sampled_at);auto grasp=grasp_for(m,binding,h,frame.wrists[h],root,frame.body.body);
				if(m.loader || !m.magazine->magazine_contacts)
				{auto centre=box.pose;centre.position=scale(centre.position,item.motion.units);grasp.contact=compose(grasp.in_wrist,centre).position;}
				const auto basis=m.magazine?w::magazine_wrist_basis(*m.magazine,grasp,binding.basis[h],false):binding.basis[h];
				const auto contact=compose({frame.wrists[h].position,normalize(multiply(frame.wrists[h].rotation,basis))},{grasp.contact,{0,0,0,1}}).position;
				pr::box_motion motion;motion.frame={scale(sub(root.position,contact),1/item.motion.units),root.rotation};motion.regions={box,box};
				if(!m.probes[h].test(item.id.generation,motion,frame.input.sampled_at))continue;
				const float distance=pr::closest_box(motion,0).distance;if(distance>=best)continue;
				best=distance;proposed[h]=item.id;proposed_grasps[h]=grasp;proposed_basis[h]=basis;
			}
			if(proposed[h])
				// Spatial contact is fresh intent while Trigger is held. event=0 is
				// deliberate: it is not a replay of the original digital press.
				hi::offer({actor,{target(proposed[h]),hi::role::supply,hi::button::trigger,hi::recipe::single,{}},0,10,best,1,true,true});
		}
	}
	void update_interactions()noexcept
	{
		const auto* frame=hi::simulation();if(!server() || !frame || !running())return;
		for(int h=0;h<2;++h)
		{
			const auto id=proposed[h];if(!id || !hi::granted(hand(h),hi::domain::reload_item,{id.slot+1,id.generation}))continue;
			if(owned.catch_item(id,hand(h),frame->input.sampled_at))
			{auto& m=sources[id.slot];m.grasp=proposed_grasps[h];m.basis=proposed_basis[h];m.target={};m.probes={};m.traced=false;++catches;
				w::feedback::carry_confirmation(hand(h),frame->input);}
		}
		for(const auto copy:owned.items())if(copy.state==phase::held)
		{
			const int h=int(copy.holder);auto& m=sources[copy.id.slot];
			if(!(frame->valid_hands&(1u<<h)))continue;
			if(!hi::has(copy.holder,hi::domain::reload_item)) {owned.expire(copy.id,w::ammunition::disposition_reason::forced_cleanup);continue;}
			const auto world=held_pose(m,frame->wrists[h]);const auto trigger=hi::input(copy.holder,hi::button::trigger);
			if(trigger.release || (frame->input.trigger[h].active && !trigger.down))
			{
				if(at_waist(m,*frame,copy.holder))
				{owned.expire(copy.id,w::ammunition::disposition_reason::waist_return);(void)settle_item(copy.id);continue;}
				flight motion;motion.start=world;motion.units=frame->body.units_per_meter;motion.born=frame->input.sampled_at;
				if(owned.release(copy.id,copy.holder,motion)){m.probes={};m.target={};m.traced=false;++releases;}continue;
			}
			if(fill_item(copy,m,*frame,world) && owned.inserted(copy.id,copy.holder,copy.rounds))++insertions;
		}
		publish(frame);
	}
	void report_interactions()noexcept
	{
		if(!server())return;synchronize();for(const auto& i:owned.items())if(i.state==phase::held)
			hi::observed(i.holder,{target(i.id),hi::role::supply,hi::button::trigger,hi::recipe::single,{}});
	}
	void lifecycle(bool suspended)noexcept
	{
		if(!server())return;synchronize();
		if(!suspended && running())physics(clock::now(),nullptr);
		if(!w::carry::active())for(const auto i:owned.items())if(i.owned())
		{owned.expire(i.id,w::ammunition::disposition_reason::forced_cleanup);(void)settle_item(i.id);}
		publish();
	}
	bool settle_inventory()noexcept
	{
		if(!server())return false;synchronize();bool ok=true;
		for(const auto item:owned.items())if(item.owned())
		{
			if(item.state!=phase::settling)owned.expire(item.id,w::ammunition::disposition_reason::forced_cleanup);
			ok=settle_item(item.id)&&ok;
		}
		publish();return ok;
	}
	std::array<std::uint64_t,2> present(const hands::interaction_rig& parts,const hands::rig& r,const controller_input::frame& input,
		const std::array<anchor,2>& targets,std::span<hands::bone> solved,unsigned occupied,unsigned visible)noexcept
	{
		std::array<std::uint64_t,2> tokens{};if(!parts.valid || r.count<=0 || r.count>256 || solved.size()<size_t(r.count))return tokens;
		snapshot s;{const std::lock_guard lock(publication);published.hands_ready=true;published.basis=parts.basis;
			for(int h=0;h<2;++h)published.mirror[h]=parts.library.mirror_basis[r.arms[h].wrist];s=published;}
		if(!s.enabled || !input.focused || s.reference!=input.reference_generation)return tokens;
		for(const auto& record:s.items)if(record.value.state==phase::held)
		{
			const int h=int(record.value.holder);if(h<0 || h>1 || r.arms[h].wrist<0 || r.arms[h].wrist>=r.count ||
				!(visible&(1u<<h)) || (occupied&(1u<<h)) || !input.grip[h].valid || !input.aim[h].valid)continue;
			const auto plan=hi::pose(hand(h));if(plan.driver!=target(record.value.id))continue;
			const auto& m=record.source;move_part(r,r.arms[h].wrist,{solved[r.arms[h].wrist].position,normalize(multiply(targets[h].rotation,m.basis))},solved);
			vr::gameplay::hands::pose_mirror::fingers(r,parts.library,hands::native_hand_schema::definition,m.grasp.fingers,h,solved,h==1);
			tokens[h]=record.value.pose_revision;
		}
		return tokens;
	}
	namespace
	{
		template<class T> bool read(const void* p,size_t offset,T& v)noexcept
		{return p && utils::native_memory::read_bytes(&v,static_cast<const std::byte*>(p)+offset,sizeof(v));}
		void submit()
		{
			snapshot s;{const std::lock_guard lock(publication);s=published;}
			if(!alive || !s.enabled || !scripted_control::predicted_allowed())return;const auto now=clock::now();
			for(size_t slot=0;slot<capacity;++slot)
			{
				const auto& r=s.items[slot];const auto& i=r.value;
				if(i.state!=phase::held && i.state!=phase::falling)continue;
				if(i.reference!=s.reference || (i.state==phase::falling && !i.motion.alive(now)))continue;
				const auto visual=models(r.source,i.rounds);const auto root=i.state==phase::held?r.held_world:i.motion.pose(now);
					const auto ticket=submissions.acquire({r,root},now);if(!ticket)continue;
				for(size_t part=0;part<visual.count;++part)
				{
					const auto world=compose(root,visual.pieces[part].in_item);game::GfxScaledPlacement place{};place.scale=1;
					std::copy(world.position.begin(),world.position.end(),place.base.origin);std::copy(world.rotation.begin(),world.rotation.end(),place.base.quat);
					float white[]{1,1,1,1};scene_models::submit(visual.pieces[part].model,&place,0,submissions.handle(*ticket,part),white,white,white,.25f*i.motion.units);
				}
			}
		}
		scene_models::placement_result prepare(const scene_models::preparation& preparing,const void* entry,game::GfxPlacement& placed,game::GfxPlacement& previous)noexcept
		{
			using result=scene_models::placement_result;
			const auto lease=submissions.lookup(entry);if(!lease)return result::unchanged;
			const auto& ticket=lease->payload;const auto part=lease->part;displayed live;
			{const std::lock_guard lock(publication);if(ticket.record.value.id)live=published.items[ticket.record.value.id.slot];}
			const auto& frozen=ticket.record;const auto now=clock::now();
			if(!alive || !frozen.value.id || frozen.value.id!=live.value.id || frozen.value.pose_revision!=live.value.pose_revision ||
				frozen.value.holder!=live.value.holder || frozen.value.state!=live.value.state || frozen.value.reference!=controller_input::latest().reference_generation ||
				!scene_models::submission_pool<render_ticket,2048,13>::fresh(*lease,now))return result::omit;
			const auto visual=models(frozen.source,frozen.value.rounds),current=models(live.source,live.value.rounds);game::XModel* model{};
			if(part>=visual.count || part>=current.count || !scene_models::native_entry::model(entry,model) || model!=visual.pieces[part].model || model!=current.pieces[part].model)return result::omit;
			const auto submitted_pose=compose(ticket.world,visual.pieces[part].in_item);
			if(!scene_models::same_placement(submitted_pose.position,submitted_pose.rotation,placed))return result::omit;
			anchor root;
			if(frozen.value.state==phase::falling)
			{
				// The submission is only coarse admission. Pack the current flight
				// at one job-wide time, shared by both eyes and all loader pieces.
				const auto at=std::max(preparing.at,lease->at);
				if(!frozen.value.motion.alive(at))return result::omit;
				bool follow{};
				{
					const std::lock_guard lock(publication);auto& state=displayed_rails[frozen.value.id.slot];
					if(state.id!=frozen.value.id || state.revision!=frozen.value.pose_revision || state.reference!=frozen.value.reference)
					{state={frozen.value.id,frozen.value.pose_revision,frozen.value.reference,{}};}
					follow=state.motion.needs_parent(frozen.value.motion,at);
				}
				anchor attached;const anchor* rail=nullptr;
				if(follow && frozen.source.magazine &&
					pr::magazine_rail_for_record(preparing,frozen.value.origin,frozen.source.magazine,frozen.value.reference,attached))rail=&attached;
				{
					const std::lock_guard lock(publication);auto& state=displayed_rails[frozen.value.id.slot];
					if(state.id!=frozen.value.id || state.revision!=frozen.value.pose_revision || state.reference!=frozen.value.reference)return result::omit;
					root=state.motion.pose(frozen.value.motion,at,rail);
				}
			}
			else if(frozen.value.state==phase::held)
			{
				std::array<float,12> camera{};vec origin{};hands::attachments::solved pose;const auto h=unsigned(frozen.value.holder);
				if(h>1 || !read(preparing.record,engine_stereo_view::h2_view_origin_offset,camera) ||
					!hands::attachments::for_record(preparing.record,camera,pose,int(h),frozen.value.pose_revision) ||
					pose.reference!=frozen.value.reference || pose.reload_items[h]!=frozen.value.pose_revision ||
					!read(preparing.record,engine_stereo_view::h2_current_model_placement_origin_offset,origin))return result::omit;
				for(float x:origin)if(!std::isfinite(x) || std::abs(x)>1e7f)return result::omit;
				root=compose({add(pose.wrists[h].position,origin),pose.wrists[h].rotation},frozen.source.grasp.in_wrist);
			}
			else return result::omit;
			const auto world=compose(root,visual.pieces[part].in_item);
			std::copy(world.position.begin(),world.position.end(),placed.origin);std::copy(world.rotation.begin(),world.rotation.end(),placed.quat);previous=placed;return result::replace;
		}
	}
	class component final:public component_interface
	{
		void post_unpack()override
		{
			penalty=dvars::register_bool(settings::discard_ammo_penalty.name,settings::discard_ammo_penalty.default_value,
				game::DVAR_FLAG_SAVED,"Lose remaining rounds in discarded magazines and revolver loads; return held containers to the waist to recover them");
			scene_models::on_submit(submit);scene_models::on_prepare_placement(prepare);
			fastfiles::on_pre_unload([]{submissions.clear_after_drain();});
			command::add("vr_reloadItems_status",[]{scheduler::once([]{
				console::info("[VR reload items] penalty=%d escrow=%lld catches=%llu releases=%llu insertions=%llu refunds=%llu rejected=%llu lost_rounds=%llu waist_returns=%llu\n",discard_penalty(),owned.rounds(),catches,releases,insertions,refunds,rejected,lost_rounds,waist_returns);
				for(const auto& i:owned.items())if(i.owned())console::info("item=%u:%llu kind=%d phase=%d hand=%d rounds=%d origin=%u:%llu\n",i.id.slot,i.id.generation,int(i.type),int(i.state),int(i.holder),i.rounds,i.origin.weapon,i.origin.generation);
			},scheduler::pipeline::server);});
		}
		void pre_destroy()override{alive=false;}
	};
}
REGISTER_COMPONENT(vr::gameplay::reload_items::component)
