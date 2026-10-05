#pragma once
#include "actions.hpp"
#include "reload_profile.hpp"
#include "../../weapon_profile_binding.hpp"

namespace vr::gameplay::weapons::usp
{
	// Live USP uses h2_viewmodel_knife (80 -> receiver tag_knife 76 duplicate).
	// viewmodel_knife has matching offline geometry/rig but is not HMD-accepted.
	// Reviewed separate knife mesh aliases. No model-name prefix wildcard and no
	// global melee policy: suppress only this USP assembly's attached prop.
	inline constexpr attachment_contract hidden_props[]{{"h2_viewmodel_knife", "tag_knife", "tag_knife"},
	                                                    {"viewmodel_knife", "tag_knife", "tag_knife"}};
	inline constexpr attachment_contract visible_silencer[]{
	    {"attach_h2_silencer_02_vm", "tag_silencer", "tag_silencer", "tag_flash_silenced"}};
	inline constexpr profile base{
	    .id = "usp",
	    .receiver = "h2_viewmodel_usp_base",
	    .variant = "h1_no_knife",
	    .aiming = aim_rule::rear_hand,
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
	            .hidden_attachments = hidden_props,
	        },
	};
	inline constexpr profile silenced = []
	{
		auto value = base;
		value.id = "usp_silencer";
		value.variant = "h1_no_knife_silenced";
		value.reload = &silenced_physical;
		value.viewmodel.visible_attachments = visible_silencer;
		return value;
	}();
	inline const profile* select_variant(std::span<const hands::model_definition> models) noexcept
	{
		for (const auto& model : models)
			if (model.name == visible_silencer[0].model)
				return &silenced;
		return &base;
	}

	inline profile_match select(std::span<const hands::model_definition> models,
	                            const hands::model_definition& receiver,
	                            const hands::rig& rig,
	                            std::span<const hands::bone_definition> bones) noexcept
	{
		return bind_profile_attachments(*select_variant(models), models, receiver, rig, bones);
	}
}
