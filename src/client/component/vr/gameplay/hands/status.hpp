#pragma once

#include <string>

namespace vr::gameplay::hands
{
	// Cached solver/native evidence only. Busy producers are reported, not waited on.
	std::string status();
}
