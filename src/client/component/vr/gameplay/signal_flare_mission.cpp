#include <std_include.hpp>
#include "signal_flare_mission.hpp"
#include "signal_flare_script.hpp"
#include "native_scripted_control.hpp"
#include "weapon_carry_runtime.hpp"
#include "component/notifies.hpp"
#include "component/scripting.hpp"
#include "component/scheduler.hpp"
#include "game/scripting/execution.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>

namespace vr::gameplay::equipment::special::flare::mission
{
    namespace
    {
        char* block{};const char* entry{};std::atomic_bool bound{},flow_bound{};
        std::atomic_uint64_t epoch{1},ignitions{},recoveries{},failures{},ignored_drops{};
        scripting::script_value fire_name,drop_name,ignored_drop_name;
        game::scr_string_t fire{},drop{},private_fire{},private_drop{},ignored_drop{};
        bool waiting{},accepted{},session{},recover_pending{};
        bool mode()
        {
            const auto* enabled=game::Dvar_FindVar("vr_abdominalEquipment");
            return flow_bound && enabled && enabled->current.enabled && head_pose_bridge::get_status().enabled;
        }
        bool flag(const char* name)
        {
            if(!*game::levelEntityId)return false;
            const scripting::entity level{*game::levelEntityId};const auto value=level.get("flag");
            if(!value.is<scripting::array>())return false;
            const auto result=value.as<scripting::array>().get(std::string(name));return result.is<int>() && result.as<int>()!=0;
        }
        bool enter()
        {
            if(!mode())return false;
            if(session)return true;
            session=true;return false;
        }
        bool await_ignition(){if(mode())waiting=true;return false;}
        bool finish(){if(mode())waiting=session=false;return mode();}
        void restore_events()
        {
            bound=false;waiting=accepted=session=recover_pending=false;
            if(block)
            {
                for(const auto [offset,original,replacement]:std::array<std::array<unsigned,3>,4>{{{92,unsigned(fire),unsigned(private_fire)},{109,unsigned(fire),unsigned(private_fire)},{97,unsigned(drop),unsigned(private_drop)},{188,unsigned(drop),unsigned(private_drop)}}})
                {unsigned current{};std::memcpy(&current,block+offset,4);if(current==replacement)utils::hook::set(block+offset,original);}
            }
            fire_name={};drop_name={};ignored_drop_name={};fire=drop=private_fire=private_drop=ignored_drop=0;
        }
        void restore()
        {
            restore_events();flow_bound=false;
            if(entry)notifies::clear_hook(entry);
            if(block)for(unsigned offset:{0u,allowfire_begin,waiter_observe,151u,165u})notifies::clear_hook(block+offset);
            block=nullptr;entry=nullptr;
        }
        void bind()
        {
            restore();++epoch;
            const auto file=scripting::script_function_table_sort.find("maps/dc_whitehouse_code");
            if(file==scripting::script_function_table_sort.end())return;
            const auto function=scripting::get_token_single(0xc660);const char* begin{},*end{};
            for(const auto& [name,pos]:file->second)if(name==function)begin=pos;
            if(!begin)return;for(const auto& [name,pos]:file->second)if(pos>begin && (!end || pos<end))end=pos;
            if(!end || end-begin>2048 || static_cast<unsigned char>(*begin)!=0x32)return;
            const scripting::script_value flare{"flare"},fired{"weapon_fired"},dropped{"drop_flare"},hint{"how_to_pop_flare"};
            fire=fired.get_raw().u.stringValue;drop=dropped.get_raw().u.stringValue;
            const auto at=locate({reinterpret_cast<const std::byte*>(begin),std::size_t(end-begin)},flare.get_raw().u.stringValue,fire,drop,hint.get_raw().u.stringValue);
            if(!at){++failures;return;}
            block=const_cast<char*>(begin)+*at;entry=begin;
            fire_name=scripting::script_value("vr_flare_ignited");drop_name=scripting::script_value("vr_flare_cancelled");
            private_fire=fire_name.get_raw().u.stringValue;private_drop=drop_name.get_raw().u.stringValue;
            ignored_drop_name=scripting::script_value("vr_flare_ignored_drop");ignored_drop=ignored_drop_name.get_raw().u.stringValue;
            utils::hook::set(block+92,private_fire);utils::hook::set(block+109,private_fire);utils::hook::set(block+97,private_drop);utils::hook::set(block+188,private_drop);
            // Stack-neutral skips retain original hint lifetime, flag_set,
            // objective notification, music and dialogue. No attack command or
            // global weapon_fired/end_firing event is synthesized.
            notifies::set_gsc_hook(entry,block+258,enter);
            notifies::set_gsc_hook(block,block+70,mode);
            notifies::set_gsc_hook(block+allowfire_begin,block+waiter_prepare,mode);
            // The VM executes waiter_prepare directly after the redirect; a
            // hook on that target would never run. The following GetString is
            // visited normally, before the original private-event wait begins.
            notifies::set_gsc_hook(block+waiter_observe,block+waiter_observe,await_ignition);
            notifies::set_gsc_hook(block+151,block+258,finish);
            notifies::set_gsc_hook(block+165,block+258,finish);
            bound=flow_bound=true;recover_pending=mode();
        }
        game::scr_string_t alias(unsigned owner,game::scr_string_t event,const game::VariableValue*)
        {
            if(!bound || (event!=fire && event!=drop))return 0;
            try
            {
                const scripting::entity player{game::scr_entref_t{0,0}};
                if(owner!=player.get_entity_id())return 0;
                // Flat mode keeps both original controls, including its timed
                // drop. In VR only the physical transaction names private_fire.
                if(!mode())return event==fire?private_fire:private_drop;
                // Suppress the player's scripted deadline drop explicitly. It
                // cannot end a held cap/tube or wake the private cancellation
                // waiter; the mission's separate dialogue/flags still proceed.
                if(event==drop){++ignored_drops;return ignored_drop;}
            }
            catch(...){++failures;}
            return 0;
        }
        void recover()
        {
            if(!recover_pending || !mode() || !game::CL_IsCgameInitialized() || !game::g_entities[0].client)return;
            recover_pending=false;
            try
            {
                if(!flag("player_flare"))return;
                const bool popped=flag("player_flare_popped");
                const scripting::entity player{game::scr_entref_t{0,0}};
                // A flat save can restore the old waittill AND a forced flare.
                // End only this exact mission thread, use its original public
                // unlock methods, and restart at the guarded native entry.
                scripting::notify(player,"remove_flare",{});waiting=session=false;
                const auto selected=player.call("getcurrentweapon");
                if(selected.is<std::string>() && selected.as<std::string>()=="flare")
                {
                    player.call("allowfire",{1});player.call("enableweaponswitch");
                    player.call("enableoffhandweapons");player.call("enableweaponpickup");
                    auto previous=player.get("old_weapon");
                    const auto valid=[&](const scripting::script_value& value){return value.is<std::string>() && !value.as<std::string>().empty() &&
                        value.as<std::string>()!="flare" && player.call("hasweapon",{value}).as<int>();};
                    if(!valid(previous))
                    {
                        const auto primaries=player.call("getweaponslistprimaries");
                        if(primaries.is<scripting::array>())
                        {
                            const auto list=primaries.as<scripting::array>();
                            for(unsigned i=0;i<unsigned(std::clamp(list.size(),0,32));++i)
                                if(const auto item=list.get(i);valid(item)){previous=item;break;}
                        }
                    }
                    if(valid(previous))
                        player.call("switchtoweaponimmediate",{previous});
                    player.call("takeweapon",{"flare"});
                    scripting::call("setsaveddvar",{"actionSlotsHide",0});
                    scripting::call("setsaveddvar",{"cg_gunDownAnimDelayTime",250});
                }
                accepted=popped;
                if(!popped)scripting::call_script_function(player,"maps/dc_whitehouse_code",scripting::get_token_single(0xc660),{});
                ++recoveries;
            }
            catch(...){++failures;}
        }
    }
    bool active()noexcept{return bound && mode();}
    std::uint64_t generation()noexcept{return epoch.load();}
    bool consumed()noexcept
    {try{return active() && game::CL_IsCgameInitialized() && flag("player_flare_popped");}catch(...){return false;}}
    bool authorized()noexcept
    {
        if(!bound || !mode() || !waiting || accepted || !scheduler::is_executing(scheduler::pipeline::server) ||
            !game::CL_IsCgameInitialized() || !scripted_control::allowed(game::g_entities[0].client))return false;
        try{return flag("player_flare") && !flag("player_flare_popped");}catch(...){return false;}
    }
    bool ignite()noexcept
    {
        if(!authorized())return false;
        try
        {
            accepted=true;waiting=false;
            scripting::notify(scripting::entity{game::scr_entref_t{0,0}},"vr_flare_ignited",{});++ignitions;return true;
        }
        catch(...){accepted=false;waiting=true;++failures;return false;}
    }
    std::string status()
    {
        bool story_ready{},story_done{};
        try{if(bound && game::CL_IsCgameInitialized()){story_ready=flag("player_flare");story_done=flag("player_flare_popped");}}catch(...){}
        return std::format("mission_bound={} waiting={} accepted={} story_ready={} story_done={} cap_authorized={} ignitions={} ignored_drops={} recoveries={} failures={} generation={}\n",
            bound.load(),waiting,accepted,story_ready,story_done,authorized(),ignitions.load(),ignored_drops.load(),recoveries.load(),failures.load(),epoch.load());
    }
    class component final:public component_interface
    {
        void post_unpack()override
        {
            scripting::on_level_start(bind);scripting::on_notify_alias(alias);
            // Saved-level loading can resume a retained script before the
            // post-load callback. Preserve only control-flow guards across that
            // boundary; never retain VM string references through shutdown.
            scripting::on_shutdown([](bool free_scripts,bool after){if(!after){if(free_scripts)restore();else restore_events();++epoch;}});
            scheduler::loop(recover,scheduler::pipeline::server,50ms);
        }
    };
}
REGISTER_COMPONENT(vr::gameplay::equipment::special::flare::mission::component)
