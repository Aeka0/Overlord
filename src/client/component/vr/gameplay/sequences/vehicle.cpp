#include <std_include.hpp>
#include "vehicle.hpp"
#include "game/game.hpp"
#include "game/scripting/execution.hpp"

namespace vr::gameplay::sequences::vehicle
{
	view observe()
	{
		const auto* dvar=game::Dvar_FindVar("mapname");
		const std::string_view map=dvar && dvar->current.string?dvar->current.string:"";
		if(map!="af_chase" && map!="cliffhanger")return {};
		const scripting::entity player{game::scr_entref_t{0,0}};
		if(!scripting::call<int>("isalive",{player}) || !player.call("islinked").as<int>())return {};
		const auto parent=player.call("getlinkedparent");
		if(map=="af_chase" && parent.is<scripting::entity>())
		{
			const scripting::entity level{*game::levelEntityId};const auto flags=level.get("flag");
			const auto jump=flags.is<scripting::array>()?flags.as<scripting::array>().get(std::string{"player_jumping_over_waterfall"}):scripting::script_value{};
			const auto boat=scripting::get_object_variable(*game::levelEntityId,0xB538u);
			const auto blend=scripting::get_object_variable(*game::levelEntityId,0xAA43u);
			const auto target=parent.as<scripting::entity>().get("targetname");
			// Waterfall replaces the boat hands with worldbody BEFORE dismount;
			// then the linked camera moves to zodiac_blend_target. Keep that body.
			const auto ending=waterfall(jump.is<int>() && jump.as<int>()!=0,true,
				boat.is<scripting::entity>() && parent.as<scripting::entity>()==boat.as<scripting::entity>(),
				(blend.is<scripting::entity>() && parent.as<scripting::entity>()==blend.as<scripting::entity>()) ||
				(target.is<std::string>() && target.as<std::string>()=="zodiac_blend_target"));
			if(ending.stage!=phase::none)return ending;
		}
		const auto driving=player.get("vehicle");
		if(!parent.is<scripting::entity>() || !driving.is<scripting::entity>())return {};
		const auto entity=driving.as<scripting::entity>();const auto rig=entity.get("animname");
		const auto type=vehicles::classify(map,true,true,parent.as<scripting::entity>()==entity,
			rig.is<std::string>()?rig.as<std::string>():std::string{});
		if(type==vehicles::kind::none)return {};
		auto result=free_look(scenario::vehicle);
		result.camera=scene_cameras::vehicle;
		result.vehicle=type;result.suspend_weapons=true;
		result.allow_movement=true;result.allow_turn=false;
		return result;
	}
}
