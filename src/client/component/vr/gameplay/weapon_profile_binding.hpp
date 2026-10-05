#pragma once
#include "weapon_profile.hpp"
#include "weapon_attachments.hpp"

namespace vr::gameplay::weapons
{
	// The dispatcher validates contiguous model ranges before calling a binder.
	inline profile_match bind_profile_attachments(const profile& selected,
		std::span<const hands::model_definition> models, const hands::model_definition& receiver,
		const hands::rig& rig, std::span<const hands::bone_definition> bones) noexcept
	{
		// Resolve the whole assembly, not merely receiver/token. Future per-weapon
		// variants explicitly map foregrip/launcher model combinations to grips.
		// Unknown weapon attachments may change anchors: never reuse base silently.
		part_mask hidden{};
		int muzzle=-1;
		for (const auto& model : models)
			if (&model != &receiver)
			{
				if (model.name == selected.receiver) return {nullptr,"duplicate receiver"};
				bool suppressed{};
				for (const auto& policy : selected.viewmodel.hidden_attachments)
					if (model.name == policy.model)
					{
						if (!bind_hidden_attachment(policy,model,receiver,rig,bones,hidden))
							return {nullptr,"cosmetic attachment rig not authored"};
						suppressed = true;
						break;
					}
				if (suppressed) continue;
				bool visible{};
				for (const auto& policy : selected.viewmodel.visible_attachments)
					if (model.name==policy.model)
					{
						const auto bound=bind_attachment(policy,model,receiver,rig,bones);
						if (!bound.valid || (bound.muzzle>=0 && muzzle>=0))
							return {nullptr,"visible attachment rig not authored or ambiguous"};
						if (bound.muzzle>=0) muzzle=bound.muzzle;
						visible=true;
						break;
					}
				if (visible) continue;
				for (int i = model.begin; i < model.begin + model.count; ++i)
					if (rig.weapon_bones[i])
						return {nullptr, "attachment grip variant not authored"};
			}
		return {&selected, "profile matched", hidden, muzzle};
	}
}
