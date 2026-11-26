#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/dvars.hpp"
#include "game/game.hpp"

#include "filesystem.hpp"
#include "console.hpp"
#include "command.hpp"
#include "language.hpp"

#include <utils/io.hpp>
#include <utils/hook.hpp>
#include <utils/string.hpp>
#include <utils/flags.hpp>

#include <mutex>

namespace database
{
	namespace
	{
		struct bnet_file_handle_t
		{
			std::unique_ptr<std::ifstream> stream;
			uint64_t offset{};
			uint64_t size{};
			std::string path;
		};

		std::unordered_map<game::DB_IFileSysFile*, bnet_file_handle_t> bnet_file_handles{};
		std::mutex bnet_file_handles_mutex{};
		utils::hook::detour bnet_fs_open_file_hook;
		utils::hook::detour bnet_fs_read_hook;
		utils::hook::detour bnet_fs_tell_hook;
		utils::hook::detour bnet_fs_size_hook;
		utils::hook::detour bnet_fs_close_hook;
		utils::hook::detour bnet_fs_exists_hook;
		utils::hook::detour bink_io_read_hook;
		utils::hook::detour bink_io_seek_hook;

		utils::hook::detour open_sound_handle_hook;

		utils::memory::allocator handle_allocator;

		game::dvar_t* db_filesysImpl = nullptr;
		utils::hook::detour db_fs_initialize_hook;

		std::unordered_map<std::string, std::string> file_search_folders =
		{
			{".flac", "sound/"},
			{".bik", "video/"},
		};

		game::DB_FileSysInterface* db_fs_initialize_stub()
		{
			switch (db_filesysImpl->current.integer)
			{
			case 0:
				return reinterpret_cast<game::DB_FileSysInterface*>(0x140BE82F8); // ptr to vtable of BnetTACTVFSManager (implements DB_FileSysInterface)
			case 1:
				return reinterpret_cast<game::DB_FileSysInterface*>(0x140BEFDC0); // ptr to vtable of DiskFS (implements DB_FileSysInterface)
			default:
				return nullptr; // this should not happen
			}
		}

		std::optional<std::string> find_fastfile(const std::string& name)
		{
			std::string name_ = name;

			if (game::DB_IsLocalized(name.data()))
			{
				const auto language = game::SEH_GetCurrentLanguageName();
				if (filesystem::exists(name))
				{
					return {name};
				}

				name_ = language + "/"s + name;
				if (filesystem::exists(name_))
				{
					return {name_};
				}

				name_ = "zone/" + name;
				if (filesystem::exists(name_))
				{
					return {name_};
				}

				name_ = "zone/"s + language + "/" + name;
				if (filesystem::exists(name_))
				{
					return {name_};
				}
			}
			else
			{
				if (filesystem::exists(name))
				{
					return {name};
				}

				name_ = "zone/" + name;
				if (filesystem::exists(name_))
				{
					return {name_};
				}
			}

			return {};
		}

		bool is_zone_file(const std::string& file)
		{
			return file.ends_with(".ff") || file.ends_with(".pak");
		}

		bool is_loc_folder(game::Sys_Folder folder)
		{
			return folder == game::SF_PAKFILE_LOC || folder == game::SF_ZONE_LOC || folder == game::SF_VIDEO_LOC;
		}

		game::DB_IFileSysFile* bnet_fs_open_file_stub(game::DB_FileSysInterface* this_, 
			game::Sys_Folder folder, const char* file)
		{
			std::string name = file;
			std::string file_ = file;

			const auto search_path = [&](const std::string& ext, const std::string& path)
			{
				if (name.ends_with(ext) && !filesystem::exists(name))
				{
					name = path + name;
				}
			};

			if (is_zone_file(name))
			{
				if (is_loc_folder(folder))
				{
					const auto loc = name.substr(0, 3);
					const auto found = find_fastfile(name);
					if (language::is_custom_language(loc) && !found.has_value())
					{
						name = "eng" + name.substr(3);
						file_ = name;
					}
				}

				const auto found = find_fastfile(name);
				if (found.has_value())
				{
					name = found.value();
				}
			}
			else
			{
				for (const auto& [ext, path] : file_search_folders)
				{
					search_path(ext, path);
				}
			}

			std::string path{};
			if (!filesystem::find_file(name, &path))
			{
				return bnet_fs_open_file_hook.invoke<game::DB_IFileSysFile*>(this_, folder, file_.data());
			}

			try
			{
#ifdef DEBUG
				console::info("[Database] Opening file %s\n", path.data());
#endif

				auto stream = std::make_unique<std::ifstream>();
				stream->open(path, std::ios::binary);

				uint64_t file_size{};
				if (stream->is_open())
				{
					stream->seekg(0, std::ios::end);
					const auto end_pos = static_cast<std::streamoff>(stream->tellg());
					stream->seekg(0, std::ios::beg);
					file_size = (end_pos > 0) ? static_cast<uint64_t>(end_pos) : uint64_t{};
				}

				bnet_file_handle_t bnet_handle{};
				bnet_handle.stream = std::move(stream);
				bnet_handle.size = file_size;
				bnet_handle.path = path;

				std::lock_guard lock(bnet_file_handles_mutex);
				const auto handle = handle_allocator.allocate<game::DB_IFileSysFile>();
				if (!handle)
				{
					return nullptr;
				}
				try
				{
					bnet_file_handles.emplace(handle, std::move(bnet_handle));
				}
				catch (...)
				{
					handle_allocator.free(handle);
					throw;
				}
				return handle;
			}
			catch (const std::exception& e)
			{
				console::error("[Database] Error opening file %s: %s\n", path.data(), e.what());
			}

			return nullptr;
		}

