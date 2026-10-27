#include <std_include.hpp>
#include "game_language.hpp"
#include "game_language_catalog.hpp"
#include "component/game_data.hpp"
#include <utils/io.hpp>
#include <utils/properties.hpp>
#include <CascLib.h>
#include <chrono>
#include <fstream>

namespace launcher_game_language
{
	namespace
	{
		using nlohmann::json;
		constexpr size_t max_config_bytes = 1024 * 1024;
		constexpr auto pending = R"({"pending":true})";
		std::string failure(const char* key) { return json{{"ok", false}, {"errorKey", key}}.dump(); }

		json read_preferences(const std::filesystem::path& path)
		{
			if (!std::filesystem::exists(path)) return json::object();
			std::ifstream file(path, std::ios::binary);
			std::string bytes(max_config_bytes + 1, '\0');
			if (!file) throw std::runtime_error("read");
			file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
			if (file.bad() || file.gcount() > max_config_bytes) throw std::runtime_error("read");
			bytes.resize(static_cast<size_t>(file.gcount()));
			auto value = json::parse(bytes);
			if (!value.is_object()) throw std::runtime_error("object");
			return value;
		}

		json scan_choices(const std::filesystem::path& root, const std::atomic<bool>& cancelled)
		{
			json choices = json::array();
			for (const auto& language : languages)
				choices.push_back({{"value", language.name}, {"label", language.label}, {"detected", false}});

			HANDLE storage{};
			CASC_OPEN_STORAGE_ARGS args{};
			args.Size = sizeof(args);
			args.dwLocaleMask = CASC_LOCALE_ALL;
			// Offline metadata is only a sorting hint. An unreadable storage leaves
			// every official language selectable with detected=false.
			if (!CascOpenStorageEx(root.c_str(), &args, false, &storage)) return choices;
			const auto close = gsl::finally([&] { CascCloseStorage(storage); });
			for (size_t i = 0; i < languages.size(); ++i)
			{
				if (cancelled.load()) throw std::runtime_error("cancelled");
				const auto& language = languages[i];
				const auto common = std::string(language.name) + "\\" + language.code + "_common.ff";
				CASC_FIND_DATA entry{};
				const auto search = CascFindFirstFile(storage, common.c_str(), &entry, nullptr);
				if (search == INVALID_HANDLE_VALUE || !search) continue;
				const auto close_search = gsl::finally([&] { CascFindClose(search); });
				// Detect local language content without auditing every map/audio file
				// or reading payload spans. This is not an installation guarantee.
				choices[i]["detected"] = entry.bFileAvailable && entry.FileSize != 0;
			}
			return choices;
		}

		std::string execute(const std::filesystem::path& root, const std::string& requested, const std::atomic<bool>& cancelled)
		{
			try
			{
				if (cancelled.load()) return failure("language.gameLoadError");
				const auto path = utils::properties::get_appdata_path() / "config.json";
				auto config = read_preferences(path);
				if (!requested.empty())
				{
					config["language"] = requested;
					if (!utils::io::write_file_atomic(path, config.dump(4)))
						return failure("language.gameSaveError");
				}
				const auto selected = config.contains("language") && config["language"].is_string()
					? config["language"].get<std::string>() : "english";
				json result{{"ok", true}, {"language", selected}, {"saved", !requested.empty()}};
				// Saving an official language never scans or checks its installation.
				if (requested.empty()) result["choices"] = scan_choices(root, cancelled);
				return result.dump();
			}
			catch (...) { return failure(requested.empty() ? "language.gameLoadError" : "language.gameSaveError"); }
		}
	}

	std::string service::start(const std::string& language)
	{
		if (worker_.valid()) return pending;
		if (!game_data::is_game_directory_available()) return failure("error.gameDirectory");
		try
		{
			const auto root = std::filesystem::current_path();
			cancelled_->store(false);
			worker_ = std::async(std::launch::async, [root, language, cancelled = cancelled_] { return execute(root, language, *cancelled); });
			return pending;
		}
		catch (...) { return failure("language.gameLoadError"); }
	}
	std::string service::scan() { return start({}); }
	std::string service::save(const std::string& language)
	{
		if (!official(language)) return failure("language.gameUnsupported");
		return start(language);
	}
	std::string service::poll()
	{
		if (!worker_.valid()) return failure("language.gameLoadError");
		if (worker_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return pending;
		try { return worker_.get(); }
		catch (...) { return failure("language.gameLoadError"); }
	}
}
