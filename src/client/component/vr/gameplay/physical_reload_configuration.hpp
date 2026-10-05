#pragma once

#include <array>
#include <cstdint>

namespace vr::gameplay::weapons::rotating_bolt
{
	struct profile;
}
namespace vr::gameplay::weapons::belt_feed
{
	struct profile;
}

namespace vr::gameplay::weapons::physical_reload
{
	struct handle_catch;
	struct receiver_bolt_release;

	enum class action_motion
	{
		reciprocating_slide,
		charging_handle,
		rotating_bolt
	};

	// Manual latch distances are metres and directions are gun-local unit vectors.
	// latch_min_speed is metres/second. Mesh contacts belong to the weapon recipe.
	struct magazine_manipulation
	{
		float grab_radius;
		float pull_travel;
		float pull_lateral_limit;
		std::array<float, 3> pull_axis;
		float latch_radius;
		float latch_rearm_radius;
		float latch_min_speed;
		float latch_min_travel;
		std::array<float, 3> latch_direction;
		bool spare_strike{true};
		// Resolve overlapping contacts using the tracked wrist's facing relative
		// to the two authored grasps. Different finger points/box sizes do not
		// provide comparable distances. Outside overlaps, contact alone suffices.
		bool prefer_grasp_facing{};
	};

	// Immutable interaction configuration. Distances are metres; cosine values
	// and pose indices are dimensionless. Hardware geometry stays weapon-local.
	// No game types, addresses, asset lookup or host lifecycle belong here.
	struct profile
	{
		float waist_radius{};
		float slide_radius{};
		float slide_stroke{};
		float locked_travel{};
		float full_stroke{};
		float slide_lateral_limit{};
		float well_radius{};
		float well_contact_depth{};
		float insertion_cosine{};
		float max_contact_step{};
		std::array<float, 3> slide_axis{}; // Gun-local unit rearward direction.
		float well_capture_below{.012f};
		float part_release_distance{.35f};
		float well_release_margin{.04f}; // Hysteresis only after a valid mouth contact.
		float close_travel{.003f};       // Forward completion threshold, below full_stroke.
		std::uint8_t slide_pose_count{1};
		action_motion motion{action_motion::reciprocating_slide};
		const magazine_manipulation* manual_magazine{};
		const handle_catch* manual_catch{};
		const rotating_bolt::profile* manual_bolt{};
		const belt_feed::profile* belt{};
		std::uint8_t knife_slide_pose_count{}; // Separate pose set, frozen at acquisition.
		const receiver_bolt_release* receiver_release{};
		std::uint8_t magazine_pose_count{1};
		float button_magazine_radius{.05f};
		bool support_magazine_catch{};     // Grip support may transfer to a Trigger-held magazine.
		float well_withdraw_margin{.015f}; // Exit/reentry gate, separate from contact hysteresis.
	};
	inline constexpr bool native_action_recoil(const profile& p) noexcept
	{
		return p.motion == action_motion::reciprocating_slide;
	}

	namespace defaults
	{
		// Explicitly selected contact defaults. A bare profile retains its original
		// zero/invalid required fields; sharing values never admits a weapon.
		inline constexpr float waist_radius_m = .18f;
		inline constexpr float slide_lateral_limit_m = .18f;
		inline constexpr float well_radius_m = .045f;
		inline constexpr float well_contact_depth_m = .06f;
		inline constexpr float insertion_cosine = .08715574f;
		inline constexpr float max_contact_step_m = .25f;
		inline constexpr std::array<float, 3> rearward_axis{-1, 0, 0};
		inline constexpr float well_capture_below_m = .06f;

		// Manual magazine distances are metres; strike speed is metres/second.
		inline constexpr float magazine_grab_radius_m = .05f;
		inline constexpr float magazine_pull_travel_m = .05f;
		inline constexpr float magazine_pull_lateral_limit_m = .10f;
		inline constexpr float magazine_latch_radius_m = .025f;
		inline constexpr float magazine_latch_rearm_radius_m = .06f;
		inline constexpr float magazine_latch_min_speed_mps = .15f;
		inline constexpr float magazine_latch_min_travel_m = .012f;
	}
}
