#include <std_include.hpp>
#include "special_equipment_runtime.hpp"
#include "native_claymore.hpp"
#include "native_ammunition.hpp"
#include "claymore_profile.hpp"
#include "notebook_runtime.hpp"
#include "signal_flare_runtime.hpp"
#include "cliffhanger_runtime.hpp"
#include "designator_events.hpp"
#include "abdominal_interaction.hpp"
#include "native_scripted_control.hpp"
#include "part_hand_constraint.hpp"
#include "hand_attachment_pose.hpp"
#include "hand_interaction/runtime.hpp"
#include "weapon_feedback.hpp"
#include "../engine_stereo_view.hpp"
#include <utils/native_memory.hpp>
#include "component/scene_models.hpp"
#include "component/scene_rigid_part.hpp"
#include "component/fastfiles.hpp"
#include "component/scheduler.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "game/dvars.hpp"
#include "loader/component_loader.hpp"
#include <utils/io.hpp>

namespace vr::gameplay::equipment::special
{
	namespace
	{
		using namespace hands;using namespace hands::pose_math;namespace hi=hand_interaction;
		using clock=controller_input::clock;
		struct resources
		{
			unsigned weapon{};game::XModel* world{};game::XModel lod0{};
			std::unique_ptr<scene_models::rigid_part> held,preview;
			float bottom{};vec center{};
		};
		struct snapshot
		{
			native::selection selected;grasp_state grip;anchor held{};native::placement preview{};
			std::array<quat,2> basis{},mirror{};bool hands_ready{};std::uint64_t reference{};clock::time_point at{};
			std::shared_ptr<resources> assets;
		};
		std::mutex mutex;snapshot published,submitted;
		grasp_state grip;native::selection selection;anchor held{};native::placement preview{};
		grab_intent draw_intent;
		std::uint64_t hold_reference{},hold_timeline{},candidate_timeline{};
		std::array<weapons::native_carry::world_key,2> candidates{};
		std::shared_ptr<resources> assets;std::vector<std::shared_ptr<resources>> retained;
		std::array<unsigned short,3> lighting{};
		std::atomic_bool alive{true},ready{};game::dvar_t* enabled{};
		std::atomic_uint64_t takes{},placed{},recovered{},rejected{};
		std::atomic<const char*> reason{"waiting for action-slot equipment"};
		bool running(){return alive && ready && enabled && enabled->current.enabled && weapons::carry::active();}
		bool fresh(clock::time_point at){const auto now=clock::now();return now>=at && now-at<=150ms;}
		hi::target item_target(unsigned part=0){return hi::object(hi::domain::special,{selection.slot.weapon,grip.revision},part);}
		anchor pose(const std::array<anchor,2>& wrists,const std::array<quat,2>& basis,const grasp_state& state)
		{
			if(!state.held())return {};
			const auto h=unsigned(state.primary);auto wrist=wrists[h];wrist.rotation=normalize(multiply(wrist.rotation,basis[h]));
			auto root=compose(wrist,inverse(authored::wrists[h]));
			if(vr::valid_hand(state.support))
			{
				const auto other=unsigned(state.support);const auto a=rotate(root.rotation,sub(authored::wrists[other].position,authored::wrists[h].position));
				const auto b=sub(wrists[other].position,wrists[h].position);
				if(length(b)>1 && length(a)>1){root.rotation=normalize(multiply(from_to(a,b),root.rotation));root.position=sub(wrists[h].position,rotate(root.rotation,authored::wrists[h].position));}
			}
			return root;
		}
		void publish(const hi::frame* f=nullptr)
		{
			const std::lock_guard lock(mutex);published.selected=selection;published.grip=grip;published.held=held;published.preview=preview;
			if(f){published.at=f->input.sampled_at;published.reference=f->input.reference_generation;}
		}
		void refresh()
		{
			if(!alive || !game::CL_IsCgameInitialized() || !scene_models::ready())return;
			native::selection chosen;{const std::lock_guard lock(mutex);chosen=published.selected;}
			if(!chosen.slot || !chosen.model || (assets && assets->weapon==chosen.slot.weapon))return;
			if(!chosen.model->numBones || !chosen.model->numLods || !chosen.model->numsurfs || chosen.model->numCompositeModels)return;
			for(unsigned c=0;c<3;++c)if(!std::isfinite(chosen.model->bounds.midPoint[c]) || std::abs(chosen.model->bounds.midPoint[c])>10000 ||
				!std::isfinite(chosen.model->bounds.halfSize[c]) || chosen.model->bounds.halfSize[c]<0 || chosen.model->bounds.halfSize[c]>1000)return;
			for(const auto& previous:retained)if(previous->weapon==chosen.slot.weapon && previous->world==chosen.model){assets=previous;const std::lock_guard lock(mutex);published.assets=previous;return;}
			if(retained.size()>=16){reason="equipment asset cache full";return;}
			auto next=std::make_shared<resources>();next->weapon=chosen.slot.weapon;next->world=chosen.model;
			next->center={chosen.model->bounds.midPoint[0],chosen.model->bounds.midPoint[1],chosen.model->bounds.midPoint[2]};
			next->bottom=chosen.model->bounds.midPoint[2]-chosen.model->bounds.halfSize[2];
			if(chosen.claymore)
			{
				auto* source=game::DB_FindXAssetHeader(game::ASSET_TYPE_XMODEL,"viewmodel_claymore",0).model;
				if(!source || !source->name || std::string_view(source->name)!="viewmodel_claymore" || source->numBones!=5) return;
				next->held=std::make_unique<scene_models::rigid_part>();const std::array<unsigned,5> bones{0,1,2,3,4};
				if(!next->held->create(source,0,bones)){reason=next->held->status();return;}
				game::Material* objective{};
				fastfiles::enum_assets(game::ASSET_TYPE_MATERIAL,[&](game::XAssetHeader header){
					auto* m=header.material;if(objective || !m || !m->techniqueSet || !m->techniqueSet->name)return;
					if(std::string_view(m->techniqueSet->name)!="m_unlit_add_lin_ndw_cltrans_objective" || !m->constantTable || m->constantCount>64)return;
					for(unsigned i=0;i<m->constantCount;++i){const auto& c=m->constantTable[i];
						if(std::string_view(c.name,strnlen(c.name,sizeof(c.name)))=="colorObjMax" && c.literal[0]>.8f && c.literal[1]>.6f && c.literal[2]<.2f)objective=m;}
				},false);
				if(!objective){reason="native objective preview material not loaded";return;}
				// Private LOD0 descriptor: the source world asset retains all native LODs.
				next->lod0=*chosen.model;next->lod0.numLods=1;next->lod0.numsurfs=static_cast<unsigned char>(chosen.model->lodInfo[0].numsurfs);
				next->lod0.materialHandles=chosen.model->materialHandles+chosen.model->lodInfo[0].surfIndex;next->lod0.lodInfo[0].surfIndex=0;
				next->preview=std::make_unique<scene_models::rigid_part>();
				std::array<unsigned,32> groups{};unsigned count{};
				for(unsigned n=0;n<next->lod0.numsurfs;++n){const auto& surface=next->lod0.lodInfo[0].surfs[n];
					if(surface.rigidVertListCount>32 || !surface.rigidVertLists)return;
					for(unsigned b=0;b<surface.rigidVertListCount;++b){const unsigned bone=surface.rigidVertLists[b].boneOffset/64;
						if(std::find(groups.begin(),groups.begin()+count,bone)==groups.begin()+count){if(count==groups.size())return;groups[count++]=bone;}}}
				if(!count || !next->preview->create(&next->lod0,groups[0],{groups.data(),count}) || !next->preview->preview_material(objective))
				{reason="objective preview mesh rejected";return;}
				const std::array<scene_models::runtime_model,2> identities{{{next->held->model(),source},{next->preview->model(),chosen.model}}};
				if(!scene_models::register_runtime_models(identities))return;
			}
			assets=next;retained.push_back(next);{const std::lock_guard lock(mutex);published.assets=next;}reason="abdominal equipment ready";
		}
		void submit()
		{
			snapshot s;{const std::lock_guard lock(mutex);s=published;submitted=s;}
			if(!running() || !scripted_control::predicted_allowed() || !s.assets || !fresh(s.at) || s.assets->weapon!=s.selected.slot.weapon)return;
			const auto draw=[&](game::XModel* model,anchor at,unsigned index){game::GfxScaledPlacement placement{};placement.scale=1;
				std::copy(at.position.begin(),at.position.end(),placement.base.origin);std::copy(at.rotation.begin(),at.rotation.end(),placement.base.quat);
				const auto flags=index<2?scene_models::no_cast_shadow:0u;
				float white[4]{1,1,1,1};scene_models::submit(model,&placement,flags,&lighting[index],white,white,white,8.f);};
			if(s.grip.held() && s.assets->held)draw(s.assets->held->model(),s.held,1);
			else if(sequences::chest_equipment_visible() && s.selected.quantity>0 && !weapons::carry::abdominal_active(s.selected.slot.weapon))
			{
				head_pose_bridge::spatial_frame body;if(!head_pose_bridge::get_spatial_frame(body) || body.generation!=s.reference)return;
				auto at=abdominal_display(body,s.selected.designator);at.position=sub(at.position,rotate(at.rotation,s.assets->center));draw(s.assets->world,at,0);
			}
			if(s.grip.preview && s.preview.valid && s.assets->preview)draw(s.assets->preview->model(),s.preview.pose,2);
		}
		scene_models::placement_result prepare(const scene_models::preparation& p,const void* entry,game::GfxPlacement& current,game::GfxPlacement& previous)noexcept
		{
			using result=scene_models::placement_result;std::uintptr_t handle{};
			if(!utils::native_memory::read_bytes(&handle,static_cast<const std::byte*>(entry)+0x68,8))return result::unchanged;
			unsigned index=3;for(unsigned i=0;i<3;++i)if(handle==reinterpret_cast<std::uintptr_t>(&lighting[i]))index=i;if(index==3)return result::unchanged;
			if(index==0 && !sequences::chest_equipment_visible())return result::omit;
			snapshot s,now;{const std::lock_guard lock(mutex);s=submitted;now=published;}
			if(!s.assets || !fresh(now.at) || s.reference!=now.reference || s.grip.revision!=now.grip.revision)return result::omit;
			if(index==2)return now.grip.preview && now.preview.valid?result::retain:result::omit;
			std::array<float,12> camera{};vec origin{};attachments::solved solved;
			if(!utils::native_memory::read_bytes(camera.data(),static_cast<const std::byte*>(p.record)+engine_stereo_view::h2_view_origin_offset,sizeof(camera)) ||
				!attachments::for_record(p.record,camera,solved) || solved.reference!=s.reference ||
				!utils::native_memory::read_bytes(origin.data(),static_cast<const std::byte*>(p.record)+engine_stereo_view::h2_current_model_placement_origin_offset,sizeof(origin)))return result::omit;
			anchor root;
			if(index==0){if(weapons::carry::abdominal_active(s.selected.slot.weapon))return result::omit;root=abdominal_display(attachments::body_frame(solved,origin),s.selected.designator);root.position=sub(root.position,rotate(root.rotation,s.assets->center));}
			else
			{
				if(!s.grip.held())return result::omit;const auto h=unsigned(s.grip.primary);
				root=compose({add(solved.wrists[h].position,origin),solved.wrists[h].rotation},inverse(authored::wrists[h]));
			}
			std::copy(root.position.begin(),root.position.end(),current.origin);std::copy(root.rotation.begin(),root.rotation.end(),current.quat);previous=current;return result::replace;
		}
		void retire(){draw_intent.reset();notebook::retire();flare::retire();cliffhanger::retire();grip.stow();selection={};preview={};assets.reset();{const std::lock_guard lock(mutex);published={};submitted={};}attachments::clear_after_drain();retained.clear();}
	}
	bool active()noexcept {return running();}
	void collect_interactions(const hi::frame& f)noexcept
	{
		// Mission providers have distinct map identities and share one abdominal
		// entry. A mission-owned slot is never also offered by action-slot inventory.
		const bool provider_enabled=alive && enabled && enabled->current.enabled && weapons::carry::active();
		const bool cliff_slot=cliffhanger::collect(f,provider_enabled);
		const bool flare_slot=flare::collect(f,provider_enabled && !cliff_slot);
		const bool mission_slot=cliff_slot || flare_slot;
		const auto next=running() && !mission_slot?native::selected():native::selection{};notebook::collect(f,next,running() && !mission_slot);
		if(!running()){draw_intent.reset();return;}
		if(next.slot.weapon!=selection.slot.weapon || next.slot.index!=selection.slot.index)draw_intent.reset();
		if(grip.held() && (next.slot.weapon!=selection.slot.weapon || next.slot.index!=selection.slot.index || !next.quantity)){grip.stow();preview={};}
		selection=next.notebook?native::selection{}:next;candidates={};candidate_timeline=weapons::native_ammunition::timeline();snapshot pub;{const std::lock_guard lock(mutex);pub=published;}
		if(!pub.hands_ready)return;
		const auto pending=abdominal_intent(draw_intent,f);
		for(unsigned h=0;h<2;++h)
		{
			const auto actor=vr::hand(h);const auto edge=hi::input(actor,hi::button::grip);
			if(!edge.down || !hi::free(actor) || !(f.valid_hands&(1u<<h)))continue;
			if(edge.press && grip.held() && actor!=grip.primary && !vr::valid_hand(grip.support))
			{
				const float distance=length(sub(f.wrists[h].position,compose(held,authored::wrists[h]).position))/(f.body.units_per_meter*.12f);
				if(distance<=1)hi::offer({actor,{item_target(1),hi::role::support,hi::button::grip,hi::recipe::single,hi::capability::aim},edge.event,20,distance,1,true,true});
			}
			if(sequences::chest_equipment_visible() && (pending&(1u<<h)) && !grip.held() && selection.slot && selection.quantity>0 && pub.assets && pub.assets->weapon==selection.slot.weapon && !weapons::carry::abdominal_active(selection.slot.weapon))
			{
				const float distance=abdominal_grab_distance(f.body,f.wrists[h],pub.basis[h],pub.mirror[h],h);
				if(distance<=1)hi::offer({actor,{hi::object(hi::domain::special,{selection.slot.weapon,grip.revision+1}),hi::role::control,hi::button::grip,hi::recipe::single,hi::capability::action},edge.event,20,distance,1,true,true});
			}
			if(!edge.press)continue;
			head_pose_bridge::world_pose aim;if(!head_pose_bridge::tracking_to_world(f.body,f.input.aim[h].tracking,aim))continue;
			const auto key=native::target(aim.position,aim.axis[0],f.body.units_per_meter);if(key.entity<0)continue;
			candidates[h]=key;hi::offer({actor,{hi::object(hi::domain::special,{unsigned(key.entity),key.generation+1},2),hi::role::world,hi::button::grip,hi::recipe::single,{}},edge.event,15,1,1,true,true});
		}
	}
	void report_interactions()noexcept
	{
		notebook::report();
		flare::report();
		cliffhanger::report();
		if(!grip.held())return;
		hi::observed(grip.primary,{item_target(),hi::role::control,hi::button::grip,hi::recipe::single,hi::capability::action});
		if(vr::valid_hand(grip.support))hi::observed(grip.support,{item_target(1),hi::role::support,hi::button::grip,hi::recipe::single,hi::capability::aim});
	}
	void update_interactions()noexcept
	{
		notebook::update();
		flare::update();
		cliffhanger::update();
		const auto* frame=hi::simulation();if(!frame || !running() || candidate_timeline!=weapons::native_ammunition::timeline())return;
		const auto& f=*frame;snapshot pub;{const std::lock_guard lock(mutex);pub=published;}
		for(unsigned h=0;h<2;++h)
		{
			const auto actor=vr::hand(h);
			if(!grip.held() && selection.slot && hi::granted(actor,hi::domain::special,{selection.slot.weapon,grip.revision+1},hi::button::grip,hi::role::control))
			{
				if(selection.claymore){grip.take(actor);hold_reference=f.input.reference_generation;hold_timeline=weapons::native_ammunition::timeline();++takes;weapons::feedback::carry_confirmation(actor,f.input);}
				else if((!selection.designator || designator_events::ready()) && (!selection.carried || weapons::carry::request_abdominal(selection.slot.weapon,actor)))
				{
					hi::completed(actor,hi::object(hi::domain::special,{selection.slot.weapon,grip.revision+1}));
					const auto token=selection.slot.weapon;
					if(selection.carried)native::activate(selection.slot,[token]{return weapons::carry::abdominal_request_valid(token);});else native::activate(selection.slot);
				}
			}
			else if(grip.held() && hi::granted(actor,hi::domain::special,{selection.slot.weapon,grip.revision},hi::button::grip,hi::role::support))
			{const auto previous=item_target(1);if(grip.join(actor))hi::completed(actor,previous);}
			const auto key=candidates[h];
			if(key.entity>=0 && hi::granted(actor,hi::domain::special,{unsigned(key.entity),key.generation+1},hi::button::grip,hi::role::world))
			{
				if(native::recover(key,0)){++recovered;weapons::feedback::carry_confirmation(actor,f.input);}else ++rejected;
				hi::completed(actor,hi::object(hi::domain::special,{unsigned(key.entity),key.generation+1},2));
			}
		}
		if(grip.held())
		{
			unsigned releases{};for(unsigned h=0;h<2;++h)if(hi::input(vr::hand(h),hi::button::grip).release)releases|=1u<<h;
			grip.release(releases);
			if(grip.held() && (hold_reference!=f.input.reference_generation || hold_timeline!=weapons::native_ammunition::timeline() || !(f.valid_hands&(1u<<unsigned(grip.primary))) ||
				weapons::carry::hand_has_weapon(grip.primary) || !hi::has(grip.primary,hi::domain::special))){grip.stow();preview={};publish(&f);return;}
			if(vr::valid_hand(grip.support) && (!(f.valid_hands&(1u<<unsigned(grip.support))) ||
				weapons::carry::hand_has_weapon(grip.support) || !hi::has(grip.support,hi::domain::special)))grip.release(1u<<unsigned(grip.support));
			if(grip.held() && pub.assets && pub.assets->weapon==selection.slot.weapon)
			{
				held=pose(f.wrists,pub.basis,grip);
				const auto t=hi::input(grip.primary,hi::button::trigger);
				const bool aiming=(t.down && (grip.preview || (t.press && grip.trigger_armed))) || (t.release && grip.preview);
				preview=aiming?native::trace(held,f.body.head_position,f.body.units_per_meter,pub.assets->bottom):native::placement{};
				if(grip.trigger(f.input.trigger[unsigned(grip.primary)].active,t.down,t.press,t.release,preview.valid))
				{
					if(native::place(selection.slot.weapon,preview)){++placed;weapons::feedback::carry_confirmation(grip.primary,f.input);grip.stow();}else ++rejected;
				}
			}
		}
		publish(&f);
	}
	void lifecycle(bool suspended)noexcept
	{
		notebook::lifecycle(suspended);
		flare::lifecycle(suspended);
		cliffhanger::lifecycle(suspended);
		if(!scheduler::is_executing(scheduler::pipeline::server))return;
		if(suspended)draw_intent.reset();
		const auto* ps=reinterpret_cast<const game::playerState_s*>(game::g_entities[0].client);
		if(suspended || !running() || !ps || (ps->e_flags&0x103000) || !scripted_control::allowed(ps) || hold_timeline!=weapons::native_ammunition::timeline()) {if(grip.held())grip.stow();preview={};}
		if(grip.held() && (weapons::carry::hand_has_weapon(grip.primary) || !selection.slot)){grip.stow();preview={};}
		if(vr::valid_hand(grip.support) && weapons::carry::hand_has_weapon(grip.support))grip.release(1u<<unsigned(grip.support));
		publish();
	}
	void present(const hands::interaction_rig& parts,const rig& r,const controller_input::frame& input,const std::array<anchor,2>& targets,
		const std::array<vec,2>& shoulders,const std::array<vec,3>& axes,float units,std::span<bone> solved,unsigned occupied,unsigned visible)noexcept
	{
		notebook::present(parts,r,input,targets,shoulders,axes,units,solved,occupied,visible);
		flare::present(parts,r,input,targets,shoulders,axes,units,solved,occupied,visible);
		cliffhanger::present(parts,r,input,targets,shoulders,axes,units,solved,occupied,visible);
		if(!parts.valid || r.count<=0 || r.count>256 || solved.size()<std::size_t(r.count))return;
		snapshot s;{const std::lock_guard lock(mutex);published.basis=parts.basis;for(unsigned h=0;h<2;++h)published.mirror[h]=parts.library.mirror_basis[r.arms[h].wrist];published.hands_ready=true;s=published;}
		if(!s.grip.held() || !fresh(s.at) || s.reference!=input.reference_generation)return;
		const auto root=pose(targets,parts.basis,s.grip);const auto rear=unsigned(s.grip.primary);
		for(unsigned h=0;h<2;++h)
		{
			if(!(visible&(1u<<h)) || (occupied&(1u<<h)) || (h!=rear && vr::hand(h)!=s.grip.support))continue;
			const auto desired=compose(root,authored::wrists[h]);
			if(h!=rear)(void)weapons::constrain_part_hand(r,parts.library,authored::grip,targets,shoulders,axes,int(rear),desired,solved);
			else move_part(r,r.arms[h].wrist,desired,solved);
			fingers(r,parts.library,authored::grip,authored::fingers,h,solved);
		}
	}
	class component final:public component_interface
	{
		void post_unpack()override
		{
			notebook::initialize();
			flare::initialize();
			cliffhanger::initialize();
			enabled=dvars::register_bool("vr_abdominalEquipment",true,game::DVAR_FLAG_SAVED,"Abdominal native action-slot equipment and physical claymore placement");ready=native::initialize();
			scheduler::loop(refresh,scheduler::pipeline::main,250ms);fastfiles::on_pre_unload(retire);scene_models::on_submit(submit);scene_models::on_prepare_placement(prepare);
			command::add("vr_special_equipment_status",[]{scheduler::once([]{
				const auto text=std::format("[VR special] ready={} slot={} weapon={} quantity={} held={} support={} preview={} valid={} takes={} placed={} recovered={} rejected={} resources={} native={}\n",
					ready.load(),selection.slot.index,selection.slot.weapon,selection.quantity,int(grip.primary),int(grip.support),grip.preview,preview.valid,takes.load(),placed.load(),recovered.load(),rejected.load(),reason.load(),native::status())+notebook::status();
				console::print_text(console::con_type_info,text);scheduler::once([text]{utils::io::write_file_atomic("minidumps/h2-mod-vr-special-equipment.txt",text);},scheduler::pipeline::async);
			},scheduler::pipeline::server);});
		}
		void pre_destroy()override{alive=false;retire();}
	};
}
REGISTER_COMPONENT(vr::gameplay::equipment::special::component)
