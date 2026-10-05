#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "scheduler.hpp"
#include "updater.hpp"
#include "game/ui_scripting/execution.hpp"
#include "console.hpp"
#include "command.hpp"
#include "database.hpp"
#include "config.hpp"

#include "version.h"

#include "game/game.hpp"
#include "game/dvars.hpp"

#include <utils/nt.hpp>
#include <utils/concurrency.hpp>
#include <utils/http.hpp>
#include <utils/latest_task_worker.hpp>
#include <utils/thread.hpp>
#include <utils/cryptography.hpp>
#include <utils/io.hpp>
#include <utils/string.hpp>
#include <utils/properties.hpp>

#define FILES_PATH "files.json"
#define FILES_PATH_DEV "files-dev.json"

#define DATA_PATH "data/"
#define DATA_PATH_DEV "data-dev/"

#define ERR_UPDATE_CHECK_FAIL "Failed to check for updates!\nMake sure your ISP is not blocking our backend (try using a VPN).\nCheck the console for more information."
#define ERR_DOWNLOAD_FAIL "Failed to download file "
#define ERR_WRITE_FAIL "Failed to write file "

constexpr auto portable_vr_no_auto_update_marker = "h2-mod-portable-vr.no-auto-update";

namespace updater
{
	namespace
	{
		// VR builds are distributed through this project's GitHub Releases.
		// Keep the update transport unconfigured until a VR-owned service exists;
		// the upstream service may replace VR binaries and remove VR resources.
		const std::vector<std::string> server_urls{};

		game::dvar_t* cl_auto_update;
		bool has_tried_update = false;

		struct status
		{
			bool done;
			bool success;
		};

		struct file_data
		{
			std::string name;
			std::string data;
		};

		struct file_info
		{
			std::string name;
			std::string hash;
		};

		struct update_data_t
		{
			std::uint64_t generation{};
			bool restart_required{};
			bool cancelled{};
			bool downloading{};
			status check{};
			status download{};
			std::string error{};
			std::string current_file{};
			std::vector<file_info> required_files{};
			std::vector<std::string> garbage_files{};
		};

		utils::concurrency::container<update_data_t> update_data;
		std::atomic_uint64_t request_generation{};
		utils::latest_task_worker update_worker;
		using request_id = std::uint64_t;

		bool request_cancelled(request_id id) noexcept
		{
			return update_worker.stopping() || request_generation.load(std::memory_order_acquire) != id;
		}

		template <typename F>
		bool publish(request_id id, F&& apply)
		{
			return update_data.access<bool>([&](update_data_t& data)
			{
				if (request_cancelled(id) || data.generation != id || data.cancelled) return false;
				apply(data);
				return true;
			});
		}

		std::unordered_map<std::string, git_branch> git_branches =
		{
			{"develop", branch_develop},
			{"main", branch_main},
		};

		std::string get_branch_name(const git_branch branch)
		{
			for (const auto& [name, b] : git_branches)
			{
				if (branch == b)
				{
					return name;
				}
			}

			throw std::runtime_error("invalid branch");
		}

		std::string select(const std::string& main, const std::string& develop)
		{
			switch (updater::get_current_branch())
			{
			case branch_develop:
				return develop;
			case branch_main:
				return main;
			}

			return main;
		}

		std::string load_binary_name()
		{
			utils::nt::library self;
			return self.get_name();
		}

		std::string get_binary_name()
		{
			static const auto name = load_binary_name();
			return name;
		}

		void notify(request_id id, const std::string& name)
		{
			scheduler::once([=]()
			{
				if (!request_cancelled(id)) ui_scripting::notify(name, {});
			}, scheduler::pipeline::lui);
		}

		void set_update_check_status(request_id id, bool done, bool success, const std::string& error = {})
		{
			publish(id, [&](update_data_t& data_)
			{
				data_.check.done = done;
				data_.check.success = success;
				data_.error = error;

				notify(id, "update_check_done");
			});
		}

		void set_update_download_status(request_id id, bool done, bool success, const std::string& error = {})
		{
			publish(id, [&](update_data_t& data_)
			{
				data_.download.done = done;
				data_.download.success = success;
				data_.downloading = false;
				data_.error = error;
				notify(id, "update_done");
			});
		}

