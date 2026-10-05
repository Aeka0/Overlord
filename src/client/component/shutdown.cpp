#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "game/game.hpp"

#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace process_shutdown
{
	namespace
	{
		utils::hook::detour com_quit_hook;
		utils::hook::detour sys_quit_hook;

		void prepare_native_shutdown()
		{
			// Drain the renderer before retiring callbacks and COM owners. The
			// native Com_Shutdown later unloads NVAPI, whose private interfaces may
			// still be attached to a device retained by the MOD.
			game::R_SyncRenderThread();
			component_loader::pre_destroy();
		}

		void com_quit_stub()
		{
			prepare_native_shutdown();
			com_quit_hook.invoke<void>();
		}

		void sys_quit_stub()
		{
			prepare_native_shutdown();
			sys_quit_hook.invoke<void>();
		}
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			// Supported H2 binary: Com_Quit_f -> Com_Shutdown -> Sys_Quit -> CRT.
			// Live unload stacks place NVAPI unloading inside Com_Shutdown, before
			// Sys_Quit. Hook the accepted quit wrapper and retain Sys_Quit for paths
			// that bypass it. Neither hook runs on cancellation or renderer restart.
			constexpr auto com_quit = 0x1405A52A0;
			constexpr auto sys_quit = 0x14064EF10;
			constexpr std::uint8_t quit_bytes[]{0x48, 0x83, 0xEC, 0x28, 0xE8, 0x27, 0xFE, 0xFF, 0xFF,
				0x48, 0x83, 0xC4, 0x28, 0xE9, 0x5E, 0x9C, 0x0A, 0x00};
			constexpr std::uint8_t quit_mask[]{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
				0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
			constexpr std::uint8_t bytes[]{0x48, 0x83, 0xEC, 0x28, 0xB9, 0x01, 0x00, 0x00, 0x00};
			constexpr std::uint8_t mask[]{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
			if (!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(com_quit),
				{quit_bytes, quit_mask, sizeof(quit_bytes)}) ||
				!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(sys_quit),
				{bytes, mask, sizeof(bytes)}))
			{
				throw std::runtime_error("Unsupported native process shutdown entry point");
			}
			com_quit_hook.create(com_quit, com_quit_stub);
			sys_quit_hook.create(sys_quit, sys_quit_stub);
		}
	};
}

REGISTER_COMPONENT(process_shutdown::component)
