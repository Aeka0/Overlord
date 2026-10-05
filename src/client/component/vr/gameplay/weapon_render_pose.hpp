#pragma once
#include "weapon_interaction.hpp"
#include "../engine_stereo_view.hpp"
#include <string>

namespace vr::gameplay::weapon_render_pose
{
	// Every independently skinned gun commits its own object/arms. Only the exact
	// projected lifetime may publish or clear the single native muzzle mailbox.
	inline bool drives_native_muzzle(bool independent,weapons::weapon_identity solved,weapons::weapon_identity projected) noexcept
	{return !independent || (solved && solved==projected);}
	struct solved_pose
	{
		std::uintptr_t object{}, matrices{};
		std::uint32_t epoch{};
		std::uint16_t muzzle_bone{};
		hands::bone bone{};
		weapons::muzzle_frame muzzle{};
		controller_input::clock::time_point committed_at{};
		weapons::model_anchor control_grip{}; // Gun-local rear contact transformed by this solved pose.
		std::uint16_t laser_bone{256};
		hands::bone laser{};
	};
	struct snapshot
	{
		weapons::muzzle_frame muzzle{};
		controller_input::clock::time_point committed_at{};
		std::uintptr_t object{}, matrices{}, surface{};
		std::uint32_t epoch{};
		weapons::model_anchor control_grip{};
	};
	// All arguments are observed inside existing engine ownership boundaries.
	// Implementation copies MOD data only; it never resamples tracking/camera.
	void publish_solved(const solved_pose& pose) noexcept;
	void begin_scene(const engine_stereo_view::slot_pair& views, std::uintptr_t record,
		const weapons::hold& owner, std::uint64_t reference) noexcept;
	bool for_scene(const engine_stereo_view::slot_pair& views, snapshot& output, weapons::weapon_identity weapon={}) noexcept;
	// Only inside native record preparation, after its scoped viewmodel skin.
	bool for_record(std::uintptr_t record, const std::array<float,12>& camera, snapshot& output, weapons::weapon_identity weapon={}) noexcept;
	std::string status();
}
