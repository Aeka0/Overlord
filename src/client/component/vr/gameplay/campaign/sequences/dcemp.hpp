#pragma once
#include "../scripted_sequences.hpp"

namespace scripting
{
	class entity;
}

namespace vr::gameplay::sequences::dcemp
{
	struct evidence
	{
		std::string_view rig;
		unsigned parent{}, wakeup{};
		unsigned ground{};
		bool returned{};
	};
	inline view classify(const evidence& e) noexcept
	{
		if (!e.parent)
			return {};
		if (e.rig == "iss_rig")
		{
			auto result = free_look(scenario::dcemp, phase::transport);
			result.camera = scene_cameras::dcemp_space;
			result.rotation_tag = game_view::scripted_camera_tag::player;
			return result;
		}
		if (e.returned && e.parent == e.ground)
		{
			auto result = free_look(scenario::dcemp, phase::scripted_combat);
			result.camera = scene_cameras::dcemp_ground;
			result.rotation_tag = game_view::scripted_camera_tag::origin;
			return result;
		}
		if (e.rig == "player_rig" && e.parent == e.wakeup)
		{
			auto result = free_look(scenario::dcemp, phase::recovery);
			result.camera = scene_cameras::dcemp_wakeup;
			return result;
		}
		return {};
	}
	bool supported();
	view observe(const scripting::entity& actor, const scripting::entity& parent);
}
