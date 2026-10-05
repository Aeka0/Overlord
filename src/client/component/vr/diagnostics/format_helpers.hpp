#pragma once

#include <cstdint>
#include <cstring>
#include <string>

namespace vr::engine_stereo_gpu_census
{
	enum class dynamic_fx_family : std::uint8_t;
}

namespace vr::diagnostics::detail
{
	const char* dynamic_fx_family_name(engine_stereo_gpu_census::dynamic_fx_family value) noexcept;

	inline const char* yes_no(const bool value)
	{
		return value ? "yes" : "no";
	}

	inline const char* available(const std::string& value)
	{
		return value.empty() ? "unavailable" : value.c_str();
	}

	inline float float_from_bits(const std::uint32_t bits) noexcept
	{
		float value{};
		std::memcpy(&value, &bits, sizeof(value));
		return value;
	}

}
