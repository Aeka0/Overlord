#pragma once
#include <functional>

namespace vr::diagnostics::support
{
	void start(std::function<bool()> enabled);
	void request();
	void stop();
}
