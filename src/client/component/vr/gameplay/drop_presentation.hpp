#pragma once
#include "native_carry.hpp"
#include "native_carry_model.hpp"
#include <string>

namespace vr::gameplay::weapons::drop_presentation
{
	// Server publication; the native item keeps inventory and collision authority.
	void begin(native_carry::world_key, const native_carry::model_geometry&, const hands::anchor&) noexcept;
	void update(native_carry::world_key, const hands::anchor&) noexcept;
	bool pending(native_carry::world_key) noexcept;
	void cancel(native_carry::world_key) noexcept;
	void clear() noexcept;
	std::string status();
}
