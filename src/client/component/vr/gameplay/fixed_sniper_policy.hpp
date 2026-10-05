#pragma once
#include "component/vr/digital_button_gate.hpp"
#include "mounted_turret_policy.hpp"
#include <string_view>

namespace vr::gameplay::fixed_sniper
{
	inline constexpr unsigned aim_hand=0,zoom_hand=1;
	inline constexpr float aim_speed_scale=.1f,zoom_input_scale=.25f;
	inline bool accepts(unsigned flags, int entity, std::string_view weapon) noexcept
	{return mounted::attached(flags) && entity>0 && entity<4000 && weapon=="m82_bipod_stand_thermal";}
	inline bool overlay_material(std::string_view name) noexcept
	{
		return name=="h1_hud_overlay_sniperescape_scope" || name=="h2_hud_overlay_sniper_thermal_reticle" ||
			name=="h1_hud_overlay_sniperescape_lensshadow" || name=="h1_hud_overlay_sniperescape_flash";
	}
	struct controls {float zoom{},pitch{},yaw{};bool fire{},exit{};};
	class controller
	{
		std::uint64_t epoch_{},reference_{},continuity_{},sequence_{};
		controller_input::clock::time_point last_{};
		std::array<bool,2> armed_{};
		std::array<controller_input::digital_button_gate,2> fire_{};
		std::array<controller_input::digital_press_gate,2> exit_{};
	public:
		void reset() noexcept {*this={};}
		controls consume(const controller_input::frame& input,std::uint64_t epoch,bool gameplay,
			float deadzone,float speed,controller_input::clock::time_point now) noexcept
		{
			if(!gameplay || !epoch || !input.sequence || !input.focused || input.orientation_settling ||
				now<input.sampled_at || now-input.sampled_at>std::chrono::milliseconds(150) ||
				!std::isfinite(deadzone) || deadzone<.05f || deadzone>.5f || !std::isfinite(speed) || speed<1 || speed>360)
			{reset();return {};}
			if(epoch_!=epoch || reference_!=input.reference_generation || continuity_!=input.continuity_generation ||
				input.sequence<sequence_ || now<last_ || now-last_>std::chrono::milliseconds(150))reset();
			const float dt=epoch_?std::clamp(std::chrono::duration<float>(now-last_).count(),0.f,.05f):0.f;
			epoch_=epoch;reference_=input.reference_generation;continuity_=input.continuity_generation;sequence_=input.sequence;last_=now;
			controls result;
			const auto axis=[&](float x){const float a=std::abs(x);return a>deadzone?std::copysign((std::min(a,1.f)-deadzone)/(1-deadzone),x):0.f;};
			for(unsigned h=0;h<2;++h)
			{
				const auto stick=h?input.turn:input.move;
				if(!(h?input.turn_active:input.move_active) || !input.grip[h].valid ||
					!std::isfinite(stick[0]) || !std::isfinite(stick[1]) || std::abs(stick[0])>1.01f || std::abs(stick[1])>1.01f)
					armed_[h]=false;
				else if(!armed_[h])armed_[h]=std::hypot(stick[0],stick[1])<=deadzone;
				else if(h==aim_hand)
				{result.pitch=-axis(stick[1])*speed*aim_speed_scale*dt;result.yaw=-axis(stick[0])*speed*aim_speed_scale*dt;}
				else if(h==zoom_hand)result.zoom=axis(stick[1])*zoom_input_scale; // Native forward/back zoom; no strafe.
				if(input.grip[h].valid && input.aim[h].valid)result.fire|=fire_[h].consume(input.trigger[h]);
				else fire_[h]={};
				if(input.grip[h].valid)result.exit|=exit_[h].consume(input.secondary[h]);else exit_[h]={};
			}
			return result;
		}
	};
}
