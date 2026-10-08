#include <std_include.hpp>
#include "webview_window.hpp"
#include "bridge_protocol.hpp"
#include <launcher_assets.hpp>
#include <utils/nt.hpp>
#include <utils/properties.hpp>
#include <Shlwapi.h>

namespace
{
	constexpr UINT failure_message = WM_APP + 23;
	void check(const HRESULT result, const char* message)
	{
		if (FAILED(result)) throw std::runtime_error(message);
	}
	std::wstring wide(const std::string_view text)
	{
		const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
		if (!size && !text.empty()) throw std::runtime_error("Invalid UTF-8 launcher data.");
		std::wstring result(size, L'\0');
		MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), size);
		return result;
	}
	std::string utf8(const wchar_t* text)
	{
		const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, nullptr, 0, nullptr, nullptr);
		if (!size || size > 2 * 1024 * 1024) throw std::runtime_error("Invalid launcher text.");
		std::string result(size, '\0');
		WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, result.data(), size, nullptr, nullptr);
		result.pop_back();
		return result;
	}
}

webview_window::webview_window() : guard_(std::make_shared<callback_guard>())
{
	guard_->owner = this;
	check(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "Could not initialize the launcher UI thread.");
	com_initialized_ = true;
}

webview_window::~webview_window()
{
	shutdown(); guard_->owner = nullptr; close();
	if (com_initialized_) CoUninitialize();
}

void webview_window::set_uri(std::string uri)
{
	if (uri == uri_) return;
#ifdef _DEBUG
	constexpr std::string_view development_origin = "http://127.0.0.1:5173";
	if (!launcher_bridge::trusted_source(uri, development_origin)) throw std::runtime_error("Launcher development URL must use http://127.0.0.1:5173.");
	origin_ = development_origin; uri_ = std::move(uri);
#else
	throw std::runtime_error("Development pages are only available in Debug builds.");
#endif
}

void webview_window::set_message_handler(std::function<void(const nlohmann::json&)> handler) { message_handler_ = std::move(handler); }

void webview_window::fail(const std::string& message)
{
	if (failed_) return;
	failed_ = true;
	try { error_ = wide(message); } catch (...) { error_ = L"The launcher could not initialize."; }
	PostMessageW(*this, failure_message, 0, 0);
}

void webview_window::initialize()
{
	const auto user_data = utils::properties::get_appdata_path() / "launcher/webview2";
	std::filesystem::create_directories(user_data);
	const std::weak_ptr<callback_guard> weak = guard_;
	check(CreateCoreWebView2EnvironmentWithOptions(nullptr, user_data.c_str(), nullptr,
		Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
		[weak](HRESULT result, ICoreWebView2Environment* environment)
		{
			return invoke(weak, [&](webview_window& self)
			{
				check(result, "Could not initialize WebView2. Install or repair Microsoft Edge WebView2 Runtime, then reopen Overlord.");
				self.environment_ = environment;
				check(environment->CreateCoreWebView2Controller(self,
					Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
					[weak](HRESULT controller_result, ICoreWebView2Controller* controller)
					{
						return invoke(weak, [&](webview_window& owner)
						{
							check(controller_result, "Could not create the WebView2 launcher.");
							owner.controller_ = controller;
							check(controller->get_CoreWebView2(&owner.webview_), "Could not get the launcher view.");
							owner.configure();
						});
					}).Get()), "Could not create the launcher controller.");
			});
		}).Get()), "Microsoft Edge WebView2 Runtime is unavailable. Install it from Microsoft's WebView2 download page.");
}

