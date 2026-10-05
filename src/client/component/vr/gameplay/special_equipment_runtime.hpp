#pragma once
#include "hand_interaction/frame.hpp"
#include "equipment_runtime.hpp"
namespace vr::gameplay::equipment::special
{
	bool active()noexcept;
	void collect_interactions(const hand_interaction::frame&)noexcept;
	void update_interactions()noexcept;
	void report_interactions()noexcept;
	void lifecycle(bool suspended)noexcept;
	void present(const hands::interaction_rig&,const hands::rig&,const controller_input::frame&,
		const std::array<hands::anchor,2>&,const std::array<hands::vec,2>&,const std::array<hands::vec,3>&,
		float,std::span<hands::bone>,unsigned,unsigned)noexcept;
}
