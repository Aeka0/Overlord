#include <std_include.hpp>
#include "camera_bob_bridge.hpp"
#include "head_pose_bridge.hpp"
#include "settings.hpp"
#include "gameplay/native_ammunition.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "game/game.hpp"
#include "game/dvars.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::camera_bob
{
	namespace
	{
		constexpr std::uintptr_t horizontal=0x1406AB290,vertical=0x1406AB3C0;
		game::dvar_t* enabled{};
		bool installed{};
		thread_local bool camera_scope{};
		std::atomic_uint64_t camera_calls{},empty_calls{},disabled_calls{};
		float scale(float amplitude,const game::dvar_t* native)noexcept
		{return weaponless_scale(amplitude,native->current.value,camera_scope);}
		template<std::uintptr_t Original> float camera(const game::playerState_s* ps,float phase,float speed)
		{
			const bool local=game::CL_IsCgameInitialized() &&
				gameplay::weapons::native_ammunition::local_role(ps)==1 && head_pose_bridge::get_status().enabled;
			if(!local)return utils::hook::invoke<float>(Original,ps,phase,speed);
			camera_calls.fetch_add(1,std::memory_order_relaxed);
			if(enabled && !enabled->current.enabled)
			{
				disabled_calls.fetch_add(1,std::memory_order_relaxed);
				return 0.f;
			}
			std::uint32_t weapon{};
			std::memcpy(&weapon,reinterpret_cast<const std::byte*>(ps)+0x3bc,sizeof(weapon));
			if(!(weapon&511u))empty_calls.fetch_add(1,std::memory_order_relaxed);
			const auto before=camera_scope;
			camera_scope=true;
			const auto restore=gsl::finally([&]{camera_scope=before;});
			return utils::hook::invoke<float>(Original,ps,phase,speed);
		}
		template<size_t N> bool verify(std::uintptr_t address,const std::uint8_t (&bytes)[N])
		{
			std::array<std::uint8_t,N> mask{};mask.fill(255);
			return bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(address),{bytes,mask.data(),N}));
		}
	}
	class component final:public component_interface
	{
		void post_unpack()override
		{
			static_assert(offsetof(game::dvar_t,current)==0x10);
			static_assert(offsetof(game::WeaponDef,bobViewVerticalFactor)==0xae4 && offsetof(game::WeaponDef,bobViewHorizontalFactor)==0xae8);
			enabled=dvars::register_bool(settings::camera_bob,true,game::DVAR_FLAG_SAVED,"Enable native movement camera bob in VR, including empty hands and body knife");
			constexpr std::uint8_t h_call[]{0xe8,0xee,0xd2,0x2f,0};
			constexpr std::uint8_t v_call[]{0xe8,0x3b,0xd4,0x2f,0};
			constexpr std::uint8_t h_scale[]{0xf3,0x0f,0x59,0x70,0x10};
			constexpr std::uint8_t v_scale[]{0xf3,0x0f,0x59,0x78,0x10};
			constexpr std::uint8_t h_entry[]{0x40,0x53,0x48,0x83,0xec,0x40,0x8b,0x81,0x14,1,0,0};
			constexpr std::uint8_t v_entry[]{0x40,0x53,0x48,0x83,0xec,0x50,0x8b,0x81,0x14,1,0,0};
			if(verify(0x1403ADF9D,h_call) && verify(0x1403ADF80,v_call) && verify(0x1406AB363,h_scale) && verify(0x1406AB49A,v_scale) && verify(horizontal,h_entry) && verify(vertical,v_entry))
			{
				const auto h=utils::hook::assemble([](auto& a){emit_scale_bridge(a,6,reinterpret_cast<std::uintptr_t>(scale));});
				const auto v=utils::hook::assemble([](auto& a){emit_scale_bridge(a,7,reinterpret_cast<std::uintptr_t>(scale));});
				// Native sites require rel32 relays even when JIT memory is far away.
				// The native dvar pointer is live in RAX at these MULSS sites.
				const auto h_scale_relay=utils::hook::create_preserving_near_jump(0x140000000,h);
				const auto v_scale_relay=utils::hook::create_preserving_near_jump(0x140000000,v);
				if (!h_scale_relay || !v_scale_relay)
				{
					console::error("[VR camera bob] preserving relay allocation failed; hooks not installed\n");
					return;
				}
				const auto h_camera_relay=utils::hook::create_far_jump<0x140000000>(camera<horizontal>);
				const auto v_camera_relay=utils::hook::create_far_jump<0x140000000>(camera<vertical>);
				utils::hook::call(0x1406AB363,h_scale_relay);
				utils::hook::call(0x1406AB49A,v_scale_relay);
				utils::hook::call(0x1403ADF9D,h_camera_relay);
				utils::hook::call(0x1403ADF80,v_camera_relay);
				installed=true;
			}
			else console::error("[VR camera bob] native contract mismatch; movement hooks not installed\n");
			command::add("vr_camera_bob_status",[]{console::info("[VR camera bob] installed=%d enabled=%d camera_calls=%llu empty_calls=%llu disabled_calls=%llu\n",installed,enabled && enabled->current.enabled,camera_calls.load(),empty_calls.load(),disabled_calls.load());});
		}
	};
}
REGISTER_COMPONENT(vr::camera_bob::component)
