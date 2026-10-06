#pragma once
#include <string>

namespace launcher_vr_settings
{
	// JSON envelopes cross the existing HTML bridge; errors are shown in-page.
	std::string load();
	std::string save(const std::string& payload);
	// Also used by direct -singleplayer starts that bypass the launcher UI.
	void initialize_startup_options(bool from_launcher);
}
