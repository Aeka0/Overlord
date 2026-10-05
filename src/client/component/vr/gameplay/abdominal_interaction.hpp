#pragma once
#include "special_equipment_policy.hpp"
#include "hand_interaction/runtime.hpp"
namespace vr::gameplay::equipment::special
{
	inline unsigned abdominal_intent(grab_intent& intent,const hand_interaction::frame& f,
		hand_interaction::button control=hand_interaction::button::grip)noexcept
	{
		namespace hi=hand_interaction;unsigned pressed{},released{},available{};
		if(control!=hi::button::grip && control!=hi::button::trigger){intent.reset();return 0;}
		for(unsigned h=0;h<2;++h){const auto actor=vr::hand(h);const auto edge=hi::input(actor,control);
			if(edge.press)pressed|=1u<<h;if(edge.release)released|=1u<<h;if(hi::free(actor))available|=1u<<h;}
		return intent.consume(f.input,available&f.valid_hands,pressed,released,
			control==hi::button::trigger?f.input.trigger:f.input.squeeze);
	}
}
