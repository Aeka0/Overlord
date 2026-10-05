#include <std_include.hpp>
#include "official_cheats.hpp"
#include "official_cheats_script.hpp"
#include "weapon_inventory_snapshot.hpp"
#include "weapon_carry_runtime.hpp"
#include "native_carry.hpp"
#include "underbarrel_native.hpp"
#include "game/game.hpp"
#include "component/gsc/script_extension.hpp"
#include "component/gsc/script_loading.hpp"
#include "component/notifies.hpp"
#include "component/scripting.hpp"
#include "component/scheduler.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "game/scripting/execution.hpp"
#include "loader/component_loader.hpp"

namespace vr::gameplay::cheats
{
    namespace
    {
        namespace saved=weapons::inventory_snapshot;
        std::atomic_bool green{},busy{},pickaxe_map{};
        bool hooks{},native_transition{};
        std::string native_script,reason="waiting for official scripts";
        std::uint64_t captures{},restores{},failures{},pickups{};
        bool flag(const char* name) noexcept
        {
            const auto* d=game::Dvar_FindVar(name);if(!d)return false;
            switch(d->type)
            {
            case game::dvar_type::boolean:return d->current.enabled;
            case game::dvar_type::integer:return d->current.integer!=0;
            case game::dvar_type::string:return d->current.string && std::string_view(d->current.string)=="1";
            default:return false;
            }
        }
        bool enabled()
        {
            const auto* vr=game::Dvar_FindVar("vr_enable");const auto* carry=game::Dvar_FindVar("vr_physicalCarry");
            return vr && vr->current.enabled && carry && carry->current.enabled;
        }
        bool guard(){return hooks && enabled() && !native_transition;}
        bool observer_guard(){return hooks && enabled();}
        void refresh()
        {
            const auto* map=game::Dvar_FindVar("mapname");
            pickaxe_map=map && map->current.string && pickaxe_level(map->current.string);
            green=hooks && enabled() && game::CL_IsCgameInitialized() && flag("g_using_greenberet_ts");
        }
        struct archive
        {
            scripting::array data;bool loading{},valid{true};
            template<class T> void operator()(const char* key,T& value)
            {
                if(!valid)return;
                const std::string name{key};
                if constexpr(std::is_same_v<T,std::string>)
                {
                    if(loading){const auto v=data.get(name);if(!v.is<std::string>()){valid=false;return;}value=v.as<std::string>();if(value.size()>96)valid=false;}
                    else data.set(name,value);
                }
                else if constexpr(std::is_enum_v<T> || std::is_integral_v<T>)
                {
                    if(loading)
                    {
                        const auto v=data.get(name);if(!v.is<int>()){valid=false;return;}const auto n=v.as<int>();
                        if constexpr(std::is_same_v<T,bool>){if(n!=0 && n!=1){valid=false;return;}}
                        if constexpr(std::is_unsigned_v<T>){if(n<0){valid=false;return;}}
                        value=static_cast<T>(n);
                    }
                    else data.set(name,int(value));
                }
                else if constexpr(std::is_floating_point_v<T>)
                {
                    if(loading){const auto v=data.get(name);if(!v.is<float>()){valid=false;return;}value=v.as<float>();if(!std::isfinite(value))valid=false;}
                    else data.set(name,value);
                }
                else
                {
                    archive nested;nested.loading=loading;
                    if(loading){const auto v=data.get(name);if(!v.is<scripting::array>()){valid=false;return;}nested.data=v.as<scripting::array>();}
                    saved::fields(nested,value);valid=nested.valid;if(!loading && valid)data.set(name,nested.data);
                }
            }
        };
        scripting::array encode(saved::snapshot s)
        {
            archive out;int schema=saved::snapshot::schema;
            out("schema",schema);out("count",s.count);out("selected",s.selected);
            scripting::array weapons;
            for(int i=0;i<s.count;++i){archive item;saved::fields(item,s.weapons[i]);weapons.push(item.data);}
            out.data.set(std::string("weapons"),weapons);return out.data;
        }
        bool decode(const scripting::script_value& value,saved::snapshot& s)
        {
            if(!value.is<scripting::array>())return false;
            archive in{value.as<scripting::array>(),true};int schema{};
            in("schema",schema);in("count",s.count);in("selected",s.selected);
            if(!in.valid || schema!=saved::snapshot::schema || s.count<0 || s.count>int(s.weapons.size()))return false;
            const auto list=in.data.get(std::string("weapons"));if(!list.is<scripting::array>())return false;
            const auto entries=list.as<scripting::array>();if(entries.size()!=s.count)return false;
            for(int i=0;i<s.count;++i)
            {const auto v=entries.get(unsigned(i));if(!v.is<scripting::array>())return false;archive item{v.as<scripting::array>(),true};saved::fields(item,s.weapons[i]);if(!item.valid)return false;}
            return saved::validate(s);
        }
        void original(const char* function)
        {
            native_transition=true;const auto restore=gsl::finally([]{native_transition=false;});
            scripting::call_script_function(scripting::entity{*game::levelEntityId},native_script,function,{});
            refresh();
        }
        void install()
        {
            hooks=false;green=false;busy=false;
            try
            {
                native_script=scripting::get_token_single(0xb190);
                const auto give=scripting::get_function_pos(native_script,"greenberet_giveweapon");
                const auto take=scripting::get_function_pos(native_script,"greenberet_takeweapon");
                const auto monitor=scripting::get_function_pos(native_script,"greenberet_monitor");
                (void)scripting::get_function_pos(native_script,"greenberet_flare_monitor");
                const auto name=std::string(script_name);
                const auto g=scripting::get_function_pos(name,"greenberet_giveweapon"),t=scripting::get_function_pos(name,"greenberet_takeweapon");
                const auto m=scripting::get_function_pos(name,"greenberet_monitor");
                notifies::set_gsc_hook(give,g,guard);notifies::set_gsc_hook(take,t,guard);notifies::set_gsc_hook(monitor,m,observer_guard);
                hooks=true;refresh();reason="official cheat boundaries ready";
            }
            catch(const std::exception& e){reason=e.what();}
        }
    }

