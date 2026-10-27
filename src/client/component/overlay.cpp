#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"
#include "game/dvars.hpp"

#include "scheduler.hpp"
#include "command.hpp"

#include <utils/string.hpp>
#include <utils/hook.hpp>

#define fps_font game::R_RegisterFont("fonts/fira_mono_regular.ttf", 25)
#define material_white game::Material_RegisterHandle("white")

namespace overlay
{
	namespace
	{
		game::dvar_t* cg_draw_fps = nullptr;

		game::dvar_t* cg_draw_game_time = nullptr;
		game::dvar_t* cg_game_time_font_color = nullptr;
		game::dvar_t* cg_game_time_background_color = nullptr;
		game::dvar_t* cg_game_time_x = nullptr;
		game::dvar_t* cg_game_time_y = nullptr;
		game::dvar_t* cg_game_time_width = nullptr;

		game::dvar_t* fx_profile = nullptr;

		float fps_color_good[4] = {0.6f, 1.0f, 0.0f, 1.0f};
		float fps_color_ok[4] = {1.0f, 0.7f, 0.3f, 1.0f};
		float fps_color_bad[4] = {1.0f, 0.3f, 0.3f, 1.0f};

		float screen_max[2];

		utils::hook::detour sub_1407C55D0_hook;

		struct cg_perf_data
		{
			std::chrono::time_point<std::chrono::steady_clock> perf_start;
			std::int32_t current_ms;
			std::int32_t previous_ms;
			std::int32_t frame_ms;
			std::int32_t history[32];
			std::int32_t count;
			std::int32_t index;
			std::int32_t instant;
			std::int32_t total;
			float average;
			float variance;
			std::int32_t min;
			std::int32_t max;
		};

		cg_perf_data cg_perf{};

		void perf_calc_fps(cg_perf_data* data, const std::int32_t value)
		{
			data->history[data->index % 32] = value;
			data->instant = value;
			data->min = 0x7FFFFFFF;
			data->max = 0;
			data->average = 0.0f;
			data->variance = 0.0f;
			data->total = 0;

			for (auto i = 0; i < data->count; ++i)
			{
				const std::int32_t idx = (data->index - i) % 32;

				if (idx < 0)
				{
					break;
				}

				data->total += data->history[idx];

				if (data->min > data->history[idx])
				{
					data->min = data->history[idx];
				}

				if (data->max < data->history[idx])
				{
					data->max = data->history[idx];
				}
			}

			data->average = static_cast<float>(data->total) / static_cast<float>(data->count);
			++data->index;
		}

		void perf_update()
		{
			cg_perf.count = 32;

			cg_perf.current_ms = static_cast<std::int32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::high_resolution_clock::now() - cg_perf.perf_start).count());
			cg_perf.frame_ms = cg_perf.current_ms - cg_perf.previous_ms;
			cg_perf.previous_ms = cg_perf.current_ms;

			perf_calc_fps(&cg_perf, cg_perf.frame_ms);

