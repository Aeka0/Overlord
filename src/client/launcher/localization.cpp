#include <std_include.hpp>
#include "localization.hpp"
#include "component/game_data.hpp"
#include <utils/io.hpp>
#include <fstream>

namespace launcher_localization
{
	namespace
	{
		using nlohmann::json;
		constexpr auto preferences_path = "players2/h2-mod/launcher.json";
		std::string system_locale()
		{
			wchar_t names[4096]{};
			ULONG count{}, size = static_cast<ULONG>(std::size(names));
			if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, names, &size) || !count || !names[0])
				if (!LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), names, LOCALE_NAME_MAX_LENGTH, 0)) return default_locale;
			std::string tag;
			for (const auto* c = names; *c; ++c) { if (*c > 127) return default_locale; tag += static_cast<char>(*c); }
			return match_system_locale(tag);
		}
		json read_preferences()
		{
			if (!std::filesystem::exists(preferences_path)) return json::object();
			std::ifstream file(preferences_path, std::ios::binary);
			if (!file) throw std::runtime_error("read");
			std::string data(max_preferences_bytes + 1, '\0');
			file.read(data.data(), static_cast<std::streamsize>(data.size()));
			if (file.bad() || file.gcount() > max_preferences_bytes) throw std::runtime_error("read");
			data.resize(static_cast<std::size_t>(file.gcount()));
			auto result = json::parse(data);
			if (!result.is_object()) throw std::runtime_error("object");
			return result;
		}
		json response(const json& data)
		{
			return {{"ok", true}, {"language", preference(data.dump())}};
		}
		void write_preferences(const json& data)
		{
			game_data::initialize_players_folder();
			auto saved = data;
			saved.erase("theme");
			saved.erase("animations");
			const auto bytes = saved.dump(2);
			if (bytes.size() > max_preferences_bytes || !utils::io::write_file_atomic(preferences_path, bytes)) throw std::runtime_error("write");
		}
	}
	std::string load()
	{
		try
		{
			if (!std::filesystem::exists(preferences_path))
			{
				const auto language = system_locale();
				auto result = json::parse(save(language));
				if (result.value("ok", false)) return result.dump();
				auto fallback = response(json{{"language", language}});
				fallback["warningKey"] = "language.saveError";
				return fallback.dump();
			}
			return response(read_preferences()).dump();
		}
		catch (const std::exception&) { return json{{"ok", false}, {"errorKey", "language.loadError"}}.dump(); }
	}
	std::string save(const std::string& language)
	{
		try
		{
			if (!supported(language)) return json{{"ok", false}, {"errorKey", "language.unsupported"}}.dump();
			auto data = read_preferences(); data["language"] = language;
			write_preferences(data);
			return response(data).dump();
		}
		catch (const std::exception&) { return json{{"ok", false}, {"errorKey", "language.saveError"}}.dump(); }
	}
}
