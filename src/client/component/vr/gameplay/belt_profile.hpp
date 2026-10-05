#pragma once
#include "part_grip_pose.hpp"

namespace vr::gameplay::weapons::belt_feed
{
	struct link_pose { std::string_view bone; hands::anchor loose,seated; };
	struct bridge_profile
	{
		std::string_view bone;hands::anchor rest;hands::vec axis;float angle;
		part_grip_pose grip;float radius{.18f};
		const part_grip_pose* release{};float release_radius{.12f};
		bool rear_secondary_release{};
	};
	struct detail_pose {std::string_view bone;hands::anchor rest,open;bool bridge{};};
	// Metres in the closed cover's hinge frame. The rectangle is on the
	// exterior skin, deliberately excluding the hinge and side/underside.
	struct cover_push_profile {float inner{},outer{},side_low{},side_high{},surface{};};
	struct cover_push_contact
	{
		bool valid{};
		hands::vec point{},palm{},world{}; // hinge-local point/normal; world palm for continuity
		hands::vec along{}; // Unit wrist-to-fingers direction in the same hinge frame.
	};
	struct profile
	{
		std::string_view cover_bone;
		hands::anchor cover_rest;
		hands::vec cover_axis{}; float cover_angle{};
		part_grip_pose cover_grip,chain_grip;
		std::span<const link_pose> links;
		float cover_radius{.15f},chain_radius{.14f},lay_radius{.11f},lay_cosine{-.25f};
		// Nearby feed parts get a small preference over the large box/handle
		// volumes. A direct grasp well outside these parts still reaches the box.
		float cover_preference{.06f},chain_preference{.08f};
		hands::vec cover_extend_low_m{},cover_extend_high_m{}; // Additional gun-axis reach around the moving cover, in metres.
		const bridge_profile* bridge{};
		std::span<const detail_pose> details;
		const cover_push_profile* push{};
	};
	inline float cover_distance(const profile& p,hands::vec delta_m)noexcept
	{
		return physical_reload::box_distance(delta_m,hands::scale(p.cover_extend_low_m,-1),p.cover_extend_high_m);
	}
	inline hands::anchor hinge_pose(hands::anchor rest,hands::vec axis,float angle,float amount)noexcept
	{
		auto out=rest;const auto half=angle*std::clamp(amount,0.f,1.f)*.5f;
		const auto a=hands::scale(axis,std::sin(half));
		out.rotation=hands::normalize(hands::multiply(out.rotation,{a[0],a[1],a[2],std::cos(half)}));return out;
	}
	inline hands::anchor cover_pose(const profile& p,float amount)noexcept
	{return hinge_pose(p.cover_rest,p.cover_axis,p.cover_angle,amount);}
	struct contact
	{
		float cover_distance{10},chain_distance{10},cover_angle{},feed_distance{10},alignment{};
		hands::vec hand{},chain_point{};
		float bridge_distance{10},bridge_angle{};
		float release_distance{10};bool bridge_settled{true};
		cover_push_contact push{};
	};
	enum class lease { none,cover,chain,bridge,bridge_release };
	// For hinges, initial is the accumulated (unclamped) progress at angle.
	// Rebase each consumed sample so crossing atan2's seam cannot reverse travel.
	struct grasp { lease part{}; float initial{},angle{}; hands::vec previous{}; };
}
