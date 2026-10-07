#pragma once
#include <memory>
#include <array>
#include <span>
#include <functional>
#include "scene_face_partition.hpp"

namespace game { struct XModel; struct Material; struct GfxPackedVertex; }
namespace scene_models
{
	// Immutable rigid geometry over a loaded asset. Face selections share source
	// vertices; static baking and composition own their copied vertex buffers.
	// Never edits source XModel/material/buffers or exports assets to disk.
	// Construct on the engine's main asset owner thread.
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
		struct instance
		{
			const rigid_part* geometry{};
			std::array<float, 3> translation{}; // Source bind coordinates; original orientation is retained.
		};
		rigid_part(); ~rigid_part();
		// Main owner only. Combine validated parts of the same source into a
		// private immutable model, merging compatible material surfaces. Only
		// referenced vertices are copied; source buffers and descriptors stay intact.
		// Bounded to five instances (body, follower, three rounds) and 262144
		// copied vertices/triangles in total.
		bool create_instances(std::span<const instance> instances);
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
		// Authored static parts may occupy rigid and skinned surfaces together.
		// Every selected face must belong wholly to the requested bones; reject
		// mixed ownership rather than freezing a deforming receiver boundary.
		bool create_static_face_partition(game::XModel*, unsigned anchor, std::span<const unsigned> bones,
		                                  std::span<const surface_face_range>);
		game::XModel* model() const noexcept;
		// Must publish descriptor -> actual source identity before native submit.
		// GPU/geometry validation alone does not grant a native asset-pool index.
		game::XModel* source() const noexcept;
		std::array<float,7> bind() const noexcept; // xyz, xyzw; source model bind coordinates
		const char* status() const noexcept { return status_; }
	};
}
