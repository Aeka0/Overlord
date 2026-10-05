#include <std_include.hpp>
#include "estate.hpp"
#include "../../scripted_control.hpp"
#include "game/game.hpp"
#include "game/scripting/execution.hpp"

namespace vr::gameplay::sequences::estate
{
	bool supported()
	{
		const auto* map = game::Dvar_FindVar("mapname");
		return map && map->current.string && std::string_view(map->current.string) == "estate";
	}
	view observe(const void* ps)
	{
		view result{};
		if (!ps)
			return result;
		const scripting::entity level{*game::levelEntityId}, player{game::scr_entref_t{0, 0}};
		const auto value = level.get("flag");
		if (!value.is<scripting::array>())
			return result;
		const auto flags = value.as<scripting::array>();
		const auto flag = [&](const char* key)
		{
			const auto v = flags.get(std::string(key));
			return v.is<int>() && v.as<int>() != 0;
		};
		if (scripting::call<int>("isalive", {player}) && player.call("islinked").as<int>())
		{
			const auto parent = player.call("getlinkedparent");
			if (parent.is<scripting::entity>())
			{
				const auto rig = parent.as<scripting::entity>().get("animname");
				std::uint32_t weapon_flags{};
				std::memcpy(&weapon_flags, static_cast<const std::byte*>(ps) + 0x3c0, 4);
				if (rig.is<std::string>())
					result = classify(flag("play_ending_sequence"),
					                  true,
					                  rig.as<std::string>(),
					                  scripted_control::permits_weapons(weapon_flags));
			}
		}
		result.dsm_progress = flag("download_started") && !flag("download_complete");
		return result;
	}
}
