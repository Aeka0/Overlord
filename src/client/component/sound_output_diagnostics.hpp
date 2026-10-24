#pragma once
#include <string>

namespace sound::output_diagnostics
{
	// Immutable lifecycle snapshot; callers never touch the native COM objects.
	std::string status();
}
