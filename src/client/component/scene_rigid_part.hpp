#pragma once
#include <memory>
#include <array>
#include <span>
#include <functional>
#include "scene_face_partition.hpp"

namespace game { struct XModel; struct Material; struct GfxPackedVertex; }
namespace scene_models
{
	// Immutable rigid view over a loaded asset. Builds only runtime
	// index buffers; never edits the source XModel/material/vertex buffer or writes
	// extracted assets to disk. Construct on the engine's main asset owner thread.
	class rigid_part
	{
		struct storage;
		std::unique_ptr<storage> data_;
		const char* status_{"not constructed"};
		bool upload_vertices();
		enum class membership {rigid,static_skin,rigid_bone};
		bool create_impl(game::XModel*,unsigned,std::span<const unsigned>,membership,
			std::span<const surface_face_range> face_ranges={},bool include=true);
	public:
		rigid_part(); ~rigid_part();
		bool create(game::XModel* source, unsigned bone);
		// A rigid mechanical bone may live in a skinned surface. Accept only
		// complete triangles whose every influence is this one bone; reject mixed
		// weights/boundaries. Handles mixed rigid/skinned source models exactly.
		bool create_rigid_bone(game::XModel* source,unsigned bone);
		// Several rigid groups across the native byte-sized material surface count, in their common
		// source bind coordinates. Keeps a magazine and its rounds/materials intact.
		bool create(game::XModel* source, unsigned anchor, std::span<const unsigned> bones);
		// Static bind-space partition of a skinned prop. All influences and all
		// vertices of each triangle must agree on membership. No dynamic skinning
		// or deformation is approximated; source buffers remain immutable.
		bool create_skin_partition(game::XModel* source,std::span<const unsigned> bones);
		// Single-surface rigid mesh partition, with inclusive sorted face ranges.
		// Caller verifies asset topology; retained/rejected faces never alter source.
		bool create_face_partition(game::XModel*,unsigned bone,std::span<const std::array<unsigned,2>>,bool include);
		// Main owner, before registration/submission. Private preview VB collapses
		// texture coordinates, so a native objective donor cannot paint its prop UVs.
		bool preview_material(game::Material* material);
		// Main owner, before registration. Bake into private immutable buffers;
		// queued models and source vertices must never be edited through this API.
		bool bake_vertices(const std::function<bool(unsigned,std::span<game::GfxPackedVertex>)>& transform);
		// Multi-material version, preserving each source surface/material. Ranges
		// must be sorted and non-overlapping within their surface.
		bool create_face_partition(game::XModel*,unsigned bone,std::span<const surface_face_range>,bool include);
		// Exact face subset across several rigid groups (e.g. magazine + rounds).
		bool create_face_partition(game::XModel*,unsigned anchor,std::span<const unsigned> bones,
			std::span<const surface_face_range>);
		game::XModel* model() const noexcept;
		// Must publish descriptor -> actual source identity before native submit.
		// GPU/geometry validation alone does not grant a native asset-pool index.
		game::XModel* source() const noexcept;
		std::array<float,7> bind() const noexcept; // xyz, xyzw; source model bind coordinates
		const char* status() const noexcept { return status_; }
	};
}
