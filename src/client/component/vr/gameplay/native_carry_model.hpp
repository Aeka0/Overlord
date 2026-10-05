#pragma once
#include "hands/pose_solver.hpp"
#include <cstdint>
#include <span>
namespace game {struct XModel;}
namespace vr::gameplay::weapons::native_carry
{
	struct model_part {game::XModel* model{};hands::anchor local{};};
	struct model_geometry
	{
		std::array<model_part,32> parts{};
		std::size_t count{};
		hands::vec low{},high{};
		hands::anchor muzzle{};
		bool valid{},has_muzzle{};
		hands::anchor brass{};
		bool has_brass{};
		game::XModel* native_root{}; // Native drop DObj identity when presentation uses a rigid subset.
	};
	// Server asset queries only. Returned descriptors reference immutable native
	// assets and bounded composed placements, never a mutable DObj or game entity.
	model_geometry geometry(std::uint32_t token) noexcept;
	// Server-only immutable mesh samples, lazily cached per native model identity.
	std::span<const hands::vec> pickup_surface(std::uint32_t token);
	void reset_model_cache() noexcept;
}
