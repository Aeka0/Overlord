#pragma once
#include "component/vr/digital_button_gate.hpp"

namespace vr::gameplay::sentry
{
	// Carrying disables ordinary weapon input. This gate supplies only the
	// native placement thread's attack button, independently of firearm hands.
	class placement_input
	{
		std::array<controller_input::digital_button_gate,2> gates_{};
		std::array<controller_input::clock::time_point,2> until_{};
		controller_input::consumer_continuity continuity_;
		std::uint64_t epoch_{},reference_{},sequence_{};
	public:
		void reset() noexcept {*this={};}
		bool consume(const controller_input::frame& input,std::uint64_t epoch,bool allowed,
			controller_input::clock::time_point now) noexcept
		{
			if(!allowed || !epoch || !input.sequence || !input.focused || input.orientation_settling ||
				now<input.sampled_at || now-input.sampled_at>std::chrono::milliseconds(150))
			{reset();return false;}
			if(continuity_.update(input,now) || epoch_!=epoch || reference_!=input.reference_generation || input.sequence<sequence_)
			{gates_={};until_={};}
			epoch_=epoch;reference_=input.reference_generation;sequence_=input.sequence;
			bool attack{};
			for(unsigned hand=0;hand<2;++hand)
			{
				auto& gate=gates_[hand];const auto& button=input.trigger[hand];
				if(!input.grip[hand].valid || !input.aim[hand].valid)
				{gate={};until_[hand]={};continue;}
				if(!button.active || button.generation!=gate.generation || button.presses<gate.presses)until_[hand]={};
				const bool pressed=button.presses!=gate.presses;
				const bool held=gate.consume(button);
				// _id_D2A4::_id_ABD2 polls at 50 ms. Preserve a short tap across
				// two polls; held input still makes its release/retry loop wait.
				if(held && pressed)until_[hand]=now+std::chrono::milliseconds(100);
				attack|=held || (gate.armed && button.active && now<until_[hand]);
			}
			return attack;
		}
	};
}
