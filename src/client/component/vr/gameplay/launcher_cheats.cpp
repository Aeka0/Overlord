#include <std_include.hpp>
#include "launcher_cheats_policy.hpp"
#include "native_ammunition.hpp"
#include "underbarrel_native.hpp"
#include "official_cheats.hpp"
#include "../settings.hpp"
#include "game/game.hpp"
#include "game/dvars.hpp"
#include "game/scripting/execution.hpp"
#include "component/scheduler.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "loader/component_loader.hpp"
#include <utils/native_memory.hpp>

namespace vr::gameplay::cheats::launcher
{
    namespace
    {
        game::dvar_t *health_option{},*notarget_option{},*ammo_option{};
        const std::string flags_key="vr_launcher_cheat_flags",sustain_key="vr_launcher_cheat_sustain";
        std::uint64_t reserve_writes{},reserve_rejections{};
        bool reported_error{},process_sustain_owned{};
        int read_saved(const scripting::entity& level,const std::string& key)
        {
            const auto value=level.get(key);
            return value.is<int>()?value.as<int>():0;
        }
        void set_sustain(const game::dvar_t* dvar,bool enabled)
        {
            game::Dvar_SetFromStringFromSource(dvar,enabled?"1":"0",game::DVAR_SOURCE_INTERNAL);
        }
        void refill_reserves(const void* player)
        {
            // Scan only the bounded native inventory. Compared writes preserve
            // every clip/chamber and share existing primary/secondary ammo pools.
            // Empty reserve cells may be allocated by the verified native adapter.
            std::array<std::uint32_t,15> inventory{};
            if(!utils::native_memory::read_bytes(inventory.data(),static_cast<const std::byte*>(player)+0x2f8,sizeof(inventory)))return;
            for(const auto weapon:inventory)
            {
                const auto ammo=weapons::native_ammunition::observe_carried(player,weapon);
                if(!ammo.valid)continue;
                if(ammo.reserve!=reserve_rounds)
                {
                    if(weapons::native_ammunition::commit_carried(ammo,ammo.loaded,reserve_rounds))++reserve_writes;
                    else ++reserve_rejections;
                }
                const auto module=weapons::underbarrel::native::resolve(weapons::native_ammunition::projected_identity(weapon));
                if(!module)continue;
                const auto secondary=weapons::underbarrel::native::observe(module);
                if(secondary.valid && secondary.ammo.reserve!=reserve_rounds)
                {
                    if(weapons::underbarrel::native::commit(secondary,{secondary.ammo.loaded,reserve_rounds}))++reserve_writes;
                    else ++reserve_rejections;
                }
            }
        }
        void update()
        {
            if(!game::SV_Loaded() || !game::CL_IsCgameInitialized() || !*game::levelEntityId || !game::g_entities[0].client)return;
            const auto* vr=game::Dvar_FindVar("vr_enable");
            const bool enabled=vr && vr->current.enabled;
            try
            {
                const scripting::entity level{*game::levelEntityId};
                auto& player=game::g_entities[0];
                const auto owned=unsigned(read_saved(level,flags_key))&flag_mask;
                const auto next=apply_flags(static_cast<unsigned char>(player.flags),owned,
                    enabled?health_option->current.integer:0,enabled && notarget_option->current.integer==1);
                // Same low native bits used by command.cpp's god/demigod/notarget.
                if(next.owned!=owned)level.set(flags_key,int(next.owned));
                player.flags=static_cast<char>(next.flags);

                const auto* sustain=game::Dvar_FindVar("player_sustainAmmo");
                const bool saved_sustain=read_saved(level,sustain_key)==1;
                const bool owned_sustain=process_sustain_owned || saved_sustain;
                const bool infinite=enabled && ammo_option->current.integer==2;
                if(sustain && infinite && !sustain_ammo())
                {
                    // Record provenance before the write, including after a native
                    // script reset. The dvar outlives levels; player flags do not.
                    if(!saved_sustain)level.set(sustain_key,1);
                    process_sustain_owned=true;
                    set_sustain(sustain,true);
                }
                else if(sustain && infinite && owned_sustain)
                {
                    if(!saved_sustain)level.set(sustain_key,1);
                    process_sustain_owned=true;
                }
                else if(sustain && !infinite && owned_sustain)
                {
                    // An independently enabled official Intel cheat retains authority.
                    const auto* official=game::Dvar_FindVar("sf_use_ignoreammo");
                    const bool official_enabled=official &&
                        ((official->type==game::dvar_type::boolean && official->current.enabled) ||
                         (official->type==game::dvar_type::integer && official->current.integer!=0) ||
                         (official->type==game::dvar_type::string && official->current.string && std::string_view(official->current.string)=="1"));
                    set_sustain(sustain,official_enabled);
                    level.set(sustain_key,0);
                    process_sustain_owned=false;
                }
                if(enabled && ammo_option->current.integer==1 && !transitioning())refill_reserves(player.client);
                reported_error=false;
            }
            catch(const std::exception& e)
            {
                if(!reported_error)console::error("[VR launcher cheats] %s\n",e.what());
                reported_error=true;
            }
        }
    }
    class component final:public component_interface
    {
        void post_unpack() override
        {
            static_assert(god==game::FL_GODMODE && demigod==game::FL_DEMI_GODMODE && notarget==game::FL_NOTARGET);
            static auto health= settings::cheat_health.values,hidden=settings::cheat_notarget.values,ammo=settings::cheat_ammo.values;
            health_option=dvars::register_enum(settings::cheat_health.name,health.data(),0,game::DVAR_FLAG_SAVED,
                "Launcher health cheat: off, native demigod or god; scripted deaths remain possible");
            notarget_option=dvars::register_enum(settings::cheat_notarget.name,hidden.data(),0,game::DVAR_FLAG_SAVED,
                "Launcher native notarget cheat: off or on");
            ammo_option=dvars::register_enum(settings::cheat_ammo.name,ammo.data(),0,game::DVAR_FLAG_SAVED,
                "Launcher ammo cheat: off, reserve locked at 500, or native sustain ammo");
            // Independent of Green Beret script hooks and physical-carry admission.
            // Level-owned provenance survives checkpoint loads, including a later
            // launch with these options disabled; no cached player pointers/epochs.
            scheduler::loop(update,scheduler::pipeline::server);
            command::add("vr_launcher_cheats_status",[]{scheduler::once([]{
                console::info("[VR launcher cheats] health=%d notarget=%d ammo=%d sustain=%d reserve_writes=%llu reserve_rejections=%llu\n",
                    health_option->current.integer,notarget_option->current.integer,ammo_option->current.integer,
                    sustain_ammo(),reserve_writes,reserve_rejections);
            },scheduler::pipeline::server);});
        }
    };
}
REGISTER_COMPONENT(vr::gameplay::cheats::launcher::component)
