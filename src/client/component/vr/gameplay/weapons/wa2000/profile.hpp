#pragma once
#include "poses.hpp"
#include "reload_profile.hpp"
#include "../attachments/precision.hpp"
#include "../../weapon_actions.hpp"

namespace vr::gameplay::weapons::wa2000
{
	inline bool suppress_equip(std::string_view name) noexcept
	{
		return base_equip_action(name, "h2_wpn_sni_wa2000_") || name == "h2_wpn_sni_wa2000_quick_pullout" ||
			   name == "h2_wpn_sni_wa2000_quick_putaway";
	}
	inline const std::array<profile, 1> assemblies = [] {
		std::array<profile, 1> out;
		for (size_t i = 0; i < skins.size(); ++i)
			out[i] = {"wa2000",	   skins[i]->rigid_magazine_source,
					  "handguard", aim_rule::two_hand,
					  wrists,	   1,
					  .10f,		   .22f,
					  .10f,		   idle_fingers,
					  equip_rest,  suppress_equip,
					  skins[i],	   {part_visibility::rigid_groups}};
		return out;
	}();
	inline constexpr auto attachments = rifle_attachments::with_common(std::array<assembly_attachment, 1>{{
		{{"attach_h2_wa2000_scope_vm", "tag_wa2000_scope", "tag_wa2000_scope"}, attachment_role::optic, 6},
	}});
	inline int receiver_skin(std::string_view name) noexcept
	{
		for (size_t i = 0; i < skins.size(); ++i)
			if (name == skins[i]->rigid_magazine_source)
				return int(i);
		return -1;
	}
	inline profile_match select(std::span<const hands::model_definition> models,
								const hands::model_definition &receiver, const hands::rig &rig,
								std::span<const hands::bone_definition> bones) noexcept
	{
		const int skin = receiver_skin(receiver.name);
		if (skin < 0 || receiver.count != 13)
			return {nullptr, "precision rifle receiver rejected"};
		const auto bound = bind_attachment_set(attachments, models, receiver, rig, bones);
		if (!bound.valid)
			return {nullptr, "precision rifle attachment topology rejected"};
		auto hidden = precision_attachments::physical_scope_mask(models, rig, bones);
		return {&assemblies[skin], "precision rifle and physical scope matched", hidden, bound.muzzle};
	}
} // namespace vr::gameplay::weapons::wa2000
