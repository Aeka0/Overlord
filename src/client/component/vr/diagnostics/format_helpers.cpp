#include <std_include.hpp>

#include "format_helpers.hpp"
#include "../engine_stereo_gpu_census.hpp"

namespace vr::diagnostics::detail
{
	const char* dynamic_fx_family_name(
		const engine_stereo_gpu_census::dynamic_fx_family value) noexcept
	{
		switch (value)
		{
		case engine_stereo_gpu_census::dynamic_fx_family::code_trans:
			return "CODE_TRANS";
		case engine_stereo_gpu_census::dynamic_fx_family::glass:
			return "GLASS";
		case engine_stereo_gpu_census::dynamic_fx_family::mark:
			return "MARK";
		case engine_stereo_gpu_census::dynamic_fx_family::spark:
			return "SPARK";
		default: return "UNKNOWN";
		}
	}

}
