#include <std_include.hpp>
#include "trainer_policy.hpp"
#include "weapon_carry_runtime.hpp"
#include "native_scripted_control.hpp"
#include "component/scripting.hpp"
#include "component/gsc/script_extension.hpp"
#include "component/scheduler.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "game/scripting/execution.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>
#include <utils/io.hpp>

namespace vr::gameplay::trainer
{
	namespace
	{
		utils::hook::detour current_weapon_hook;
		const char* tutorial_begin{},*tutorial_end{};
		const char* hint_begin{},*hint_end{};
		std::uint32_t last_weapon{};
		switch_hint pending{};
		std::atomic_uint64_t query_count{},request_count{},completion_count{};
		void reset() noexcept {tutorial_begin=tutorial_end=hint_begin=hint_end=nullptr;last_weapon=0;pending={};}
		void bind()
		{
			reset();
			const auto file=scripting::script_function_table_sort.find("maps/trainer");
			if (file==scripting::script_function_table_sort.end()) return;
			const auto function=scripting::get_token_single(0xb168);
			for (const auto& [name,pos]:file->second) if (name==function) tutorial_begin=pos;
			if (!tutorial_begin) return;
			for (const auto& [name,pos]:file->second)
				if (pos>tutorial_begin && (!tutorial_end || pos<tutorial_end)) tutorial_end=pos;
			const auto hint_token=scripting::get_token_single(0xced3);
			for (const auto& [name,pos]:file->second) if (name==hint_token) hint_begin=pos;
			for (const auto& [name,pos]:file->second)
				if (pos>hint_begin && (!hint_end || pos<hint_end)) hint_end=pos;
			if(!hint_begin || !hint_end || !*game::levelEntityId)return;
			// Saved games can restore a waittill without re-executing its earlier
			// notifyoncommand registration. Recover only this level's exact native
			// switch wait, once at load; never scan the VM table every frame.
			for(const auto& thread:game::scr_VarGlob->objectVariableValue)
			{
				if((thread.w.type&0xff)!=game::VAR_NOTIFY_THREAD || thread.u.o.u.self!=*game::levelEntityId)continue;
				const auto* name=game::SL_ConvertToString(thread.w.notifyName>>8);if(!name)continue;
				const auto hint=switch_event(name);if(hint==switch_hint::none)continue;
				if(pending!=switch_hint::none && pending!=hint){pending={};return;}
				pending=hint;
			}
			if(pending!=switch_hint::none)++request_count;
		}
		bool in_range(const char* begin,const char* end) noexcept
		{
			const auto pos=reinterpret_cast<std::uintptr_t>(game::scr_function_stack->pos);
			return begin && end && pos>=reinterpret_cast<std::uintptr_t>(begin) && pos<reinterpret_cast<std::uintptr_t>(end);
		}
		bool observe_hint(game::BuiltinFunction)
		{
			// _id_CED3 is sometimes called synchronously INSIDE the weapon poll.
			// Let native notifyoncommand register normally; its following waittill
			// must be live before the server adapter can deliver a physical action.
			if(in_range(hint_begin,hint_end) && game::scr_VmPub->outparamcount==2)
			{
				const scripting::script_value event{game::scr_VmPub->top[0]};
				if(event.is<std::string>())
					if(const auto hint=switch_event(event.as<std::string>());hint!=switch_hint::none){pending=hint;++request_count;}
			}
			return false;
		}
		std::int32_t observe_notify(unsigned entity,std::int32_t name,const game::VariableValue*)
		{
			if(entity==*game::levelEntityId && pending!=switch_hint::none)
			{
				const auto* text=game::SL_ConvertToString(name);
				if(text && (std::string_view(text)=="clearing_hints" || std::string_view(text)=="sideArmTraining_end"))pending={};
			}
			return 0;
		}
		void complete_hint()
		{
			if(pending==switch_hint::none || !weapons::carry::active() || !game::CL_IsCgameInitialized() ||
				!scripted_control::allowed(game::g_entities[0].client) || *game::keyCatchers)return;
			const auto* paused=game::Dvar_FindVar("cl_paused");if(!paused || paused->current.integer)return;
			const auto held=tutorial_weapon(weapons::carry::held_instances(),0);if(!held)return;
			try
			{
				const auto name=scripting::get_object_variable(*game::levelEntityId,0xd033u);
				if(!name.is<std::string>())return;
				const auto pistol_name=name.as<std::string>();std::uint32_t pistol{};
				for(std::uint32_t i=1;i<512;++i)
					if(game::weapon_defs[i] && game::weapon_defs[i]->szInternalName && pistol_name==game::weapon_defs[i]->szInternalName){pistol=i;break;}
				if(!completes_hint(pending,held,pistol))return;
				const auto event=pending==switch_hint::primary?"did_action_primary":"did_action_sidearm";
				pending={};last_weapon=held; // Commit before the native callback resumes.
				scripting::notify(scripting::entity{game::scr_entref_t{0,0}},event,{});++completion_count;
			}
			catch(const std::exception&) { /* Missing script evidence cannot advance the lesson. */ }
		}
		void current_weapon(game::scr_entref_t self)
		{
			// Only the seven no-argument reads in Dunn's sidearm lesson. Other
			// mission queries, native firing and ammo projection keep their owner.
			if (in_range(tutorial_begin,tutorial_end) && !self.entnum && !self.classnum &&
				!game::scr_VmPub->outparamcount && weapons::carry::active() &&
				scripted_control::allowed(game::g_entities[0].client))
			{
				++query_count;
				if (!last_weapon && game::g_entities[0].client)
					std::memcpy(&last_weapon,reinterpret_cast<const std::byte*>(game::g_entities[0].client)+0x3bc,4);
				last_weapon=tutorial_weapon(weapons::carry::held_instances(),last_weapon&511);
				if (last_weapon && last_weapon<512 && game::weapon_defs[last_weapon])
				{
					std::array<char,1024> name{};
					game::BG_GetWeaponNameComplete(game::Weapon{last_weapon},false,name.data(),static_cast<unsigned>(name.size()));
					if (name[0] && name.back()=='\0') {scripting::push_value(std::string(name.data()));return;}
				}
			}
			current_weapon_hook.invoke<void>(self);
		}
	}
	class component final:public component_interface
	{
		void post_unpack() override
		{
			// Native method 0x831c; capture confirms PS+0x3bc and the complete
			// weapon-name accessor. Verify before detouring its public entry.
			constexpr std::array<std::uint8_t,18> bytes{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,
				0x57,0x48,0x81,0xec,0x30,0x04,0,0};
			std::array<std::uint8_t,bytes.size()> mask{};mask.fill(255);
			if (!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x1404b6350),{bytes.data(),mask.data(),bytes.size()})) return;
			current_weapon_hook.create(0x1404b6350,current_weapon);
			gsc::intercept_builtin("notifyoncommand",observe_hint);
			scripting::on_notify_alias(observe_notify);
			scripting::on_level_start(bind);
			scripting::on_shutdown([](bool,bool after){if(!after)reset();});
			scheduler::loop(complete_hint,scheduler::pipeline::server);
			command::add("vr_trainer_status",[] {
				const auto report=std::format("weapon_queries={} hint_requests={} physical_completions={}\n",query_count.load(),request_count.load(),completion_count.load());
				console::info("[VR trainer] %s",report.c_str());utils::io::write_file_atomic("minidumps/overlord-trainer.txt",report);
			});
		}
	};
}
REGISTER_COMPONENT(vr::gameplay::trainer::component)
