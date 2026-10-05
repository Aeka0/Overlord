#include <std_include.hpp>
#include "presentation_options.hpp"
#include "native_hud_visibility_bridge.hpp"
#include "settings.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "game/game.hpp"
#include "game/dvars.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::presentation_options
{
	namespace
	{
		game::dvar_t* hud_option{};
		game::dvar_t* blur_option{};
		const game::dvar_t disabled_hud{};
		bool install_hud_reads()
		{
			static_assert(std::atomic_bool::is_always_lock_free && sizeof(hide_hud)==1);
			// All native cg_drawHUD readers, including Game.IsHudEnabled and the
			// ownerdraw fallback. cg_draw2D and LUI/menu rendering stay untouched.
			constexpr std::uintptr_t native=0x141E39EA0;
			constexpr std::array<std::uintptr_t,8> sites{0x140367A2A,0x140367A69,0x140367B03,0x140367B51,
				0x140367BCF,0x140368815,0x140368975,0x14038AD7E};
			std::array<void*,sites.size()> relays{};
			for(unsigned i=0;i<sites.size();++i)
			{
				std::array<std::uint8_t,11> bytes{0x48,0x8b,0x05,0,0,0,0,0x80,0x78,0x10,0},mask{};mask.fill(0xff);
				const auto displacement=static_cast<std::int32_t>(native-sites[i]-7);
				std::memcpy(bytes.data()+3,&displacement,4);
				if(!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(sites[i]),{bytes.data(),mask.data(),bytes.size()}))return false;
			}
			// Prepare every relay before changing any site; reject partial contracts.
			for(unsigned i=0;i<sites.size();++i)
			{
				const auto thunk=utils::hook::assemble([&](utils::hook::assembler& a) {
					emit_hud_read(a,reinterpret_cast<std::uintptr_t>(&hide_hud),reinterpret_cast<std::uintptr_t>(&disabled_hud),native,sites[i]+7);
				});
				relays[i]=utils::hook::create_preserving_near_jump(sites[i],thunk);
				if(!relays[i])return false;
			}
			for(unsigned i=0;i<sites.size();++i){utils::hook::jump(sites[i],relays[i]);utils::hook::nop(sites[i]+5,2);}
			return true;
		}
	}
	class component final:public component_interface
	{
		void post_unpack() override
		{
			hud_option=dvars::register_bool(settings::hide_hud.name,settings::hide_hud.default_value,game::DVAR_FLAG_SAVED,
				"Hide native and spatial gameplay HUD while preserving menus");
			blur_option=dvars::register_bool(settings::disable_blur.name,settings::disable_blur.default_value,game::DVAR_FLAG_SAVED,
				"Disable fullscreen and spatial UI background blur in VR");
			if(!install_hud_reads())console::error("[VR HUD] visibility read signatures rejected; native HUD gates preserved\n");
			scheduler::loop([] {
				const auto* enabled=game::Dvar_FindVar("vr_enable");
				const bool vr=enabled && enabled->current.enabled;
				hide_hud.store(vr && hud_option->current.enabled,std::memory_order_relaxed);
				disable_blur.store(vr && blur_option->current.enabled,std::memory_order_relaxed);
			},scheduler::pipeline::main);
		}
		void pre_destroy() override {hide_hud=false;disable_blur=false;}
	};
}
REGISTER_COMPONENT(vr::presentation_options::component)
