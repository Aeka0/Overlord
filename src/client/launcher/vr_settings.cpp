#include <std_include.hpp>
#include "vr_settings.hpp"
#include "vr_settings_config.hpp"
#include "build_config.hpp"
#include "component/game_data.hpp"
#include <utils/io.hpp>
#include <fstream>

namespace launcher_vr_settings
{
	namespace
	{
		std::string read_profile(const std::string& path)
		{
			if (!std::filesystem::exists(path)) return {};
			std::ifstream file(path, std::ios::binary);
			if (!file) throw std::runtime_error("Could not read the game configuration.");
			// Bound the read itself, including files that grow after opening.
			std::string data(max_config_bytes + 1, '\0');
			file.read(data.data(), static_cast<std::streamsize>(data.size()));
			data.resize(static_cast<std::size_t>(file.gcount()));
			if (data.size() > max_config_bytes) throw std::runtime_error("The game configuration is too large to edit safely.");
			if (file.bad()) throw std::runtime_error("Could not read the game configuration.");
			if (data.find('\0') != std::string::npos) throw std::runtime_error("The game configuration is not a valid text file.");
			return data;
		}

		std::string response(const json& values, const bool has_vr_config = true)
		{
			json limits = json::object();
			for (const auto& field : vr::settings::numbers)
				limits[field.name] = {{"min", setting_number(field.min)}, {"max", setting_number(field.max)},
					{"step", setting_number(field.step)}, {"displayScale", setting_number(field.display_scale)}};
			return json{{"ok", true}, {"values", values}, {"defaults", defaults()}, {"limits", limits},
				{"onboarding", {{"gameAvailable", game_data::is_game_directory_available()}, {"hasVRConfig", has_vr_config}}},
				{"controllerPresets",controller_presets()},
				{"choices",choice_catalog()},
				{"build", {{"configuration", build_config::name}, {"optimized", build_config::optimized}}}}.dump();
		}
	}

	std::string load()
	{
		try
		{
			const auto profile = read_profile(game_data::get_config_source_path());
			return response(read_values(profile), has_vr_configuration(profile));
		}
		catch (const std::exception& e) { return json{{"ok", false}, {"error", e.what()}}.dump(); }
	}

	void initialize_startup_options(bool from_launcher)
	{
		const auto values = read_values(read_profile(game_data::get_config_source_path()));
		const auto backend = backend_environment_update(values,
			from_launcher ? startup_source::launcher : startup_source::direct,
			GetEnvironmentVariableA("H2V_VR_BACKEND", nullptr, 0) != 0);
		if (backend && !SetEnvironmentVariableA("H2V_VR_BACKEND", backend->c_str()))
			throw std::runtime_error("Could not apply the selected VR backend before game startup.");
		vr::debug_options::initialize(debug_selection(values));
	}

	std::string save(const std::string& payload)
	{
		try
		{
			if (payload.size() > max_payload_bytes) throw std::runtime_error("VR settings are too large.");
			const auto values = json::parse(payload, nullptr, false);
			if (!validate(values)) throw std::runtime_error("Check the VR setting values and allowed ranges.");
			if (!game_data::is_game_directory_available()) throw std::runtime_error("Place Overlord in a supported game directory before saving VR settings.");
			game_data::initialize_players_folder();
			const auto path = game_data::get_config_file_path();
			// Re-read immediately before merging so unrelated profile edits survive.
			const auto updated = update_config(read_profile(path), values);
			if (updated.size() > max_config_bytes || !utils::io::write_file_atomic(path, updated))
				throw std::runtime_error("Could not save VR settings. Check folder permissions and free disk space.");
			return response(values);
		}
		catch (const std::exception& e) { return json{{"ok", false}, {"error", e.what()}}.dump(); }
	}
}
