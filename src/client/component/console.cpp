#include <std_include.hpp>
#include "console.hpp"
#include "console_format.hpp"
#include "console_output_queue.hpp"
#include "console_history.hpp"
#include "loader/component_loader.hpp"

#include "game/game.hpp"

#include "command.hpp"
#include "game_console.hpp"

#include <utils/thread.hpp>
#include <utils/hook.hpp>

#define OUTPUT_HANDLE GetStdHandle(STD_OUTPUT_HANDLE)

namespace game_console
{
	void print(int type, const std::string& data);
}

namespace console
{
	namespace
	{
		utils::hook::detour printf_hook;
		std::recursive_mutex print_mutex;
		detail::output_queue output_queue;
		detail::message_history diagnostic_messages;
		std::thread output_thread;
		HANDLE output_event{};
		std::atomic_bool output_stopping{};

		struct
		{
			std::atomic_bool kill;
			std::thread thread;
			HANDLE kill_event;
			char buffer[512]{};
			int cursor;
			std::deque<std::string> history;
			std::int32_t history_index = -1;
		} con{};

		void set_cursor_pos(int x)
		{
			CONSOLE_SCREEN_BUFFER_INFO info{};
			GetConsoleScreenBufferInfo(OUTPUT_HANDLE, &info);
			info.dwCursorPosition.X = static_cast<short>(x);
			SetConsoleCursorPosition(OUTPUT_HANDLE, info.dwCursorPosition);
		}

		void show_cursor(const bool show)
		{
			CONSOLE_CURSOR_INFO info{};
			GetConsoleCursorInfo(OUTPUT_HANDLE, &info);
			info.bVisible = show;
			SetConsoleCursorInfo(OUTPUT_HANDLE, &info);
		}

		template <typename... Args>
		int invoke_printf(const char* fmt, Args&&... args)
		{
			if (printf_hook.get_original() == nullptr)
			{
				return printf(fmt, std::forward<Args>(args)...);
			}

			return printf_hook.invoke<int>(fmt, std::forward<Args>(args)...);
		}

		std::string format(va_list* ap, const char* message)
		{
			return detail::format_message(message,*ap);
		}

		uint8_t get_attribute(const int type)
		{
			switch (type)
			{
			case con_type_info:
				return 7; // white
			case con_type_warning:
				return 6; // yellow
			case con_type_error:
				return 4; // red
			case con_type_debug:
				return 3; // cyan
			}

			return 7;
		}

		void update()
		{
			std::lock_guard _0(print_mutex);
			if(output_stopping.load())return;

			show_cursor(false);
			set_cursor_pos(0);
			invoke_printf("%s", con.buffer);
			set_cursor_pos(con.cursor);
			show_cursor(true);
		}

		void clear_output()
		{
			std::lock_guard _0(print_mutex);

			show_cursor(false);
			set_cursor_pos(0);

			for (auto i = 0; i < std::strlen(con.buffer); i++)
			{
				invoke_printf(" ");
			}

			set_cursor_pos(con.cursor);
			show_cursor(true);
		}

		int write_message(const int type, const std::string& message)
		{
			std::lock_guard _0(print_mutex);
			if(output_stopping.load())return 0;

			clear_output();
			set_cursor_pos(0);

			SetConsoleTextAttribute(OUTPUT_HANDLE, get_attribute(type));
			const auto res = invoke_printf("%s", message.data());
			SetConsoleTextAttribute(OUTPUT_HANDLE, get_attribute(con_type_info));

			game_console::print(type, message);

			if (message.size() <= 0 || message[message.size() - 1] != '\n')
			{
				invoke_printf("\n");
			}

			update();
			return res;
		}
		int dispatch_message(const int type, const std::string& message)
		{
			if(output_stopping.load())return 0;
			diagnostic_messages.push(type,message,GetTickCount64());
			output_queue.push(type,message);
			if(output_event)SetEvent(output_event);
			return static_cast<int>(std::min(message.size(),std::size_t(INT_MAX)));
		}
		void drain_output()
		{
			auto batch=output_queue.take();
			for(const auto& message:batch.messages)
			{
				if(output_stopping.load())return;
				write_message(message.type,message.text);
				if(message.repetitions>1)write_message(message.type,std::format("[console] previous message repeated {} times\n",message.repetitions-1));
			}
			if(batch.dropped)write_message(con_type_warning,std::format("[console] {} messages dropped while terminal output was blocked\n",batch.dropped));
		}

