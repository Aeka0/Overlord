#pragma once
#include "../scripted_sequence_policy.hpp"
#include "component/game_text.hpp"
#include <optional>

namespace vr::gameplay::sequences::rappel
{
	struct evidence
	{
		bool supported{}, alive{}, linked{}, hooked{}, descending{}, at_bottom{}, killing{}, ending{}, failed{}, was_active{};
	};
	inline phase classify(const evidence& e) noexcept
	{
		if (!e.supported || !e.alive || !e.linked || !e.hooked) return phase::none;
		if (e.failed || e.ending) return e.was_active ? phase::release : phase::none;
		if (e.killing) return phase::execution;
		if (e.at_bottom) return phase::melee;
		return e.descending ? phase::descent : phase::hookup;
	}
	inline std::optional<game_text::key> instruction_key(phase stage) noexcept
	{
		if(stage==phase::descent)return game_text::key::rappel_brake;
		if(stage==phase::melee)return game_text::key::story_melee;
		return {};
	}
	// Server pipeline only; no script references escape the adapter.
	bool supported();
	phase observe(bool was_active);
}
