#pragma once
#include "part_mesh_partition.hpp"

namespace vr::gameplay::weapons
{
	struct magazine_round_instance
	{
		std::span<const scene_models::surface_face_range> faces;
		hands::vec translation{}; // Measured source-bind offset; no caliber-based scaling.
	};
	struct magazine_round_stack
	{
		std::array<magazine_round_instance, 3> rounds;
		// Optional permanent support geometry, kept in all four states. Its
		// translation follows the authored stack instead of occluding lower rounds.
		std::span<const scene_models::surface_face_range> follower_faces;
		std::array<hands::vec, 4> follower_translations{};
	};
	// Four immutable population states. Complete source stacks use original
	// face selections; shorter stacks use explicitly placed native-round copies.
	// Geometry is authored offline and prepared once, never sliced per frame.
	struct magazine_fill_recipe
	{
		const char* source{};
		unsigned bones{};
		std::span<const std::array<unsigned, 2>> surfaces;
		std::array<std::span<const scene_models::surface_face_range>, 4> faces;
		std::array<hands::vec, 4> low, high; // Source bind space, native units.
		// Optional reuse of native single-round geometry. faces[0] is the permanent
		// body; each population adds the first N instances from this authored stack.
		const magazine_round_stack* stack{};
	};
	inline constexpr size_t magazine_fill_level(int rounds) noexcept
	{
		return rounds <= 0 ? 0 : rounds >= 3 ? 3 : size_t(rounds);
	}
	inline bool valid_magazine_fill(const magazine_fill_recipe& p) noexcept
	{
		if (p.stack)
		{
			if (!valid_partition({p.source, p.bones, p.surfaces, p.faces[0], p.low[0], p.high[0]}))
				return false;
			if (!p.stack->follower_faces.empty())
			{
				for (const auto& follower : p.stack->follower_faces)
					for (const auto& body : p.faces[0])
						if (body.surface == follower.surface && body.first <= follower.last &&
						    follower.first <= body.last)
							return false;
				for (size_t state = 0; state < p.stack->follower_translations.size(); ++state)
				{
					if (!valid_partition({p.source,
					                      p.bones,
					                      p.surfaces,
					                      p.stack->follower_faces,
					                      p.low[state],
					                      p.high[state]}))
						return false;
					for (float value : p.stack->follower_translations[state])
						if (!std::isfinite(value) || std::abs(value) > 10000)
							return false;
				}
			}
			for (size_t round = 0; round < p.stack->rounds.size(); ++round)
			{
				const auto& instance = p.stack->rounds[round];
				if (!valid_partition(
				        {p.source, p.bones, p.surfaces, instance.faces, p.low[round + 1], p.high[round + 1]}))
					return false;
				for (float value : instance.translation)
					if (!std::isfinite(value) || std::abs(value) > 10000)
						return false;
			}
			return true;
		}
		unsigned previous{};
		for (size_t i = 0; i < 4; ++i)
		{
			if (!valid_partition({p.source, p.bones, p.surfaces, p.faces[i], p.low[i], p.high[i]}))
				return false;
			unsigned count{};
			for (const auto& r : p.faces[i])
				count += r.last - r.first + 1;
			if (count <= previous)
				return false;
			// Every higher level retains the whole lower level (including body).
			if (i)
				for (const auto& r : p.faces[i - 1])
				{
					unsigned next = r.first;
					bool covered = false;
					for (const auto& higher : p.faces[i])
						if (higher.surface == r.surface && higher.last >= next)
						{
							if (higher.first > next)
								return false;
							if (higher.last >= r.last)
							{
								covered = true;
								break;
							}
							next = higher.last + 1;
						}
					if (!covered)
						return false;
				}
			previous = count;
		}
		return true;
	}
}
