#pragma once
#include "rifle.hpp"

namespace vr::gameplay::weapons::precision_attachments
{
	// Captured on M14 EBR and M200; opt in per receiver, not globally by name.
	inline constexpr assembly_attachment silencer03{
		{"attach_h2_silencer_03_vm","tag_silencer","tag_silencer","tag_flash_silenced"},attachment_role::silencer,2};
	// Call only after bind_attachment_set has validated this complete assembly.
	// Native sniper scopes carry an alternate flattened ADS mesh. VR keeps the
	// ordinary physical scope and suppresses only that authored subtree.
	inline part_mask physical_scope_mask(std::span<const hands::model_definition> models, const hands::rig &rig,
										 std::span<const hands::bone_definition> bones) noexcept
	{
		part_mask hidden{};
		for (const auto &model : models)
			if (model.name.starts_with("attach_h2_"))
				for (int root = model.begin; root < model.begin + model.count; ++root)
					if (bones[root].name == "tag_scope_ads_on")
						for (int i = root; i < model.begin + model.count; ++i)
							if (hands::descendant(i, root, rig))
								hidden[i / 32] |= 0x80000000u >> (i % 32);
		return hidden;
	}
} // namespace vr::gameplay::weapons::precision_attachments
