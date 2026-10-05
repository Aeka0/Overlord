#pragma once
#include "body_equipment.hpp"
#include "hand_service.hpp"
#include "weapon_carry_runtime.hpp"
#include "hand_interaction/frame.hpp"
#include "hand_interaction/pose_plan.hpp"
#include "grip_edges.hpp"

namespace vr::gameplay::equipment
{
	bool held_by(vr::hand) noexcept;
	bool current_knife(vr::hand,std::uint64_t revision,std::uint64_t reference) noexcept;
	void collect_interactions(const hand_interaction::frame&)noexcept;
	void report_interactions()noexcept;
	void suspend(bool return_knife=false) noexcept;
	// Called by the single carry/server owner after existing part leases are
	// excluded, before support/body/world acquisitions consume the same edges.
	unsigned settle_interactions(const hand_interaction::frame&,
	                             const controller_input::frame& raw_input,
	                             const weapons::carry::grip_edges&);
	struct knife_hand_snapshot
	{
		knife_state knife{};
		bool active{};
	};
	knife_hand_snapshot prepare_hand_pose(const hands::interaction_rig&,const hands::rig&) noexcept;
	void present(const knife_hand_snapshot&,const hands::interaction_rig&,const hands::rig&,
		const std::array<hands::anchor,2>& targets,const std::array<hand_interaction::pose_plan,2>&,
		std::span<hands::bone>,unsigned occupied_hands,unsigned visible_hands) noexcept;
}
