#pragma once

#include "component/d3d11.hpp"

namespace d3d11::swap_chain_hook
{
	bool install(const device_snapshot& graphics);
	bool is_inside_callback() noexcept;
	bool has_active_callbacks() noexcept;
	void begin_shutdown() noexcept;
	void reset_active_swap_chain(std::uint64_t generation);
	void shutdown();
}
