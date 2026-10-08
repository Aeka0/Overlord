#include <std_include.hpp>
#include "hud_prompts.hpp"
#include "gameplay/signal_flare_mission.hpp"
#include "gameplay/weapon_carry_runtime.hpp"
#include "gameplay/native_use.hpp"
#include "gameplay/nightvision_runtime.hpp"
#include "gameplay/special_equipment_runtime.hpp"
#include "gameplay/designator_events.hpp"
#include "gameplay/notebook_runtime.hpp"
#include "gameplay/fixed_sniper.hpp"
#include "gameplay/heartbeat_runtime.hpp"
#include "gameplay/underbarrel_runtime.hpp"
#include <utils/native_memory.hpp>
#include "game/game.hpp"
#include <utils/hook.hpp>

namespace vr::hud_prompts
{
	namespace
	{
		bool enabled(const char* name)
		{const auto* value=game::Dvar_FindVar(name);return value && value->current.enabled;}
		std::string_view setting(const char* name)
		{
			const auto* value=game::Dvar_FindVar(name);if(!value || !value->current.string)return {};
			const auto length=strnlen(value->current.string,64);
			return length<64?std::string_view(value->current.string,length):std::string_view{};
		}
		context snapshot(const native_rule& rule)
		{
			context state;if(!enabled("vr_enable"))return state;state.vr=true;
			if(!rule.map.empty())
			{
				if(!game::CL_IsCgameInitialized())return {};
				state.map=setting("mapname");if(state.map!=rule.map)return state;
			}
			// Query only the owner needed by this recognized instruction. No VM
			// reads, gameplay mutations or dependencies between unrelated owners.
			bool available{};
			switch(rule.required)
			{
			case feature::none:available=true;break;
			case feature::signal_flare:available=gameplay::equipment::special::flare::mission::active();break;
			case feature::carry:available=gameplay::weapons::carry::active();break;
			case feature::world:available=enabled("vr_worldInteraction") && gameplay::interaction::native::ready() && gameplay::weapons::carry::active();break;
			case feature::special:available=gameplay::equipment::special::active();break;
			case feature::nightvision:available=gameplay::equipment::nightvision::available();break;
			case feature::designator:available=gameplay::equipment::special::active() && gameplay::equipment::special::designator_events::ready();break;
			case feature::notebook:available=gameplay::equipment::special::notebook::controlling();break;
			case feature::notebook_device:available=gameplay::equipment::special::notebook::available();break;
			case feature::fixed_sniper:available=gameplay::fixed_sniper::current(game::CG_GetPredictedPlayerState(0)).epoch!=0;break;
			case feature::heartbeat:
				if(gameplay::weapons::heartbeat::enabled())for(const auto& item:gameplay::weapons::carry::held_instances())
					if(gameplay::weapons::heartbeat::equivalent_mode(item.id.weapon))available=true;
				break;
			case feature::museum:available=setting("ui_char_museum_mode")=="free" && enabled("vr_worldInteraction") && gameplay::interaction::native::ready() && gameplay::weapons::carry::active();break;
			case feature::m203:
				if(gameplay::weapons::underbarrel::enabled())for(const auto& item:gameplay::weapons::carry::held_instances())
				{
					const auto module=gameplay::weapons::underbarrel::current(item.id);
					if(module.active && !module.fault && module.ammo.id.type==gameplay::weapons::underbarrel::kind::m203)available=true;
				}
				break;
			}
			if(available)state.available=rule.required;
			return state;
		}
		template<std::size_t N>bool verify(std::uintptr_t address,const std::uint8_t (&expected)[N])
		{
			std::array<std::uint8_t,N> actual{};
			return utils::native_memory::read_bytes(actual.data(),reinterpret_cast<const void*>(address),N) &&
				std::equal(actual.begin(),actual.end(),expected);
		}
		bool hint_contract()
		{
			// sethintstring -> config slots [1,31]+0xd5 -> native cursor formatter.
			static const bool valid=[] {
				constexpr std::uint8_t range[]{0x81,0xc1,0xd5,0,0,0};
				constexpr std::uint8_t current[]{0x8b,0x0d,0x21,0x80,0x88,0x01};
				constexpr std::uint8_t lookup[]{0x48,0x63,0xc1,0x48,0x8d,0x0d,0x3a,0xfc,0xc6,0x01,0x8b,0x0c,0x81};
				return verify(0x14038A957,range) && verify(0x14038A951,current) && verify(0x1403C9D60,lookup);
			}();return valid;
		}
		std::array<char,128> hint_key(unsigned hint)
		{
			std::array<char,128> result{};
			if(!valid_world_hint(hint) || !game::CL_IsCgameInitialized() || !hint_contract())return result;
			const auto* value=utils::hook::invoke<const char*>(0x1403C9D60,int(hint)+0xd5);
			if(!value || !utils::native_memory::read_bytes(result.data(),value,result.size()) ||
				std::find(result.begin(),result.end(),'\0')==result.end())return {};
			return result;
		}
	}
	std::optional<message> compose_current(key id,game_text::locale language,arguments args)
	{
		const auto input=controller_input::latest();
		const auto now=controller_input::clock::now();
		const bool fresh=input.focused && now>=input.sampled_at && now-input.sampled_at<=std::chrono::milliseconds(150);
		if(args.hand==-1 && (id==key::trainer_fire || id==key::trainer_hip_fire || id==key::vehicle_reload))
		{
			const auto held=gameplay::weapons::carry::current_hold();
			if(vr::valid_hand(held.rear))args.hand=static_cast<int>(held.rear);
		}
		return compose(id,language,args,fresh?input.prompt_bindings.get():nullptr);
	}
	std::optional<std::string> replace(source producer,std::string_view name)
	{
		const auto* rule=identify(producer,name);if(!rule)return {};
		const auto id=resolve(producer,name,snapshot(*rule));if(!id)return {};
		const auto value=compose_current(*id,game_text::current(),{-1,true,{}});if(!value)return {};
		auto result=text(*value,style::native_colors);if(result.empty())return {};
		return result;
	}
	std::optional<key> world_message(unsigned hint)
	{
		const auto name=hint_key(hint);const auto* rule=identify(source::world,name.data());if(!rule)return {};
		return resolve(source::world,name.data(),snapshot(*rule));
	}
	bool native_cursor_required()
	{
		const auto language=game_text::current();if(!owns_world_text(language))return true;
		if(!game::CL_IsCgameInitialized() || !hint_contract())return true;
		unsigned hint{};
		if(!utils::native_memory::read_bytes(&hint,reinterpret_cast<const void*>(0x141C12978),sizeof(hint)))return true;
		const auto id=world_message(hint);
		return id && !compose_current(*id,language,{-1,true,{}});
	}
}
