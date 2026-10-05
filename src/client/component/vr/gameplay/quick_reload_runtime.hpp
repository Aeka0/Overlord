#pragma once
#include "quick_reload.hpp"

namespace vr::gameplay::weapons::quick_reload
{
	// Server-only; uses the coordinator's current hand and actual holster volumes.
	bool ready(dwell&,const hold&,std::uint64_t instance,std::uint64_t assembly,std::string_view feed,
		bool eligible,controller_input::clock::time_point now) noexcept;
}
