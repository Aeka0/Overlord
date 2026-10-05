#pragma once
#include "poses.hpp"
#include "reload_profile.hpp"
#include "../attachments/precision.hpp"
#include "../../weapon_actions.hpp"

namespace vr::gameplay::weapons::cheytac
{
	inline bool suppress_equip(std::string_view name) noexcept
	{
		return base_equip_action(name, "h2_wpn_sni_cheytac_");
	}
	inline const std::array<profile, 2> assemblies = []
	{
		const profile ordinary{
		    .id = "cheytac",
		    .receiver = "h2_viewmodel_cheytac_base",
		    .variant = "handguard",
		    .aiming = aim_rule::two_hand,
		    .wrists = wrists,
		    .authored_rear = 1,
		    .acquire_meters = profile_defaults::acquire_meters,
		    .release_meters = profile_defaults::release_meters,
		    .blend_seconds = profile_defaults::blend_seconds,
		    .fingers = idle_fingers,
		    .equip_rest = equip_rest,
		    .suppress_equip = suppress_equip,
		    .reload = &physical,
		    .viewmodel =
		        {
		            .visibility = part_visibility::rigid_groups,
		        },
		};
		auto variant = ordinary;
		variant.receiver = "h2_viewmodel_cheytac_base_desert";
		variant.reload = &desert;
		return std::array{ordinary, variant};
	}();
	inline const auto& base = assemblies[0];
	inline constexpr auto attachments = rifle_attachments::with_common(std::array<assembly_attachment, 3>{
	    {{{"attach_h2_cheytac_scope_vm", "tag_cheytac_scope", "tag_cheytac_scope"},
	      attachment_role::optic,
	      4},
	     {{"attach_h2_cheytac_scope_vm_desert", "tag_cheytac_scope", "tag_cheytac_scope"},
	      attachment_role::optic,
	      4},
	     precision_attachments::silencer03}});
	inline profile_match select(std::span<const hands::model_definition> models,
	                            const hands::model_definition& receiver,
	                            const hands::rig& rig,
	                            std::span<const hands::bone_definition> bones) noexcept
	{
		const auto variant = std::find_if(
		    assemblies.begin(), assemblies.end(), [&](const auto& p) { return p.receiver == receiver.name; });
		if (variant == assemblies.end() || receiver.count != 17)
			return {nullptr, "M200 receiver topology rejected"};
		const auto bound = bind_attachment_set(attachments, models, receiver, rig, bones);
		if (!bound.valid)
			return {nullptr, "M200 attachment topology rejected"};
		return {&*variant,
		        "M200 manual bolt matched",
		        precision_attachments::physical_scope_mask(models, rig, bones),
		        bound.muzzle};
	}
} // namespace vr::gameplay::weapons::cheytac
