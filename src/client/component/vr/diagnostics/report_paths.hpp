#pragma once
#include <filesystem>
#include <vector>
#include <string>
#include <string_view>
#include <shlobj.h>
#include <mutex>
#include <utils/io.hpp>

namespace vr::diagnostics
{
	inline std::string report_path_text(const std::filesystem::path& path)
	{const auto text=path.u8string();return {reinterpret_cast<const char*>(text.data()),text.size()};}
	inline std::vector<std::filesystem::path> report_directories()
	{
		std::vector<std::filesystem::path> roots;
		std::error_code error;const auto cwd=std::filesystem::current_path(error);
		if(!error)roots.push_back(cwd/"diagnose");
		PWSTR local{};
		if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local)))
		{roots.emplace_back(std::filesystem::path(local)/"Overlord"/"diagnose");CoTaskMemFree(local);}
		const auto temporary=std::filesystem::temp_directory_path(error);
		if(!error)roots.push_back(temporary/"Overlord"/"diagnose");
		return roots;
	}
	inline std::filesystem::path save_named_report(const char* name,const std::string& report)
	{
		if(!name||!*name||std::string_view(name)=="."||std::string_view(name)==".."||
			std::string_view(name).find_first_of("/\\:")!=std::string_view::npos)return {};
		static std::mutex writer;
		const std::lock_guard lock(writer);
		for(const auto& root:report_directories())
			if(const auto path=root/name;utils::io::write_file_atomic(path,report))return path;
		return {};
	}
}