    bool sustain_ammo() noexcept
    {return flag("player_sustainAmmo");}
    bool ragdoll_impact() noexcept
    {
        if(!scheduler::is_executing(scheduler::pipeline::server) || !game::CL_IsCgameInitialized() || !*game::levelEntityId)return false;
        try
        {
            // sf_use_ragdoll_mode's native update owns level._id_AE66, including
            // mission eligibility and checkpoint restoration. Do not read the menu request.
            const auto active=scripting::get_object_variable(*game::levelEntityId,0xae66u);
            return active.is<int>() && active.as<int>()!=0;
        }
        catch(const std::exception&){return false;}
    }
    bool green_beret() noexcept{return green.load();}
    bool transitioning() noexcept{return busy.load();}
    bool chest_bayonet() noexcept{return green_beret() && !pickaxe_map;}
    bool waist_pickaxes() noexcept{return green_beret() && pickaxe_map;}
    bool body_weapon(std::string_view name) noexcept{return green_beret() && body_weapon_definition(name);}
    bool settle_pickup(std::uint32_t weapon)
    {
        if(!green_beret())return true;
        const auto* def=weapon<512?game::weapon_defs[weapon]:nullptr;if(!def || !def->szInternalName)return false;
        if(std::string_view(def->szInternalName)=="claymore")return true; // Native mission exception.
        const auto ammo=weapons::native_ammunition::observe_carried(game::g_entities[0].client,weapon);
        if(!ammo.valid)return false;
        if(!weapons::native_ammunition::commit_carried(ammo,ammo.loaded,0))return false;
        const auto module=weapons::underbarrel::native::resolve(ammo.id());
        if(module)
        {const auto secondary=weapons::underbarrel::native::observe(module);if(!secondary.valid || !weapons::underbarrel::native::commit(secondary,{secondary.ammo.loaded,0}))return false;}
        ++pickups;return true;
    }
    bool update()
    {
        if(!scheduler::is_executing(scheduler::pipeline::server) || !hooks || !game::CL_IsCgameInitialized() || !*game::levelEntityId)return false;
        refresh();
        try
        {
            const scripting::entity level{*game::levelEntityId};const auto request=level.get(std::string("vr_cheat_transition"));
            if(!request.is<int>()){busy=false;return false;}
            const int mode=request.as<int>();busy=true;
            if(mode!=1 && mode!=2){level.set(std::string("vr_cheat_transition"),{});busy=false;return true;}
            const auto value=level.get(std::string("vr_cheat_inventory"));
            if(mode==1)
            {
                // Repeated native init callbacks must not overwrite the original
                // loadout with guns collected while the mode is already active.
                if(value.get_raw().type==game::SCRIPT_NONE)
                {
                    saved::snapshot snapshot;
                    if(!saved::capture(snapshot)){reason="inventory capture rejected; native weapons retained";++failures;level.set(std::string("vr_cheat_transition"),{});busy=false;console::error("[VR cheats] %s\n",reason.c_str());return true;}
                    level.set(std::string("vr_cheat_inventory"),encode(snapshot));level.set(std::string("vr_cheat_phase"),1);++captures;
                    original("greenberet_giveweapon");level.set(std::string("vr_cheat_phase"),2);
                    weapons::carry::restore_inventory({},{});
                }
                reason="Green Beret inventory escrow active";
            }
            else
            {
                saved::snapshot snapshot;
                if(value.get_raw().type==game::SCRIPT_NONE)original("greenberet_takeweapon");
                else
                {
                    if(!decode(value,snapshot))throw std::runtime_error("saved VR inventory rejected; recovery snapshot retained");
                    const auto phase=level.get(std::string("vr_cheat_phase"));
                    if(!phase.is<int>() || phase.as<int>()!=3)
                    {if(!saved::settle())throw std::runtime_error("reload escrow settlement rejected before inventory restoration");original("greenberet_takeweapon");level.set(std::string("vr_cheat_phase"),3);}
                    if(!saved::restore(snapshot))throw std::runtime_error("VR inventory restoration rejected; recovery snapshot retained");
                    level.set(std::string("vr_cheat_inventory"),{});level.set(std::string("vr_cheat_phase"),{});++restores;
                }
                reason=value.get_raw().type==game::SCRIPT_NONE?"native inventory restored; no VR escrow":"native and VR inventory restored";
            }
            level.set(std::string("vr_cheat_transition"),{});busy=false;return true;
        }
        catch(const std::exception& e)
        {if(reason!=e.what()){reason=e.what();++failures;console::error("[VR cheats] %s\n",reason.c_str());}return busy;}
    }
    class component final:public component_interface
    {
        void post_unpack() override
        {
            gsc::register_virtual_source(std::string(script_name),std::string(script_source));
            gsc::add_function("vrcheatflaremonitor",[]{
                scripting::call_script_function(scripting::entity{*game::levelEntityId},native_script,"greenberet_flare_monitor",{});
            });
            scripting::on_level_start(install);
            scripting::on_shutdown([](bool,bool after){if(!after){hooks=false;green=false;busy=false;}});
            command::add("vr_cheats_status",[]{scheduler::once([]{
                console::info("[VR cheats] hooks=%d sustain=%d ragdoll=%d green=%d transition=%d captures=%llu restores=%llu pickups=%llu failures=%llu reason=%s\n",
                    hooks,sustain_ammo(),ragdoll_impact(),green_beret(),transitioning(),captures,restores,pickups,failures,reason.c_str());
            },scheduler::pipeline::server);});
        }
    };
}
REGISTER_COMPONENT(vr::gameplay::cheats::component)
