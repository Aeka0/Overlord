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
		constexpr size_t max_files = 100000;
		constexpr DWORD max_spans = 4096;
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

		bool readable(HANDLE storage, const CASC_FIND_DATA& entry)
		{
			if (!entry.bFileAvailable || !entry.FileSize || !entry.dwSpanCount || entry.dwSpanCount > max_spans) return false;
			HANDLE file{};
			if (!CascOpenFile(storage, entry.szFileName, CASC_LOCALE_ALL, CASC_OPEN_BY_NAME, &file)) return false;
			const auto close = gsl::finally([&] { CascCloseFile(file); });
			std::vector<CASC_FILE_SPAN_INFO> spans(entry.dwSpanCount);
			if (!CascGetFileInfo(file, CascFileSpanInfo, spans.data(), spans.size() * sizeof(spans[0]), nullptr)) return false;
			// Resolve every span against local data archives, including its tail.
			// Metadata or a shared startup zone alone is not an installed language.
			for (const auto& span : spans)
			{
				if (span.EndOffset <= span.StartOffset || span.EndOffset > entry.FileSize) return false;
				for (const auto position : {span.StartOffset, span.EndOffset - 1})
				{
					char byte{}; DWORD read{};
					if (!CascSetFilePointer64(file, static_cast<LONGLONG>(position), nullptr, FILE_BEGIN) ||
						!CascReadFile(file, &byte, 1, &read) || read != 1) return false;
				}
			}
			return true;
		}

		json installed(const std::filesystem::path& root)
		{
			HANDLE storage{};
			CASC_OPEN_STORAGE_ARGS args{};
			args.Size = sizeof(args);
			args.dwLocaleMask = CASC_LOCALE_ALL;
			// Neither ONLINE nor ALLOW_DOWNLOAD is set. Never consult a CDN.
			if (!CascOpenStorageEx(root.c_str(), &args, false, &storage)) throw std::runtime_error("storage");
			const auto close = gsl::finally([&] { CascCloseStorage(storage); });
			json choices = json::array();
			size_t total{};
			for (const auto& language : languages)
			{
				const auto prefix = std::string(language.name) + "\\" + language.code + "_";
				CASC_FIND_DATA entry{};
				const auto search = CascFindFirstFile(storage, (prefix + "*").c_str(), &entry, nullptr);
				if (search == INVALID_HANDLE_VALUE || !search) continue;
				const auto close_search = gsl::finally([&] { CascFindClose(search); });
				bool complete = true, startup = false, common = false, audio = false;
				do
				{
					if (++total > max_files) throw std::runtime_error("inventory");
					const std::string_view name(entry.szFileName);
					startup |= name == prefix + "code_post_gfx.ff";
					common |= name == prefix + "common.ff";
					audio |= name.ends_with(".pak");
					if (!readable(storage, entry)) { complete = false; break; }
				} while (CascFindNextFile(search, &entry));
				if (complete && startup && common && audio)
					choices.push_back({{"value", language.name}, {"label", language.label}});
			}
			return choices;
		}

		std::string execute(const std::filesystem::path& root, const std::string& requested)
		{
			json choices;
			try { choices = installed(root); }
			catch (...) { return failure("language.gameLoadError"); }
			if (!requested.empty() && std::none_of(choices.begin(), choices.end(), [&](const auto& c) { return c.at("value") == requested; }))
				return failure("language.gameUnavailable");
			try
			{
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
				return json{{"ok", true}, {"language", selected}, {"choices", choices}, {"saved", !requested.empty()}}.dump();
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
			worker_ = std::async(std::launch::async, [root, language] { return execute(root, language); });
			return pending;
		}
		catch (...) { return failure("language.gameLoadError"); }
	}
	std::string service::scan() { return start({}); }
	std::string service::save(const std::string& language)
	{
		if (!official(language)) return failure("language.gameUnavailable");
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
