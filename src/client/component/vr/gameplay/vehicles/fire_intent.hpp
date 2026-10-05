#pragma once
#include "../../controller_input.hpp"

namespace vr::gameplay::vehicles
{
	// Bridge a short XR press to the native 50 ms script polling loop. A
	// ballistic acceptance acknowledges it once; native waits still pace fire.
	struct fire_press
	{
		std::uint64_t serial{};controller_input::clock::time_point at{};
		bool pending(std::uint64_t consumed,controller_input::clock::time_point now) const noexcept
		{return serial && serial!=consumed && now>=at && now-at<=std::chrono::milliseconds(150);}
	};
}
