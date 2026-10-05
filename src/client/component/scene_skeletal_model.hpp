#pragma once
#include "game/assets.hpp"
#include <array>
#include <memory>
#include <span>

namespace scene_models
{
	// A retained native DObj for a continuously posed prop. Native skinning owns
	// vertex deformation and frame storage; the source mesh/materials stay intact.
	class skeletal_model
	{
		struct storage;
		std::unique_ptr<storage> data_;
	public:
		// 4000..4003 belong to carried weapons; 4032 is the native table bound.
		static constexpr unsigned scene_begin=4004,capacity=8;
		skeletal_model();~skeletal_model();
		bool create(game::XModel*);
		// Absolute world-space bones. The provider converts them to the current
		// native render origin while applying the pose, including cache reuse.
		bool submit(std::span<const game::DObjAnimMat> world_bones,unsigned flags=0);
		// Called with the DObj lock held at the existing skeleton boundary, after
		// native evaluation and before any skin jobs are published.
		static bool owns(const void*) noexcept;
		static void apply(void*) noexcept;
		static void enable() noexcept;
	};
}
