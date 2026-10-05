#pragma once
#include <cstdint>
#include <string_view>

namespace vr::gameplay::weapons::heartbeat
{
	inline bool admits_native_mode(std::string_view name,std::uint32_t linked_weapon,
		bool primary_tracker,bool alternate_tracker)noexcept
	{
		if(!primary_tracker)return false;
		// This captured M240 has a fixed tracker and no alternate definition.
		// Both native mode queries resolve to the same weapon; VR owns its fold.
		if(name=="m240_heartbeat_reflex_arctic")return !linked_weapon && alternate_tracker;
		// ACR retains its witnessed tracker-on / tracker-off definition pair.
		return name.starts_with("masada_") && name.find("_mt_")!=name.npos &&
			linked_weapon && !(linked_weapon&~511u) && !alternate_tracker;
	}
}
