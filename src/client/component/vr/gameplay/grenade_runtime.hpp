#pragma once
#include "hand_interaction/frame.hpp"
#include "equipment_runtime.hpp"

namespace vr::gameplay::grenades
{
	void collect_interactions(const hand_interaction::frame&)noexcept;
	void update_interactions()noexcept;
	void report_interactions()noexcept;
	void lifecycle(bool suspended)noexcept;
	bool occupies_slot(unsigned slot)noexcept;
	void present(const hands::interaction_rig&,const hands::rig&,const controller_input::frame&,
		const std::array<hands::anchor,2>& targets,const std::array<hands::vec,2>& shoulders,
		const std::array<hands::vec,3>& axes,float units,std::span<hands::bone>,unsigned occupied,unsigned visible)noexcept;
}
