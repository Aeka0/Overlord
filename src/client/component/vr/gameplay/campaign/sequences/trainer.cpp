#include <std_include.hpp>
#include "trainer.hpp"
#include "../../scripted_control.hpp"
#include "game/game.hpp"
#include "game/scripting/execution.hpp"

namespace vr::gameplay::sequences::trainer
{
	bool supported()
	{
		const auto* map = game::Dvar_FindVar("mapname");
		return map && map->current.string && std::string_view(map->current.string) == "trainer";
	}
	view observe(const void* ps)
	{
		if (!ps)
			return {};
		std::uint32_t weapon_flags{};
		std::memcpy(&weapon_flags, static_cast<const std::byte*>(ps) + 0x3c0, 4);
		if (scripted_control::permits_weapons(weapon_flags))
			return {};
		const scripting::entity level{*game::levelEntityId}, player{game::scr_entref_t{0, 0}};
		if (!player.call("islinked").as<int>())
			return {};
		const auto parent = player.call("getlinkedparent"), value = level.get("flag");
		if (!parent.is<scripting::entity>() || !value.is<scripting::array>())
			return {};
		const auto flags = value.as<scripting::array>();
		const auto initialized = flags.get(std::string{"firing_range_initialized"});
		const auto finished = flags.get(std::string{"training_intro_end_anims"});
		if (!initialized.is<int>() || !finished.is<int>())
			return {};
		const auto body = parent.as<scripting::entity>();
		const auto classname = body.get("classname"), rig = body.get("animname");
		return classify(initialized.as<int>() != 0,
		                finished.as<int>() != 0,
		                true,
		                false,
		                classname.is<std::string>() ? classname.as<std::string>() : "",
		                rig.is<std::string>() ? rig.as<std::string>() : "");
	}
}
