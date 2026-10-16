#pragma once
#include "../scripted_sequence_policy.hpp"

namespace vr::gameplay::sequences::rappel
{
	struct evidence
	{
		bool supported{}, alive{}, linked{}, hooked{}, descending{}, at_bottom{}, killing{}, ending{},
		    failed{}, was_active{};
	};
	inline phase classify(const evidence& e) noexcept
	{
		if (!e.supported || !e.alive || !e.linked || !e.hooked)
			return phase::none;
		if (e.failed || e.ending)
			return e.was_active ? phase::release : phase::none;
		if (e.killing)
			return phase::execution;
		if (e.at_bottom)
			return phase::melee;
		return e.descending ? phase::descent : phase::hookup;
	}
	// Server pipeline only; no script references escape the adapter.
	bool supported();
	phase observe(bool was_active);
}
