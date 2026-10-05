#pragma once
#include "closed_bolt.hpp"
#include <string_view>
namespace vr::gameplay::weapons
{
	const closed_bolt::rules* native_chamber_profile(std::string_view name,int capacity) noexcept;
}
