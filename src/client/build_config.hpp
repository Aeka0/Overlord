#pragma once

namespace build_config
{
#if defined(H2VR_PROFILE_BUILD)
	inline constexpr auto name = "RelWithDebInfo";
	inline constexpr bool optimized = true;
#elif defined(NDEBUG)
	inline constexpr auto name = "Release";
	inline constexpr bool optimized = true;
#else
	inline constexpr auto name = "Debug";
	inline constexpr bool optimized = false;
#endif
}
