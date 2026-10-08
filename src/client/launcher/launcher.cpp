#include <std_include.hpp>
#include "launcher.hpp"
#include "webview_window.hpp"
#include "services.hpp"
#include "game_language.hpp"
#include "launch_process.hpp"
#include "component/game_data.hpp"
#include "product.hpp"
#include <utils/flags.hpp>
#include <utils/io.hpp>

struct launcher::impl
{
	using json = nlohmann::json;
	webview_window window_;
	launcher_bridge::services services_;
	launcher_game_language::service languages_;
	std::string session_;
	bool language_busy_{};
	std::optional<bool> language_available_;
	bool settings_loaded_{};
	bool launched_{};
	std::optional<int> pending_launch_;
	std::optional<std::string> probe_path_ = utils::flags::get_flag("launcher-smoke");
	ULONGLONG started_ = GetTickCount64();
	bool probe_failed_{};
	bool probe_complete_{};

	impl()
	{
		window_.set_quiet_failure(probe_path_.has_value());
		if (const auto development_uri = utils::flags::get_flag("launcher-dev-url")) window_.set_uri(*development_uri);
		window_.set_message_handler([this](const json& message) { receive(launcher_bridge::parse(message.dump())); });
		window_.set_callback([this](window* owner, UINT message, WPARAM w_param, LPARAM l_param)
		{
			if (message == WM_SIZE && !session_.empty())
				window_.send(json{{"session", session_}, {"event", "window.state"}, {"state", {{"maximized", window_.is_maximized()}}}});
			if (message == WM_TIMER)
			{
				if (probe_path_ && !probe_complete_ && GetTickCount64() - started_ > 20000)
				{
					probe_failed_ = true; probe_complete_ = true;
					utils::io::write_file_atomic(*probe_path_, R"({"ok":false,"error":"Renderer startup timed out"})");
					PostMessageW(window_, WM_CLOSE, 0, 0);
				}
				for (auto response : services_.drain())
				{
					if (response.at("session") != session_) continue;
					auto& result = response.at("result");
					if (result.contains("settings")) settings_loaded_ = result.at("settings").value("ok", false);
					if (pending_launch_ && response.at("id") == *pending_launch_)
					{
						pending_launch_.reset();
						if (result.value("ok", false) && result.value("approved", false))
						{
							try
							{
								launcher_process::start_game(); launched_ = true;
								result = {{"ok", true}, {"launched", true}};
							}
							catch (const std::exception& error) { result = {{"ok", false}, {"error", error.what()}}; }
						}
						else result.erase("approved");
					}
					window_.send(response);
					if (launched_) PostMessageW(window_, WM_CLOSE, 0, 0);
				}
				return LRESULT{0};
			}
			return DefWindowProcW(*owner, message, w_param, l_param);
		});
		int width = 1040, height = 720;
		if (probe_path_)
			if (const auto size = utils::flags::get_flag("launcher-probe-size"))
			{
				std::istringstream input(*size); char separator{};
				if (!(input >> width >> separator >> height) || separator != 'x' || !(input >> std::ws).eof()
					|| width < 640 || width > 3840 || height < 480 || height > 2160) throw std::runtime_error("Invalid renderer probe size.");
			}
		window_.create(product::name, width, height);
		SetTimer(window_, 1, 30, nullptr);
		if (!probe_path_) window_.show();
	}

	json language_result(const std::string& raw)
	{
		auto result = json::parse(raw);
		language_busy_ = result.value("pending", false);
		if (!language_busy_ && result.value("ok", false))
		{
			const auto selected = result.at("language");
			language_available_ = std::any_of(result.at("choices").begin(), result.at("choices").end(), [&](const auto& choice) { return choice.at("value") == selected; });
		}
		return result;
	}