		bool check_file(const std::string& name, const std::string& sha)
		{
			std::string data;

			if (get_binary_name() == name)
			{
				if (!utils::io::read_file(name, &data))
				{
					return false;
				}
			}
			else
			{
				const auto appdata_folder = utils::properties::get_appdata_path();
				const auto path = (appdata_folder / name).generic_string();
				if (!utils::io::read_file(path, &data))
				{
					return false;
				}
			}

			if (utils::cryptography::sha1::compute(data, true) != sha)
			{
				return false;
			}

			return true;
		}

		std::string get_time_str()
		{
			return utils::string::va("%i", uint32_t(time(nullptr)));
		}

		std::optional<std::string> server_file(const std::string& endpoint, const utils::http::request_options& options)
		{
			for (const auto& base_url : server_urls)
			{
				if (options.cancelled && options.cancelled()) return {};
				const auto url = base_url + endpoint;
				console::debug("[HTTP] GET file \"%s\"\n", url.data());
				auto result = utils::http::get_data(url, {}, {}, options);
				if (result.has_value()) return result;
				if (options.cancelled && options.cancelled()) return {};
				console::error("[updater] failed to get file \"%s\" from server \"%s\"\n", endpoint.data(), base_url.data());
			}
			return {};
		}

		utils::http::request_options options_for(request_id id, bool payload)
		{
			utils::http::request_options options;
			// Large fastfiles get a finite transfer budget; metadata is small.
			if (payload) options.timeout = std::chrono::minutes(15);
			else options.max_response_bytes = 4 * 1024 * 1024;
			options.cancelled = [id] { return request_cancelled(id); };
			return options;
		}

		std::optional<std::string> download_data_file(request_id id, const std::string& name)
		{
			const auto file = std::format("{}{}?{}", select(DATA_PATH, DATA_PATH_DEV), name, get_time_str());
			return server_file(file, options_for(id, true));
		}

		std::optional<std::string> download_file_list(request_id id)
		{
			const auto file = std::format("{}?{}", select(FILES_PATH, FILES_PATH_DEV), get_time_str());
			return server_file(file, options_for(id, false));
		}

		bool write_file(const std::string& name, const std::string& data)
		{
			if (get_binary_name() == name && 
				utils::io::file_exists(name) && 
				!utils::io::move_file(name, name + ".old"))
			{
				return false;
			}

			if (get_binary_name() == name)
			{
				return utils::io::write_file(name, data);
			}
			else
			{
				const auto appdata_folder = utils::properties::get_appdata_path();
				const auto path = (appdata_folder / name).generic_string();
				return utils::io::write_file(path, data);
			}
		}

		void delete_old_file()
		{
			utils::io::remove_file(get_binary_name() + ".old");
		}

		std::vector<std::string> find_garbage_files(request_id id,
			const std::vector<std::string>& update_files, bool& restart_required)
		{
			std::vector<std::string> garbage_files{};

			const auto appdata_folder = utils::properties::get_appdata_path();
			const auto path = (appdata_folder / CLIENT_DATA_FOLDER).generic_string();
			if (!utils::io::directory_exists(path))
			{
				return {};
			}

			const auto current_files = utils::io::list_files_recursively(path);
			for (const auto& file : current_files)
			{
				if (request_cancelled(id)) return {};
				bool found = false;
				for (const auto& update_file : update_files)
				{
					const auto update_file_ = (appdata_folder / update_file).generic_string();
					const auto path_a = std::filesystem::path(file);
					const auto path_b = std::filesystem::path(update_file_);
					const auto is_directory = utils::io::directory_exists(file);
					const auto compare = path_a.compare(path_b);

					if ((is_directory && compare == -1) || compare == 0)
					{
						found = true;
						break;
					}
				}

				if (!found)
				{
#ifdef DEBUG
					console::info("[Updater] Found extra file %s\n", file.data());
#endif
					if (file.ends_with(".ff"))
					{
						restart_required = true;
					}

					garbage_files.push_back(file);
				}
			}

			return garbage_files;
		}

