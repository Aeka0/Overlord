#include <std_include.hpp>
#include "../h2/entrypoints.hpp"
#include "notebook_runtime.hpp"
#include "notebook_model.hpp"
#include "abdominal_interaction.hpp"
#include "notebook_profile.hpp"
#include "part_hand_constraint.hpp"
#include "hand_position_offset.hpp"
#include "hand_interaction/runtime.hpp"
#include "native_scripted_control.hpp"
#include "native_ammunition.hpp"
#include "component/vr/digital_button_gate.hpp"
#include <utils/hook.hpp>
#include "weapon_feedback.hpp"
#include "component/scene_models.hpp"
#include "../eye_composition.hpp"
#include "component/scheduler.hpp"

namespace vr::gameplay::equipment::special::notebook
{
	namespace
	{
		using namespace hands;namespace hi=hand_interaction;using clock=controller_input::clock;
		struct snapshot
		{
			session state{};native::selection selected{};std::shared_ptr<model> asset;
			std::array<quat,2> basis{},mirror{};bool ready{},enabled{},remote{};anchor root{};
			std::uint64_t reference{};clock::time_point at{};
		};
		std::mutex mutex;snapshot published;
		session state;native::selection selected;std::shared_ptr<model> asset;
		grab_intent draw_intent;
		std::vector<std::shared_ptr<model>> retained;
		std::atomic_uint64_t authorization{1},activations{},cancellations{},empty_returns{};
		std::atomic<const char*> reason{"waiting for AGM notebook"};
		std::uint64_t timeline{},reference{};clock::time_point requested_at{};int camera_seen_at{-1};
		anchor root{};bool enabled{},remote{};game::XModel* attempted{};
		unsigned completed_weapon{};std::uint64_t completed_timeline{};const void* completed_player{};
		bool fresh(clock::time_point at){const auto now=clock::now();return now>=at && now-at<=150ms;}
		anchor anatomical_wrist(anchor wrist,quat basis)noexcept
		{wrist.rotation=normalize(multiply(wrist.rotation,basis));return wrist;}
		hi::button lid_button(const session& value)noexcept{return value.opener_trigger?hi::button::trigger:hi::button::grip;}
		anchor held_root(const std::array<anchor,2>& wrists,const std::array<quat,2>& basis,const std::array<quat,2>& mirror,vr::hand h)
		{const auto i=unsigned(h);auto w=wrists[i];w.rotation=normalize(multiply(w.rotation,basis[i]));return compose(w,inverse(authored::case_wrist(i,mirror[i])));}
		hi::target target(unsigned part=0){return hi::object(hi::domain::special,{selected.slot.weapon,state.revision},10+part);}
		void publish(const hi::frame* f=nullptr)
		{
			const std::lock_guard lock(mutex);published.state=state;published.selected=selected;published.enabled=enabled;published.root=root;published.remote=remote;
			// The render stamp belongs to the last input frame. The separate
			// held-session reference is unset before the first abdominal draw.
			if(f){published.at=f->input.sampled_at;published.reference=f->input.reference_generation;}
		}
		void cancel()
		{
			if(state.requested()){state.release();return;}
			++authorization;++cancellations;state.close();
		}
		void settle_native_return()
		{
			if(!completed_weapon)return;
			if(state.requested() || !game::CL_IsCgameInitialized() || game::g_entities[0].client!=completed_player ||
				weapons::native_ammunition::timeline()!=completed_timeline){completed_weapon=0;return;}
			unsigned actual{};std::memcpy(&actual,static_cast<const std::byte*>(completed_player)+0x3bc,4);
			const auto desired=vr::h2::sp::weapon_selection_request.read();
			if(!needs_empty_return(completed_weapon,weapons::carry::current_hold().weapon,desired,actual))
			{completed_weapon=0;return;}
			// Wait for native script ownership to end, then use the same verified
			// G_SelectWeapon(0) path as an ordinary physical holster. It sends the
			// native 'sw 0' command and settles both client/server selection; no
			// player-state flags, inventory, or script variables are rewritten.
			if(scripted_control::allowed(completed_player) && weapons::native_carry::select(0))
			{completed_weapon=0;++empty_returns;reason="native AGM completed; empty-hand selection restored";}
		}
		void refresh()
		{
			snapshot s;{const std::lock_guard lock(mutex);s=published;}
			if(!s.enabled || !s.selected.notebook || !s.selected.model || s.selected.model==attempted || !game::CL_IsCgameInitialized() || !scene_models::ready())return;
			for(auto& saved:retained)if(saved->source==s.selected.model){asset=saved;const std::lock_guard lock(mutex);published.asset=saved;return;}
			attempted=s.selected.model;if(retained.size()>=2){reason="notebook asset cache full";return;}
			auto next=std::make_shared<model>();
			if(!next->create(s.selected.model)){reason="notebook mesh contract rejected";return;}
			asset=next;retained.push_back(next);{const std::lock_guard lock(mutex);published.asset=next;}reason="closed notebook ready";
		}
		void submit()
		{
			snapshot s;{const std::lock_guard lock(mutex);s=published;}
			if(!s.enabled || !head_pose_bridge::get_status().enabled || !s.selected.notebook || !s.asset || s.asset->source!=s.selected.model || !fresh(s.at) || s.state.stage==phase::remote ||
				(!s.state.requested() && !scripted_control::predicted_allowed()))return;
			const auto input=controller_input::latest();head_pose_bridge::spatial_frame body;
			if(!input.focused || !fresh(input.sampled_at) || !head_pose_bridge::get_spatial_frame(body) || body.generation!=s.reference || input.reference_generation!=s.reference)return;
			anchor base=stowed_root(body);float angle{};
			if(s.state.held())
			{
				const auto* inward=game::Dvar_FindVar("vr_handOffsetInward");const auto* back=game::Dvar_FindVar("vr_handOffsetBack");const auto* up=game::Dvar_FindVar("vr_handOffsetUp");
				if(!s.ready || !inward || !back || !up || !vr::valid_hand(s.state.holder))return;
				const position_offsets offsets{inward->current.value,back->current.value,up->current.value};std::array<anchor,2> wrists{};
				const auto h=unsigned(s.state.holder);if(!tracked_wrist(input,body,{},int(h),offsets,wrists[h]))return;
				base=held_root(wrists,s.basis,s.mirror,s.state.holder);angle=s.state.angle;
				// Preview the latest XR stroke on a copy, just like the ACR sensor.
				// Only server update commits the full-open/release AGM transition.
				if(s.state.physical() && vr::valid_hand(s.state.opener))
				{const auto off=unsigned(s.state.opener);if(tracked_wrist(input,body,{},int(off),offsets,wrists[off]))angle=s.state.drag.sample(compose(inverse(base),anatomical_wrist(wrists[off],s.basis[off])));}
			}
			(void)s.asset->submit(base,angle);
		}

	}
	void initialize(){scheduler::loop(refresh,scheduler::pipeline::main,250ms);scene_models::on_submit(submit);eye_composition::remote_camera_epoch=camera_epoch;}
	void retire()
	{
		draw_intent.reset();
		completed_weapon=0;completed_timeline=0;completed_player=nullptr;
		++authorization;state.close();selected={};asset.reset();attempted=nullptr;enabled=remote=false;camera_seen_at=-1;
		{const std::lock_guard lock(mutex);published={};}retained.clear();
	}
	void collect(const hi::frame& f,native::selection selection,bool active)noexcept
	{
		if(state.requested())
		{
			publish(&f);if(!state.physical())return;
			// The accepted physical request retains its native slot identity until
			// acknowledgement/timeout, even while its case can be manipulated.
			selection=selected;
		}
		enabled=active;
		if(!state.requested() && selection.notebook && native::observe_agm().using_uav)enabled=false;
		if(state.physical() && (!active || !selection.notebook || selection.slot.weapon!=selected.slot.weapon || selection.slot.index!=selected.slot.index))cancel();
		selected=selection;publish(&f);
		snapshot s;{const std::lock_guard lock(mutex);s=published;}
		if(!enabled || !selection.notebook || !s.asset || s.asset->source!=selection.model || !s.ready){draw_intent.reset();return;}
		if(state.physical())root=held_root(f.wrists,s.basis,s.mirror,state.holder);
		const auto pending=abdominal_intent(draw_intent,f);
		for(unsigned h=0;h<2;++h)
		{
			const auto actor=vr::hand(h);
			if(!hi::free(actor) || !(f.valid_hands&(1u<<h)))continue;
			const bool pickup=state.stage==phase::stowed;const bool open=state.physical() && actor!=state.holder && !vr::valid_hand(state.opener);
			if(!pickup && !open)continue;
			const auto local=inverse(root);
			const auto palm=knife_profile::palm_contact(f.wrists[h],s.basis[h],s.mirror[h],h==1);
			const float distance=pickup?abdominal_grab_distance_at(f.body,stowed_root(f.body).position,f.wrists[h],s.basis[h],s.mirror[h],h):
				std::min(screen_edge_distance(compose(local,f.wrists[h]).position,state.angle),
					screen_edge_distance(compose(local,{palm,{0,0,0,1}}).position,state.angle))/(f.body.units_per_meter*.13f);
			if(distance>1)continue;
			for(const auto button:{hi::button::grip,hi::button::trigger})
			{
				const auto edge=hi::input(actor,button);
				if(!edge.down || (pickup ? button!=hi::button::grip || !(pending&(1u<<h)) : !edge.press))continue;
				hi::offer({actor,{pickup?hi::object(hi::domain::special,{selected.slot.weapon,state.revision+1},10):target(1),
					pickup?hi::role::control:hi::role::part,button,hi::recipe::single,hi::capability::action},edge.event,20,distance,1,true,true});
			}
		}
	}
	void update()noexcept
	{
		const auto* frame=hi::simulation();if(!frame || !enabled || !selected.notebook || (state.requested() && !state.physical()))return;const auto& f=*frame;
		snapshot s;{const std::lock_guard lock(mutex);s=published;}
		for(unsigned h=0;h<2;++h)
		{
			const auto actor=vr::hand(h);
			if(state.stage==phase::stowed && hi::granted(actor,hi::domain::special,{selected.slot.weapon,state.revision+1},hi::button::grip,hi::role::control))
			{
				state.take(actor);timeline=weapons::native_ammunition::timeline();reference=f.input.reference_generation;
				weapons::feedback::carry_confirmation(actor,f.input);
			}
			else if(state.physical() && actor!=state.holder &&
				(hi::granted(actor,hi::domain::special,{selected.slot.weapon,state.revision},hi::button::grip,hi::role::part) ||
				 hi::granted(actor,hi::domain::special,{selected.slot.weapon,state.revision},hi::button::trigger,hi::role::part)))
			{
				root=held_root(f.wrists,s.basis,s.mirror,state.holder);
				const bool trigger=hi::granted(actor,hi::domain::special,{selected.slot.weapon,state.revision},hi::button::trigger,hi::role::part);
				(void)state.take_lid(actor,trigger,compose(inverse(root),anatomical_wrist(f.wrists[h],s.basis[h])));
			}
		}
		if(state.physical())
		{
			const unsigned h=unsigned(state.holder);
			if(hi::input(state.holder,hi::button::grip).release || !(f.valid_hands&(1u<<h)) || reference!=f.input.reference_generation || timeline!=weapons::native_ammunition::timeline() || weapons::carry::hand_has_weapon(state.holder) || !hi::has(state.holder,hi::domain::special))cancel();
			else
			{
				root=held_root(f.wrists,s.basis,s.mirror,state.holder);
				if(vr::valid_hand(state.opener))
				{
					const unsigned off=unsigned(state.opener);const auto local=compose(inverse(root),anatomical_wrist(f.wrists[off],s.basis[off]));
					const auto grip=hi::input(state.opener,lid_button(state));
					const bool tracked=(f.valid_hands&(1u<<off)) && reference==f.input.reference_generation;
					const bool within_reach=tracked && length(sub(local.position,state.drag.constrained(state.angle).position))<=f.body.units_per_meter*.30f;
					if(!within_reach)state.release_lid(false);
					else
					{
						// Sample this release frame first, but only a real grip-release
						// edge commits. Tracking loss or an inactive button never does.
						state.open(state.drag.sample(local));
						if(grip.release && state.release_lid(true))
						{
							requested_at=clock::now();camera_seen_at=-1;const auto ticket=++authorization;
							native::activate(selected.slot,[ticket]{return authorization.load()==ticket;});++activations;reason="fully opened screen released; awaiting native AGM transition";
						}
					}
				}
			}
		}
		publish(&f);
	}
	void report()noexcept
	{
		if(!state.held())return;
		hi::observed(state.holder,{target(),hi::role::control,hi::button::grip,hi::recipe::single,hi::capability::action});
		if(vr::valid_hand(state.opener))hi::observed(state.opener,{target(1),hi::role::part,lid_button(state),hi::recipe::single,hi::capability::action});
	}
	void lifecycle(bool suspended)noexcept
	{
		if(!scheduler::is_executing(scheduler::pipeline::server))return;
		settle_native_return();
		if(suspended)draw_intent.reset();
		if(state.requested())
		{
			const auto input=controller_input::latest();const auto native=native::observe_agm();
			const bool valid=game::CL_IsCgameInitialized() && timeline==weapons::native_ammunition::timeline();
			if(!valid){++authorization;state.close();selected={};remote=false;publish();return;}
			// The remote-control lease outlives both hand leases. Recentring,
			// grip release and automatic docking cannot emit force_out_of_uav.
			reference=input.reference_generation;
			const int time=game::CG_GetGameTime(0);
			if(native.valid)
			{
				if(native.remote && camera_seen_at<0)camera_seen_at=time;
				const bool settled=camera_settled(time,camera_seen_at);
				const auto before=state.stage;const bool accepted=state.saw_native;
				state.observe(native.using_uav,native.remote,settled,clock::now()-requested_at>5s);
				if(before!=phase::stowed && state.stage==phase::stowed)
				{
					++authorization;
					if(accepted)
					{
						completed_weapon=selected.slot.weapon;completed_timeline=timeline;completed_player=game::g_entities[0].client;
						settle_native_return();
					}
				}
				remote=state.requested() && native.remote;
			}
			// Retain the visible open case during preparation. At the native
			// black-frame/camera acknowledgement it becomes hidden; docking then
			// waits for the native camera blend, independently of AGM input.
			if(state.held() && fresh(input.sampled_at))
			{
				head_pose_bridge::spatial_frame body;head_pose_bridge::world_pose pose;
				const auto h=unsigned(state.holder);snapshot s;{const std::lock_guard lock(mutex);s=published;}
				if(head_pose_bridge::get_spatial_frame(body) && body.generation==reference && input.grip[h].valid &&
					head_pose_bridge::tracking_to_world(body,input.grip[h].tracking,pose))
				{
					const anchor w{pose.position,normalize(multiply(from_axis(pose.axis),s.basis[h]))};
					root=compose(w,inverse(authored::case_wrist(h,s.mirror[h])));
				}
			}
			{const std::lock_guard lock(mutex);published.at=input.sampled_at;published.reference=reference;}
			publish();return;
		}
		if(state.physical() && suspended)cancel();
		remote=false;publish();
	}
	bool controlling()noexcept
	{const std::lock_guard lock(mutex);return published.state.requested();}
	bool native_control()noexcept
	{const std::lock_guard lock(mutex);return published.state.native_control();}
	std::uint64_t camera_epoch()noexcept
	{const std::lock_guard lock(mutex);return published.state.stage==phase::remote?published.state.control_revision:0;}
	bool available()noexcept
	{const std::lock_guard lock(mutex);return published.enabled && published.selected.notebook && !published.state.requested();}
	void command(const controller_input::frame& input,bool gameplay,int& buttons)noexcept
	{
		// Command-thread owner, separate from server interaction state.
		static controller_input::paired_button_gate fire;static std::uint64_t last_revision{},last_reference{};
		snapshot s;{const std::lock_guard lock(mutex);s=published;}
		if(!gameplay || s.state.stage!=phase::remote || !s.remote || !input.focused || !fresh(input.sampled_at) || input.reference_generation!=s.reference)
		{fire={};return;}
		if(last_revision!=s.state.control_revision || last_reference!=input.reference_generation){fire={};last_revision=s.state.control_revision;last_reference=input.reference_generation;}
		const auto trigger=fire.consume(input.trigger);
		if(trigger.held)
		{
			buttons|=1;
			// Native GSC launch waits on the reliable binding notification. Flight
			// boost reads the attack bit. Both paths must receive the same input.
			const auto* table=reinterpret_cast<const char* const*>(0x140BF84E0);
			if(trigger.pressed && table[1] && std::string_view(table[1])=="+attack")utils::hook::invoke<void>(0x1403D3A90,0,1,0);
		}
	}
	void present(const hands::interaction_rig& parts,const rig& r,const controller_input::frame& input,const std::array<anchor,2>& targets,
		const std::array<vec,2>& shoulders,const std::array<vec,3>& axes,float,std::span<bone> solved,unsigned occupied,unsigned visible)noexcept
	{
		if(!parts.valid || r.count<=0 || r.count>256 || solved.size()<std::size_t(r.count))return;
		snapshot s;{const std::lock_guard lock(mutex);published.basis=parts.basis;for(unsigned h=0;h<2;++h)published.mirror[h]=parts.library.mirror_basis[r.arms[h].wrist];published.ready=true;s=published;}
		if(!s.state.held() || s.state.stage==phase::remote || !fresh(s.at) || s.reference!=input.reference_generation)return;
		const auto h=unsigned(s.state.holder);if(!(visible&(1u<<h)) || (occupied&(1u<<h)))return;
		const auto base=held_root(targets,parts.basis,s.mirror,s.state.holder);const auto desired=compose(base,authored::case_wrist(h,s.mirror[h]));move_part(r,r.arms[h].wrist,desired,solved);
		hands::pose_mirror::fingers(r,parts.library,hands::native_hand_schema::definition,authored::case_fingers,h,solved,h==0);
		if(vr::valid_hand(s.state.opener))
		{
			const auto off=unsigned(s.state.opener);if((visible&(1u<<off)) && !(occupied&(1u<<off)))
			{
				const auto local=compose(inverse(base),anatomical_wrist(targets[off],parts.basis[off]));const float angle=s.state.drag.sample(local);
				(void)weapons::constrain_part_hand(r,parts.library,hands::native_hand_schema::definition,targets,shoulders,axes,int(h),compose(base,s.state.drag.constrained(angle)),solved);
				hands::pose_mirror::fingers(r,parts.library,hands::native_hand_schema::definition,authored::screen_fingers,off,solved,off==1);
			}
		}
	}
	std::string status()
	{
		const std::lock_guard lock(mutex);return std::format("[VR notebook] phase={} holder={} opener={} activation_hand={} docked={} angle={:.1f} native_seen={} activations={} cancellations={} empty_returns={} native_submissions={} native_rejected={} resources={}\n",
			int(published.state.stage),int(published.state.holder),int(published.state.opener),int(published.state.controller),published.state.docked,published.state.angle,published.state.saw_native,activations.load(),cancellations.load(),empty_returns.load(),published.asset?published.asset->submissions.load():0,published.asset?published.asset->rejected.load():0,reason.load());
	}
}
