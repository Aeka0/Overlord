#pragma once
#include "physical_reload_runtime.hpp"
#include "hands/pose_library.hpp"
#include "physical_reload_rig.hpp"
#include "part_presentation.hpp"
#include <string>

namespace scene_models {struct preparation;}

namespace vr::gameplay::weapons::physical_reload
{
	bool presentation_available(const reload_profile* definition = nullptr,
		part_visibility visibility = part_visibility::surface) noexcept;
	std::string presentation_status();
	// Authored rail attachment from this record's exact gun skin and CURRENT
	// model origin, including after its inserted magazine has been removed.
	bool magazine_rail_for_record(const scene_models::preparation&,weapon_identity,
		const reload_profile*,std::uint64_t reference,hands::anchor&) noexcept;
	using presentation_result = part_presentation::result;
	// Called under the existing native skeleton ownership, after grip/IK solve.
	// Optional seating only retargets the off hand; gun/rear/muzzle stay fixed.
	presentation_result present(void* object, std::uint32_t epoch, const void* matrix_buffer,
		const part_rig& parts, const hands::rig& rig, const hands::pose_library& library,
		const weapons::profile& grip_profile, const controller_input::frame& input, const hold& owner,
		const presentation& state,bool knife_held,
		std::uint64_t assembly, bool gameplay, bool manipulation, const std::array<hands::anchor, 2>& targets,
		const std::array<hands::vec, 2>& shoulders, const std::array<hands::vec, 3>& body_axis,
		hands::vec head, hands::vec view_offset, float units, std::span<hands::bone> solved,
		clock::time_point now,hands::quat ordinary_wrist,std::optional<hands::quat> paired_wrist,
		const body_pose::estimate& body_reference,hands::part_hand_frame* hand_motion=nullptr) noexcept;
}
