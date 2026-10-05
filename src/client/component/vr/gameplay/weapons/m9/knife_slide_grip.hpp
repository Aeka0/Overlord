#pragma once
#include "../../part_grip_pose.hpp"
#include "../../knife_slide_pose.hpp"
namespace vr::gameplay::weapons::m9
{
	// Native USP pickup frame 17, fitted by slide rear edge and upper body height.
	// Shared +Z lift; contact rebound to the real slide, not lifted off the metal.
	inline const std::array<part_grip_pose,1> knife_slide_grips{{
		{"knife_pickup_slide",{{-3.174449825f,-0.039551332f,4.621080902f+equipment::knife_slide_pose::wrist_raise},{0.991546151f,0.119713046f,-0.009198040f,-0.049197689f}},{3.480711844f,1.699487444f,2.088658255f},equipment::knife_slide_pose::fingers}
	}};
}
