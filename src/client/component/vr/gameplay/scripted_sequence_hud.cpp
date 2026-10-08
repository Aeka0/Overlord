#include <std_include.hpp>
#include "campaign/scripted_sequences.hpp"
#include "../hud_prompts.hpp"
#include "../native_waypoints.hpp"
#include "../engine_stereo_bridge.hpp"
#include "component/scheduler.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"

namespace vr::gameplay::sequences
{
	namespace
	{
		std::atomic_uint64_t attempts{},recorded{};
		std::atomic<const char*> reason{"no active instruction"};
		void draw()
		{
			if (!engine_stereo_bridge::is_active() || !game::CL_IsCgameInitialized() || *game::keyCatchers) return;
			const auto* paused=game::Dvar_FindVar("cl_paused");
			if (!paused || paused->current.integer) return;
			const auto state=for_player(game::CG_GetPredictedPlayerState(0));
			if (!state.epoch) return;
			const auto key=state.instruction;
			if (!key) return;
			++attempts;reason="instruction preparation rejected";
			const auto language=game_text::current();
			// If this locale lacks the complete VR instruction, retain the native
			// script hint and its original reminder timing on the narrative canvas.
			const auto message=hud_prompts::compose_current(*key,language);if(!message)return;
			const auto text=hud_prompts::text(*message,hud_prompts::style::native_colors);
			if (text.empty()) return;
			const auto* placement=game::ScrPlace_GetViewPlacement();if(!placement)return;
			const float w=placement->realViewportSize[0],h=placement->realViewportSize[1];
			if(!std::isfinite(w)||!std::isfinite(h)||w<16||h<16||w>8192||h>8192)return;
			auto* font=game::R_RegisterFont("fonts/defaultBold.otf",std::clamp(int(h*.025f),18,72));
			if(!font)return;
			const float width=float(game::R_TextWidth(text.c_str(),int(text.size()),font));
			if(!std::isfinite(width)||width<=0)return;
			const float scale=std::min(1.f,w*.72f/width);
			// Draw once on the native frontend, in a named command range. The
			// existing narrative target handles stereo composition and expiry.
			const bool accepted=native_waypoints::draw_narrative_text([&] {
				float white[]{1,1,1,1};
				game::R_AddCmdDrawText(text.c_str(),int(text.size()),font,(w-width*scale)*.5f,h*.69f,
					scale,scale,0,white,4);
			});
			if(accepted){++recorded;reason="native instruction range recorded";}
			else reason="native instruction range rejected";
		}
	}
	std::string prompt_status()
	{return std::format("prompt_attempts={} prompt_recorded={} last_prompt_result={}\n",attempts.load(),recorded.load(),reason.load());}
	class hud_component final:public component_interface
	{
		void post_unpack() override {scheduler::loop(draw,scheduler::pipeline::lui);}
	};
}
REGISTER_COMPONENT(vr::gameplay::sequences::hud_component)
