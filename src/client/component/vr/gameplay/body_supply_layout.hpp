#pragma once

namespace vr::gameplay
{
	// Body-relative metres, shared by ammunition providers. A weapon may opt
	// into an explicit override without changing another provider's reach.
	struct body_supply_layout
	{
		float half_width{.21f}, down{.62f}, height{.20f};
	};
}
