#include <std_include.hpp>
#include "native_fullscreen_blur.hpp"
#include "fullscreen_blur_policy.hpp"
#include "head_pose_bridge.hpp"
#include "component/scripting.hpp"
#include "component/console.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::native_fullscreen_blur
{
	namespace
	{
		utils::hook::detour player_blur_hook;
		script_range damage_range; // Script VM pipeline only; never consumed by the renderer.
		std::atomic_bool damage_bound{},source_ready{};
		std::atomic_uint64_t damage_suppressed{},menu_suppressed{},source_frames{};
		std::atomic<float> menu_radius{};
		bool vr_enabled() {return head_pose_bridge::get_status().enabled;}
		void reset_damage() {damage_range={};damage_bound=false;}
		void bind_damage()
		{
			reset_damage();
			const auto file=scripting::script_function_table_sort.find("maps/_gameskill");
			if(file==scripting::script_function_table_sort.end())return;
			std::uintptr_t begin{},end{};
			for(const auto& [name,pos]:file->second)if(name=="blurview")begin=reinterpret_cast<std::uintptr_t>(pos);
			if(!begin)return;
			for(const auto& [name,pos]:file->second)
			{
				const auto next=reinterpret_cast<std::uintptr_t>(pos);
				if(next>begin && (!end || next<end))end=next;
			}
			const script_range range{begin,end};
			if(!range.contains(begin))return;
			damage_range=range;damage_bound=true;
		}
		void player_blur_stub(game::scr_entref_t ref)
		{
			// This native method is also used by story scripts. Reject only the
			// current VM caller in gameskill::blurview, including its delayed zero
			// ramp; that ramp must not clear a newer authored/death blur request.
			const auto position=reinterpret_cast<std::uintptr_t>(game::scr_function_stack->pos);
			if(damage_range.contains(position) && suppress_damage_request(vr_enabled(),!ref.classnum && !ref.entnum,
				game::scr_VmPub->outparamcount,position,damage_range))
			{++damage_suppressed;return;}
			// A void method: the VM retains its normal argument/return cleanup.
			player_blur_hook.invoke<void>(ref);
		}
		float menu_radius_stub(int client)
		{
			const auto radius=utils::hook::invoke<float>(0x1403D8790,client);
			const bool filter=client==0 && vr_enabled();
			if(filter){menu_radius=radius;++source_frames;if(radius>0)++menu_suppressed;}
			// Runs in CG_CalcViewValues before refdef publication. Both eyes get
			// this frame's value through the normal immutable native view copy.
			// Keep the separate gameplay and HUD terms, even during pause/death.
			return filter_menu_radius(filter,radius);
		}
		template<std::size_t N> bool verify(std::uintptr_t address,const std::uint8_t (&bytes)[N])
		{
			std::array<std::uint8_t,N> mask{};mask.fill(0xff);
			return bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(address),{bytes,mask.data(),N}));
		}
	}
	std::string source_status()
	{
		return std::format("source_filter={} damage_bound={} damage_requests_suppressed={} menu_frames_suppressed={} source_frames={} native_menu_radius={}\n",
			source_ready.load(),damage_bound.load(),damage_suppressed.load(),menu_suppressed.load(),source_frames.load(),menu_radius.load());
	}
	class source_component final:public component_interface
	{
		void post_unpack() override
		{
			constexpr std::uint8_t method[]{0x48,0x89,0x5c,0x24,8,0x57,0x48,0x83,0xec,0x60};
			constexpr std::uint8_t menu_call[]{0xe8,0xa8,0x8a,2,0};
			constexpr std::uint8_t menu_entry[]{0x48,0x89,0x5c,0x24,8,0x57,0x48,0x83,0xec,0x20};
			if(!verify(0x1404B8EA0,method) || !verify(0x1403AFCE3,menu_call) || !verify(0x1403D8790,menu_entry))
			{console::error("[VR blur] native source filter signatures rejected\n");return;}
			player_blur_hook.create(0x1404B8EA0,player_blur_stub);
			utils::hook::call(0x1403AFCE3,menu_radius_stub);
			scripting::on_level_start(bind_damage);
			scripting::on_shutdown([](bool,bool after){if(!after)reset_damage();});
			source_ready=true;
		}
		void pre_destroy() override {source_ready=false;reset_damage();}
	};
}
REGISTER_COMPONENT(vr::native_fullscreen_blur::source_component)