		game::FileSysResult bnet_fs_read_stub(game::DB_FileSysInterface* this_, game::DB_IFileSysFile* handle,
			unsigned __int64 offset, unsigned __int64 size, void* dest)
		{
			std::unique_lock lock(bnet_file_handles_mutex);
			const auto it = bnet_file_handles.find(handle);
			if (it == bnet_file_handles.end())
			{
				lock.unlock();
				return bnet_fs_read_hook.invoke<game::FileSysResult>(this_, handle, offset, size, dest);
			}

			auto& handle_ = it->second;
			if (!handle_.stream->is_open())
			{
				return game::FILESYSRESULT_ERROR;
			}

			try
			{
				const auto start_pos = offset - handle_.offset;
				if (start_pos >= handle_.size)
				{
					handle->last_read = 0;
					return game::FILESYSRESULT_SUCCESS;
				}

				handle_.stream->clear();
				handle_.stream->seekg(static_cast<std::streamoff>(start_pos), std::ios::beg);

				const auto len = handle_.size - start_pos;
				const auto bytes_to_read = len <= size ? len : size;

				const auto& res = handle_.stream->read(reinterpret_cast<char*>(dest), static_cast<std::streamsize>(bytes_to_read));
				if (res.bad())
				{
					return game::FILESYSRESULT_ERROR;
				}

				const auto bytes_read = static_cast<uint64_t>(res.gcount());
				handle->bytes_read += bytes_read;
				handle->last_read = bytes_read;

				return game::FILESYSRESULT_SUCCESS;
			}
			catch (const std::exception& e)
			{
				console::error("[Database] bnet_fs_read_stub: %s\n", e.what());
				return game::FILESYSRESULT_ERROR;
			}
		}

		game::FileSysResult bnet_fs_tell_stub(game::DB_FileSysInterface* this_, game::DB_IFileSysFile* handle, uint64_t* bytes_read)
		{
			std::unique_lock lock(bnet_file_handles_mutex);
			const auto it = bnet_file_handles.find(handle);
			if (it == bnet_file_handles.end())
			{
				lock.unlock();
				return bnet_fs_tell_hook.invoke<game::FileSysResult>(this_, handle, bytes_read);
			}

			auto& handle_ = it->second;
			if (!handle_.stream->is_open())
			{
				return game::FILESYSRESULT_ERROR;
			}

			try
			{
				*bytes_read = handle->last_read;
				return game::FILESYSRESULT_SUCCESS;
			}
			catch (const std::exception& e)
			{
				console::error("[Database] bnet_fs_tell_stub: %s\n", e.what());
				return game::FILESYSRESULT_ERROR;
			}
		}

		uint64_t bnet_fs_size_stub(game::DB_FileSysInterface* this_, game::DB_IFileSysFile* handle)
		{
			std::unique_lock lock(bnet_file_handles_mutex);
			const auto it = bnet_file_handles.find(handle);
			if (it == bnet_file_handles.end())
			{
				lock.unlock();
				return bnet_fs_size_hook.invoke<uint64_t>(this_, handle);
			}

			return it->second.size;
		}

		void bnet_fs_close_stub(game::DB_FileSysInterface* this_, game::DB_IFileSysFile* handle)
		{
			std::unique_lock lock(bnet_file_handles_mutex);
			const auto it = bnet_file_handles.find(handle);
			if (it == bnet_file_handles.end())
			{
				lock.unlock();
				bnet_fs_close_hook.invoke<uint64_t>(this_, handle);
				return;
			}

			bnet_file_handles.erase(it);
			handle_allocator.free(handle);
		}

		bool bnet_fs_exists_stub(game::DB_FileSysInterface* this_, game::DB_IFileSysFile* handle, const char* filename)
		{
			std::string name = filename;

			const auto search_path = [&](const std::string& ext, const std::string& path)
			{
				if (!name.ends_with(ext))
				{
					return false;
				}

				return filesystem::exists(name) || filesystem::exists(path + name);
			};

			if (filesystem::exists(filename))
			{
				return true;
			}

			if (is_zone_file(name))
			{
				const auto found = find_fastfile(name);
				if (found.has_value())
				{
					return true;
				}
			}
			else
			{
				for (const auto& [ext, path] : file_search_folders)
				{
					if (search_path(ext, path))
					{
						return true;
					}
				}
			}

			return bnet_fs_exists_hook.invoke<bool>(this_, handle, filename);
		}

