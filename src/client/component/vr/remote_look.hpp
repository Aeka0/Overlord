#pragma once
#include "controller_input.hpp"

namespace vr::controller_input
{
	struct remote_stick {float pitch{},yaw{},seconds{};};
	class remote_look
	{
		std::uint64_t epoch_{},reference_{},continuity_{},sequence_{};
		clock::time_point at_{};
		bool armed_{},wire_{};
	public:
		void reset()noexcept{*this={};}
		remote_stick consume(const frame& input,std::uint64_t epoch,bool wire,bool enabled,float deadzone,clock::time_point now)noexcept
		{
			if(!enabled || !epoch || !input.focused || !input.sequence || !input.reference_generation || !input.turn_active || input.orientation_settling ||
				now<input.sampled_at || now-input.sampled_at>std::chrono::milliseconds(150) ||
				!std::isfinite(deadzone) || deadzone<.05f || deadzone>.5f ||
				!std::isfinite(input.turn[0]) || !std::isfinite(input.turn[1]) || std::abs(input.turn[0])>1.01f || std::abs(input.turn[1])>1.01f)
			{reset();return {};}
			if(epoch_!=epoch || reference_!=input.reference_generation || continuity_!=input.continuity_generation || wire_!=wire ||
				input.sequence<sequence_ || now<at_ || now-at_>std::chrono::milliseconds(150))reset();
			const float seconds=epoch_?std::clamp(std::chrono::duration<float>(now-at_).count(),0.f,.05f):0.f;
			epoch_=epoch;reference_=input.reference_generation;continuity_=input.continuity_generation;sequence_=input.sequence;at_=now;wire_=wire;
			const float magnitude=std::hypot(input.turn[0],input.turn[1]);
			if(!armed_){armed_=magnitude<=deadzone;return {};}
			if(magnitude<=deadzone)return {0,0,seconds};
			const float scale=(std::min(magnitude,1.f)-deadzone)/(1.f-deadzone)/magnitude;
			// Logical positive values mean up/right. Each native input adapter
			// owns its wire/view-angle signs. Remote aim turns continuously.
			return {input.turn[1]*scale,input.turn[0]*scale,seconds};
		}
	};
}
