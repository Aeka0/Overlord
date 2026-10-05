#pragma once
#include "closed_bolt.hpp"

namespace vr::gameplay::weapons::native_closed_bolt
{
	enum class boundary { admission, refill };
	enum class reload_kind { unknown, tactical, empty };
	// The native action identifies the reload that started. Never reinterpret
	// an empty reload as tactical after its first fill (including replay).
	inline int capacity(const closed_bolt::rules& r, int original, int loaded,
		boundary where, reload_kind kind) noexcept
	{
		closed_bolt::state feed{};
		if (original != r.magazine_capacity || !closed_bolt::from_native_automatic(r, loaded, feed))
			return original;
		const bool retained = feed.chamber_loaded &&
			(where == boundary::admission || kind == reload_kind::tactical);
		// Native transfer must not receive a negative amount on a late callback
		// when an already loaded +1 exceeds the base magazine capacity.
		return std::max(loaded, closed_bolt::reload_capacity(r, retained));
	}
}
