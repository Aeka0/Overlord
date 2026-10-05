#include <std_include.hpp>
#include "../h2/entrypoints.hpp"
#include "weapon_interaction.hpp"
#include "designator_events.hpp"
#include "designator_event_policy.hpp"
#include "weapon_carry_runtime.hpp"
#include "native_scripted_control.hpp"
#include "native_ammunition.hpp"
#include "native_carry.hpp"
#include "special_equipment_policy.hpp"
#include "component/gsc/script_extension.hpp"
#include "component/scripting.hpp"
#include "component/notifies.hpp"
#include "component/console.hpp"
#include "component/command.hpp"
#include "component/scheduler.hpp"
#include "game/scripting/execution.hpp"
#include "game/scripting/stack_isolation.hpp"
#include "game/scripting/safe_execution.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>

namespace vr::gameplay::equipment::special::designator_events
{
	namespace
	{
		const char* begin{},*end{};std::atomic_uint64_t traces{},rejected{};
		weapons::muzzle_frame shot{};controller_input::clock::time_point shot_at{};
		char* operand{};game::scr_string_t original_event{},private_event{};scripting::script_value alias_name;
		std::atomic_bool bound{},flow_bound{};std::atomic_uint64_t confirmed{},other_fires{};
		char* close_operand{};game::scr_string_t close_event{},cycle_event{};scripting::script_value cycle_name;
		cooldown charge;bool rearm_pending{};std::atomic_uint64_t rearmed{},cooldown_rejections{};
		std::atomic_bool firing_available{};
		unsigned recovering_weapon{};std::atomic_uint64_t activation_recoveries{};
		const char* startup_wait_start{},*pickup_lock_site{};
		const char* idle_site{},*opening_site{},*closing_site{},*pacing_site{};std::array<char*,2> switch_sites{};
		activation_phase observed_phase{};
		bool vr_mode(){const auto* enabled=game::Dvar_FindVar("vr_abdominalEquipment");return flow_bound && enabled && enabled->current.enabled && head_pose_bridge::get_status().enabled;}
		bool instant_startup(){return vr_mode();}
		bool note_idle(){observed_phase=activation_phase::idle;return false;}
		bool note_opening(){observed_phase=activation_phase::opening;return false;}
		bool instant_closing(){observed_phase=activation_phase::closing;return vr_mode();}
		bool switch_method(unsigned which)
		{
			observed_phase=which?activation_phase::opening:activation_phase::closing;
			const std::uint16_t method=vr_mode()?0x8321:0x8320;
			// Guard executes before the VM decodes this verified method operand.
			// Continue normal decoding/argument handling; only selection mode differs.
			utils::hook::set(switch_sites[which]+1,method);return false;
		}
		bool switch_closing(){return switch_method(0);}bool switch_opening(){return switch_method(1);}
		void reset_session(){recovering_weapon=0;charge={};rearm_pending=false;shot={};observed_phase=activation_phase::unknown;firing_available=bound.load();}
		weapons::hold device_owner()
		{
			const auto owner=weapons::carry::current_hold();if(!owner.weapon || owner.weapon>=512 || !owner.can_fire())return {};
			const auto* def=game::weapon_defs[owner.weapon];return def && def->szInternalName && std::string_view(def->szInternalName)=="usp_laserdesignator"?owner:weapons::hold{};
		}
		void restore_events()
		{
			bound=false;
			if(operand){game::scr_string_t current{};std::memcpy(&current,operand,4);if(current==private_event)utils::hook::set(operand,original_event);}
			if(close_operand){game::scr_string_t current{};std::memcpy(&current,close_operand,4);if(current==cycle_event)utils::hook::set(close_operand,close_event);}
			operand=close_operand=nullptr;alias_name={};cycle_name={};original_event=private_event=close_event=cycle_event=0;
		}
		void restore()
		{
			restore_events();flow_bound=false;
			if(pickup_lock_site){notifies::clear_hook(pickup_lock_site);pickup_lock_site=nullptr;}
			for(auto* site:{idle_site,opening_site,closing_site,pacing_site})if(site)notifies::clear_hook(site);
			for(auto* site:switch_sites)if(site){notifies::clear_hook(site);utils::hook::set<std::uint16_t>(site+1,0x8320);}
			idle_site=opening_site=closing_site=pacing_site=nullptr;switch_sites={};reset_session();
			if(startup_wait_start){notifies::clear_hook(startup_wait_start);notifies::clear_hook(startup_wait_start+6);startup_wait_start=nullptr;}
			begin=end=nullptr;
		}
		game::scr_string_t alias(unsigned owner,game::scr_string_t event,const game::VariableValue* top)
		{
			if(!bound || (event!=original_event && event!=cycle_event && event!=close_event) || !game::CL_IsCgameInitialized())return 0;
			try
			{
				const scripting::entity player{game::scr_entref_t{0,0}};if(owner!=player.get_entity_id())return 0;
				if(event==close_event){const auto active=player.get("laserforceon");observed_phase=active.is<int>() && active.as<int>()?activation_phase::closing:activation_phase::requested;return 0;}
				if(event==cycle_event)
				{
					if(weapons::carry::active() && device_owner().weapon){rearm_pending=true;return 0;}
					observed_phase=activation_phase::closing;return close_event;
				}
				const auto value=top?scripting::script_value(*top):scripting::script_value{};
				const auto weapon=value.is<std::string>()?value.as<std::string>():std::string{};
				if(!forward_confirmation(weapons::carry::active(),weapon)){++other_fires;return 0;}
				if(weapons::carry::active() && !charge.accept(game::CG_GetGameTime(0))){++cooldown_rejections;return 0;}
				if(weapons::carry::active())firing_available=false;
				++confirmed;return private_event;
			}
			catch(...){++rejected;return 0;}
		}
		void bind()
		{
			const auto file=scripting::script_function_table_sort.find("maps/arcadia_code");
			if(bound && file!=scripting::script_function_table_sort.end())
			{
				const char* current{};for(const auto& [name,pos]:file->second)if(name=="get_laser_designated_trace")current=pos;
				game::scr_string_t a{},b{};if(operand)std::memcpy(&a,operand,4);if(close_operand)std::memcpy(&b,close_operand,4);
				if(current==begin && a==private_event && b==cycle_event){reset_session();return;}
			}
			restore();if(file==scripting::script_function_table_sort.end())return;
			for(const auto& [name,pos]:file->second)if(name=="get_laser_designated_trace")begin=pos;
			if(!begin)return;for(const auto& [name,pos]:file->second)if(pos>begin && (!end || pos<end))end=pos;
			const char* waiter{},*waiter_end{};
			for(const auto& [name,pos]:file->second)if(name=="laser_designate_target")waiter=pos;
			if(!waiter || !end)return;for(const auto& [name,pos]:file->second)if(pos>waiter && (!waiter_end || pos<waiter_end))waiter_end=pos;
			if(!waiter_end || waiter_end-waiter>8192)return;
			const scripting::script_value native_name{"weapon_fired"};original_event=native_name.get_raw().u.stringValue;
			const auto offset=wait_operand({reinterpret_cast<const std::byte*>(waiter),std::size_t(waiter_end-waiter)},original_event);
			const scripting::script_value close_name{"use_laser"};close_event=close_name.get_raw().u.stringValue;
			const auto close_offset=notify_operand({reinterpret_cast<const std::byte*>(waiter),std::size_t(waiter_end-waiter)},close_event);
			if(!offset || !close_offset)return;
			const char* startup{},*startup_end{};
			for(const auto& [name,pos]:file->second)if(name=="laser_targeting_device")startup=pos;
			if(!startup)return;for(const auto& [name,pos]:file->second)if(pos>startup && (!startup_end || pos<startup_end))startup_end=pos;
			if(!startup_end)return;const auto wait=startup_wait({reinterpret_cast<const std::byte*>(startup),std::size_t(startup_end-startup)});if(!wait)return;
			const scripting::script_value device_name{"usp_laserdesignator"};
			const auto sites=transition_layout({reinterpret_cast<const std::byte*>(startup),std::size_t(startup_end-startup)},device_name.get_raw().u.stringValue,close_event);if(!sites)return;
			const auto lock=pickup_lock({reinterpret_cast<const std::byte*>(startup),std::size_t(startup_end-startup)});
			if(!lock || *lock+5!=sites->opening)return;
			// Only this device's flat-game pickup restriction is omitted. Other
			// mission restrictions and native pickup admission remain authoritative.
			pickup_lock_site=startup+*lock;notifies::set_gsc_hook(pickup_lock_site,pickup_lock_site+5,instant_startup);
			idle_site=startup+sites->idle;opening_site=startup+sites->opening;closing_site=startup+sites->closing;
			pacing_site=startup+sites->pacing;notifies::set_gsc_hook(pacing_site,pacing_site+6,instant_startup);
			notifies::set_gsc_hook(idle_site,idle_site,note_idle);notifies::set_gsc_hook(opening_site,opening_site,note_opening);
			notifies::set_gsc_hook(closing_site,closing_site+17,instant_closing);
			for(unsigned i=0;i<2;++i){switch_sites[i]=const_cast<char*>(startup)+sites->switches[i];notifies::set_gsc_hook(switch_sites[i],switch_sites[i],i?switch_opening:switch_closing);}
			startup_wait_start=startup+*wait;
			// Preserve instant physical selection. Skip only animation waits, after
			// native watchers were started; retain enableweaponswitch and all logic.
			notifies::set_gsc_hook(startup_wait_start,startup_wait_start+21,instant_startup);
			notifies::set_gsc_hook(startup_wait_start+6,startup_wait_start+21,instant_startup);
			alias_name=scripting::script_value("vr_designator_confirmed");private_event=alias_name.get_raw().u.stringValue;
			cycle_name=scripting::script_value("vr_designator_cycle_finished");cycle_event=cycle_name.get_raw().u.stringValue;
			close_operand=const_cast<char*>(waiter)+*close_offset;utils::hook::set(close_operand,cycle_event);
			operand=const_cast<char*>(waiter)+*offset;utils::hook::set(operand,private_event);bound=true;
			flow_bound=true;
			firing_available=true;
			// Loading a save resets derived ownership, not the original toggle.
			// Restored opening/closing threads retain their native continuation.
		}
		void recover_activation()
		{
			if(!bound || !game::CL_IsCgameInitialized() || !weapons::carry::active() || !scripted_control::allowed(game::g_entities[0].client))return;
			const auto* ps=reinterpret_cast<const std::byte*>(game::g_entities[0].client);unsigned actual{};std::memcpy(&actual,ps+0x3bc,4);
			const auto desired=vr::h2::sp::weapon_selection_request.read();
			if(recovering_weapon)
			{
				const auto state=activation(recovering_weapon);
				if(!state.enabled || (weapons::carry::abdominal_active(recovering_weapon) && state.phase!=activation_phase::closing)){recovering_weapon=0;return;}
				if(!state.active){if(weapons::carry::restore_native_projection()){recovering_weapon=0;++activation_recoveries;}return;}
				if(state.ready() && state.phase==activation_phase::idle)
				{try{const scripting::entity player{game::scr_entref_t{0,0}};scripting::notify(player,"use_laser",{});}catch(...){++rejected;}}
				return;
			}
			for(const auto slot:weapon_slots({ps,0x1fc0}))if(slot)
			{
				const auto state=activation(slot.weapon);
				if(state.ready() && (state.phase==activation_phase::idle || state.phase==activation_phase::unknown) && !weapons::carry::abdominal_active(slot.weapon))
				{recovering_weapon=slot.weapon;try{const scripting::entity player{game::scr_entref_t{0,0}};scripting::notify(player,"use_laser",{});}catch(...){++rejected;}return;}
				if(!state.orphaned((actual&511)==0 && desired==0,weapons::carry::abdominal_active(slot.weapon)))continue;
				// Resume the original startup wait, then let its own watcher and
				// use_laser teardown restore both weapon-switch and pickup permissions.
				if(weapons::native_carry::select(slot.weapon))recovering_weapon=slot.weapon;
				return;
			}
		}
		void rearm()
		{
			recover_activation();
			if(!rearm_pending){firing_available=bound && game::CL_IsCgameInitialized() && charge.ready(game::CG_GetGameTime(0));return;}
			if(!bound || !game::CL_IsCgameInitialized() || !weapons::carry::active()){rearm_pending=false;return;}
			const auto owner=device_owner();if(!owner.weapon){rearm_pending=false;return;}
			if(!charge.ready(game::CG_GetGameTime(0)) || !scripted_control::allowed(game::g_entities[0].client))return;
			try
			{
				const scripting::entity player{game::scr_entref_t{0,0}};const auto active=player.get("laserforceon");
				if(!active.is<int>() || !active.as<int>()){rearm_pending=false;return;}
				const auto ammo=weapons::native_ammunition::observe_carried(game::g_entities[0].client,owner.id());
				if(!ammo.valid || (ammo.loaded<1 && !weapons::native_ammunition::commit_carried(ammo,1,ammo.reserve)))return;
				rearm_pending=false;
				(void)scripting::call_script_function(player,"maps/arcadia_code","laser_designate_target",{});++rearmed;firing_available=true;
			}
			catch(...){++rejected;rearm_pending=false;}
		}
		bool trace(game::BuiltinFunction original)
		{
			const auto pos=game::scr_function_stack->pos;
			if(!bound || !begin || !end || !pos || pos<begin || pos>=end || game::scr_VmPub->outparamcount!=4 || !weapons::carry::active())return false;
			try
			{
				std::vector<scripting::script_value> args;args.reserve(4);for(unsigned i=0;i<4;++i)args.emplace_back(game::scr_VmPub->top[-int(i)]);
				if(!args[3].is<scripting::entity>())return false;
				const auto self=args[3].as<scripting::entity>().get_entity_reference();if(self.entnum || self.classnum)return false;
				auto pose=weapons::current_muzzle();const auto owner=weapons::sample_native_equipped();const auto input=controller_input::latest();const auto now=controller_input::clock::now();
				const bool latched=shot.valid && shot.owner.id()==owner.id() && now>=shot_at && now-shot_at<500ms;
				if(latched)pose=shot;
				if(pose.profile_id!="laserdesignator" || (!latched && !weapons::ready(pose,owner,input.reference_generation,now)) || !scripted_control::predicted_allowed())
				{++rejected;return false;}
				// Preserve the mission's trace flags, ignored entity and return schema.
				// Only this one script function changes its eye ray into a device ray.
				const auto target=hands::add(pose.position,hands::scale(pose.axis[0],7000));
				args[0]=scripting::vector(pose.position.data());args[1]=scripting::vector(target.data());
				scripting::script_value result;
				{
					scripting::stack_isolation isolated;
					for(auto i=args.rbegin();i!=args.rend();++i)scripting::push_value(*i);
					game::scr_VmPub->outparamcount=game::scr_VmPub->inparamcount;game::scr_VmPub->inparamcount=0;
					if(!scripting::safe_execution::call(reinterpret_cast<scripting::script_function>(original),game::scr_entref_t{0xffff,0xffff}))return false;
					if(!game::scr_VmPub->inparamcount)return false;
					game::Scr_ClearOutParams();game::scr_VmPub->outparamcount=game::scr_VmPub->inparamcount;game::scr_VmPub->inparamcount=0;
					result=scripting::script_value(game::scr_VmPub->top[1-game::scr_VmPub->outparamcount]);
				}
				game::Scr_ClearOutParams();scripting::push_value(result);shot={};++traces;return true;
			}
			catch(...){++rejected;return false;}
		}
	}
	void record_shot(const weapons::muzzle_frame& value)noexcept{shot=value;shot_at=controller_input::clock::now();}
	bool ready()noexcept{return bound.load();}
	bool owns_native_transition()noexcept{return bound && (recovering_weapon || observed_phase==activation_phase::closing);}
	bool can_fire()noexcept{return bound && firing_available.load();}
	activation_state activation(std::uint32_t weapon)noexcept
	{
		activation_state out;if(!weapon || weapon>=512)return out;
		const auto* def=game::weapon_defs[weapon];if(!def || !def->szInternalName || std::string_view(def->szInternalName)!="usp_laserdesignator")return out;
		out.device=true;
		if(!scheduler::is_executing(scheduler::pipeline::server) || !game::CL_IsCgameInitialized() || !game::g_entities[0].client)return out;
		try
		{
			const scripting::entity player{game::scr_entref_t{0,0}};const auto active=player.get("laserforceon"),disabled=player.get("disable_laser_designator");
			out.active=active.is<int>() && active.as<int>()!=0;out.enabled=bound && !(disabled.is<int>() && disabled.as<int>());
			unsigned selected{},flags{};const auto* ps=reinterpret_cast<const std::byte*>(game::g_entities[0].client);
			std::memcpy(&selected,ps+0x3bc,4);std::memcpy(&flags,ps+0x3c0,4);
			// Live disable/enableweaponswitch methods 0x832C/0x832D own bit0x800.
			out.selected=(selected&511)==weapon;out.locked=(flags&0x800)!=0;
			out.phase=observed_phase;
		}
		catch(...){out.enabled=false;}
		return out;
	}
	bool request_activation(std::uint32_t weapon)noexcept
	{
		const auto state=activation(weapon);if(!state.enabled || state.phase==activation_phase::closing)return false;
		if(state.active)return !state.locked && weapons::native_carry::select(weapon);
		if(state.phase==activation_phase::requested || state.phase==activation_phase::opening)return false;
		try{const scripting::entity player{game::scr_entref_t{0,0}};observed_phase=activation_phase::requested;scripting::notify(player,"use_laser",{});return true;}
		catch(...){++rejected;return false;}
	}
	class component final:public component_interface
	{
		void post_unpack()override
		{
			gsc::intercept_builtin("bullettrace",trace);scripting::on_level_start(bind);scripting::on_notify_alias(alias);
			scheduler::loop(rearm,scheduler::pipeline::server,50ms);
			scripting::on_shutdown([](bool free_scripts,bool after){if(!after){if(free_scripts)restore();else {restore_events();reset_session();}}});
			command::add("vr_designator_status",[]{scheduler::once([]{console::info("[VR designator] mission_trace_bound=%d event_bound=%d confirmations=%llu other_fires=%llu hand_traces=%llu rejected=%llu rearmed=%llu cooldown_rejections=%llu pending=%d activation_recoveries=%llu recovering_weapon=%u\n",begin && end,bound.load(),confirmed.load(),other_fires.load(),traces.load(),rejected.load(),rearmed.load(),cooldown_rejections.load(),rearm_pending,activation_recoveries.load(),recovering_weapon);},scheduler::pipeline::server);});
		}
	};
}
REGISTER_COMPONENT(vr::gameplay::equipment::special::designator_events::component)
