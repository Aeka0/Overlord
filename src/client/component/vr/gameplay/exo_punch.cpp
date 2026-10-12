#include <std_include.hpp>
#include "exo_punch.hpp"
#include "exo_punch_policy.hpp"
#include "exo_punch_script.hpp"
#include "../settings.hpp"
#include "game/game.hpp"
#include "game/dvars.hpp"
#include "game/scripting/execution.hpp"
#include "component/gsc/script_extension.hpp"
#include "component/gsc/script_loading.hpp"
#include "component/notifies.hpp"
#include "component/scripting.hpp"
#include "component/scheduler.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "loader/component_loader.hpp"
#include <utils/native_memory.hpp>

namespace vr::gameplay::cheats::exo_punch
{
    namespace
    {
        struct pending_hit
        {
            hit_ticket ticket;
            int time{};
            std::string weapon;
        };
        // One bounded claim per native actor; no saved script fields, raw actor
        // pointers or process-wide ragdoll state. Entity generations reject reuse.
        std::array<pending_hit,3998> pending;
        game::dvar_t* option{};
        bool ready{};
        std::uint64_t serial{},claims{},dispatches{};
        std::string reason="waiting for native death scripts";
        bool configured() noexcept
        {
            const auto* vr=game::Dvar_FindVar("vr_enable");
            return ready && option && option->current.enabled && vr && vr->current.enabled;
        }
        bool truth(const scripting::script_value& value)
        {return value.is<int>() && value.as<int>()!=0;}
        bool death_guard()
        {
            // Native GSC deaths run in G_RunFrame, before the scheduler dispatcher.
            // This VM hook is the authority boundary; do not require the narrower
            // scheduler::server execution scope used by physical contact callers.
            if(!configured() || !*game::levelEntityId || !game::scr_VmPub->function_frame)return false;
            try
            {
                const auto local=game::scr_VmPub->function_frame->fs.localId;
                if(!local || local>=56320)return false;
                const auto self=game::scr_VarGlob->objectVariableValue[local].u.f.next;
                if(!self || self>=56320 || game::scr_VarGlob->objectVariableValue[self].w.type!=game::SCRIPT_ENTITY)return false;
                const scripting::entity actor{self};
                const auto ref=actor.get_entity_reference();
                if(ref.classnum || !ref.entnum || ref.entnum>=pending.size())return false;
                auto& hit=pending[ref.entnum];
                if(!hit.ticket.serial || hit.ticket.target!=weapons::native_carry::entity_key(ref.entnum))return false;
                int health{};
                if(!utils::native_memory::read_at(&game::g_entities[ref.entnum],0x184,health))return false;
                const auto attacker=actor.get(std::string("attacker"));
                const auto mod=actor.get(std::string("damagemod"));
                const auto weapon=actor.get(std::string("damageweapon"));
                const auto classname=actor.get(std::string("classname"));
                if(!weapon.is<std::string>() || !classname.is<std::string>())return false;
                const auto native_weapon=weapon.as<std::string>(),native_class=classname.as<std::string>();
                const auto attacker_ref=attacker.is<scripting::entity>()?
                    attacker.as<scripting::entity>().get_entity_reference():game::scr_entref_t{0xffff,0xffff};
                const bool player=!attacker_ref.entnum && !attacker_ref.classnum;
                const auto budget=actor.get(std::string("damagetaken"));
                const float damage=budget.is<int>()?float(budget.as<int>()):budget.is<float>()?budget.as<float>():0.f;
                if(!std::isfinite(damage) || damage<=0 || damage>100000)return false;
                const auto* ragdolls=game::Dvar_FindVar("ragdoll_enable");
                const bool immediate=actor.get(std::string("ragdoll_immediate")).get_raw().type!=game::SCRIPT_NONE ||
                    truth(actor.get(std::string("forceragdollimmediate")));
                const death_evidence evidence{game::CG_GetGameTime(0),health,
                    mod.is<std::string>() && mod.as<std::string>()=="MOD_MELEE"?8:0,
                    true,truth(scripting::get_object_variable(*game::levelEntityId,0xae66u)),
                    ragdolls && ((ragdolls->type==game::dvar_type::boolean && ragdolls->current.enabled) ||
                        (ragdolls->type==game::dvar_type::integer && ragdolls->current.integer!=0)),
                    player,immediate,native_weapon,native_class};
                const bool accepted=admits({hit.time,hit.weapon},evidence);
                // One death dispatch only, even if a later callback re-enters.
                hit={};
                if(accepted)++dispatches;
                return accepted;
            }
            catch(const std::exception&){return false;}
        }
        void install()
        {
            ready=false;pending={};
            try
            {
                const auto native=scripting::get_function_pos("animscripts/death","specialdeath");
                (void)scripting::get_function_pos("animscripts/death","doimmediateragdolldeath");
                const auto replacement=scripting::get_function_pos(std::string(script_name),"exo_punch_death");
                notifies::set_gsc_hook(native,replacement,death_guard);
                ready=true;reason="native melee death boundary ready";
            }
            catch(const std::exception& e){reason=e.what();}
        }
    }
    bool active() noexcept
    {return scheduler::is_executing(scheduler::pipeline::server) && game::CL_IsCgameInitialized() && configured();}
    hit_ticket begin_hit(weapons::native_carry::world_key target,std::string_view weapon) noexcept
    {
        if(!active() || target.entity<=0 || target.entity>=int(pending.size()) ||
            target!=weapons::native_carry::entity_key(target.entity) || weapon.empty() || weapon.size()>127)return {};
        try
        {
            const hit_ticket ticket{target,++serial};
            pending[target.entity]={ticket,game::CG_GetGameTime(0),std::string(weapon)};
            ++claims;return ticket;
        }
        catch(const std::exception&){return {};}
    }
    void finish_hit(hit_ticket ticket,bool lethal) noexcept
    {
        if(!ticket.serial || ticket.target.entity<=0 || ticket.target.entity>=int(pending.size()))return;
        auto& hit=pending[ticket.target.entity];
        if(hit.ticket.serial==ticket.serial && !lethal)hit={};
    }
    class component final:public component_interface
    {
        void post_unpack() override
        {
            option=dvars::register_bool(settings::exo_punch.name,settings::exo_punch.default_value,game::DVAR_FLAG_SAVED,
                "Boost player VR melee damage and lethal melee ragdoll impact; official Ragdoll Impact takes precedence");
            gsc::register_virtual_source(std::string(script_name),std::string(script_source));
            gsc::add_function("vrexopunchdeath",[]{
                if(!game::scr_VmPub->outparamcount)return;
                const scripting::script_value value{game::scr_VmPub->top[0]};
                if(!value.is<scripting::entity>())return;
                // Keep the native helper intact. No global level flag, damage
                // callback, bullet policy or custom ragdoll implementation.
                scripting::call_script_function(value.as<scripting::entity>(),"animscripts/death","doimmediateragdolldeath",{});
            });
            scripting::on_level_start(install);
            scripting::on_shutdown([](bool,bool after){if(!after){ready=false;pending={};}});
            command::add("vr_exo_punch_status",[]{scheduler::once([]{
                console::info("[VR EXO punch] enabled=%d ready=%d claims=%llu death_dispatches=%llu reason=%s\n",
                    option->current.enabled,ready,claims,dispatches,reason.c_str());
            },scheduler::pipeline::server);});
        }
    };
}
REGISTER_COMPONENT(vr::gameplay::cheats::exo_punch::component)
