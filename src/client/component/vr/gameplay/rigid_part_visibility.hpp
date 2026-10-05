#pragma once
#include "viewmodel_policy.hpp"

namespace vr::gameplay::weapons
{
	inline constexpr size_t max_visibility_groups = 32;
	inline bool surface_intersects(const part_mask& local, unsigned bone_base, const part_mask& hidden) noexcept
	{
		if (bone_base >= 256) return false;
		const unsigned shift=bone_base%32, first=bone_base/32;
		for (unsigned i=0;i+first<8;++i)
		{
			auto word=hidden[i+first] << shift;
			if (shift && i+first+1<8) word |= hidden[i+first+1] >> (32-shift);
			if (word & local[i]) return true;
		}
		return false;
	}
	struct rigid_group_range
	{
		std::uint16_t bone_offset{}, vertices{}, first_triangle{}, triangles{};
	};
	struct rigid_visibility_plan { bool valid{}; std::uint32_t hidden_groups{}; };
	inline rigid_visibility_plan plan_rigid_visibility(std::span<const rigid_group_range> groups,
		unsigned bone_base, unsigned vertices, unsigned triangles, const part_mask& hidden) noexcept
	{
		if (groups.empty() || groups.size() > max_visibility_groups || bone_base >= 256) return {};
		unsigned next_vertex{}, next_triangle{};
		std::uint32_t mask{};
		for (size_t i=0;i<groups.size();++i)
		{
			const auto& group=groups[i];
			const unsigned bone=bone_base+(group.bone_offset >> 6);
			if ((group.bone_offset & 63) || bone >= 256 || !group.vertices || !group.triangles ||
				group.first_triangle != next_triangle || group.vertices > vertices-next_vertex ||
				group.triangles > triangles-next_triangle) return {};
			next_vertex+=group.vertices; next_triangle+=group.triangles;
			if (hidden[bone/32] & (0x80000000u >> (bone%32))) mask |= 1u << i;
		}
		return {next_vertex==vertices && next_triangle==triangles,mask};
	}
}
