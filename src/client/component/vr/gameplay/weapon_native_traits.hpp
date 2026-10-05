#pragma once
#include <string_view>

namespace vr::gameplay::weapons
{
	// Scalar native-shape admission. Native readers copy fields under their
	// memory guard; catalog/family policy runs afterwards on the copied values.
	bool admits_native_reload(std::string_view name,int capacity,bool no_partial,bool segmented,int add) noexcept;
}
