#pragma once
#include "pose_solver.hpp"
#include "../../controller_input.hpp"
#include "../knife_hand_pose.hpp"
#include "../../body_pose.hpp"
#include "../../head_pose_bridge.hpp"

namespace vr::gameplay::hands::attachments
{
	struct solved
	{
		std::uintptr_t object{},matrices{};
		std::uint32_t epoch{};
		std::array<unsigned,2> indices{};
		std::array<bone,2> wrists{};
		std::uint64_t reference{},sequence{};
		controller_input::clock::time_point at{};
		std::uint64_t lease{}; // Opaque equipment-pose generation, never a weapon token.
		// Body/head in the SAME model-origin space and solve as the wrists.
		// Chest attachments must not follow the lower-rate server body mailbox.
		hands::vec head{};
		std::array<hands::vec,3> head_axis{};
		float units{};
		std::array<hands::quat,2> mirror_basis{};
		std::array<equipment::knife_hand_pose,2> knife_poses{}; // From this exact solved record, not a later runtime state.
		body_pose::estimate body{}; // Same solved record; position is model-origin-relative.
		std::array<std::uint64_t,2> reload_items{}; // Exact independent container pose revisions for either hand.
	};
	// Rigid body attachments share the exact skin record's body and placement
	// origin. Never combine its relative head with a later global camera origin.
	inline head_pose_bridge::spatial_frame body_frame(const solved& pose,vec origin) noexcept
	{
		head_pose_bridge::spatial_frame out;out.units_per_meter=pose.units;out.generation=pose.reference;out.captured_at=pose.at;
		out.head_position=add(pose.head,origin);out.head_yaw_axis=pose.head_axis;out.body=pose.body;
		if(out.body.valid)out.body.position=add(out.body.position,origin);
		return out;
	}
	// Shared record binding for held equipment, including the hands-only DObj.
	// No weapon/muzzle identity is invented for an empty native selection.
	void publish(const solved&) noexcept;
	void begin_record(const void* record) noexcept;
	solved before_skin(const void* object,const bone* matrices) noexcept;
	void after_skin(const solved&,int result,const void* record) noexcept;
	bool for_record(const void* record,const std::array<float,12>& camera,solved&,
		int reload_hand=-1,std::uint64_t reload_revision=0) noexcept;
	void clear_after_drain() noexcept;
}
