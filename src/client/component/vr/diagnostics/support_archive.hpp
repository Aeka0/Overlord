#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include <json.hpp>

namespace vr::diagnostics::support
{
	// File/ZIP layer, independent of the game, runtime and UI. Each capture owns
	// a unique staging directory; only its own files are removed after ZIP close.
	class archive
	{
		std::filesystem::path staging_,destination_;
		std::vector<std::pair<std::string,std::string>> files_;
		nlohmann::json manifest_=nlohmann::json::array();
		std::size_t bytes_{};
	public:
		static constexpr std::size_t file_limit=8*1024*1024,total_limit=24*1024*1024;
		archive(const std::filesystem::path& root,const std::string& id);
		void add(const std::string& name,std::string contents);
		void include(const std::filesystem::path& source,const std::string& name);
		void note(const std::string& name,const std::string& state);
		std::filesystem::path finish();
		const std::filesystem::path& staging() const noexcept {return staging_;}
	};
}
