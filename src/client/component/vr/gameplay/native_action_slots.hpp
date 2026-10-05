#pragma once
#include <functional>

namespace vr::gameplay::equipment::action_slots
{
	// Schedule the original input binding on the main owner after rechecking
	// this exact native slot. CL_ExecuteKey owns notification and slot handling;
	// the whitelist's +actionslot/-actionslot names are not console commands.
	// Neither inventory nor script state is written directly.
	void request(unsigned index,unsigned type,unsigned weapon,std::function<bool()> authorized={});
}