	void receive(const launcher_bridge::request& request)
	{
		if (request.method == "bootstrap") { session_ = request.session; settings_loaded_ = false; pending_launch_.reset(); }
		const bool window_request = request.method == "window.state" || request.method == "window.control";
		if (session_ != request.session && !window_request) return;
		try
		{
			if (pending_launch_ && !window_request) throw std::runtime_error("launcher.busy");
			const auto& params = request.params;
			json result{{"ok", true}};
			if (request.method == "renderer.ready")
			{
				if (!probe_path_ || probe_complete_ || game_data::is_game_directory_available()) throw std::runtime_error("Renderer probing requires a separate directory without game binaries.");
				if (!params.contains("ok") || !params.at("ok").is_boolean()) throw std::runtime_error("Invalid renderer probe report.");
				RECT outer{}, client{};
				GetWindowRect(window_, &outer); GetClientRect(window_, &client);
				const bool borderless = outer.right - outer.left == client.right && outer.bottom - outer.top == client.bottom;
				const auto hit = SendMessageW(window_, WM_NCHITTEST, 0, MAKELPARAM(outer.left + 1, outer.top + 1));
				auto report = params;
				report["frame"] = {{"borderless", borderless}, {"resizeCorner", hit == HTTOPLEFT}};
				report["ok"] = params.value("ok", false) && borderless && hit == HTTOPLEFT;
				probe_complete_ = true;
				probe_failed_ = !report.value("ok", false);
				if (!utils::io::write_file_atomic(*probe_path_, report.dump(2))) probe_failed_ = true;
				PostMessageW(window_, WM_CLOSE, 0, 0);
			}
			else if (request.method == "window.control")
			{
				if (params.size() != 1 || !params.contains("action") || !params.at("action").is_string()) throw std::runtime_error("Invalid window control.");
				window_.request_control(params.at("action").get<std::string>());
			}
			else if (request.method == "window.state")
			{
				if (!params.empty()) throw std::runtime_error("Invalid window state request.");
				result["maximized"] = window_.is_maximized();
			}
			else if (request.method == "languages.scan" || request.method == "languages.poll")
			{
				if (!params.empty()) throw std::runtime_error("Invalid language parameters.");
				result = language_result(request.method == "languages.scan" ? languages_.scan() : languages_.poll());
			}
			else if (request.method == "languages.save")
			{
				if (params.size() != 1 || !params.contains("language") || !params.at("language").is_string() || language_busy_) throw std::runtime_error("language.gameScanning");
				result = language_result(languages_.save(params.at("language").get<std::string>()));
			}
			else if (request.method == "links.open")
			{
				if (params.size() != 1 || !params.contains("id") || !params.at("id").is_string()) throw std::runtime_error("Invalid project link.");
				const auto id = params.at("id").get<std::string>();
				const std::string url = id == "project" ? product::repository_url : id == "releases" ? product::releases_url : "";
				if (url.empty()) throw std::runtime_error("Unsupported project link.");
				const std::wstring destination(url.begin(), url.end());
				if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", destination.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32) throw std::runtime_error("Could not open the project link.");
			}
			else if (request.method == "game.launch")
			{
				if (probe_path_) throw std::runtime_error("Game startup is unavailable in renderer probe mode.");
				if (params.size() != 1 || !params.contains("warnings") || !params.at("warnings").is_array() || params.at("warnings").size() > 2)
					throw std::runtime_error("Invalid startup parameters.");
				for (const auto& id : params.at("warnings"))
					if (!id.is_string() || (id != "shaders" && id != "shadows")) throw std::runtime_error("Invalid startup acknowledgement.");
				if (launched_ || !services_.idle() || !settings_loaded_) throw std::runtime_error("launcher.busy");
				if (language_busy_) throw std::runtime_error("language.gameScanning");
				if (language_available_ == false) throw std::runtime_error("language.gameCurrentUnavailable");
				// Keep the fresh launch gate on the ordered file worker. The UI
				// thread only spawns after approval and cannot enqueue mutations
				// in between; closing/reloading the window discards that approval.
				services_.enqueue(request);
				pending_launch_ = request.id;
				return;
			}
			else { services_.enqueue(request); return; }
			window_.send(launcher_bridge::response(request, std::move(result)));
			if (launched_) PostMessageW(window_, WM_CLOSE, 0, 0);
		}
		catch (const std::exception& error) { window_.send(launcher_bridge::response(request, json{{"ok", false}, {"error", error.what()}})); }
	}
};

launcher::launcher() : impl_(std::make_unique<impl>()) {}
launcher::~launcher() = default;
int launcher::run() const
{
	window::run();
	if (impl_->probe_path_ && impl_->window_.failed()) utils::io::write_file_atomic(*impl_->probe_path_, R"({"ok":false,"error":"WebView2 startup failed"})");
	return impl_->window_.failed() || impl_->probe_failed_ ? 1 : 0;
}
