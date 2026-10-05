#include <std_include.hpp>
#include "ladder_native.hpp"
#include "native_scripted_control.hpp"
#include "../settings.hpp"
#include "../head_pose_bridge.hpp"
#include "game/dvars.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::gameplay::ladders::native
{
    namespace
    {
        game::dvar_t* option{};bool ready{};
        utils::hook::detour check_ladder;
        void check_stub(game::pmove_t* pm,game::pml_t* pml)
        {
            check_ladder.invoke<void>(pm,pml);
            if(!enabled() || !pm || !pm->ps)return;
            const auto* server=reinterpret_cast<const game::playerState_s*>(game::g_entities[0].client);
            const auto* predicted=game::CG_GetPredictedPlayerState(0);
            if((pm->ps!=server && pm->ps!=predicted) || !scripted_control::allowed(pm->ps))return;
            // PM_CheckLadderMove supplies trace/normal bookkeeping. Only its
            // automatic ladder admission is replaced; Pmove then uses ordinary
            // collision/gravity unless the hand-owned native carrier is linked.
            // PM_Single at 0x14068FD25 calls this routine with (pm,pml), then
            // tests PS+0x54 bit 8 before calling PM_LadderMove (0x14068AB00).
            pm->ps->pm_flags&=~game::PMF_LADDER;
        }
    }
    bool enabled()noexcept
    {
        const auto* vr=game::Dvar_FindVar("vr_enable");const auto* hands=game::Dvar_FindVar("vr_independentHands");
        return ready && option && option->current.enabled && vr && vr->current.enabled && hands && hands->current.enabled && head_pose_bridge::get_status().enabled;
    }
    bool initialize()
    {
        option=dvars::register_bool(settings::physical_ladders.name,settings::physical_ladders.default_value,
            game::DVAR_FLAG_SAVED,"Use hand grips and physical pulling instead of automatic native ladder climbing");
        constexpr std::uint8_t bytes[]{0x40,0x55,0x57,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24,0xc1,0x48,0x81,0xec,0xf8,0,0,0};
        std::array<std::uint8_t,sizeof(bytes)> mask{};mask.fill(255);
        if(!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x1406880D0),{bytes,mask.data(),sizeof(bytes)}))return false;
        check_ladder.create(0x1406880D0,check_stub);ready=true;return true;
    }
}
