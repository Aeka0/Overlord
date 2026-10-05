#include <std_include.hpp>
#include "nightvision_runtime.hpp"
#include "weapon_carry_runtime.hpp"
#include "nightvision.hpp"
#include "native_action_slots.hpp"
#include "native_scripted_control.hpp"
#include "native_ammunition.hpp"
#include "../head_pose_bridge.hpp"
#include "hand_interaction/runtime.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "component/scripting.hpp"
#include "loader/component_loader.hpp"
#include <utils/io.hpp>
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>
#include <mutex>

namespace vr::gameplay::equipment::nightvision
{
	namespace
	{
		namespace hi=hand_interaction;
		drag gesture;native_state state{};bool verified{};
		vec start_forward{},start_up{};int started_slot{-1};
		clock::time_point pending_until{};
		std::atomic_uint64_t authorization{1};std::uint64_t requests{};
		const char* reason="waiting for native nightvision slot";
		utils::hook::detour transition_hook,vision_hook,fade_hook;bool transition_ready{};
		std::mutex presentation_mutex;device_transition presentation;
		std::atomic_bool presenting{};
		std::atomic_uint64_t presentations_started{},sound_requests{},foley_requests{};
		template<class T>T field(const void* ps,std::size_t offset)noexcept
		{T value{};utils::native_memory::read_bytes(&value,static_cast<const std::byte*>(ps)+offset,sizeof(value));return value;}
		template<std::size_t N>bool matches(std::uintptr_t address,const std::array<std::uint8_t,N>& expected)noexcept
		{std::array<std::uint8_t,N> actual{};return utils::native_memory::read_bytes(actual.data(),reinterpret_cast<const void*>(address),N) && actual==expected;}
		device_view current_presentation(bool rendering=false)
		{
			if(!presenting.load(std::memory_order_relaxed))return {};
			const auto* vr=game::Dvar_FindVar("vr_enable");
			const auto* disabled=game::Dvar_FindVar("nightVisionDisableEffects");
			const auto* ps=game::CL_IsCgameInitialized()?game::CG_GetPredictedPlayerState(0):nullptr;
			const bool allowed=vr && vr->current.enabled && ps && !player_life::dead(ps) && (!disabled || !disabled->current.enabled);
			const auto epoch=weapons::native_ammunition::timeline();
			const int time=allowed?field<int>(ps,0x4c):0;
			const bool on=allowed && (field<unsigned>(ps,0x3c0)&0x40)!=0;
			const std::lock_guard lock(presentation_mutex);
			const auto value=allowed?(rendering?presentation.render(epoch,time,on):presentation.sample(epoch,time,on)):device_view{};
			// Prediction may temporarily replay a command older than the accepted
			// toggle. That lookup must not cancel the final frame's device clock.
			if(!allowed || presentation.expired(epoch,time))presenting=false;
			return value;
		}
		bool vision(int client)
		{
			if(client==0)if(const auto view=current_presentation();view.owned)return view.vision;
			return vision_hook.invoke<bool>(client);
		}
		float visibility(int client)
		{
			if(client==0)if(const auto view=current_presentation(true);view.owned)return view.visibility;
			return fade_hook.invoke<float>(client);
		}
		bool claim_sound(const device_view& view)
		{const std::lock_guard lock(presentation_mutex);return presenting && presentation.claim_sound(view.serial,view.time);}
		bool claim_foley(std::uint64_t serial)
		{const std::lock_guard lock(presentation_mutex);return presenting && presentation.claim_foley(serial);}
		bool loaded_alias(const game::snd_alias_list_t* alias,std::string_view expected={})
		{
			game::snd_alias_list_t copy{};std::array<char,64> name{};
			return utils::native_memory::read_bytes(&copy,alias,sizeof(copy)) && copy.head && copy.count &&
				utils::native_memory::read_bytes(name.data(),copy.aliasName,name.size()) &&
				strnlen(name.data(),name.size())<name.size() && name[0] && (expected.empty() || std::string_view(name.data())==expected);
		}
		void native_sound(int client,const game::snd_alias_list_t* alias)
		{
			// The native event is tied to weapon animation timing. Device audio
			// now waits for the rendered blackout, including empty hands and
			// temporary prediction rewinds, instead of racing this event.
			if(client==0 && presenting.load(std::memory_order_relaxed))
			{
				current_presentation();if(presenting.load(std::memory_order_relaxed))return;
			}
			utils::hook::invoke<void>(0x1403801C0,client,alias);
		}
		void native_foley(int client,short entity,const game::snd_alias_list_t* alias,bool on)
		{
			if(client==0 && presenting.load(std::memory_order_relaxed) && game::CL_IsCgameInitialized() &&
				entity==field<signed char>(game::CG_GetPredictedPlayerState(0),0))
			{
				const auto view=current_presentation();
				if(presenting.load(std::memory_order_relaxed))
				{
					if(view.owned && view.target==on && loaded_alias(alias) && claim_foley(view.serial))
					{utils::hook::invoke<void>(0x140380200,client,entity,alias);++foley_requests;}
					return;
				}
			}
			utils::hook::invoke<void>(0x140380200,client,entity,alias);
		}
		void native_foley_on(int client,short entity,const game::snd_alias_list_t* alias){native_foley(client,entity,alias,true);}
		void native_foley_off(int client,short entity,const game::snd_alias_list_t* alias){native_foley(client,entity,alias,false);}
		void queue_sound(bool on,std::uint64_t serial)
		{
			const auto epoch=weapons::native_ammunition::timeline(),queued=GetTickCount64();
			scheduler::schedule([on,serial,epoch,queued,foley_attempted=false]()mutable {
				if(weapons::native_ammunition::timeline()!=epoch || GetTickCount64()-queued>30000)return scheduler::cond_end;
				{const std::lock_guard lock(presentation_mutex);
					if(!presenting || presentation.serial()!=serial || presentation.sound_claimed(serial))return scheduler::cond_end;}
				const auto view=current_presentation();
				// Server acceptance can precede the predicted snapshot. Wait only
				// within this bounded request; a newer toggle cancels the old sound.
				if(!view.owned)return scheduler::cond_continue;
				if(view.serial!=serial || view.target!=on)return scheduler::cond_end;
				const auto* paused=game::Dvar_FindVar("cl_paused");if(!paused || paused->current.integer)return scheduler::cond_continue;
				if(!foley_attempted)
				{
					foley_attempted=true;
					// Captured local NVG sound slots 66/68 on defaultweapon and both
					// M4 modes resolve these same shared equipment aliases. Resolve
					// by asset name, never equip/borrow a weapon or retain its pointers.
					const auto* name=on?"nightvision_wear_plr_default":"nightvision_remove_plr_default";
					const auto alias=game::DB_FindXAssetHeader(game::ASSET_TYPE_SOUND,name,0).sound;
					if(loaded_alias(alias,name) && claim_foley(serial))
					{utils::hook::invoke<void>(0x1403801C0,0,alias);++foley_requests;}
				}
				{const std::lock_guard lock(presentation_mutex);
					if(!presentation.sound_ready(serial,view.time))return scheduler::cond_continue;}
				const game::snd_alias_list_t* alias{};
				if(!utils::native_memory::read_bytes(&alias,reinterpret_cast<const void*>(on?0x141B84DA8:0x141B84DB0),sizeof(alias)) ||
					!loaded_alias(alias,on?"item_nightvision_on":"item_nightvision_off"))return scheduler::cond_end;
				if(claim_sound(view)){utils::hook::invoke<void>(0x1403801C0,0,alias);++sound_requests;}
				return scheduler::cond_end;
			},scheduler::pipeline::main);
		}
		void transition(game::pmove_t* pm)
		{
			if(!pm || !(pm->cmd.buttons&0x40000) || (pm->oldcmd.buttons&0x40000))
			{transition_hook.invoke<void>(pm);return;}
			const auto* ps=pm->ps;
			const int role=ps?weapons::native_ammunition::local_role(ps):-1;
			const bool local=role>=0 && head_pose_bridge::get_status().enabled;
			const bool was_on=local && (field<unsigned>(ps,0x3c0)&0x40)!=0;
			const std::array before{local?field<int>(ps,0x2c0):-1,local?field<int>(ps,0x2e4):-1};
			transition_hook.invoke<void>(pm);
			if(!local || pm->ps!=ps)return;
			const bool on=(field<unsigned>(ps,0x3c0)&0x40)!=0;
			if(on==was_on)return; // Native rejection never starts visual/audio feedback.
			const auto* fade=game::Dvar_FindVar("nightVisionFadeInOutTime");
			const auto* power=game::Dvar_FindVar("nightVisionPowerOnTime");
			if(!fade || !power)return;
			const auto time=timing(fade->current.value,power->current.value);if(!time)return;
			// Rendering consumes one equipment clock, not a weapon definition or
			// the old arm animation. Empty hands therefore follow the same path.
			{
				bool started{};std::uint64_t serial{};
				{const std::lock_guard lock(presentation_mutex);
					started=presentation.begin(weapons::native_ammunition::timeline(),pm->cmd.serverTime,pm->cmd.serverTime,on,*time);
					if(started){presenting=true;serial=presentation.serial();}}
				if(started){++presentations_started;queue_sound(on,serial);}
			}
			for(unsigned side=0;side<2;++side)
			{
				const auto offset=side*0x24;
				const int weapon_state=field<int>(ps,0x2c0+offset);
				if(weapon_state==before[side] || (weapon_state!=33 && weapon_state!=34))continue;
				const int remaining=field<int>(ps,0x2b4+offset);
				const int shortened=std::min(remaining,time->duration(on));
				if(shortened>=0 && shortened<remaining)
					std::memcpy(reinterpret_cast<std::byte*>(pm->ps)+0x2b4+offset,&shortened,sizeof(shortened));
			}
		}
		hi::object_identity identity(std::uint64_t reference)noexcept{return {0,reference};}
		hi::grasp grasp(button source,std::uint64_t reference)noexcept
		{return {hi::head_gesture_object(reference),hi::role::part,source,hi::recipe::single,hi::capability::action};}
		void reset()noexcept{gesture.reset();pending_until={};started_slot=-1;++authorization;}
		void reset_level()noexcept
		{reset();const std::lock_guard lock(presentation_mutex);presentation.reset();presenting=false;}
		std::optional<vec> gesture_point(const hi::frame& f,hand actor)noexcept
		{
			if(!valid_hand(actor) || f.body.generation!=f.input.reference_generation)return {};
			const auto& grip=f.input.runtime_grip[unsigned(actor)];
			head_pose_bridge::world_pose world;
			// Head equipment follows the physical controller grip. The cosmetic
			// wrist includes glove alignment/pivot offsets and can be 10-20 cm
			// below the real grasp at the forehead (Gulag capture, 2026-10-03).
			if(!grip.valid || !head_pose_bridge::tracking_to_world(f.body,grip.tracking,world))return {};
			return head_local(f.body,world.position);
		}
		native_state observe()noexcept
		{
			if(!verified || !game::CL_IsCgameInitialized())return {};
			const auto* ps=reinterpret_cast<const std::byte*>(game::g_entities[0].client);
			unsigned flags{};std::array<unsigned,4> types{};
			if(!scripted_control::allowed(ps) || !utils::native_memory::read_bytes(&flags,ps+0x3c0,4) ||
				!utils::native_memory::read_bytes(types.data(),ps+0x1fa0,sizeof(types)))return {};
			return decode(types,flags);
		}
	}
	bool available()noexcept {return weapons::carry::active() && bool(observe());}
	void collect_interactions(const hi::frame& f)noexcept
	{
		state=observe();
		if(!state || !f.input.focused || f.input.orientation_settling){reset();return;}
		if(gesture.active() || f.input.sampled_at<pending_until)return;
		for(int h=0;h<2;++h)
		{
			const auto actor=hand(h);if(!(f.valid_hands&(1u<<h)) || !hi::free(actor))continue;
			const auto point=gesture_point(f,actor);if(!point || !acquisition(state.on,*point))continue;
			for(const auto source:{button::trigger,button::grip})
			{
				const auto edge=hi::input(actor,source);if(!edge.press || !edge.down || edge.release)continue;
				// Empty hands only; the common arbiter also prevents simultaneous
				// knife, magazine, grenade, world or firearm acquisitions.
				hi::offer({actor,grasp(source,f.input.reference_generation),edge.event,source==button::trigger?10u:11u,
					std::abs((*point)[1]),1,true,true});
			}
		}
	}
	void update_interactions()noexcept
	{
		const auto* f=hi::simulation();if(!f)return;
		if(!state){reset();return;}
		if(!gesture.active())for(int h=0;h<2 && !gesture.active();++h)for(const auto source:{button::trigger,button::grip})
		{
			if(!hi::granted(hand(h),hi::domain::gesture,identity(f->input.reference_generation),source))continue;
			const auto p=gesture_point(*f,hand(h));
			if(p && gesture.begin(hand(h),source,state.on,*p,f->input))
			{start_forward=f->body.head_forward;start_up=f->body.head_up;started_slot=state.slot;reason="head equipment grasped";}
		}
		if(!gesture.active())return;
		const auto actor=gesture.actor();const auto source=gesture.source();const auto reference=gesture.reference();const bool on=gesture.original_on();
		const auto p=gesture_point(*f,actor);
		const auto edge=hi::input(actor,source);
		const bool allowed=p && state.slot==started_slot && (f->valid_hands&(1u<<unsigned(actor))) &&
			hi::has(actor,hi::domain::gesture) && hands::dot(start_forward,f->body.head_forward)>.8f && hands::dot(start_up,f->body.head_up)>.8f;
		if(!gesture.update(p.value_or(vec{}),state.on,edge.down && !edge.release,allowed,f->input))return;
		hi::completed(actor,grasp(source,reference).destination);
		const auto token=++authorization;const auto expected=state;
		pending_until=f->input.sampled_at+std::chrono::milliseconds(500);++requests;
		reason=on?"native raise nightvision requested":"native lower nightvision requested";
		action_slots::request(unsigned(state.slot),3,0,[token,reference,expected,actor]{
			// Main dispatch may precede finish() publishing the completed grasp.
			// Accept our prior gesture witness, but never another held object.
			if(authorization.load()!=token || !weapons::carry::active() || !command_hand_free(hi::current(),actor,reference))return false;
			const auto input=controller_input::latest();const auto now=clock::now();
			if(!input.focused || input.reference_generation!=reference || now<input.sampled_at || now-input.sampled_at>std::chrono::milliseconds(150))return false;
			const auto current=observe();return current && current.slot==expected.slot && current.on==expected.on;
		});
	}
	void report_interactions()noexcept
	{if(gesture.active())hi::observed(gesture.actor(),grasp(gesture.source(),gesture.reference()));}
	void lifecycle(bool suspended)noexcept
	{if(suspended){reset();reason="head equipment gesture suspended";}}
	class component final:public component_interface
	{
		void post_unpack()override
		{
			// Read-only force-on/off method witnesses establish the active bit;
			// the actual operation always follows the original action-slot input.
			constexpr std::array<unsigned char,7> on{0x83,0x88,0xc0,3,0,0,0x40},off{0x83,0xa0,0xc0,3,0,0,0xbf};
			constexpr std::array<unsigned char,4> slot{0x44,0x8d,0x70,3};
			std::array<unsigned char,7> a{},b{};std::array<unsigned char,4> c{};
			verified=utils::native_memory::read_bytes(a.data(),reinterpret_cast<const void*>(0x1404BBE03),a.size()) && a==on &&
				utils::native_memory::read_bytes(b.data(),reinterpret_cast<const void*>(0x1404BBE53),b.size()) && b==off &&
				utils::native_memory::read_bytes(c.data(),reinterpret_cast<const void*>(0x1404B5229),c.size()) && c==slot;
			// Native input acceptance, native view mode/fade and the two local
			// goggles aliases are one contract. No shared weapon asset is edited.
			constexpr std::array<std::uint8_t,13> entry{0x48,0x89,0x5c,0x24,0x20,0x56,0x57,0x41,0x54,0x48,0x83,0xec,0x20};
			constexpr std::array<std::uint8_t,14> vision_entry{0x48,0x83,0xec,0x28,0x48,0x63,0xc1,0x48,0x69,0xc8,0x14,0x02,0,0};
			constexpr std::array<std::uint8_t,8> fade_entry{0x40,0x53,0x55,0x57,0x48,0x83,0xec,0x20};
			constexpr std::array<std::uint8_t,7> sound_entry{0x0f,0xbe,0x0d,0x69,0x3a,0x83,0x01};
			constexpr std::array<std::uint8_t,5> sound_on{0xe8,0x05,0xb9,0,0},sound_off{0xe8,0x9e,0xb8,0,0};
			constexpr std::array<std::uint8_t,5> foley_on{0xe8,0x1d,0xb9,0,0},foley_off{0xe8,0xb6,0xb8,0,0};
			constexpr std::array<std::uint8_t,10> entity_sound{0x48,0x0f,0xbf,0xc2,0x48,0x8d,0x0d,0x29,0x28,0x8b};
			transition_ready=verified && matches(0x140697780,entry) && matches(0x1403B1050,vision_entry) &&
				matches(0x1403B32E0,fade_entry) && matches(0x1403801C0,sound_entry) &&
				matches(0x1403748B6,sound_on) && matches(0x14037491D,sound_off) &&
				matches(0x1403748DE,foley_on) && matches(0x140374945,foley_off) && matches(0x140380200,entity_sound);
			if(transition_ready)
			{
				transition_hook.create(0x140697780,transition);
				vision_hook.create(0x1403B1050,vision);fade_hook.create(0x1403B32E0,visibility);
				utils::hook::call(0x1403748B6,native_sound);utils::hook::call(0x14037491D,native_sound);
				utils::hook::call(0x1403748DE,native_foley_on);utils::hook::call(0x140374945,native_foley_off);
			}
			scripting::on_level_start(reset_level);scripting::on_shutdown([](bool,bool after){if(!after)reset_level();});
			command::add("vr_nightvision_status",[]{scheduler::once([]{
				const auto current=observe();const auto text=std::format("verified={} slot={} on={} grasped={} requests={} presentation={} device_transitions={} sound_requests={} foley_requests={} reason={}\n",
					verified,current.slot,current.on,gesture.active(),requests,transition_ready,presentations_started.load(),sound_requests.load(),foley_requests.load(),reason);
				console::info("[VR nightvision] %s",text.c_str());
				scheduler::once([text]{utils::io::write_file_atomic("minidumps/h2-mod-vr-nightvision.txt",text);},scheduler::pipeline::async);
			},scheduler::pipeline::server);});
		}
	};
}
REGISTER_COMPONENT(vr::gameplay::equipment::nightvision::component)
