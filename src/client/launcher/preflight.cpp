#include <std_include.hpp>
#include "preflight.hpp"
#include "preflight_policy.hpp"
#include "preflight_resource.hpp"
#include "vr_settings.hpp"
#include "component/game_data.hpp"
#include <utils/io.hpp>
#include <utils/nt.hpp>
#include <fstream>

namespace launcher_preflight
{
	namespace
	{
		bool readable_file(const char* name)
		{
			std::error_code error;
			if (!std::filesystem::is_regular_file(name, error) || error) return false;
			std::ifstream file(name, std::ios::binary);
			char signature[2]{};
			return file.read(signature, sizeof(signature)) && signature[0] == 'M' && signature[1] == 'Z';
		}
	}

	json check()
	{
		auto issues = json::array();
		const bool game_available = game_data::is_game_directory_available();
		if (!game_available)
			issues.push_back({{"id", "game.binary"}, {"severity", "error"}, {"titleKey", "preflight.gameExe"},
				{"detailKey", "preflight.gameExeDetail"}, {"fixable", false}});
		for (const auto* name : game_dlls)
			if (!readable_file(name))
				issues.push_back({{"id", std::string("game.") + name}, {"severity", "error"}, {"titleKey", "preflight.file"},
					{"detailKey", "preflight.gameDllDetail"}, {"values", {{"file", name}}}, {"fixable", false}});

		auto backend = std::string("openxr");
		try
		{
			const auto profile = launcher_vr_settings::read_profile();
			backend = launcher_vr_settings::read_values(profile).at("vr_runtimeBackend").get<std::string>();
			append_risks(issues, profile, game_available);
		}
		catch (const std::exception&)
		{
			issues.push_back({{"id", "config.read"}, {"severity", "error"}, {"titleKey", "preflight.config"},
				{"detailKey", "preflight.configDetail"}, {"fixable", false}});
		}
		if (backend == "openxr" && !readable_file("openxr_loader.dll"))
			issues.push_back({{"id", "loader.openxr"}, {"severity", "error"}, {"titleKey", "preflight.file"},
				{"detailKey", "preflight.loaderDetail"}, {"values", {{"file", "openxr_loader.dll"}}}, {"fixable", true}});
		return report(std::move(issues), game_available);
	}

	json fix(const std::string& id)
	{
		if (id == "loader.openxr")
		{
			if (!readable_file("openxr_loader.dll"))
			{
				const auto bytes = utils::nt::load_resource(OPENXR_LOADER_RESOURCE);
				if (bytes.empty() || bytes.size() > 16 * 1024 * 1024 || !utils::io::write_file_atomic("openxr_loader.dll", bytes))
					throw std::runtime_error("preflight.fixFailed");
			}
		}
		else
		{
			(void)launcher_vr_settings::risk_group(id);
			if (!game_data::is_game_directory_available()) return check();
			try { launcher_vr_settings::repair_risk_settings(id); }
			catch (const std::exception&) { throw std::runtime_error("preflight.fixFailed"); }
		}
		return check();
	}
}
