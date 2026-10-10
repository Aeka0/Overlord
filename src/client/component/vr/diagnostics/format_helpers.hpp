#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <json.hpp>

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

	inline std::string quoted_text(std::string_view value, std::size_t limit = 512)
	{
		std::string bounded(value.substr(0, limit));
		if (value.size() > limit) bounded += "[truncated]";
		return nlohmann::json(bounded).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
	}

	inline float float_from_bits(const std::uint32_t bits) noexcept
	{
		float value{};
		std::memcpy(&value, &bits, sizeof(value));
		return value;
	}

}
