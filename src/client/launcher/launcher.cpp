#include <std_include.hpp>
#include "launcher.hpp"
#include "vr_settings.hpp"
#include "localization.hpp"
#include "product.hpp"

#include <utils/nt.hpp>

launcher::launcher()
{
	this->create_main_menu();
}

void launcher::create_main_menu()
{
	this->main_window_.register_callback("scanGameLanguages", [this](html_frame::callback_params* params)
	{
		params->result.set_string(game_language_.scan());
	});
	this->main_window_.register_callback("pollGameLanguages", [this](html_frame::callback_params* params)
	{
		params->result.set_string(game_language_.poll());
	});
	this->main_window_.register_callback("saveGameLanguage", [this](html_frame::callback_params* params)
	{
		if (params->arguments.size() != 1 || !params->arguments[0].is_string()) return;
		params->result.set_string(game_language_.save(params->arguments[0].get_string()));
	});
	this->main_window_.register_callback("loadLauncherLanguage", [](html_frame::callback_params* params)
	{
		params->result.set_string(launcher_localization::load());
	});
	this->main_window_.register_callback("saveLauncherLanguage", [](html_frame::callback_params* params)
	{
		if (params->arguments.size() != 1 || !params->arguments[0].is_string()) return;
		params->result.set_string(launcher_localization::save(params->arguments[0].get_string()));
	});
	this->main_window_.register_callback("loadVRSettings", [](html_frame::callback_params* params)
	{
		params->result.set_string(launcher_vr_settings::load());
	});
	this->main_window_.register_callback("saveVRSettings", [](html_frame::callback_params* params)
	{
		if (params->arguments.size() != 1 || !params->arguments[0].is_string()) return;
		params->result.set_string(launcher_vr_settings::save(params->arguments[0].get_string()));
	});
	this->main_window_.register_callback("disableVRRiskSettings", [](html_frame::callback_params* params)
	{
		params->result.set_string(launcher_vr_settings::disable_risk_settings());
	});

	this->main_window_.register_callback("openUrl", [](html_frame::callback_params* params)
	{
		if (params->arguments.empty()) return;

		const auto param = params->arguments[0];
		if (!param.is_string()) return;

		const auto url = param.get_string();
		ShellExecuteA(nullptr, "open", url.data(), nullptr, nullptr, SW_SHOWNORMAL);
	});

	this->main_window_.register_callback("selectMode", [this](html_frame::callback_params* params)
	{
		if (params->arguments.empty()) return;

		const auto param = params->arguments[0];
		if (!param.is_number()) return;

		const auto number = static_cast<mode>(param.get_number());
		this->select_mode(number);
	});

	this->main_window_.set_callback(
		[](window* window, const UINT message, const WPARAM w_param, const LPARAM l_param) -> LRESULT
		{
			if (message == WM_CLOSE)
			{
				window::close_all();
			}

			return DefWindowProcA(*window, message, w_param, l_param);
		});

	this->main_window_.create(product::name, 960, 640);
	this->main_window_.load_html(load_content(MENU_MAIN));
	this->main_window_.show();
}

launcher::mode launcher::run() const
{
	window::run();
	return this->mode_;
}

void launcher::select_mode(const mode mode)
{
	this->mode_ = mode;
	this->main_window_.close();
}

std::string launcher::load_content(const int res)
{
	auto content = utils::nt::load_resource(res);
	if (res == MENU_MAIN)
	{
		constexpr std::string_view onboarding_marker = "<!-- LAUNCHER_ONBOARDING_STYLES -->";
		const auto onboarding_position = content.find(onboarding_marker);
		if (onboarding_position == std::string::npos) throw std::runtime_error("Launcher onboarding style marker missing");
		content.replace(onboarding_position, onboarding_marker.size(), "<style>" + utils::nt::load_resource(LAUNCHER_ONBOARDING_STYLE) + "</style>");
		constexpr std::string_view style_marker = "<!-- LAUNCHER_HELP_STYLES -->";
		const auto style_position = content.find(style_marker);
		if (style_position == std::string::npos) throw std::runtime_error("Launcher help style marker missing");
		content.replace(style_position, style_marker.size(), "<style>" + utils::nt::load_resource(LAUNCHER_HELP_STYLE) + "</style>");
		constexpr std::string_view marker = "<!-- LAUNCHER_SCRIPTS -->";
		const auto position = content.find(marker);
		if (position == std::string::npos) throw std::runtime_error("Launcher script marker missing");
		content.replace(position, marker.size(), launcher_localization::scripts());
	}
	return content;
}