		void perform_update_check(request_id id)
		{
			utils::thread::set_name("Updater");
			if (request_cancelled(id)) return;
			const auto files_data = download_file_list(id);

			if (request_cancelled(id)) return;

			if (!files_data.has_value())
			{
				set_update_check_status(id, true, false, ERR_UPDATE_CHECK_FAIL);
				return;
			}

			rapidjson::Document j;
			j.Parse(files_data.value().data());

			if (!j.IsArray())
			{
				set_update_check_status(id, true, false, ERR_UPDATE_CHECK_FAIL);
				return;
			}

			bool restart_required{};
			std::vector<file_info> required_files;
			std::vector<std::string> update_files;

			const auto files = j.GetArray();
			for (const auto& file : files)
			{
				if (request_cancelled(id)) return;
				if (!file.IsArray() || file.Size() != 3 || !file[0].IsString() || !file[2].IsString())
				{
					continue;
				}

				const auto name = file[0].GetString();
				const auto sha = file[2].GetString();

				update_files.push_back(name);

				if (!check_file(name, sha))
				{
					if (get_binary_name() == name)
					{
						restart_required = true;
					}

					std::string name_ = name;
					if (name_.ends_with(".ff"))
					{
						restart_required = true;
					}

#ifdef DEBUG
					console::info("[Updater] need file %s\n", name);
#endif

					required_files.emplace_back(name, sha);
				}
			}

			const auto garbage_files = find_garbage_files(id, update_files, restart_required);

			publish(id, [&](update_data_t& data_)
			{
				data_.restart_required = restart_required;
				data_.check.done = true;
				data_.check.success = true;
				data_.required_files = std::move(required_files);
				data_.garbage_files = garbage_files;
				notify(id, "update_check_done");
			});
		}

		void perform_update_download(request_id id, const std::vector<file_info>& required_files,
			const std::vector<std::string>& garbage_files)
		{
			utils::thread::set_name("Updater");
			for (const auto& file : garbage_files)
			{
				if (request_cancelled(id)) return;
				try { std::filesystem::remove_all(file); }
				catch (...) { console::error("Failed to delete %s\n", file.data()); }
			}
			std::vector<file_data> downloads;

			for (const auto& file : required_files)
			{
				if (request_cancelled(id)) return;
				publish(id, [&](update_data_t& data_)
				{
					data_.current_file = file.name;
				});

#ifdef DEBUG
				console::info("[Updater] downloading file %s\n", file.name.data());
#endif

				auto data = download_data_file(id, file.name);

				if (request_cancelled(id)) return;

				if (!data.has_value())
				{
					set_update_download_status(id, true, false, ERR_DOWNLOAD_FAIL + file.name);
					return;
				}

				const auto& value = data.value();
				if (file.hash != utils::cryptography::sha1::compute(value, true))
				{
					set_update_download_status(id, true, false, ERR_DOWNLOAD_FAIL + file.name);
					return;
				}

				downloads.emplace_back(file.name, std::move(data.value()));
			}

			for (const auto& download : downloads)
			{
				if (request_cancelled(id)) return;
				if (!write_file(download.name, download.data))
				{
					set_update_download_status(id, true, false, ERR_WRITE_FAIL + download.name);
					return;
				}
			}

			set_update_download_status(id, true, true);
		}

		void task_failed(request_id id, bool checking, std::exception_ptr error)
		{
			if (request_cancelled(id)) return;
			std::string detail = "unknown error";
			try { std::rethrow_exception(error); }
			catch (const std::exception& e) { detail = e.what(); }
			catch (...) {}
			console::error("[Updater] request failed: %s\n", detail.c_str());
			if (checking) set_update_check_status(id, true, false, ERR_UPDATE_CHECK_FAIL);
			else set_update_download_status(id, true, false, ERR_DOWNLOAD_FAIL + detail);
		}
	}

	std::optional<std::string> get_server_file(const std::string& endpoint)
	{
		utils::http::request_options options;
		options.cancelled = [] { return update_worker.stopping(); };
		return server_file(endpoint, options);
	}

	void relaunch()
	{
		utils::nt::relaunch_self("-singleplayer");
		utils::nt::terminate();
	}

	void set_has_tried_update(bool tried)
	{
		has_tried_update = tried;
	}

	bool get_has_tried_update()
	{
		return has_tried_update;
	}

	bool auto_updates_enabled()
	{
		if (!updates_available()) return false;
		if (!cl_auto_update->current.enabled)
		{
			return false;
		}

		const utils::nt::library self;
		if (self)
		{
			const auto marker = std::filesystem::path(self.get_folder()) / portable_vr_no_auto_update_marker;
			if (utils::io::file_exists(marker.generic_string()))
			{
				return false;
			}
		}

		return true;
	}

