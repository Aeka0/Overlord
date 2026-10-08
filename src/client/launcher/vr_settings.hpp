#pragma once
#include <string>

namespace launcher_vr_settings
{
	// JSON envelopes cross the WebView2 bridge; errors are shown in-page.
	std::string load();
	std::string save(const std::string& payload);
	std::string disable_risk_settings();
	std::string read_profile();
	void repair_risk_settings(const std::string& group);
	// Also used by direct -singleplayer starts that bypass the launcher UI.
	void initialize_startup_options(bool from_launcher);
}
