#pragma once
#include <cstdint>
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
		// Optional native-view refinement, under this DObj's existing lock.
		// Callback copies only bounded world-space poses; it must not perform
		// native playback, file I/O or wait for another rendering job.
		using pose_provider=bool(*)(const void* native_view,std::span<game::DObjAnimMat>) noexcept;
		// Optional copied metadata for the committed native pose. Observers run
		// under the existing DObj lock and must not invoke native work or wait.
		using pose_observer=void(*)(const void* object,const void* matrices,std::uint32_t epoch,float model_scale) noexcept;
		// 4000..4003 belong to carried weapons; 4032 is the native table bound.
		static constexpr unsigned scene_begin=4004,capacity=8;
		skeletal_model();~skeletal_model();
		bool create(game::XModel*,pose_provider=nullptr,pose_observer=nullptr);
		// Absolute world-space bones. The provider converts them to the current
		// native render origin while applying the pose, including cache reuse.
		// model_scale describes the matching local mesh to the observer; the
		// supplied world-space bones already include their positional size.
		bool submit(std::span<const game::DObjAnimMat> world_bones,unsigned flags=0,float model_scale=1.f);
		// Called with the DObj lock held at the existing skeleton boundary, after
		// native evaluation and before any skin jobs are published.
		static bool owns(const void*) noexcept;
		static void apply(void*) noexcept;
		static void enable() noexcept;
	};
}