	bool is_update_check_done()
	{
		return update_data.access<bool>([](update_data_t& data_)
		{
			return data_.check.done;
		});
	}

	bool is_update_download_done()
	{
		return update_data.access<bool>([](update_data_t& data_)
		{
			return data_.download.done;
		});
	}

	bool get_update_check_status()
	{
		return update_data.access<bool>([](update_data_t& data_)
		{
			return data_.check.success;
		});
	}

	bool get_update_download_status()
	{
		return update_data.access<bool>([](update_data_t& data_)
		{
			return data_.download.success;
		});
	}

	bool is_update_available()
	{
		return update_data.access<bool>([](update_data_t& data_)
		{
			return data_.required_files.size() > 0 || data_.garbage_files.size() > 0;
		});
	}

	bool is_restart_required()
	{
		return update_data.access<bool>([](update_data_t& data_)
		{
			return data_.restart_required;
		});
	}

	std::string get_last_error()
	{
		return update_data.access<std::string>([](update_data_t& data_)
		{
			return data_.error;
		});
	}

	std::string get_current_file()
	{
		return update_data.access<std::string>([](update_data_t& data_)
		{
			return data_.current_file;
		});
	}

	void cancel_update()
	{
#ifdef DEBUG
		console::info("[Updater] Cancelling update\n");
#endif

		return update_data.access([](update_data_t& data_)
		{
			data_.cancelled = true;
			request_generation.fetch_add(1, std::memory_order_acq_rel);
			update_worker.cancel_pending();
		});
	}

	void start_update_check()
	{
		if (!updates_available()) return;
		update_data.access([](update_data_t& data)
		{
			if (update_worker.stopping()) return;
			const auto id = request_generation.fetch_add(1, std::memory_order_acq_rel) + 1;
			data = {};
			data.generation = id;
			update_worker.replace([id] { perform_update_check(id); },
				[id](std::exception_ptr error) { task_failed(id, true, error); });
		});
	}

	void start_update_download()
	{
		if (!updates_available()) return;
		update_data.access([](update_data_t& data)
		{
			const auto id = data.generation;
			if (!id || request_cancelled(id) || data.cancelled || data.downloading ||
				!data.check.done || !data.check.success || (data.download.done && data.download.success)) return;
			// Native file-handle closure retains its original caller-thread boundary.
			if (data.restart_required) database::close_fastfile_handles();
			data.downloading = true;
			data.download = {};
			data.error.clear();
			update_worker.replace([id, files = data.required_files, garbage = data.garbage_files]
			{
				perform_update_download(id, files, garbage);
			}, [id](std::exception_ptr error) { task_failed(id, false, error); });
		});
	}

	bool should_force_update()
	{
		if (!updates_available()) return false;
		const auto folder = (utils::properties::get_appdata_path() / CLIENT_DATA_FOLDER).generic_string();
		return !utils::io::directory_exists(folder) || utils::io::directory_is_empty(folder);
	}

	bool is_valid_git_branch(const std::string& branch)
	{
		return git_branches.contains(branch);
	}

	bool updates_available()
	{
		return !server_urls.empty();
	}

	std::string get_git_branch()
	{
		return GIT_BRANCH;
	}

	git_branch get_current_branch()
	{
		const auto get_branch_name = []()
			-> std::string
		{
			const auto branch_opt = config::get<std::string>("branch");
			if (!branch_opt.has_value())
			{
				return GIT_BRANCH;
			}

			return branch_opt.value();
		};

		const auto branch_name = get_branch_name();
		return git_branches.at(branch_name);
	}

	void set_branch(const git_branch branch)
	{
		if (branch >= branch_count)
		{
			return;
		}

		const auto name = get_branch_name(branch);
		config::set("branch", name);
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			if (updates_available()) delete_old_file();
			cl_auto_update = dvars::register_bool("cg_auto_update", true, 
				game::DVAR_FLAG_SAVED, "Automatically check for updates on launch");
		}

		void pre_destroy() override
		{
			cancel_update();
			update_worker.stop();
		}
	};
}

REGISTER_COMPONENT(updater::component)
