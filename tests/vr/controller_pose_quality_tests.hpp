#pragma once
#include "component/vr/controller_input.hpp"

template<class Check> void controller_pose_quality_tests(Check check)
{
	using namespace vr::controller_input;
	frame input;
	input.focused=true; input.sequence=1; input.reference_generation=1;
	for (unsigned h=0;h<2;++h)
	{
		input.grip[h].valid=input.aim[h].valid=true;
		input.grip[h].quality=input.aim[h].quality={true,true,true,true,true};
		input.runtime_grip[h]=input.sdk_grip[h]=input.grip[h];
		input.runtime_aim[h]=input.sdk_aim[h]=input.aim[h];
	}
	input.squeeze[1]={true,true,4,7,2};
	const auto before=input;
	input.grip[1].quality.position_tracked=input.aim[1].quality.position_tracked=false;
	input.runtime_grip[1]=input.sdk_grip[1]=input.grip[1];
	input.runtime_aim[1]=input.sdk_aim[1]=input.aim[1];
	const auto precision=interaction_snapshot(input);
	check(input.grip[1].valid && !precision.grip[1].valid && precision.grip[0].valid,
		"inferred hand remains visible while precision admission is independent per hand");
	check(precision.squeeze[1].down && precision.squeeze[1].generation==7 && precision.sdk_grip[1].valid,
		"quality degradation preserves real button history and SDK witnesses");
	check(producer_discontinuity(before,input),"VALID-to-inferred transition fences physical motion history");
	check(!interaction_ready(hand_pose{true,{}}),"unknown tracking quality cannot impersonate tracked data");
}
