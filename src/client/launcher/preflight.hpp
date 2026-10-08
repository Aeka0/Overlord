#pragma once
#include <json.hpp>
#include <string>

namespace launcher_preflight
{
	nlohmann::json check();
	nlohmann::json fix(const std::string& id);
}
