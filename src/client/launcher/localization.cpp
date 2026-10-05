#include <std_include.hpp>
#include "localization.hpp"
#include "component/game_data.hpp"
#include <utils/io.hpp>
#include <utils/nt.hpp>
#include <fstream>
#include <version.h>

namespace launcher_localization
{
	namespace
	{
		// Launcher-only preferences never enter the game's language/dvar store.
		constexpr auto preferences_path = "players2/h2-mod/launcher.json";

		std::string system_locale()
		{
			// UI language, not keyboard layout, game language or regional format.
			wchar_t names[4096]{};
			ULONG count{}, size = static_cast<ULONG>(std::size(names));
			if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, names, &size) || !count || !names[0])
			{
				if (!LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), names,
					LOCALE_NAME_MAX_LENGTH, 0)) return default_locale;
			}
			std::string tag;
			for (const auto* c = names; *c; ++c)
			{
				if (*c > 127) return default_locale;
				tag.push_back(static_cast<char>(*c));
			}
			return match_system_locale(tag);
		}

		std::string script_variable(const char* name, const nlohmann::json& value)
		{
			// MSHTML receives ASCII JSON. Escape markup delimiters before embedding.
			auto data = value.dump(-1, ' ', true);
			for (size_t at = 0; (at = data.find('<', at)) != std::string::npos; at += 6)
				data.replace(at, 1, "\\u003c");
			return "<script>var " + std::string(name) + "=" + data + ";</script>";
		}
	}
	std::string scripts()
	{
		nlohmann::json catalog;
		for (const auto& locale : locales)
		{
			catalog[locale.id] = nlohmann::json::parse(utils::nt::load_resource(locale.resource));
			catalog[locale.id]["about.stage"] = VERSION;
		}
		const auto help = nlohmann::json::parse(utils::nt::load_resource(LAUNCHER_HELP_CONTENT));
		return script_variable("launcherLocaleCatalog", catalog) +
			script_variable("launcherHelpCatalog", help) + "<script>" +
			utils::nt::load_resource(LAUNCHER_I18N) + "</script><script>" +
			utils::nt::load_resource(LAUNCHER_HELP) + "</script><script>" +
			utils::nt::load_resource(LAUNCHER_APP) + "</script><script>" +
			utils::nt::load_resource(LAUNCHER_ONBOARDING) + "</script>";
	}
	std::string load()
	{
		using nlohmann::json;
		try
		{
			if (!std::filesystem::exists(preferences_path))
			{
				const auto language = system_locale();
				// Persist the first match so later system-language changes cannot
				// silently override the launcher's established preference.
				auto result = json::parse(save(language));
				if (!result.value("ok", false))
					return json{{"ok", true}, {"language", language}, {"warningKey", "language.saveError"}}.dump();
				return result.dump();
			}
			std::ifstream file(preferences_path, std::ios::binary);
			if (!file) throw std::runtime_error("read");
			std::string data(max_preferences_bytes + 1, '\0');
			file.read(data.data(), static_cast<std::streamsize>(data.size()));
			if (file.bad()) throw std::runtime_error("read");
			data.resize(static_cast<size_t>(file.gcount()));
			return json{{"ok", true}, {"language", preference(data)}}.dump();
		}
		catch (const std::exception&) { return json{{"ok", false}, {"errorKey", "language.loadError"}}.dump(); }
	}
	std::string save(const std::string& language)
	{
		using nlohmann::json;
		try
		{
			if (!supported(language)) return json{{"ok", false}, {"errorKey", "language.unsupported"}}.dump();
			game_data::initialize_players_folder();
			if (!utils::io::write_file_atomic(preferences_path, json{{"language", language}}.dump(2)))
				throw std::runtime_error("write");
			return json{{"ok", true}, {"language", language}}.dump();
		}
		catch (const std::exception&) { return json{{"ok", false}, {"errorKey", "language.saveError"}}.dump(); }
	}
}
