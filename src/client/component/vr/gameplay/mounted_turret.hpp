#pragma once
#include "mounted_turret_policy.hpp"

namespace vr::gameplay::mounted
{
	// Bounded publications; no entity scans or script calls on input/render threads.
	bool active() noexcept;
	bool hands_active() noexcept;
	struct camera_view {std::uint64_t epoch{};std::array<float,3> angles{};};
	std::uint64_t camera_epoch() noexcept;
	// Final native-camera boundary, before HMD composition (Blackhawk only).
	camera_view prepare_camera(float* origin,float (*axis)[3]) noexcept;
	bool owns_hands(const void* object) noexcept;
	bool owns_model(const void* object) noexcept;
	// Called under the native DObj lock after completing its skeleton.
	void apply_pose(void* object,bool rebuilt) noexcept;
	// Final skin consumer: scene preparation has already released the DObj lock.
	void prepare_skin(void* object,const hands::bone* matrices) noexcept;
	bool command(const controller_input::frame&,bool gameplay,int& buttons) noexcept;
	void observe_wrists(const std::array<anchor,2>& world) noexcept;
}
