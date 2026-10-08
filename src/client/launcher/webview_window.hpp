#pragma once
#include "window.hpp"
#include <WebView2.h>
#include <wrl.h>
#include <json.hpp>

class webview_window final : public window
{
public:
	webview_window();
	~webview_window() override;
	void set_uri(std::string uri);
	void set_message_handler(std::function<void(const nlohmann::json&)> handler);
	void send(const nlohmann::json& message);
	bool failed() const { return failed_; }
	void set_quiet_failure(bool quiet) { quiet_failure_ = quiet; }
	void shutdown();

protected:
	LRESULT processor(UINT message, WPARAM w_param, LPARAM l_param) override;

private:
	struct callback_guard { webview_window* owner{}; };
	std::shared_ptr<callback_guard> guard_;
	Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment_;
	Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
	Microsoft::WRL::ComPtr<ICoreWebView2> webview_;
	std::function<void(const nlohmann::json&)> message_handler_;
	std::string uri_ = "https://launcher.invalid/index.html";
	std::string origin_ = "https://launcher.invalid";
	std::wstring error_;
	bool failed_{};
	bool closed_{};
	bool com_initialized_{};
	bool quiet_failure_{};
	void initialize();
	void configure();
	void resize();
	void fail(const std::string& message);

	template <typename Function>
	static HRESULT invoke(const std::weak_ptr<callback_guard>& weak, Function function)
	{
		const auto guard = weak.lock();
		if (!guard || !guard->owner || guard->owner->closed_) return S_OK;
		try { function(*guard->owner); }
		catch (const std::exception& error) { guard->owner->fail(error.what()); }
		return S_OK;
	}
};
