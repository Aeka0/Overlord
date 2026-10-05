#include <std_include.hpp>
#include "launcher_runtime.hpp"
#include "launcher_targeting.hpp"
#include "launcher_aim_bridge.hpp"
#include "javelin_screen.hpp"
#include "weapon_carry_runtime.hpp"
#include "native_scripted_control.hpp"
#include "component/console.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::gameplay::weapons::launcher
{
	namespace
	{
		std::atomic_uint64_t projected{},selection_retained{},ownership_retained{};
		utils::hook::detour aim_mode_hook;
		int aim_mode(std::uint32_t weapon,bool alternate,const void* player)
		{
			const int native=aim_mode_hook.invoke<int>(weapon,alternate);
			return player && physical_fire_mode(player,weapon,alternate)?0:native;
		}
		void automatic_select(int local_client,bool fire_attempt)
		{
			if(local_client!=0){utils::hook::invoke<void>(0x1403B9FA0,local_client,fire_attempt);return;}
			// The two local EV_NOAMMO / FIREATTEMPT event branches. Suppressing
			// inventory cleanup alone would still make carry adopt an automatic
			// replacement weapon before the player released the spent tube.
			const auto owner=carry::current_hold();
			const auto* ps=game::CL_IsCgameInitialized()?game::CG_GetPredictedPlayerState(local_client):nullptr;
			if(ps && native_ammunition::observe(ps).weapon==owner.weapon && retain_empty(ps,owner.weapon)){++selection_retained;return;}
			utils::hook::invoke<void>(0x1403B9FA0,local_client,fire_attempt);
		}
		void automatic_remove(void* ps,std::uint32_t token)
		{
			// Only PM's empty-inventory cleanup call. Drop_Weapon and script
			// takeweapon still reach the native ownership-removal authority.
			if(retain_empty(ps,token)){++ownership_retained;return;}
			utils::hook::invoke<void>(0x1406A8E90,ps,token);
		}
		bool project_target(game::gentity_s* actor,float fov,const float* delta,float* screen)
		{
			const auto owner=carry::current_hold();const auto scene=carry::firing_scene(owner.id());
			const auto* profile=scene.authored?scene.authored->launcher:nullptr;
			if(actor!=&game::g_entities[0] || !profile || !profile->guided || !ready())
				return utils::hook::invoke<bool>(0x1405130D0,actor,fov,delta,screen);
			const auto input=controller_input::latest();auto muzzle=current_muzzle();
			if(profile->id=="javelin" && javelin_screen::requested(owner) && !javelin_screen::lock_aim(owner,muzzle))return false;
			if(!actor->client || !delta || !screen || !input.focused || !scripted_control::allowed(actor->client) ||
				!aim_supported(owner) ||
				!weapons::ready(muzzle,owner,input.reference_generation,controller_input::clock::now()))return false;
			// GScr target projection passes target - (actor.currentOrigin + viewHeight).
			// Its native projection reads entity angles at +0x40. Keep both read-only.
			hands::vec eye{};std::memcpy(eye.data(),reinterpret_cast<const std::byte*>(actor)+0xf4,sizeof(eye));
			eye[2]+=reinterpret_cast<const game::playerState_s*>(actor->client)->viewHeightCurrent;
			std::array<hands::vec,3> axis{};
			static_assert(sizeof(axis) == 9 * sizeof(float));
			game::AnglesToAxis(reinterpret_cast<const float*>(reinterpret_cast<const std::byte*>(actor)+0x40),
				reinterpret_cast<float(*)[3]>(axis.data()));
			const auto corrected=reticle_delta({delta[0],delta[1],delta[2]},eye,muzzle,axis);
			for(float x:corrected)if(!std::isfinite(x))return false;
			++projected;
			return utils::hook::invoke<bool>(0x1405130D0,actor,fov,corrected.data(),screen);
		}
		template<size_t N>bool verify(std::uintptr_t at,const std::uint8_t(&bytes)[N])
		{std::array<std::uint8_t,N> mask{};mask.fill(255);return bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(at),{bytes,mask.data(),N}));}
	}
	std::array<std::uint64_t,3> native_statistics()noexcept{return {projected.load(),selection_retained.load(),ownership_retained.load()};}
	class native_component final:public component_interface
	{
		void post_unpack()override
		{
			constexpr std::uint8_t removal[]{0xe8,0xeb,0x62,0x01,0x00};
			constexpr std::uint8_t empty_select[]{0xe8,0x83,0x5f,0x04,0x00};
			constexpr std::uint8_t attempt_select[]{0xe8,0x74,0x5f,0x04,0x00};
			constexpr std::uint8_t select_entry[]{0x44,0x0f,0xb6,0xc2,0x33,0xd2,0xe9,0x05,0,0,0};
			constexpr std::uint8_t target[]{0xe8,0xc1,0xfd,0xff,0xff};
			constexpr std::uint8_t projection[]{0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x70};
			constexpr std::uint8_t eye_height[]{0xf3,0x0f,0x5c,0x88,0x18,0x01,0x00,0x00};
			constexpr std::uint8_t angles[]{0x48,0x83,0xc1,0x40,0x48,0x8d,0x54,0x24,0x30};
			constexpr std::uint8_t mode_entry[]{0x48,0x83,0xec,0x28,0x41,0xb8,0xec,0x05,0,0,0xe8,0xb1,0xe7,0xff,0xff};
			constexpr std::uint8_t forced_aim[]{0xe8,0x87,0xd1,0,0,0x41,0x3b,0xc6};
			constexpr std::uint8_t required_aim[]{0xe8,0x5f,0x5c,0,0,0x83,0xf8,0x02};
			constexpr std::uint8_t aim_delay[]{0xe8,0x4c,0x59,0,0,0x83,0xf8,0x01};
			if(!native_ammunition::initialize() || !verify(0x140692BA0,removal) || !verify(0x14051330A,target) ||
				!verify(0x1405130D0,projection) || !verify(0x1405132F9,eye_height) || !verify(0x1405130EE,angles) ||
				!verify(0x140374018,empty_select) || !verify(0x140374027,attempt_select) || !verify(0x1403B9FA0,select_entry) ||
				!verify(0x1406A15D0,mode_entry) || !verify(0x140694444,forced_aim) || !verify(0x14069B96C,required_aim) || !verify(0x14069BC7F,aim_delay))
			{console::error("[VR launcher] Native target/disposal contracts rejected\n");return;}
			// Only three witnessed PM queries provide a local player context.
			// Keep the CALL instructions intact for the shared projectile signature
			// owner. No WeaponDef mutation and no global/NPC aim-mode override.
			auto* mode_bridge=utils::hook::assemble([](utils::hook::assembler& a){
				emit_aim_bridge(a,reinterpret_cast<std::uintptr_t>(aim_mode));
			});
			if(!mode_bridge){console::error("[VR launcher] Aim-mode bridge unavailable\n");return;}
			aim_mode_hook.create(0x1406A15D0,mode_bridge);
			utils::hook::call(0x140692BA0,automatic_remove);utils::hook::call(0x14051330A,project_target);
			utils::hook::call(0x140374018,automatic_select);utils::hook::call(0x140374027,automatic_select);set_boundary_ready(2);
		}
	};
}
REGISTER_COMPONENT(vr::gameplay::weapons::launcher::native_component)
