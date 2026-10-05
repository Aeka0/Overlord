#pragma once
#include <cstdint>

namespace scene_model_lighting
{
	inline constexpr unsigned native_minimum=4096,expanded_minimum=8192;
	inline constexpr unsigned reserved_limit=8192,static_table_capacity=16384;
	constexpr unsigned minimum(unsigned reserved)noexcept
	{return reserved<=reserved_limit?expanded_minimum:native_minimum;}
	// Native sizing rounds reserved dynamic slots plus static cache to a power
	// of two. Keep larger native reservations on their original quota so the
	// fixed 16K static owner/free/stamp tables can never be overrun by this change.
	constexpr unsigned total_slots(unsigned reserved)noexcept
	{
		if(reserved>7*4096)return 0;
		unsigned slots=1;while(slots<reserved+minimum(reserved))slots*=2;return slots;
	}
}