		void clear()
		{
			std::lock_guard _0(print_mutex);

			clear_output();
			strncpy_s(con.buffer, "", sizeof(con.buffer));

			con.cursor = 0;
			set_cursor_pos(0);
		}

		size_t get_max_input_length()
		{
			CONSOLE_SCREEN_BUFFER_INFO info{};
			GetConsoleScreenBufferInfo(OUTPUT_HANDLE, &info);
			const auto columns = static_cast<size_t>(info.srWindow.Right - info.srWindow.Left - 1);
			return std::max(size_t(0), std::min(columns, sizeof(con.buffer)));
		}

		void handle_resize()
		{
			clear();
			update();
		}

		void handle_input(const INPUT_RECORD record)
		{
			if (record.EventType == WINDOW_BUFFER_SIZE_EVENT)
			{
				handle_resize();
				return;
			}

			if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown)
			{
				return;
			}

			std::lock_guard _0(print_mutex);

			const auto key = record.Event.KeyEvent.wVirtualKeyCode;
			switch (key)
			{
			case VK_UP:
			{
				if (++con.history_index >= con.history.size())
				{
					con.history_index = static_cast<int>(con.history.size()) - 1;
				}

				clear();

				if (con.history_index != -1)
				{
					strncpy_s(con.buffer, con.history.at(con.history_index).data(), sizeof(con.buffer));
					con.cursor = static_cast<int>(strlen(con.buffer));
				}

				update();
				break;
			}
			case VK_DOWN:
			{
				if (--con.history_index < -1)
				{
					con.history_index = -1;
				}

				clear();

				if (con.history_index != -1)
				{
					strncpy_s(con.buffer, con.history.at(con.history_index).data(), sizeof(con.buffer));
					con.cursor = static_cast<int>(strlen(con.buffer));
				}

				update();
				break;
			}
			case VK_LEFT:
			{
				if (con.cursor > 0)
				{
					con.cursor--;
					set_cursor_pos(con.cursor);
				}

				break;
			}
			case VK_RIGHT:
			{
				if (con.cursor < std::strlen(con.buffer))
				{
					con.cursor++;
					set_cursor_pos(con.cursor);
				}

				break;
			}
			case VK_RETURN:
			{
				if (con.history_index != -1)
				{
					const auto itr = con.history.begin() + con.history_index;

					if (*itr == con.buffer)
					{
						con.history.erase(con.history.begin() + con.history_index);
					}
				}

				if (con.buffer[0])
				{
					con.history.push_front(con.buffer);
				}

				if (con.history.size() > 10)
				{
					con.history.erase(con.history.begin() + 10);
				}

				con.history_index = -1;

				game_console::add(con.buffer);

				con.cursor = 0;

				clear_output();
				strncpy_s(con.buffer, "", sizeof(con.buffer));
				break;
			}
			case VK_BACK:
			{
				if (con.cursor <= 0)
				{
					break;
				}

				clear_output();

				std::memmove(con.buffer + con.cursor - 1, con.buffer + con.cursor,
					strlen(con.buffer) + 1 - con.cursor);
				con.cursor--;

				update();
				break;
			}
			case VK_ESCAPE:
			{
				con.cursor = 0;
				clear_output();
				strncpy_s(con.buffer, "", sizeof(con.buffer));
				break;
			}
			default:
			{
				const auto c = record.Event.KeyEvent.uChar.AsciiChar;
				if (!c)
				{
					break;
				}

				if (std::strlen(con.buffer) + 1 >= get_max_input_length())
				{
					break;
				}

				std::memmove(con.buffer + con.cursor + 1,
					con.buffer + con.cursor, std::strlen(con.buffer) + 1 - con.cursor);
				con.buffer[con.cursor] = c;
				con.cursor++;

				update();
				break;
			}
			}
		}

		int __cdecl printf_stub(const char* fmt, ...)
		{
			va_list ap;
			va_start(ap, fmt);
			const auto result = format(&ap, fmt);
			va_end(ap);

			return dispatch_message(con_type_info, result);
		}

