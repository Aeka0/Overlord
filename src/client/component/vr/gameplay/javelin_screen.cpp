#include <std_include.hpp>
#include "javelin_screen.hpp"
#include "javelin_display_control.hpp"
#include "javelin_lock_gate.hpp"
#include "weapon_carry_runtime.hpp"
#include "launcher_runtime.hpp"
#include "fixed_sniper.hpp"
#include "../eye_composition.hpp"
#include "../native_hud_capture.hpp"
#include "../screen_scope_layout.hpp"
#include "../spatial_panel_renderer.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scripting.hpp"
#include "game/scripting/functions.hpp"
#include "game/dvars.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/io.hpp>
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::gameplay::weapons::javelin_screen
{
	namespace
	{
		game::dvar_t* option{};std::atomic_bool alive{true};
		std::mutex mutex;proximity_controls proximity;
		struct selection {hold owner{};control_state control{};std::uint64_t epoch{},reference{};controller_input::clock::time_point at{};bool loaded{};};
		struct camera_state {muzzle_frame aim{};std::uint64_t epoch{};controller_input::clock::time_point at{};};
		selection selected;camera_state camera;std::uint64_t next_epoch{};
		std::atomic_uint64_t planned{},drawn{},missing{};
		std::atomic<const char*> reason{"inactive"};
		bool hud_watch_ready{};
		utils::hook::detour player_ads_hook;
		std::atomic<const char*> lock_ads_call{};
		std::atomic_uint64_t lock_ads_overrides{};
		struct pair_state
		{
			std::uint64_t pair{},publication{},device{},epoch{};
			std::shared_ptr<const native_hud_capture::frame> ink;
			std::array<spatial_panel::projected_quad,2> canvas{};
			auxiliary_scene::request request{};
		};
		thread_local pair_state pair;thread_local spatial_panel::renderer renderer;
		thread_local screen_scope::anchored_plane plane;
		thread_local std::shared_ptr<const native_hud_capture::frame> retained_ink;
		selection current() noexcept
		{
			selection value;{const std::lock_guard lock(mutex);value=selected;}
			const auto now=controller_input::clock::now();
			if(!enabled() || now<value.at || now-value.at>150ms)return {};
			return value;
		}
		void player_ads(game::scr_entref_t self)
		{
			const auto* site=lock_ads_call.load();
			const bool scoped=site && game::scr_function_stack->pos==site && !self.classnum && !self.entnum;
			// Keep native validation, stack allocation and return typing intact.
			player_ads_hook.invoke<void>(self);
			if(!scoped || !enabled() || !carry::active() || game::scr_VmPub->outparamcount ||
				game::scr_VmPub->inparamcount!=1 || game::scr_VmPub->top->type!=game::SCRIPT_FLOAT)return;
			const auto value=current();const auto owner=current_hold();
			const auto* ps=game::g_entities[0].client;
			const auto ammo=ps?native_ammunition::observe(ps):native_ammunition::snapshot{};
			const bool near_eye=value.control.open && value.owner.id()==owner.id() && value.owner.rear_revision==owner.rear_revision;
			const bool ready=ammo.valid && ammo.id()==owner.id() && lock_gate_ready(near_eye,ammo.loaded,ammo.weapon_state,ammo.weapon_time);
			game::scr_VmPub->top->u.floatValue=ready?1.f:0.f;
			++lock_ads_overrides;
		}
		void bind_lock_gate()
		{
			lock_ads_call=nullptr;
			const auto file=scripting::script_function_table_sort.find(scripting::get_token_single(44133));
			if(file==scripting::script_function_table_sort.end())return;
			const auto name=scripting::get_token_single(0xbcbd);const char* begin{};const char* end{};
			for(const auto& entry:file->second)if(entry.first==name)begin=entry.second;
			if(!begin)return;
			for(const auto& entry:file->second)if(entry.second>begin && (!end || entry.second<end))end=entry.second;
			if(!end)return;
			const auto call=full_ads_call({reinterpret_cast<const std::uint8_t*>(begin),std::size_t(end-begin)},0x834b);
			if(!call){console::warn("[VR Javelin] native lock ADS script contract rejected\n");return;}
			if(!player_ads_hook.is_enabled())
			{
				const auto method=scripting::find_function("playerads",false);
				constexpr std::uint8_t entry[]{0x48,0x83,0xec,0x28,0x8b,0xc1,0xc1,0xe8,0x10,0x66,0x85,0xc0};
				std::array<std::uint8_t,sizeof(entry)> mask{};mask.fill(255);
				if(reinterpret_cast<std::uintptr_t>(method)!=0x1404B4DB0 ||
					!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(method),{entry,mask.data(),mask.size()}))
				{console::error("[VR Javelin] native playerads method contract rejected\n");return;}
				player_ads_hook.create(reinterpret_cast<void*>(method),player_ads);
			}
			lock_ads_call=begin+*call;
		}
		bool hud_active(int local_client)
		{
			const bool native=utils::hook::invoke<bool>(0x1403B9780,local_client);
			if(local_client!=0 || !enabled() || !carry::active())return native;
			const auto value=current();const auto owner=current_hold();
			// This selection exists only for the admitted Javelin. Use its near-eye
			// intent directly, before the native ADS fraction reaches one. This is
			// only the LUI watch read; it does not change native player-state ADS.
			if(!value.owner.weapon || value.owner.id()!=owner.id() || value.owner.rear_revision!=owner.rear_revision)return native;
			return value.control.open;
		}
		auxiliary_scene::request plan(const eye_composition::event& event) noexcept
		{
			pair={};pair.pair=event.pair_id;pair.publication=event.views.eyes[0].publication;pair.device=event.device_generation;
			pair.epoch=event.views.weapon_display_epoch;
			if(!pair.epoch || event.views.screen_scope_epoch){plane={};retained_ink.reset();return {};}
			try
			{
				const auto state=current();const auto capture=native_hud_capture::latest_screen_scope();
				const auto head=head_pose_bridge::get_status();const auto now=GetTickCount64();
				if(!state.control.open || state.epoch!=pair.epoch || !event.model_origins.valid ||
					!head.enabled || !head.pose_available || head.recenter_pending || head.recenter_count!=state.reference ||
					!std::isfinite(head.world_scale) || head.world_scale<=0)
				{retained_ink.reset();reason="waiting for matching screen camera";return {};}
				const auto matches=[&](const auto& ink,unsigned age) {
					return ink && ink->view && ink->height && !ink->screen_scope_epoch && ink->weapon_display_epoch==pair.epoch &&
						ink->id()==state.owner.id() && ink->rear_revision==state.owner.rear_revision && ink->reference_generation==state.reference &&
						ink->generation==pair.device && ink->timestamp<=now && now-ink->timestamp<=age;
				};
				// Native HUD intentionally disappears during reload/ADS transitions.
				// It must never gate the live camera. Bridge only brief loaded-state
				// publication gaps; discard old lock ink as soon as the shot empties it.
				if(!state.loaded || !matches(retained_ink,100))retained_ink.reset();
				if(matches(capture.ink,100))pair.ink=capture.ink;
				else if(state.loaded)pair.ink=retained_ink;
				if(state.loaded && pair.ink)retained_ink=pair.ink;
				spatial_panel::vec3 position{};float separation{};
				for(unsigned i=0;i<3;++i)
				{position[i]=head.local_position_units[i]/head.world_scale;const float d=event.model_origins.eyes[0][i]-event.model_origins.eyes[1][i];separation+=d*d;}
				const float half_ipd=.5f*std::sqrt(separation)/head.world_scale;
				if(!plane.update(pair.epoch,state.reference,position,head.local_orientation,fixed_sniper::head_gain()))return {};
				engine_stereo_bridge::eye_projection source;
				for(unsigned eye=0;eye<2;++eye)
				{
					engine_stereo_bridge::eye_projection projection;
					if(!engine_stereo_view::read_projection(event.views.eyes[eye],projection) ||
						!plane.project(projection,event.views.eyes[eye].view_eye==0?half_ipd:-half_ipd,canvas_width,canvas_height,pair.canvas[eye]))return {};
					if(!eye)source=projection;
				}
				auxiliary_scene::request request;
				const float half_y=event.views.native_tan_half[1];
				if(!screen_scope::window(source,half_y*canvas_aspect,half_y,request.window) || !auxiliary_scene::valid_window(request.window))
				{reason="native Javelin FOV rejected";return {};}
				request.eye=0;request.owner=state.owner.weapon;request.generation=state.owner.instance_generation;
				request.reference=state.reference;request.revision=pair.epoch;request.native_center=true;request.valid=true;
				// Remove source-eye IPD: scene, native HUD and lock projection share the center camera.
				pair.request=request;++planned;return request;
			}
			catch(...){reason="display planning rejected";return {};}
		}
		void compose(const eye_composition::event& event,ID3D11DeviceContext* context,
			ID3D11ShaderResourceView*,ID3D11RenderTargetView* target) noexcept
		{
			if(!event.views.weapon_display_epoch || event.views.screen_scope_epoch || event.eye>1 || !context || !target)return;
			const auto fail=[&]{const float black[4]{0,0,0,1};context->ClearRenderTargetView(target,black);++missing;};
			try
			{
				const auto* request=event.auxiliary;
				if(pair.pair!=event.pair_id || pair.publication!=event.views.eyes[event.eye].publication || pair.device!=event.device_generation ||
					pair.epoch!=event.views.weapon_display_epoch || (pair.ink && pair.ink->context!=reinterpret_cast<std::uintptr_t>(context)) ||
					!request || !request->valid || !request->native_center || !event.auxiliary_image || request->owner!=pair.request.owner ||
					request->generation!=pair.request.generation || request->revision!=pair.epoch || request->reference!=pair.request.reference)
				{reason="display frame unavailable";fail();return;}
				if(!renderer.draw_screen_scope(context,event.auxiliary_image,pair.ink?pair.ink->view.Get():nullptr,target,pair.canvas[event.eye],event.width,event.height))
				{reason="display composition rejected";fail();return;}
				++drawn;reason=pair.ink?"native Javelin camera and HUD on independent screen":"live Javelin camera; native HUD temporarily absent";
			}
			catch(...){reason="display composition exception";fail();}
		}
	}
	bool enabled() noexcept{return alive && option && option->current.enabled;}
	void input(const controller_input::frame& frame,const hold& owner,bool allowed) noexcept
	{
		const auto muzzle=current_muzzle();const auto now=controller_input::clock::now();
		const bool valid=enabled() && allowed && muzzle.profile_id=="javelin" && ready(muzzle,owner,frame.reference_generation,now);
		const bool loaded=valid && launcher::current(owner.id()).loaded>0;
		const std::lock_guard lock(mutex);
		// Button entry/exit is temporarily disabled for the near-eye ADS trial.
		// static controls buttons; const auto next=buttons.consume(frame,owner,valid,now);
		const auto next=proximity.consume(frame,owner,valid,muzzle,now);
		if(next.open && (!selected.control.open || selected.owner.id()!=owner.id() || selected.reference!=frame.reference_generation))selected.epoch=++next_epoch;
		selected.owner=muzzle.profile_id=="javelin"?owner:hold{};selected.control=next;selected.reference=frame.reference_generation;selected.at=now;selected.loaded=loaded;
	}
	bool requested(const hold& owner) noexcept
	{const auto value=current();return value.owner.id()==owner.id() && value.owner.rear_revision==owner.rear_revision && value.control.open;}
	bool allows_fire(const hold& owner) noexcept
	{
		if(!enabled())return true;selection value;{const std::lock_guard lock(mutex);value=selected;}
		if(value.owner.id()!=owner.id())return true;
		const auto now=controller_input::clock::now();
		return now>=value.at && now-value.at<=150ms && value.control.open && value.control.fire_ready;
	}
	bool lock_aim(const hold& owner,muzzle_frame& aim) noexcept
	{
		const auto value=current();camera_state captured;{const std::lock_guard lock(mutex);captured=camera;}
		const auto now=controller_input::clock::now();
		if(!value.control.open || value.owner.id()!=owner.id() || captured.epoch!=value.epoch || now<captured.at || now-captured.at>150ms ||
			captured.aim.owner.id()!=owner.id() || captured.aim.owner.rear_revision!=owner.rear_revision)return false;
		aim=captured.aim;return true;
	}
	hold capture_owner() noexcept
	{const auto value=current();muzzle_frame aim;return value.control.open && lock_aim(value.owner,aim)?value.owner:hold{};}
	std::uint64_t camera_epoch() noexcept {const std::lock_guard lock(mutex);return camera.epoch;}
	void apply_camera(float* origin,float (*axis)[3],bool allowed,float* tan_half) noexcept
	{
		const auto value=current();const auto muzzle=current_muzzle();const auto frame=controller_input::latest();camera_state next;
		const auto* definition=value.owner.weapon && value.owner.weapon<512?game::weapon_defs[value.owner.weapon]:nullptr;
		static_assert(offsetof(game::WeaponDef,adsZoomFov)==0x9a8);
		const float half_y=definition?optical_half_y(definition->adsZoomFov):0;
		if(allowed && origin && axis && tan_half && half_y>0 && value.control.open && muzzle.profile_id=="javelin" && ready(muzzle,value.owner,frame.reference_generation,controller_input::clock::now()))
		{
			next={muzzle,value.epoch,controller_input::clock::now()};std::copy(muzzle.position.begin(),muzzle.position.end(),origin);
			for(unsigned i=0;i<3;++i)std::copy(muzzle.axis[i].begin(),muzzle.axis[i].end(),axis[i]);
			// Preserve optical framing during native reload and ADS interpolation.
			// HUD projection sees this same corrected widescreen camera.
			tan_half[0]=half_y*canvas_aspect;tan_half[1]=half_y;
		}
		const std::lock_guard lock(mutex);camera=next;
	}
	class component final:public component_interface
	{
		void post_unpack() override
		{
			option=dvars::register_bool("vr_javelinDisplay",true,game::DVAR_FLAG_SAVED,"Independent Javelin display while the actual eyepiece is close to the eyes");
			// UIIntWatch's JavelinActive case: CALL followed by TEST AL,AL and
			// SETNE into its integer result. Do not detour the shared getter: its
			// other callers include target drawing and non-UI native code.
			constexpr std::uint8_t watch[]{0xe8,0x76,0x35,0x06,0x00,0x84,0xc0,0xb0,0x01};
			std::array<std::uint8_t,sizeof(watch)> mask{};mask.fill(255);
			hud_watch_ready=bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x140356205),{watch,mask.data(),mask.size()}));
			if(hud_watch_ready)utils::hook::call(0x140356205,hud_active);
			else console::error("[VR Javelin display] HUD watch contract rejected; native HUD timing retained\n");
			scripting::on_level_start(bind_lock_gate);
			scripting::on_shutdown([](bool,bool after){if(!after)lock_ads_call=nullptr;});
			auxiliary_scene::weapon_display_plan=plan;auxiliary_scene::weapon_display_epoch=camera_epoch;
			eye_composition::set_consumer(compose,eye_composition::layer::weapon_display);
			::command::add("vr_javelinDisplay_status",[]{
				const auto state=current();muzzle_frame aim;const bool valid=lock_aim(state.owner,aim);
				const auto report=std::format("enabled={} hud_watch={} lock_gate={} lock_queries={} open={} armed={} epoch={} aim={} planned={} eyes={} missing={} reason={}\norigin={},{},{} forward={},{},{}\n",
					enabled(),hud_watch_ready,lock_ads_call.load()!=nullptr,lock_ads_overrides.load(),state.control.open,state.control.fire_ready,state.epoch,valid,planned.load(),drawn.load(),missing.load(),reason.load(),
					aim.position[0],aim.position[1],aim.position[2],aim.axis[0][0],aim.axis[0][1],aim.axis[0][2]);
				console::info("[VR Javelin display] %s",report.c_str());utils::io::write_file_atomic("minidumps/h2-mod-vr-javelin-display.txt",report);
			});
		}
		void pre_destroy() override{alive=false;lock_ads_call=nullptr;auxiliary_scene::weapon_display_plan=nullptr;auxiliary_scene::weapon_display_epoch=nullptr;eye_composition::set_consumer(nullptr,eye_composition::layer::weapon_display);}
	};
}
REGISTER_COMPONENT(vr::gameplay::weapons::javelin_screen::component)
