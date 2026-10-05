#include "std_include.hpp"

#include "component/vr/engine_backend_probe.hpp"

namespace vr::engine_backend_probe
{
	bool read_target_registry_entry(const std::uint32_t,
		native_render_contract::target_registry_entry&) noexcept
	{
		return false;
	}
}
