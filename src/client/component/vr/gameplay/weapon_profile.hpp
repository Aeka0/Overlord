#pragma once
#include "hand_pose_schema.hpp"
#include "forearm_mount.hpp"
#include "viewmodel_policy.hpp"
#include <string_view>

namespace vr::gameplay::weapons
{
	namespace shield
	{
		struct profile;
	}
	namespace special_melee
	{
		struct blade;
	}
	struct reload_profile;
	struct cylinder_profile;
	struct tube_profile;
	struct break_action_profile;
	struct secondary_motion_profile;
	struct launcher_profile;
	enum class aim_rule
	{
		rear_hand,
		two_hand
	};
	using hands::joint_pose;
	using hands::part_pose;
	namespace profile_defaults
	{
		// Explicit hand acquisition/release hysteresis and pose blending defaults.
		// Receiver geometry and mechanical-family admission remain authored locally.
		inline constexpr float acquire_meters = .10f;
		inline constexpr float release_meters = .22f;
		inline constexpr float blend_seconds = .10f;
	}
	struct profile
	{
		std::string_view id, receiver, variant;
		aim_rule aiming;
		// Gun-local native game units. Authored right-rear poses are mirrored by
		// the hand adapter; gun parts and their actual contact locations stay fixed.
		std::array<hands::anchor, 2> wrists;
		int authored_rear;
		float acquire_meters;
		float release_meters;
		float blend_seconds;
		std::span<const joint_pose> fingers;
		std::span<const part_pose> equip_rest;
		bool (*suppress_equip)(std::string_view);
		const reload_profile* reload{};
		viewmodel_policy viewmodel{};
		const cylinder_profile* cylinder{}; // Separate feed family, never a dummy slide/magazine profile.
		// Optional shared anatomical controller basis. A support attachment may
		// change its contact rotation without rotating the free/reloading hand.
		const std::array<hands::anchor, 2>* free_hand_reference{};
		const secondary_motion_profile* secondary_motion{};
		const tube_profile* tube{}; // Individual shells in a fixed tube, independent of detachable magazines.
		bool fixed_support_position{}; // One physical support location shared by either hand.
		const break_action_profile* break_open{};
		const shield::profile* defense{};
		const std::array<hands::anchor, 2>*
		    control_grips{}; // Explicit bilateral controls; their fingers are already authored for both hands.
		const std::array<hands::quat, 2>*
		    control_rotations{}; // Model orientation relative to the tracked controlling hand.
		bool support_enabled{true};
		const hands::forearm_mount* forearm{};
		const launcher_profile* launcher{};
		const std::array<hands::anchor, 2>*
		    support_grips{};                 // Explicit asymmetric hardware, indexed by supporting hand.
		const special_melee::blade* melee{}; // Dedicated blade geometry; no firearm feed or muzzle.
		const hands::vec*
		    support_mirror_center{}; // Physical asymmetric grip centre; mirror the hand around hardware, not its wrist.
	};
	inline hands::quat free_hand_rotation(const profile& p, int hand) noexcept
	{
		return (p.free_hand_reference ? *p.free_hand_reference : p.wrists)[hand].rotation;
	}
	inline hands::quat control_rotation(const profile& p, int hand, hands::quat tracked) noexcept
	{
		return p.control_rotations ? hands::normalize(hands::multiply(tracked, (*p.control_rotations)[hand]))
		                           : tracked;
	}
	struct profile_match
	{
		const profile* value{};
		const char* reason{"weapon profile not authored"};
		part_mask hidden{};
		int muzzle{-1};
	};
	inline hands::quat aimed_rotation(aim_rule rule,
	                                  hands::quat rear_rotation,
	                                  hands::vec rear,
	                                  hands::vec support,
	                                  hands::vec authored_span) noexcept
	{
		using namespace hands;
		const auto delta = sub(support, rear);
		if (rule != aim_rule::two_hand || !std::isfinite(length(delta)) || length(delta) < 0.5f ||
		    !std::isfinite(length(authored_span)) || length(authored_span) < 0.5f)
			return rear_rotation;
		// Swing the authored grip-to-grip vector; keep rear-hand roll. No world-up
		// cross-product singularity and no assumption that the foregrip is on +X.
		return normalize(multiply(from_to(rotate(rear_rotation, authored_span), delta), rear_rotation));
	}
} // namespace vr::gameplay::weapons
