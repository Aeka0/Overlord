#pragma once
#include "hand_interaction/frame.hpp"

namespace vr::gameplay::equipment::nightvision
{
	bool available()noexcept;
	void collect_interactions(const hand_interaction::frame&)noexcept;
	void update_interactions()noexcept;
	void report_interactions()noexcept;
	void lifecycle(bool suspended)noexcept;
}
