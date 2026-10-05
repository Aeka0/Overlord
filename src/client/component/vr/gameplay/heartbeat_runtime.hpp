#pragma once
#include "heartbeat_rig.hpp"
#include "part_hand_transition.hpp"
#include "weapon_carry_runtime.hpp"
#include "hand_interaction/frame.hpp"

namespace game {struct XModel;}

namespace vr::gameplay::weapons::heartbeat
{
	part_rig bind_native(std::span<const game::XModel* const>,std::span<const hands::model_definition>,
		const hands::rig&,std::span<const hands::bone_definition>)noexcept;
	// Server hand arbitration precedes support/body/world acquisition.
	void update(const controller_input::frame&,std::span<const carry::instance>,std::span<const carry::scene>,
		const std::array<hands::anchor,2>& wrists,float units,unsigned available)noexcept;
	void suspend()noexcept;
	void collect_interactions(const hand_interaction::frame&)noexcept;
	void report_interactions()noexcept;
	bool equivalent_mode(std::uint32_t weapon)noexcept;
	bool enabled()noexcept;
	void present(const part_rig&,const hands::rig&,const hands::pose_library&,const profile&,
		const controller_input::frame&,const hold&,std::uint64_t assembly,const std::array<hands::anchor,2>&,
		const std::array<hands::vec,2>&,const std::array<hands::vec,3>&,hands::vec view_offset,float units,
		std::span<hands::bone>,hands::part_hand_frame* hand_motion=nullptr)noexcept;
}
