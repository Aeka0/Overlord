#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"
#include "game/dvars.hpp"

#include "component/scheduler.hpp"
#include "component/console.hpp"
#include "gui.hpp"
#include "input_capture.hpp"

#include <utils/string.hpp>
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>
#include <utils/concurrency.hpp>
#include <utils/properties.hpp>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace gui
{
	std::unordered_map<std::string, bool> enabled_menus;

	namespace
	{
		struct frame_callback
		{
			std::function<void()> callback;
			bool always;
		};

		struct event
		{
			HWND hWnd;
			UINT msg;
			WPARAM wParam;
			LPARAM lParam;
		};

		struct menu_t
		{
			std::string name;
			std::string title;
			std::function<void()> render;
		};

		utils::concurrency::container<std::vector<frame_callback>> on_frame_callbacks;
		utils::concurrency::container<std::deque<notification_t>> notifications;
		utils::concurrency::container<std::vector<event>> event_queue;
		std::vector<menu_t> menus;

		struct
		{
			ID3D11Device* device;
			ID3D11DeviceContext* device_context;
			bool initialized = false;
			input_capture capture;
		} globals;

		utils::hook::detour add_catcher_hook, remove_catcher_hook, set_catcher_hook;

		void add_catcher_stub(const int client, const int flags)
		{
			add_catcher_hook.invoke<void>(client, flags);
			if (client == 0 && (flags & 0x10)) globals.capture.native_catcher_write();
		}

		void remove_catcher_stub(const int client, const int keep_mask)
		{
			remove_catcher_hook.invoke<void>(client, keep_mask);
			if (client == 0 && !(keep_mask & 0x10)) globals.capture.native_catcher_write();
		}

		void set_catcher_stub(const int client, const int flags)
		{
			set_catcher_hook.invoke<void>(client, flags);
			if (client == 0) globals.capture.native_catcher_write();
		}

		void install_catcher_observers()
		{
			// H2 native setters verified from the live ESC/UI_SetActiveMenu path:
			// OR flags, AND keep-mask, and replace (preserving the console bit).
			// Observe native intent; never rewrite it or infer ownership from pause.
			constexpr std::array<std::uintptr_t, 3> addresses{0x1403D2FC0, 0x1403D3410, 0x1403D3480};
			constexpr std::array<std::uint8_t, 21> add_bytes{
				0x48,0x63,0xc1,0x48,0x69,0xc8,0x14,0x02,0,0,0x48,0x8d,0x05,0xef,0xc3,0xc6,0x01,0x09,0x14,0x01,0xc3};
			constexpr std::array<std::uint8_t, 21> remove_bytes{
				0x48,0x63,0xc1,0x48,0x69,0xc8,0x14,0x02,0,0,0x48,0x8d,0x05,0x9f,0xbf,0xc6,0x01,0x21,0x14,0x01,0xc3};
			constexpr std::array<std::uint8_t, 30> set_bytes{
				0x48,0x63,0xc1,0x48,0x69,0xc8,0x14,0x02,0,0,0x48,0x8d,0x05,0x2f,0xbf,0xc6,0x01,
				0xf6,0x04,0x01,0x01,0x74,0x03,0x83,0xca,0x01,0x89,0x14,0x01,0xc3};
			const auto matches = [](const std::uintptr_t address, const auto& bytes)
			{
				std::array<std::uint8_t, 30> mask; mask.fill(0xff);
				return static_cast<bool>(utils::hook_validation::verify_masked_bytes(
					reinterpret_cast<const void*>(address), {bytes.data(), mask.data(), bytes.size()}));
			};
			if (!matches(addresses[0], add_bytes) || !matches(addresses[1], remove_bytes) || !matches(addresses[2], set_bytes))
				throw std::runtime_error("GUI input catcher native contract mismatch");
			add_catcher_hook.create(addresses[0], add_catcher_stub);
			remove_catcher_hook.create(addresses[1], remove_catcher_stub);
			set_catcher_hook.create(addresses[2], set_catcher_stub);
		}

		void set_cfg_path()
		{
			const auto path = utils::properties::get_appdata_path() / "imgui.ini";
			const auto path_str = utils::memory::duplicate_string(path.generic_string());

			auto& io = ImGui::GetIO();
			io.IniFilename = path_str;
		}

		void initialize_gui_context()
		{
			ImGui::CreateContext();
			ImGui::StyleColorsDark();

			ImGui_ImplWin32_Init(*game::hWnd);
			ImGui_ImplDX11_Init(globals.device, globals.device_context);

			globals.initialized = true;

			set_cfg_path();
		}

		void run_event_queue()
		{
			event_queue.access([](std::vector<event>& queue)
			{
				for (const auto& event : queue)
				{
					ImGui_ImplWin32_WndProcHandler(event.hWnd, event.msg, event.wParam, event.lParam);
				}

				queue.clear();
			});
		}

		std::vector<int> imgui_colors =
		{
			ImGuiCol_FrameBg,
			ImGuiCol_FrameBgHovered,
			ImGuiCol_FrameBgActive,
			ImGuiCol_TitleBgActive,
			ImGuiCol_ScrollbarGrabActive,
			ImGuiCol_CheckMark,
			ImGuiCol_SliderGrab,
			ImGuiCol_SliderGrabActive,
			ImGuiCol_Button,
			ImGuiCol_ButtonHovered,
			ImGuiCol_ButtonActive,
			ImGuiCol_Header,
			ImGuiCol_HeaderHovered,
			ImGuiCol_HeaderActive,
			ImGuiCol_SeparatorHovered,
			ImGuiCol_SeparatorActive,
			ImGuiCol_ResizeGrip,
			ImGuiCol_ResizeGripHovered,
			ImGuiCol_ResizeGripActive,
			ImGuiCol_TextSelectedBg,
			ImGuiCol_NavHighlight,
		};

		void update_colors()
		{
			auto& style = ImGui::GetStyle();
			const auto colors = style.Colors;

			const auto now = std::chrono::system_clock::now();
			const auto days = std::chrono::floor<std::chrono::days>(now);
			std::chrono::year_month_day y_m_d{days};

			if (y_m_d.month() != std::chrono::month(6))
			{
				return;
			}

			for (const auto& id : imgui_colors)
			{
				const auto color = colors[id];

				ImVec4 hsv_color =
				{
					static_cast<float>((game::Sys_Milliseconds() / 100) % 256) / 255.f,
					1.f, 1.f, 1.f,
				};

				ImVec4 rgba_color{};
				ImGui::ColorConvertHSVtoRGB(hsv_color.x, hsv_color.y, hsv_color.z, rgba_color.x, rgba_color.y, rgba_color.z);

				rgba_color.w = color.w;
				colors[id] = rgba_color;
			}
		}

		void new_gui_frame()
		{
			ImGui::GetIO().MouseDrawCursor = globals.capture.is_open();

			update_colors();

			ImGui_ImplDX11_NewFrame();
			ImGui_ImplWin32_NewFrame();
			run_event_queue();

			ImGui::NewFrame();
		}

		void end_gui_frame()
		{
			ImGui::EndFrame();
			ImGui::Render();
			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		}

		void toggle_menu(const std::string& name)
		{
			enabled_menus[name] = !enabled_menus[name];
		}

		void show_notifications()
		{
			static const auto window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | 
											 ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | 
											 ImGuiWindowFlags_NoMove;

			notifications.access([](std::deque<notification_t>& notifications_)
			{
				auto index = 0;
				for (auto i = notifications_.begin(); i != notifications_.end();)
				{
					const auto now = std::chrono::high_resolution_clock::now();
					if (now - i->creation_time >= i->duration)
					{
						i = notifications_.erase(i);
						continue;
					}

					const auto title = utils::string::truncate(i->title, 34, "...");
					const auto text = utils::string::truncate(i->text, 34, "...");

					ImGui::SetNextWindowSizeConstraints(ImVec2(250, 50), ImVec2(250, 50));
					ImGui::SetNextWindowBgAlpha(0.6f);
					ImGui::Begin(utils::string::va("Notification #%i", index), nullptr, window_flags);

					ImGui::SetWindowPos(ImVec2(10, 30.f + static_cast<float>(index) * 60.f));
					ImGui::SetWindowSize(ImVec2(250, 0));
					ImGui::Text(title.data());
					ImGui::Text(text.data());

					ImGui::End();

					++i;
					++index;
				}
			});
		}

		void menu_checkbox(const std::string& name, const std::string& menu)
		{
			ImGui::Checkbox(name.data(), &enabled_menus[menu]);
		}

		void run_frame_callbacks()
		{
			on_frame_callbacks.access([](std::vector<frame_callback>& callbacks)
			{
				for (const auto& callback : callbacks)
				{
					if (callback.always || globals.capture.is_open())
					{
						callback.callback();
					}
				}
			});
		}

		void draw_main_menu_bar()
		{
			if (ImGui::BeginMainMenuBar())
			{
				if (ImGui::BeginMenu("Windows"))
				{
					for (const auto& menu : menus)
					{
						menu_checkbox(menu.title, menu.name);
					}

					ImGui::EndMenu();
				}

				ImGui::EndMainMenuBar();
			}
		}

		void gui_on_frame()
		{
			if (!game::Sys_IsDatabaseReady2())
			{
				return;
			}

			if (!globals.initialized)
			{
				console::info("[ImGui] Initializing\n");
				initialize_gui_context();
			}
			else
			{
				new_gui_frame();
				run_frame_callbacks();
				end_gui_frame();
			}
		}

		utils::hook::detour wnd_proc_hook;
		LRESULT wnd_proc_stub(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
		{
			if (wParam != VK_ESCAPE && globals.capture.is_open())
			{
				event_queue.access([hWnd, msg, wParam, lParam](std::vector<event>& queue)
				{
					queue.emplace_back(hWnd, msg, wParam, lParam);
				});
			}

			return wnd_proc_hook.invoke<LRESULT>(hWnd, msg, wParam, lParam);
		}
	}

	bool gui_key_event(const int local_client_num, const int key, const int down)
	{
		if (key == game::K_F10 && down)
		{
			globals.capture.set_open(*game::keyCatchers, !globals.capture.is_open());
			return false;
		}

		if (key == game::K_ESCAPE && down && globals.capture.is_open())
		{
			globals.capture.set_open(*game::keyCatchers, false);
			return false;
		}

		return !globals.capture.is_open();
	}

	bool gui_char_event(const int local_client_num, const int key)
	{
		return !globals.capture.is_open();
	}

	bool gui_mouse_event(const int local_client_num, int x, int y)
	{
		return !globals.capture.is_open();
	}

	void on_frame(const std::function<void()>& callback, bool always)
	{
		on_frame_callbacks.access([always, callback](std::vector<frame_callback>& callbacks)
		{
			callbacks.emplace_back(callback, always);
		});
	}

	bool captures_input() noexcept {return globals.capture.is_open();}
	bool is_menu_open(const std::string& name)
	{
		return enabled_menus[name];
	}

	void notification(const std::string& title, const std::string& text, const std::chrono::milliseconds duration)
	{
		notification_t notification{};
		notification.title = title;
		notification.text = text;
		notification.duration = duration;
		notification.creation_time = std::chrono::high_resolution_clock::now();

		notifications.access([notification](std::deque<notification_t>& notifications_)
		{
			notifications_.push_front(notification);
		});
	}

	void copy_to_clipboard(const std::string& text)
	{
		utils::string::set_clipboard_data(text);
		gui::notification("Text copied to clipboard", utils::string::va("\"%s\"", text.data()));
	}

	void register_menu(const std::string& name, const std::string& title,
		const std::function<void()>& callback, bool always)
	{
		menus.emplace_back(name, title, callback);
		enabled_menus[name] = false;

		on_frame([=]
		{
			if (enabled_menus.at(name))
			{
				callback();
			}
		}, always);
	}

	void register_callback(const std::function<void()>& callback, bool always)
	{
		on_frame([=]
		{
			callback();
		}, always);
	}

	void shutdown_gui()
	{
		if (globals.initialized)
		{
			ImGui_ImplDX11_Shutdown();
			ImGui_ImplWin32_Shutdown();
			ImGui::DestroyContext();
		}

		globals.initialized = false;
		if (globals.capture.is_open()) globals.capture.set_open(*game::keyCatchers, false);
		globals.device = nullptr;
		globals.device_context = nullptr;
	}

	void set_device(ID3D11Device* device, ID3D11DeviceContext* context)
	{
		globals.device = device;
		globals.device_context = context;
	}

	template<typename T>
	bool checkbox_flags_t(const char* label, T* flags, T flags_value)
	{
		bool all_on = (*flags & flags_value) == flags_value;
		bool any_on = (*flags & flags_value) != 0;
		bool pressed;
		if (!all_on && any_on)
		{
			ImGuiContext& g = *GImGui;
			ImGuiItemFlags backup_item_flags = g.CurrentItemFlags;
			g.CurrentItemFlags |= ImGuiItemFlags_MixedValue;
			pressed = ImGui::Checkbox(label, &all_on);
			g.CurrentItemFlags = backup_item_flags;
		}
		else
		{
			pressed = ImGui::Checkbox(label, &all_on);

		}
		if (pressed)
		{
			if (all_on)
			{
				*flags |= flags_value;
			}
			else
			{
				*flags &= ~flags_value;
			}
		}
		return pressed;
	}

	void input_flags8(std::uint8_t* flags, const std::vector<const char*>& flag_names)
	{
		for (auto i = 0u; i < flag_names.size(); i++)
		{
			if (flag_names[i] != nullptr && flag_names[i][0] != 0)
			{
				checkbox_flags_t<std::uint8_t>(flag_names[i], flags, static_cast<std::uint8_t>(1 << i));
			}
		}
	}

	void input_flags(std::uint32_t* flags, const std::vector<const char*>& flag_names)
	{
		for (auto i = 0u; i < flag_names.size(); i++)
		{
			if (flag_names[i] != nullptr && flag_names[i][0] != 0)
			{
				ImGui::CheckboxFlags(flag_names[i], flags, (1 << i));
			}
		}
	}

	void input_flags(int* flags, const std::vector<const char*>& flag_names)
	{
		input_flags(reinterpret_cast<std::uint32_t*>(flags), flag_names);
	}

	bool input_u8(const char* label, unsigned char* v, int step, int step_fast, ImGuiInputTextFlags flags)
	{
		const char* format = (flags & ImGuiInputTextFlags_CharsHexadecimal) ? "%08X" : "%d";
		return ImGui::InputScalar(label, ImGuiDataType_U8, (void*)v, (void*)(step > 0 ? &step : NULL), (void*)(step_fast > 0 ? &step_fast : NULL), format, flags);
	}

	bool input_u16(const char* label, unsigned short* v, int step, int step_fast, ImGuiInputTextFlags flags)
	{
		const char* format = (flags & ImGuiInputTextFlags_CharsHexadecimal) ? "%08X" : "%d";
		return ImGui::InputScalar(label, ImGuiDataType_U16, (void*)v, (void*)(step > 0 ? &step : NULL), (void*)(step_fast > 0 ? &step_fast : NULL), format, flags);
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			install_catcher_observers();
			utils::hook::nop(0x1407A14BB, 9);
			utils::hook::call(0x1407A14BE, gui_on_frame);
			wnd_proc_hook.create(0x140650F10, wnd_proc_stub);

			on_frame([]()
			{
				show_notifications();
				draw_main_menu_bar();
			});
		}

		void pre_destroy() override
		{
			shutdown_gui();
		}
	};
}

REGISTER_COMPONENT(gui::component)
