#include <std_include.hpp>
#include "sliding.hpp"
#include "game/scripting/entity.hpp"

namespace vr::gameplay::sequences::sliding
{
	view observe(const scripting::entity& player, const scripting::entity& parent)
	{
		// maps/_utility::beginsliding assigns player.slidemodel before any of
		// its animated, reverse or script-origin linking paths. endsliding
		// retains that link throughout slide_out, then unlinks and deletes it.
		// The early "sliding_out" flag and "completed" notice are not exits.
		const auto model = player.get("slidemodel");
		return classify(true,
		                true,
		                parent.get_entity_id(),
		                model.is<scripting::entity>() ? model.as<scripting::entity>().get_entity_id() : 0u);
	}
}
