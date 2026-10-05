#pragma once
#include <string_view>
#include <span>
namespace game { struct XModel; struct XSurface; }
#include <array>
#include "viewmodel_policy.hpp"
#include <string>

namespace vr::gameplay::weapons::viewmodel_visibility
{
	// Main asset-owner preparation; read-only source metadata, immutable GPU subset.
	bool prepare_skinned_part(game::XModel* model,std::string_view bone,bool subtree = false);
	bool prepare_skinned_parts(game::XModel* model,std::span<const std::string_view> bones,bool subtree);
	const void* skin_object(const void* object) noexcept;
	// One assembled visibility snapshot per solved viewmodel pose. Producers
	// combine cosmetic/interaction masks before publish; this renderer knows
	// nothing about ammo, gestures, weapons' native identities or grip actions.
	bool ready(part_visibility mode) noexcept;
	void publish(const void* object, const void* matrices, std::uint32_t epoch,
		const part_mask& bits, part_visibility mode, int omitted_arm_root = -1,
		const std::array<const game::XSurface*,8>& hidden_surfaces = {}) noexcept;
	std::string status();
}
