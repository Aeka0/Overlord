#include <std_include.hpp>
#include "support_archive.hpp"
#include <utils/compression.hpp>
#include <fstream>

namespace vr::diagnostics::support
{
	archive::archive(const std::filesystem::path& root,const std::string& id)
	{
		if(id.empty()||id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos)
			throw std::runtime_error("Invalid diagnostic capture identity");
		staging_=std::filesystem::absolute(root)/id;
		destination_=staging_.parent_path()/(id+".zip");
		std::filesystem::create_directories(staging_.parent_path());
		if(std::filesystem::exists(destination_)||!std::filesystem::create_directory(staging_))
			throw std::runtime_error("Diagnostic capture path already exists");
		const auto probe=staging_/"write-check.tmp";
		{std::ofstream output(probe,std::ios::binary);output<<"write check";output.close();
			if(!output)throw std::filesystem::filesystem_error("Diagnostic folder is not writable",staging_,std::make_error_code(std::errc::io_error));}
		std::filesystem::remove(probe);
	}
	void archive::note(const std::string& name,const std::string& state)
	{manifest_.push_back({{"entry",name},{"state",state}});}
	void archive::add(const std::string& name,std::string contents)
	{
		if(name.empty()||name.find_first_of("/\\:")!=std::string::npos||name=="."||name=="..")
			throw std::runtime_error("Invalid diagnostic entry name");
		if(contents.size()>file_limit||contents.size()>total_limit-bytes_){note(name,"omitted_size_limit");return;}
		for(const auto& file:files_)if(file.first==name)throw std::runtime_error("Duplicate diagnostic entry");
		std::ofstream output(staging_/name,std::ios::binary|std::ios::trunc);
		output.write(contents.data(),static_cast<std::streamsize>(contents.size()));output.close();
		if(!output)throw std::runtime_error("Diagnostic file write failed");
		manifest_.push_back({{"entry",name},{"state","included"},{"bytes",contents.size()}});
		bytes_+=contents.size();files_.emplace_back(name,std::move(contents));
	}
	void archive::include(const std::filesystem::path& source,const std::string& name)
	{
		std::error_code error;
		if(!std::filesystem::is_regular_file(std::filesystem::symlink_status(source,error))||error)
		{note(name,"missing_or_nonregular");return;}
		const auto size=std::filesystem::file_size(source,error);
		if(error||size>file_limit||size>total_limit-bytes_){note(name,"unreadable_or_size_limit");return;}
		std::ifstream input(source,std::ios::binary);
		if(!input){note(name,"unreadable");return;}
		std::string contents(static_cast<std::size_t>(size)+1,'\0');
		input.read(contents.data(),static_cast<std::streamsize>(contents.size()));
		const auto count=input.gcount();
		if(input.bad()||count<0||static_cast<std::uintmax_t>(count)!=size){note(name,"unreadable_or_changed_during_copy");return;}
		contents.resize(static_cast<std::size_t>(count));
		const auto modified=std::filesystem::last_write_time(source,error);
		manifest_.push_back({{"entry",name},{"source","existing_file_association_not_assumed"},
			{"modified_file_clock_ticks",error?0:modified.time_since_epoch().count()}});
		add(name,std::move(contents));
	}
	std::filesystem::path archive::finish()
	{
		const auto manifest=manifest_.dump(2,' ',false,nlohmann::json::error_handler_t::replace);
		utils::compression::zip::archive zip;
		for(const auto& file:files_)zip.add(file.first,file.second);
		zip.add("manifest.json",manifest);
		const auto partial=staging_/"package.partial";
		if(!zip.write_file(partial,"Overlord diagnostics")||!MoveFileExW(partial.c_str(),destination_.c_str(),MOVEFILE_WRITE_THROUGH))
			throw std::runtime_error("Diagnostic ZIP could not be finalized; captured text remains in the capture folder");
		std::error_code ignored;
		for(const auto& file:files_)std::filesystem::remove(staging_/file.first,ignored);
		std::filesystem::remove(staging_,ignored);
		return destination_;
	}
}