		uint64_t bink_io_read_stub(game::DB_IFileSysFile** handle, void* dest, uint64_t bytes)
		{
			const auto handle_ptr = *handle;
			std::unique_lock lock(bnet_file_handles_mutex);
			const auto it = bnet_file_handles.find(handle_ptr);
			if (it == bnet_file_handles.end())
			{
				lock.unlock();
				return bink_io_read_hook.invoke<uint64_t>(handle, dest, bytes);
			}

			auto& handle_ = it->second;
			if (!handle_.stream->is_open())
			{
				return 0;
			}

			try
			{
				const auto& res = handle_.stream->read(reinterpret_cast<char*>(dest), static_cast<std::streamsize>(bytes));
				return static_cast<uint64_t>(res.gcount());
			}
			catch (const std::exception& e)
			{
				console::error("[Database] bink_io_read_stub: %s\n", e.what());
				return 0;
			}
		}

		bool bink_io_seek_stub(game::DB_IFileSysFile** handle, uint64_t pos)
		{
			const auto handle_ptr = *handle;
			std::unique_lock lock(bnet_file_handles_mutex);
			const auto it = bnet_file_handles.find(handle_ptr);
			if (it == bnet_file_handles.end())
			{
				lock.unlock();
				return bink_io_seek_hook.invoke<bool>(handle, pos);
			}

			auto& handle_ = it->second;
			if (!handle_.stream->is_open())
			{
				return false;
			}

			try
			{
				if (!handle_.stream->bad())
				{
					handle_.stream->clear();
				}
				const auto& res = handle_.stream->seekg(static_cast<std::streamoff>(pos));
				return !(res.fail() || res.bad());
			}
			catch (const std::exception& e)
			{
				console::error("[Database] bink_io_seek_stub: %s\n", e.what());
				return false;
			}
		}

		utils::hook::detour sys_set_folder_hook;
		void sys_set_folder_stub(game::Sys_Folder folder, const char* path)
		{
			if (is_loc_folder(folder) && language::is_custom_language(path))
			{
				path = "english";
			}

			sys_set_folder_hook.invoke<void>(folder, path);
		}
	}

	void close_fastfile_handles()
	{
		std::lock_guard lock(bnet_file_handles_mutex);
		for (const auto& handle : bnet_file_handles)
		{
			if (handle.second.path.ends_with(".ff") && handle.second.stream)
			{
				handle.second.stream->close();
			}
		}
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			static const char* values[] = 
			{
				"BnetTACTVFSManager", // (load files from CASC)
				"DiskFS", // (load files from disk)
				nullptr
			};

			const auto default_value = static_cast<int>(!utils::io::directory_exists("Data/data")
				|| !utils::io::directory_exists("Data/config")
				|| !utils::io::directory_exists("Data/indices"));

			db_filesysImpl = dvars::register_enum("db_filesysImpl", values, default_value, game::DVAR_FLAG_READ, "Filesystem implementation");

			if (default_value == 1)
			{
				utils::hook::nop(0x1405A4868, 22); // TACT related stuff that's pointless if we're using DiskFS
				utils::hook::nop(0x14071AF83, 45); // Skip setting Bink file OS callbacks (not necessary since we're loading from disk)
			}
			
			db_fs_initialize_hook.create(game::DB_FSInitialize, db_fs_initialize_stub);

			// Allow bnet filesystem to also load files from disk
			if (db_filesysImpl->current.integer == 0)
			{
				const auto bnet_interface = reinterpret_cast<game::DB_FileSysInterface*>(0x140BE82F8);

				bnet_fs_open_file_hook.create(bnet_interface->vftbl->OpenFile, bnet_fs_open_file_stub);
				bnet_fs_read_hook.create(bnet_interface->vftbl->Read, bnet_fs_read_stub);
				bnet_fs_tell_hook.create(bnet_interface->vftbl->Tell, bnet_fs_tell_stub);
				bnet_fs_size_hook.create(bnet_interface->vftbl->Size, bnet_fs_size_stub);
				bnet_fs_close_hook.create(bnet_interface->vftbl->Close, bnet_fs_close_stub);
				bnet_fs_exists_hook.create(bnet_interface->vftbl->Exists, bnet_fs_exists_stub);

				bink_io_read_hook.create(0x1407191B0, bink_io_read_stub);
				bink_io_seek_hook.create(0x140719200, bink_io_seek_stub);
			}

			sys_set_folder_hook.create(0x140623830, sys_set_folder_stub);

			command::add("extractFile", [](const command::params& params)
			{
				const std::string file = params.get(1);
				const auto fs_inst = game::DB_FSInitialize();
				const auto handle = fs_inst->vftbl->OpenFile(fs_inst, game::SF_ZONE, file.data());
				const auto _0 = gsl::finally([&]
				{
					if (handle != nullptr)
					{
						fs_inst->vftbl->Close(fs_inst, handle);
					}
				});

				if (handle == nullptr)
				{
					return;
				}

				const auto size = fs_inst->vftbl->Size(fs_inst, handle);
				std::string buffer;
				buffer.resize(size);
				fs_inst->vftbl->Read(fs_inst, handle, 0, size, buffer.data());
				utils::io::write_file("bnet/" + file, buffer);
			});
		}
	};
}

REGISTER_COMPONENT(database::component)
