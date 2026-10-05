#pragma once
#include "../scripted_sequences.hpp"

namespace scripting
{
	class entity;
}

namespace vr::gameplay::sequences::roadkill
{
	struct boarding_evidence
	{
		bool alive{}, triggered{}, mounted{};
		std::string_view rig;
		unsigned rig_vehicle{}, player_vehicle{};
	};
	inline view classify(const boarding_evidence& e) noexcept
	{
		if (!e.alive || !e.triggered || e.mounted || e.rig != "player_rig" || !e.rig_vehicle ||
		    e.rig_vehicle != e.player_vehicle)
			return {};
		auto result = free_look(scenario::roadkill, phase::hookup);
		result.camera = scene_cameras::roadkill;
		// Only the view is free. Authored boarding arms, weapon prohibition,
		// movement and the subsequent native turret takeover remain unchanged.
		return result;
	}
	struct recovery_evidence
	{
		bool alive{}, intro_done{}, on_the_line{}, mounted{};
		std::string_view rig;
	};
	inline view classify_recovery(const recovery_evidence& e) noexcept
	{
		if (!e.alive || !e.intro_done || e.on_the_line || e.mounted || e.rig != "player_worldbody")
			return {};
		auto result = free_look(scenario::roadkill, phase::recovery);
		result.camera = scene_cameras::roadkill;
		return result;
	}
	bool supported();
	view observe(const scripting::entity& parent, unsigned entity_flags);
}
