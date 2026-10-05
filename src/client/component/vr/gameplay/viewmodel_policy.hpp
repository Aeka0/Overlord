#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace vr::gameplay::weapons
{
	enum class part_visibility { surface, rigid_groups, skinned_groups };
	using part_mask = std::array<std::uint32_t,8>;
	struct attachment_contract
	{
		// Exact model and resolved root/receiver attachment names. Assembly data:
		// no native inventory, mechanical state, or melee behavior is implied.
		std::string_view model, root, receiver_parent;
		std::string_view muzzle{}; // Optional replacement muzzle on this visible attachment.
	};
	struct viewmodel_policy
	{
		part_visibility visibility{part_visibility::surface};
		std::span<const attachment_contract> hidden_attachments{};
		std::span<const attachment_contract> visible_attachments{};
	};
	inline part_mask combine_part_masks(part_mask assembly, const part_mask& interaction) noexcept
	{
		for (std::size_t i=0;i<assembly.size();++i) assembly[i] |= interaction[i];
		return assembly;
	}
}