		BOOL WINAPI console_ctrl_handler(DWORD ctrl_type)
		{
			if (ctrl_type == CTRL_CLOSE_EVENT)
			{
				if (command::is_game_initialized())
				{
					command::execute("quit");
					while (!con.kill)
					{
						std::this_thread::sleep_for(10ms);
					}

					return TRUE;
				}
			}

			return FALSE;
		}
	}

	void print(const int type, const char* fmt, ...)
	{
		va_list ap;
		va_start(ap, fmt);
		const auto result = format(&ap, fmt);
		va_end(ap);

		dispatch_message(type, result);
	}

	void print_text(const int type, const std::string_view text)
	{
		for (const auto chunk : split_text_chunks(text))
		{
			print(type, "%.*s", static_cast<int>(chunk.size()), chunk.data());
		}
	}
	std::string diagnostic_history(const bool errors_only)
	{
		return diagnostic_messages.format(errors_only);
	}

	class component final : public component_interface
	{
	public:
		component()
		{
			ShowWindow(GetConsoleWindow(), SW_HIDE);
		}

		void post_start() override
		{
			output_event=CreateEvent(nullptr,FALSE,FALSE,nullptr);
			if(!output_event)throw std::runtime_error("Unable to create console output event");
			output_thread=utils::thread::create_named_thread("Console output",[]
			{
				while(!output_stopping.load())
				{
					if(WaitForSingleObject(output_event,INFINITE)!=WAIT_OBJECT_0)break;
					if(!output_stopping.load())drain_output();
				}
			});
			printf_hook.create(printf, printf_stub);
		}

		void post_unpack() override
		{
			ShowWindow(GetConsoleWindow(), SW_SHOW);
			SetConsoleTitle("Overlord");

#ifndef DEBUG
			SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
#endif

			con.kill_event = CreateEvent(NULL, TRUE, FALSE, NULL);

			con.thread = utils::thread::create_named_thread("Console", []()
			{
				const auto handle = GetStdHandle(STD_INPUT_HANDLE);
				HANDLE handles[2] = {handle, con.kill_event};
				MSG msg{};

				INPUT_RECORD record{};
				DWORD num_events{};

				while (!con.kill)
				{
					const auto result = MsgWaitForMultipleObjects(2, handles, FALSE, INFINITE, QS_ALLINPUT);
					if (con.kill)
					{
						return;
					}

					switch (result)
					{
					case WAIT_OBJECT_0:
					{
						if (!ReadConsoleInput(handle, &record, 1, &num_events) || num_events == 0)
						{
							break;
						}

						handle_input(record);
						break;
					}
					case WAIT_OBJECT_0 + 1:
					{
						if (!PeekMessageA(&msg, GetConsoleWindow(), NULL, NULL, PM_REMOVE))
						{
							break;
						}

						if (msg.message == WM_QUIT)
						{
							command::execute("quit", false);
							break;
						}

						TranslateMessage(&msg);
						DispatchMessage(&msg);
						break;
					}
					}
				}
			});
		}

		void pre_destroy() override
		{
			output_stopping=true;
			con.kill = true;
			SetEvent(con.kill_event);
			SetEvent(output_event);
			// Terminal I/O can be blocked by selection or a stalled pipe. Cancel
			// the two I/O owners before joining; no producer waits on either owner.
			bool input_done=!con.thread.joinable(),output_done=!output_thread.joinable();
			while(!input_done || !output_done)
			{
				// Cancellation is edge-triggered. Retry until each owner exits so an
				// I/O call starting just after the first cancellation cannot hang exit.
				if(!input_done){CancelSynchronousIo(con.thread.native_handle());input_done=WaitForSingleObject(con.thread.native_handle(),10)==WAIT_OBJECT_0;}
				if(!output_done){CancelSynchronousIo(output_thread.native_handle());output_done=WaitForSingleObject(output_thread.native_handle(),10)==WAIT_OBJECT_0;}
			}

			if (con.thread.joinable())
			{
				con.thread.join();
			}
			if(output_thread.joinable())output_thread.join();
			if(output_event){CloseHandle(output_event);output_event=nullptr;}
			if(con.kill_event){CloseHandle(con.kill_event);con.kill_event=nullptr;}
		}
	};
}

REGISTER_COMPONENT(console::component)
