#pragma once
#include "component/vr/digital_button_gate.hpp"
#include "../../controller_input.hpp"

namespace vr::gameplay::sequences
{
	enum class phase { none, hookup, descent, melee, execution, release, transport, swim, recovery, breach_plant, breach_combat, scripted_combat, scripted_use, death };
	enum class scenario { none, vehicle, rappel, oilrig, breach, estate, favela, gulag, sliding, death, ending, trainer, cliffhanger, roadkill, dcemp, museum_credits, airport };
	enum class melee_delivery { press, polled };
	inline const char* name(phase p) noexcept
	{
		switch(p) {
		case phase::death:return "death";
		case phase::hookup:return "hookup"; case phase::descent:return "descent";
		case phase::melee:return "melee"; case phase::execution:return "execution";
		case phase::release:return "release";
		case phase::scripted_combat:return "scripted_combat";
		case phase::scripted_use:return "scripted_use";
		case phase::transport:return "transport"; case phase::swim:return "swim";
		case phase::recovery:return "recovery";case phase::breach_plant:return "breach_plant";case phase::breach_combat:return "breach_combat";
		default:return "none";
		}
	}
	inline constexpr int brake_button=1, melee_button=4;
	class actions
	{
		phase phase_{};
		std::uint64_t epoch_{}, reference_{}, sequence_{};
		controller_input::clock::time_point last_{};
		std::array<controller_input::digital_button_gate,2> brake_{};
		std::array<controller_input::digital_press_gate,2> melee_{};
		std::array<controller_input::clock::time_point,2> melee_until_{};
		melee_delivery delivery_{};
	public:
		// Oilrig's GSC polling period is 50 ms. Keep short taps visible across
		// two polls; a physical hold retains the native held-button semantics.
		inline static constexpr auto polled_minimum=std::chrono::milliseconds(100);
		void reset() noexcept { *this={}; }
		int consume(const controller_input::frame& input, phase p, std::uint64_t epoch,
			bool admitted, controller_input::clock::time_point now,melee_delivery delivery=melee_delivery::press) noexcept
		{
			if (!admitted || !epoch || p==phase::none || !input.focused || !input.sequence ||
				input.orientation_settling || now<input.sampled_at || now-input.sampled_at>std::chrono::milliseconds(150))
			{reset();return 0;}
			if (epoch_!=epoch || phase_!=p || delivery_!=delivery || reference_!=input.reference_generation || input.sequence<sequence_ ||
				now<last_ || now-last_>std::chrono::milliseconds(150)) reset();
			epoch_=epoch;phase_=p;reference_=input.reference_generation;sequence_=input.sequence;last_=now;
			delivery_=delivery;
			int result{};
			for (int hand=0;hand<2;++hand)
			{
				if (!input.grip[hand].valid || !input.aim[hand].valid) {brake_[hand]={};melee_[hand]={};melee_until_[hand]={};continue;}
				// Same physical Trigger, different native semantics. Phase changes
				// invalidate both gates, so braking cannot become a buffered kill.
				if (p==phase::descent && brake_[hand].consume(input.trigger[hand])) result|=brake_button;
				if (p==phase::melee)
				{
					const auto& value=input.trigger[hand];auto& gate=melee_[hand];
					if(!value.active || value.generation!=gate.generation || value.presses<gate.presses)melee_until_[hand]={};
					const bool pressed=gate.consume(value);
					if(delivery==melee_delivery::press) {if(pressed)result|=melee_button;}
					else
					{
						if(pressed)melee_until_[hand]=now+polled_minimum;
						if(gate.armed && value.active && (value.down || now<melee_until_[hand]))result|=melee_button;
					}
				}
			}
			return result;
		}
	};
}
