#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"
#include "input.hpp"
#include "native_ui_pointer.hpp"
#include "vr/debug_options.hpp"

#include "game_console.hpp"
#include "gui/gui.hpp"
#include "game/ui_scripting/execution.hpp"

#include <utils/hook.hpp>

namespace input
{
	namespace
	{
		struct point
		{
			short x;
			short y;
		};

		utils::hook::detour cl_char_event_hook;
		utils::hook::detour cl_key_event_hook;
		utils::hook::detour cl_mouse_move_hook;
		utils::hook::detour lui_cod_key_event_hook;
		std::array<bool,256> vr_owned{};
		std::atomic_uint64_t activity{};
		std::atomic_uint64_t mouse_polls{},mouse_moves{},mouse_suppressed{},key_events{};
		std::mutex pointer_mutex;
		native_ui_pointer pointer_source;

		void cl_char_event_stub(const int local_client_num, const int key)
		{
			if (!game_console::console_char_event(local_client_num, key))
			{
				return;
			}

			if (!gui::gui_char_event(local_client_num, key))
			{
				return;
			}

			cl_char_event_hook.invoke<void>(local_client_num, key);
		}

		void cl_key_event_stub(const int local_client_num, const int key, const int down)
		{
			++activity;
			if(vr::debug_options::enabled(vr::debug_options::probe::menu_input))++key_events;
			if(key>=0&&key<int(vr_owned.size()))vr_owned[key]=false;
			if (!game_console::console_key_event(local_client_num, key, down))
			{
				return;
			}

			if (!gui::gui_key_event(local_client_num, key, down))
			{
				return;
			}

			if(local_client_num==0&&down&&key>=game::K_MOUSE1&&key<=game::K_MWHEELUP)
			{
				// A stationary physical click/wheel still owns its desktop position,
				// not the last VR ray's position. Restore before delivering the edge.
				native_ui_pointer::position desktop;
				{const std::lock_guard lock(pointer_mutex);desktop=pointer_source.native_button();}
				if(desktop.restore)cl_mouse_move_hook.invoke<void>(local_client_num,desktop.x,desktop.y);
			}
			cl_key_event_hook.invoke<void>(local_client_num, key, down);
		}

		void lui_cod_key_event_stub(const int local_client_num, const int a2, const int key, const int down, void* a5, void* a6)
		{
			const auto state = *game::hks::lua_state;
			if (game::LUI_BeginCachedEvent(local_client_num, down ? 3 : 4, state))
			{
				const auto key_str = game::Key_KeynumToString(key, 0, 1);
				game::LUI_SetTableInt("keynum", key, state);
				game::LUI_SetTableString("key", key_str, state);
				game::LUI_SetTableString("name", down ? "keydown" : "keyup", state);
				game::LUI_EndEvent(state);
			}

			lui_cod_key_event_hook.invoke<void>(local_client_num, a2, key, down, a5, a6);
		}

		void cl_mouse_move_stub(const int local_client_num, int x, int y)
		{
			if(local_client_num==0)
			{
				const bool trace_input=vr::debug_options::enabled(vr::debug_options::probe::menu_input);
				if(trace_input)++mouse_polls;
				native_ui_pointer::decision decision;
				{
					const std::lock_guard lock(pointer_mutex);
					if(gui::captures_input())pointer_source.release();
					decision=pointer_source.native_position(x,y);
				}
				if(decision.activity){++activity;if(trace_input)++mouse_moves;}
				// Passive native polling must not snap an owned VR pointer back to
				// the stationary desktop cursor. Real movement immediately forwards.
				if(!decision.forward){if(trace_input)++mouse_suppressed;return;}
			}
			if (!gui::gui_mouse_event(local_client_num, x, y))
			{
				return;
			}

			cl_mouse_move_hook.invoke<void>(local_client_num, x, y);
		}
	}

	bool vr_ui_key(int key,bool down)
	{
		if(key<0||key>=int(vr_owned.size()))return false;
		if(down)
		{
			if(vr_owned[key])return true;
			// CL_KeyEvent owns this native key state. A held physical key cannot
			// be borrowed by VR, and its later release must remain physical-owned.
			if(*reinterpret_cast<const int*>(0x141e87684+key*12))return false;
			vr_owned[key]=true;cl_key_event_hook.invoke<void>(0,key,1);return true;
		}
		if(vr_owned[key]){vr_owned[key]=false;cl_key_event_hook.invoke<void>(0,key,0);}
		return true;
	}
	void vr_ui_pointer(int x,int y)
	{
		{const std::lock_guard lock(pointer_mutex);pointer_source.acquire();}
		cl_mouse_move_hook.invoke<void>(0,x,y);
	}
	void release_vr_ui_input()
	{
		{const std::lock_guard lock(pointer_mutex);pointer_source.release();}
		for(int key=0;key<int(vr_owned.size());++key)if(vr_owned[key])vr_ui_key(key,false);
	}
	std::uint64_t physical_activity() noexcept{return activity.load();}
	ui_activity ui_activity_counters() noexcept{return {mouse_polls.load(),mouse_moves.load(),mouse_suppressed.load(),key_events.load()};}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			static const char* lui_cached_events[5] = 
			{
				"process_events",
				"gamepad_button",
				"transition_complete",
				"keydown",
				"keyup",
			};

			utils::hook::inject(0x14031EB8B, lui_cached_events);

			cl_char_event_hook.create(0x1403D27B0, cl_char_event_stub);
			cl_key_event_hook.create(0x1403D2AE0, cl_key_event_stub);
			cl_mouse_move_hook.create(0x1403296F0, cl_mouse_move_stub);
			lui_cod_key_event_hook.create(0x140328F50, lui_cod_key_event_stub);
		}
	};
}

REGISTER_COMPONENT(input::component)
