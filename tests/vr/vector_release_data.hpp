#pragma once
#include <array>
#include "component/vr/gameplay/hand_pose_solver.hpp"
namespace vector_release_data
{
// Boundary vertices of the exposed native j_switch left surface; native units.
// Receiver SHA256 b7801e00d2aec40edb6f9e5ef7bc2a9e4d263ab149167cc8e5c0a2b9321ece7b.
inline constexpr std::array<vr::gameplay::hands::vec,6> surface{{
	{7.91070067f,0.75251103f,1.36214495f},
	{7.91070067f,0.75251103f,0.83082595f},
	{8.28916445f,0.70519100f,0.88366599f},
	{6.65714234f,0.70484196f,1.00825096f},
	{6.58355097f,0.89196201f,1.21325301f},
	{6.56200769f,0.71652100f,1.21325301f},
}};
}
