#include <std_include.hpp>
#include "hand_service.hpp"
#include "native_hand_schema.hpp"
#include "empty_hand_pose.hpp"
#include "equipment_runtime.hpp"
#include "grenade_runtime.hpp"
#include "special_equipment_runtime.hpp"
#include "cliffhanger_runtime.hpp"
#include "cliffhanger_physical.hpp"
#include "scripted_sequences.hpp"
#include "vehicle_runtime.hpp"
#include "reload_item_runtime.hpp"
#include "hand_interaction/runtime.hpp"
#include "game/game.hpp"

namespace vr::gameplay::hands
{
	namespace
	{
		empty_hand::pose_controller default_hands;
	}
	interaction_rig bind_interaction_rig(const hands::rig& r,
	                                     std::span<const hands::bone_definition> bones) noexcept
	{
		interaction_rig out;
		out.library = bind_poses(r, bones, hands::native_hand_schema::definition);
		if (!out.library.valid || r.weapon_tag < 0 || r.weapon_tag >= r.count)
			return out;
		const auto reference = conjugate(normalize(bones[r.weapon_tag].bind.rotation));
		for (unsigned h = 0; h < 2; ++h)
			out.basis[h] = normalize(multiply(reference, bones[r.arms[h].wrist].bind.rotation));
		out.bar_grips = bar_grip::bind(r, out.library, bones, native_hand_schema::definition);
		out.vehicle_poses = vehicles::bind_hands(r, bones);
		out.vehicle_handles = vehicles::bind_handle_hands(r, bones);
		out.valid = true;
		return out;
	}
	presentation_result present_interactions(const interaction_rig& parts,
	                                         const presentation_input& presentation) noexcept
	{
		const auto& r = presentation.skeleton;
		const auto& input = presentation.controllers;
		const auto& targets = presentation.targets;
		const auto& shoulders = presentation.shoulders;
		const auto& axes = presentation.axes;
		const auto units = presentation.units_per_meter;
		const auto solved = presentation.solved;
		const auto posed_hands = presentation.posed_hands;
		const auto visible_hands = presentation.visible_hands;
		if (!parts.valid || solved.size() < std::size_t(r.count))
			return {};
		if (cliffhanger_physical::independent_hands())
		{
			equipment::special::cliffhanger::present(
			    parts, r, input, targets, shoulders, axes, units, solved, 0, visible_hands);
			return {};
		}
		if (sequences::independent_hands(game::CG_GetPredictedPlayerState(0)))
		{
			for (unsigned h = 0; h < 2; ++h)
				if (visible_hands & (1u << h))
				{
					const hand_interaction::pose_plan plan{};
					empty_hand::orient_wrist(
					    r, hand(h), plan, false, targets[h].rotation, parts.basis[h], solved);
					(void)default_hands.present(r,
					                            parts.library,
					                            native_hand_schema::definition,
					                            input,
					                            hand(h),
					                            plan,
					                            false,
					                            solved);
				}
			return {};
		}
		if (vehicles::presentation_allowed())
		{
			vehicles::present(parts, r, input, targets, shoulders, axes, units, solved);
			return {};
		}
		const auto knife_state = equipment::prepare_hand_pose(parts, r);
		unsigned occupied_mask{};
		std::array<bool, 2> occupied{}, knife{};
		std::array<hand_interaction::pose_plan, 2> plans{};
		for (unsigned h = 0; h < 2; ++h)
			if (visible_hands & (1u << h))
			{
				const auto actor = hand(h);
				plans[h] = hand_interaction::pose(actor);
				knife[h] = knife_state.active && knife_state.knife.holder == actor;
				occupied[h] = (posed_hands & (1u << h)) || weapons::carry::hand_has_weapon(actor);
				if (occupied[h])
					occupied_mask |= 1u << h;
				if (input.grip[h].valid)
					empty_hand::orient_wrist(r,
					                         actor,
					                         plans[h],
					                         occupied[h] || knife[h],
					                         targets[h].rotation,
					                         parts.basis[h],
					                         solved);
			}
		equipment::present(knife_state, parts, r, targets, plans, solved, occupied_mask, visible_hands);
		for (unsigned h = 0; h < 2; ++h)
			if (visible_hands & (1u << h))
				(void)default_hands.present(r,
				                            parts.library,
				                            native_hand_schema::definition,
				                            input,
				                            hand(h),
				                            plans[h],
				                            occupied[h] || knife[h],
				                            solved);
		grenades::present(
		    parts, r, input, targets, shoulders, axes, units, solved, posed_hands, visible_hands);
		equipment::special::present(
		    parts, r, input, targets, shoulders, axes, units, solved, posed_hands, visible_hands);
		const auto tokens =
		    reload_items::present(parts, r, input, targets, solved, posed_hands, visible_hands);
		return {.knife_revision = knife_state.knife.revision, .reload_item_tokens = tokens};
	}
}