void webview_window::configure()
{
	Microsoft::WRL::ComPtr<ICoreWebView2Settings> settings;
	check(webview_->get_Settings(&settings), "Could not configure the launcher view.");
	settings->put_IsScriptEnabled(TRUE);
	settings->put_IsWebMessageEnabled(TRUE);
	settings->put_AreHostObjectsAllowed(FALSE);
	settings->put_AreDefaultScriptDialogsEnabled(FALSE);
	settings->put_AreDefaultContextMenusEnabled(FALSE);
	settings->put_IsStatusBarEnabled(FALSE);
	settings->put_IsZoomControlEnabled(FALSE);
	Microsoft::WRL::ComPtr<ICoreWebView2Settings9> non_client_settings;
	check(settings.As(&non_client_settings), "Update Microsoft Edge WebView2 Runtime to enable the integrated title bar.");
	check(non_client_settings->put_IsNonClientRegionSupportEnabled(TRUE), "Could not enable the integrated title bar.");
	Microsoft::WRL::ComPtr<ICoreWebView2_13> profile_view;
	Microsoft::WRL::ComPtr<ICoreWebView2Profile> profile;
	check(webview_.As(&profile_view), "Could not access the launcher color profile.");
	check(profile_view->get_Profile(&profile), "Could not read the launcher color profile.");
	check(profile->put_PreferredColorScheme(COREWEBVIEW2_PREFERRED_COLOR_SCHEME_DARK), "Could not apply the dark launcher theme.");
#ifndef _DEBUG
	settings->put_AreDevToolsEnabled(FALSE);
#endif
	const std::weak_ptr<callback_guard> weak = guard_;
	EventRegistrationToken token{};
	check(webview_->add_NavigationStarting(Microsoft::WRL::Callback<ICoreWebView2NavigationStartingEventHandler>(
		[weak](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args)
		{
			return invoke(weak, [&](webview_window& self)
			{
				LPWSTR uri{};
				check(args->get_Uri(&uri), "Could not read navigation URI.");
				const auto free = gsl::finally([&] { CoTaskMemFree(uri); });
				if (!launcher_bridge::trusted_source(utf8(uri), self.origin_)) args->put_Cancel(TRUE);
			});
		}).Get(), &token), "Could not guard launcher navigation.");
	check(webview_->add_NewWindowRequested(Microsoft::WRL::Callback<ICoreWebView2NewWindowRequestedEventHandler>(
		[](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) { return args->put_Handled(TRUE); }).Get(), &token), "Could not guard new windows.");
	check(webview_->add_PermissionRequested(Microsoft::WRL::Callback<ICoreWebView2PermissionRequestedEventHandler>(
		[](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* args) { return args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY); }).Get(), &token), "Could not guard page permissions.");
	check(webview_->add_WebMessageReceived(Microsoft::WRL::Callback<ICoreWebView2WebMessageReceivedEventHandler>(
		[weak](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args)
		{
			return invoke(weak, [&](webview_window& self)
			{
				LPWSTR source{}, raw{};
				const auto free = gsl::finally([&] { CoTaskMemFree(source); CoTaskMemFree(raw); });
				if (FAILED(args->get_Source(&source)) || !launcher_bridge::trusted_source(utf8(source), self.origin_)) return;
				if (FAILED(args->get_WebMessageAsJson(&raw))) return;
				try {
					const auto request = launcher_bridge::parse(utf8(raw));
					if (self.message_handler_) self.message_handler_({{"id", request.id}, {"session", request.session}, {"method", request.method}, {"params", request.params}});
				} catch (const std::exception& error) { OutputDebugStringA(error.what()); }
			});
		}).Get(), &token), "Could not register launcher messages.");
	check(webview_->AddWebResourceRequestedFilter(L"https://launcher.invalid/*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL), "Could not register launcher resources.");
	check(webview_->add_WebResourceRequested(Microsoft::WRL::Callback<ICoreWebView2WebResourceRequestedEventHandler>(
		[weak](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args)
		{
			return invoke(weak, [&](webview_window& self)
			{
				Microsoft::WRL::ComPtr<ICoreWebView2WebResourceRequest> request;
				check(args->get_Request(&request), "Could not read a launcher resource request.");
				LPWSTR uri{}; check(request->get_Uri(&uri), "Could not read a launcher resource URI.");
				const auto free = gsl::finally([&] { CoTaskMemFree(uri); });
				const auto address = utf8(uri);
				if (!launcher_bridge::trusted_source(address)) return;
				auto path = address.substr(launcher_bridge::origin.size());
				if (const auto query = path.find('?'); query != std::string::npos) path.resize(query);
				if (path.empty() || path == "/") path = "/index.html";
				const auto asset = std::find_if(launcher_assets::assets.begin(), launcher_assets::assets.end(), [&](const auto& item) { return item.path == path; });
				const bool found = asset != launcher_assets::assets.end();
				const std::string bytes = found ? utils::nt::load_resource(asset->resource) : "Not found";
				Microsoft::WRL::ComPtr<IStream> stream;
				stream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(bytes.data()), static_cast<UINT>(bytes.size())));
				if (!stream) throw std::runtime_error("Could not open an embedded launcher resource.");
				const auto headers = wide("Content-Type: " + std::string(found ? asset->mime : "text/plain") + "\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n");
				Microsoft::WRL::ComPtr<ICoreWebView2WebResourceResponse> response;
				check(self.environment_->CreateWebResourceResponse(stream.Get(), found ? 200 : 404,
					found ? L"OK" : L"Not Found", headers.c_str(), &response), "Could not serve a launcher resource.");
				args->put_Response(response.Get());
			});
		}).Get(), &token), "Could not serve embedded launcher content.");
	check(webview_->add_ProcessFailed(Microsoft::WRL::Callback<ICoreWebView2ProcessFailedEventHandler>(
		[weak](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*) { return invoke(weak, [](webview_window& self) { self.fail("The WebView2 launcher stopped responding. Reopen Overlord to retry."); }); }).Get(), &token), "Could not monitor the launcher view.");
	resize();
	check(webview_->Navigate(wide(uri_).c_str()), "Could not load the launcher page.");
}

