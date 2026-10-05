#pragma once
#include "../controller_input.hpp"

namespace vr::gameplay::sentry
{
	// Fresh, server-observed native placement session; no script VM access from
	// command construction. Zero also covers native weapon restoration/death.
	std::uint64_t placement_epoch() noexcept;
}