			sub_1407C55D0_hook.invoke<void>();
		}

		void check_resize()
		{
			screen_max[0] = game::ScrPlace_GetViewPlacement()->realViewportSize[0];
			screen_max[1] = game::ScrPlace_GetViewPlacement()->realViewportSize[1];
		}
		
		float relative(int x)
		{
			return (static_cast<float>(x) / 1000) * screen_max[0];
		}

		void draw_box(const float x, const float y, const float w, const float h, float* color)
		{
			game::vec4_t dark_color{};

			dark_color[0] = color[0] * 0.5f;
			dark_color[1] = color[1] * 0.5f;
			dark_color[2] = color[2] * 0.5f;
			dark_color[3] = color[3];

			game::R_AddCmdDrawStretchPic(x, y, w, h, 0.0f, 0.0f, 0.0f, 0.0f, color, material_white);
			game::R_AddCmdDrawStretchPic(x, y, 2.0f, h, 0.0f, 0.0f, 0.0f, 0.0f, dark_color, material_white);
			game::R_AddCmdDrawStretchPic((x + w) - 2.0f, y, 2.0f, h, 0.0f, 0.0f, 0.0f, 0.0f, dark_color,
				material_white);
			game::R_AddCmdDrawStretchPic(x, y, w, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, dark_color, material_white);
			game::R_AddCmdDrawStretchPic(x, (y + h) - 2.0f, w, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, dark_color,
				material_white);
		}

		void draw_fps()
		{
			if (cg_draw_fps->current.integer < 1)
			{
				return;
			}

			const auto fps = static_cast<std::int32_t>(static_cast<float>(1000.0f / static_cast<float>(cg_perf.
				average))
				+ 9.313225746154785e-10);

			const auto font = fps_font;
			const auto fps_string = utils::string::va("%i", fps);
			const auto x = screen_max[0] - 15.f - game::R_TextWidth(fps_string, 0x7FFFFFFF, font);
			const auto color = fps >= 60 ? fps_color_good : (fps >= 30 ? fps_color_ok : fps_color_bad);

			game::R_AddCmdDrawText(fps_string, 0x7FFFFFFF, font, x, 35.f, 1.0f, 1.0f, 0.0f, color, 1);
		}

		void draw_pos()
		{
			if (cg_draw_fps->current.integer < 2)
			{
				return;
			}

			const auto font = fps_font;
			const auto pos_string = utils::string::va("%f %f %f",
				game::refdef->org[0],
				game::refdef->org[1],
				game::refdef->org[2]);

			const auto x = screen_max[0] - 15.f - game::R_TextWidth(pos_string, 0x7FFFFFFF, font);

			game::R_AddCmdDrawText(pos_string, 0x7FFFFFFF, font, x,
				60.f, 1.0f, 1.0f, 0.0f, fps_color_ok, 0);
		}

		void draw_game_time()
		{
			if (!cg_draw_game_time->current.enabled)
			{
				return;
			}

			auto msec = *game::gameTime;

			// remove first frames
			if (msec >= 300)
			{
				msec -= 300;
			}

			const auto ms = static_cast<int>((msec % 1000) / 10);
			const auto s = static_cast<int>(msec / 1000) % 60;
			const auto m = static_cast<int>(msec / 1000 / 60);
			const auto text = utils::string::va("%d:%02d.%02d", m, s, ms);

			const auto height = relative(24);
			const auto base_width = relative(cg_game_time_width->current.integer);

			const auto base_x = relative(cg_game_time_x->current.integer);
			const auto base_y = screen_max[1] - relative(cg_game_time_y->current.integer) - height;

			const auto font_height = relative(20);
			const auto font = game::R_RegisterFont("fonts/default.otf", static_cast<int>(font_height));
			const auto text_width = game::R_TextWidth(text, 0x7FFFFFFF, font);

			draw_box(base_x, base_y, base_width + 5.f,
				height, cg_game_time_background_color->current.vector);

			const auto text_y = base_y + (height / 2.f) + ((font_height + relative(5)) / 2.f);

			game::R_AddCmdDrawText(text, 0x7FFFFFFF, font, base_x + base_width - text_width,
				text_y, 1.0f, 1.0f, 0.0f, cg_game_time_font_color->current.vector, 0);
		}

		void draw_fx_profile()
		{
			if (!fx_profile->current.enabled)
			{
				return;
			}

			const auto system = game::FX_GetSystem(0);
			static float color[4]{};
			color[0] = 0.f;
			color[1] = 1.f;
			color[2] = 1.f;
			color[3] = 1.f;

			auto line = 0;
			const auto draw_line = [&](const char* name, int count, int max)
			{
				const auto font = fps_font;
				const auto text = utils::string::va("%5i of %5i %s", count, max, name);
				const auto x = screen_max[0] - 500.f;
				game::R_AddCmdDrawText(text, 0x7FFFFFFF, font, x, 85.f + 25.f * line++, 1.0f, 1.0f, 0.0f, color, 0);
			};

			const auto get_count = [&](void* elems, int first_free)
			{
				const auto free_elem = reinterpret_cast<size_t>(elems) + first_free * 32;
				return static_cast<int>((free_elem - reinterpret_cast<size_t>(elems)) >> 5);
			};

			draw_line("effect objects", system->firstFreeEffect - system->firstActiveEffect, 2048);
			draw_line("bolts", get_count(system->bolts, system->firstFreeBolt), 512);
			draw_line("trails", get_count(system->trails, system->firstFreeTrail), 256);
		}

		void draw()
		{
			if (*dvars::cg_draw_2d && !(*dvars::cg_draw_2d)->current.enabled)
			{
				return;
			}

			check_resize();
			draw_fps();

			if (!game::CL_IsCgameInitialized() || game::g_entities[0].client == nullptr)
			{
				return;
			}

			draw_pos();
			draw_game_time();
			draw_fx_profile();
		}
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			scheduler::loop(draw, scheduler::pipeline::renderer);

			sub_1407C55D0_hook.create(0x1407C55D0, perf_update);

			cg_draw_fps = dvars::register_int("cg_drawFPS", 0, 0, 4, game::DVAR_FLAG_SAVED, "Draw fps");

			cg_draw_game_time = dvars::register_bool("cg_drawGameTime", false, game::DVAR_FLAG_SAVED, "Draw game time");
			cg_game_time_font_color = dvars::register_vec4("cg_gameTimeFontColor",
				1.f, 1.f, 1.f, 1.f, 0.f, 1.f, game::DVAR_FLAG_SAVED, "Game time font color");
			cg_game_time_background_color = dvars::register_vec4("cg_gameTimeBackgroundColor",
				0.f, 0.f, 0.f, 0.8f, 0.f, 1.f, game::DVAR_FLAG_SAVED, "Game time background color");
			cg_game_time_x = dvars::register_int("cg_gameTimeX", 15, 0, 1000, game::DVAR_FLAG_SAVED, "Game time x position");
			cg_game_time_y = dvars::register_int("cg_gameTimeY", 15, 0, 1000, game::DVAR_FLAG_SAVED, "Game time y position");
			cg_game_time_width = dvars::register_int("cg_gameTimeWidth", 200, 0, 1000, game::DVAR_FLAG_SAVED, "Game time width");

			fx_profile = dvars::register_bool("fx_profile", false, game::DVAR_FLAG_NONE, "Turn on FX profiling");
		}
	};
}

REGISTER_COMPONENT(overlay::component)