void webview_window::send(const nlohmann::json& message)
{
	if (!webview_ || closed_) return;
	LPWSTR source{};
	const auto free = gsl::finally([&] { CoTaskMemFree(source); });
	if (SUCCEEDED(webview_->get_Source(&source)) && launcher_bridge::trusted_source(utf8(source), origin_))
		webview_->PostWebMessageAsJson(wide(message.dump()).c_str());
}

void webview_window::resize()
{
	if (!controller_) return;
	controller_->put_Bounds(content_bounds());
}

void webview_window::shutdown()
{
	if (closed_) return;
	closed_ = true;
	if (controller_) controller_->Close();
	webview_.Reset(); controller_.Reset(); environment_.Reset();
}

LRESULT webview_window::processor(const UINT message, const WPARAM w_param, const LPARAM l_param)
{
	if (message == WM_CREATE) { try { initialize(); } catch (const std::exception& error) { fail(error.what()); } return 0; }
	if (message == WM_SIZE) { resize(); }
	if (message == WM_MOVE && controller_) controller_->NotifyParentWindowPositionChanged();
	if (message == WM_SETFOCUS && controller_) controller_->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
	if (message == WM_CLOSE) { shutdown(); close(); return 0; }
	if (message == failure_message)
	{
		if (quiet_failure_) { shutdown(); close(); return 0; }
		// Modal native UI is deferred out of COM callbacks to avoid reentrancy.
		const auto text = error_ + L"\n\nOpen Microsoft's WebView2 download page?";
		if (MessageBoxW(*this, text.c_str(), L"Overlord", MB_ICONERROR | MB_YESNO) == IDYES)
			ShellExecuteW(nullptr, L"open", L"https://developer.microsoft.com/en-us/microsoft-edge/webview2/", nullptr, nullptr, SW_SHOWNORMAL);
		shutdown(); close(); return 0;
	}
	return window::processor(message, w_param, l_param);
}
