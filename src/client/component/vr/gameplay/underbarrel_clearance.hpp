#pragma once
#include "underbarrel_rig.hpp"
#include "game/assets.hpp"
#include <utils/native_memory.hpp>
#include <limits>
#include <vector>

namespace vr::gameplay::weapons::underbarrel
{
	// Main asset owner only. Measure the actual loaded skin's rigid tube and
	// case, without allocating GPU resources or changing any native asset.
	// When their axial envelopes no longer overlap, the tube cannot support
	// any part of the case. Return native model units, independent of XR scale.
	inline float measure_case_clearance(const game::XModel* source) noexcept
	{
		try
		{
			game::XModel model;
			if (!source || !utils::native_memory::read_bytes(&model, source, sizeof(model)) ||
			    model.numBones != m203_bones.size() || model.numLods != 1 || !model.numsurfs ||
			    model.numsurfs > 8 || model.lodInfo[0].numsurfs != model.numsurfs || !model.lodInfo[0].surfs)
				return 0;
			float case_front = -std::numeric_limits<float>::infinity(),
			      tube_rear = std::numeric_limits<float>::infinity();
			for (unsigned s = 0; s < model.numsurfs; ++s)
			{
				game::XSurface surface;
				if (!utils::native_memory::read_bytes(
				        &surface, model.lodInfo[0].surfs + s, sizeof(surface)) ||
				    (surface.flags & ~3u) || !surface.vertCount || !surface.triCount ||
				    !surface.rigidVertListCount || surface.rigidVertListCount > 32 ||
				    !surface.rigidVertLists || !surface.verts0.packedVerts0 || !surface.triIndices)
					return 0;
				std::vector<game::GfxPackedVertex> vertices(surface.vertCount);
				std::vector<game::Face> faces(surface.triCount);
				std::vector<game::XRigidVertList> groups(surface.rigidVertListCount);
				if (!utils::native_memory::read_bytes(vertices.data(),
				                                      surface.verts0.packedVerts0,
				                                      vertices.size() * sizeof(vertices[0])) ||
				    !utils::native_memory::read_bytes(
				        faces.data(), surface.triIndices, faces.size() * sizeof(faces[0])) ||
				    !utils::native_memory::read_bytes(
				        groups.data(), surface.rigidVertLists, groups.size() * sizeof(groups[0])))
					return 0;
				unsigned first_vertex{}, first_face{};
				for (const auto& group : groups)
				{
					const auto bone = unsigned(group.boneOffset / 64);
					if (group.boneOffset % 64 || bone >= model.numBones || group.triOffset != first_face ||
					    group.vertCount > vertices.size() - first_vertex ||
					    group.triCount > faces.size() - first_face)
						return 0;
					if (bone == 3 || bone == 6)
						for (unsigned f = 0; f < group.triCount; ++f)
						{
							const auto& face = faces[first_face + f];
							for (const auto index : {face.v1, face.v2, face.v3})
							{
								if (index < first_vertex || index >= first_vertex + group.vertCount)
									return 0;
								const auto& v = vertices[index];
								const hands::vec point{v.xyz[0], v.xyz[1], v.xyz[2]};
								for (float x : point)
									if (!std::isfinite(x) || std::abs(x) > 10000)
										return 0;
								const float axial = hands::dot(point, authored::m203_axis);
								if (bone == 3)
									tube_rear = std::min(tube_rear, axial);
								else
									case_front = std::max(case_front, axial);
							}
						}
					first_vertex += group.vertCount;
					first_face += group.triCount;
				}
				if (first_vertex != vertices.size() || first_face != faces.size())
					return 0;
			}
			const float distance = case_front - tube_rear;
			return std::isfinite(distance) && distance > 0 && distance < 4 ? distance : 0;
		}
		catch (...)
		{
			return 0;
		}
	}
}
