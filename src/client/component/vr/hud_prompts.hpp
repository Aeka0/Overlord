#pragma once
#include "component/game_text.hpp"
#include <optional>

// All VR action instructions pass through this registry and formatter. Native
// bridges supply identity, never translated/rendered text. Gameplay supplies a
// semantic key; renderers receive owned runs and retain layout/lifetime control.
namespace vr::hud_prompts
{
	using game_text::key;
	enum class source { script_hud, lui, world };
	enum class control { none, trigger, right_stick_down, grip };
	enum class feature : unsigned
	{
		none=0, signal_flare=1, carry=2, world=4, special=8,
		nightvision=16, designator=32, notebook=64, fixed_sniper=128, heartbeat=256, museum=512, notebook_device=1024, m203=2048
	};
	constexpr feature operator|(feature a,feature b) noexcept {return feature(unsigned(a)|unsigned(b));}
	constexpr bool includes(feature available,feature required) noexcept
	{return (unsigned(available)&unsigned(required))==unsigned(required);}
	struct definition {key message;control operation;};
	inline constexpr std::array definitions{
		definition{key::ending_knife_pull,control::none},
		definition{key::interaction_use,control::grip},
		definition{key::interaction_pickup,control::grip},
		definition{key::interaction_resupply,control::grip},
		definition{key::rappel_brake,control::trigger},
		definition{key::story_melee,control::trigger},
		definition{key::mine_prone,control::right_stick_down},
		definition{key::vehicle_duck,control::right_stick_down},
		definition{key::signal_flare,control::none},
		definition{key::cinematic_skip,control::none},
		definition{key::nightvision_on,control::none},
		definition{key::nightvision_off,control::none},
		definition{key::breach_place,control::grip},
		definition{key::dsm_connect,control::grip},
		definition{key::dsm_recover,control::grip},
		definition{key::claymore_place,control::none},
		definition{key::rappel_attach,control::grip},
		definition{key::gulag_attach,control::grip},
		definition{key::designator_draw,control::none},
		definition{key::designator_target,control::none},
		definition{key::briefcase_pickup,control::grip},
		definition{key::notebook_pickup,control::grip},
		definition{key::notebook_open,control::none},
		definition{key::notebook_boost,control::none},
		definition{key::notebook_control,control::none},
		definition{key::fixed_sniper_zoom,control::none},
		definition{key::fixed_sniper_aim,control::none},
		definition{key::fixed_sniper_controls,control::none},
		definition{key::heartbeat_fold,control::none},
		definition{key::snowmobile_board,control::grip},
		definition{key::vehicle_drive,control::none},
		definition{key::vehicle_reload,control::none},
		definition{key::museum_warning,control::grip},
		definition{key::trainer_fire,control::none},
		definition{key::trainer_hip_fire,control::none},
		definition{key::trainer_ads,control::none},
		definition{key::trainer_stop_ads,control::none},
		definition{key::trainer_next_target,control::none},
		definition{key::trainer_sidearm,control::none},
		definition{key::trainer_primary,control::none},
		definition{key::trainer_reload,control::none},
		definition{key::trainer_knife,control::none},
		definition{key::trainer_crouch,control::none},
		definition{key::trainer_stand,control::none},
		definition{key::trainer_prone,control::none},
		definition{key::trainer_jump,control::none},
		definition{key::trainer_mantle,control::none},
		definition{key::trainer_sprint,control::none},
		definition{key::trainer_frag,control::none},
		definition{key::trainer_flash,control::none},
		definition{key::trainer_menu,control::none},
		definition{key::m203_reload,control::none},
		definition{key::c4_draw,control::none},
		definition{key::c4_detonate,control::none},

	};
	constexpr const definition* describe(key id) noexcept
	{for(const auto& entry:definitions)if(entry.message==id)return &entry;return nullptr;}

	struct native_rule
	{
		source producer;std::string_view native_key;key message;
		std::string_view map;feature required{};
	};
	// Scope is part of the identity: no global translation override and no
	// substring matching against subtitles, glyphs, bindings or localized prose.
	inline constexpr std::array native_rules{
		native_rule{source::script_hud,"SCRIPT_PLATFORM_OILRIG_HINT_STEALTH_KILL",key::story_melee,"oilrig"},
		native_rule{source::script_hud,"OILRIG_HINT_C4_SWITCH",key::c4_draw,"oilrig",feature::special},
		native_rule{source::script_hud,"OILRIG_HINT_C4_DETONATE",key::c4_detonate,"oilrig",feature::special},
		native_rule{source::script_hud,"ESTATE_LEARN_PRONE",key::mine_prone,"estate"},
		native_rule{source::script_hud,"ESTATE_LEARN_PRONE_TOGGLE",key::mine_prone,"estate"},
		native_rule{source::script_hud,"ESTATE_LEARN_PRONE_HOLDDOWN",key::mine_prone,"estate"},
		native_rule{source::script_hud,"FAVELA_DUCK_HINT",key::vehicle_duck,"favela"},
		native_rule{source::script_hud,"FAVELA_DUCK_HINT_KEYBOARD",key::vehicle_duck,"favela"},
		native_rule{source::script_hud,"SCRIPT_PLATFORM_HINTSTR_POPFLARE",key::signal_flare,"dc_whitehouse",feature::signal_flare},
		native_rule{source::script_hud,"SCRIPT_PLATFORM_HINTSTR_POPFLARE_KEYBOARD",key::signal_flare,"dc_whitehouse",feature::signal_flare},
		native_rule{source::script_hud,"SCRIPT_NIGHTVISION_USE",key::nightvision_on,"",feature::nightvision},
		native_rule{source::script_hud,"SCRIPT_NIGHTVISION_STOP_USE",key::nightvision_off,"",feature::nightvision},
		native_rule{source::script_hud,"SCRIPT_PLATFORM_BREACH_ACTIVATE",key::breach_place,"",feature::world},
		native_rule{source::world,"SCRIPT_PLATFORM_BREACH_ACTIVATE",key::breach_place,"",feature::world},
		native_rule{source::script_hud,"SCRIPT_PLATFORM_BREACH_ACTIVATE_KB",key::breach_place,"",feature::world},
		native_rule{source::world,"SCRIPT_PLATFORM_BREACH_ACTIVATE_KB",key::breach_place,"",feature::world},
		native_rule{source::script_hud,"ESTATE_DSM_USE_HINT",key::dsm_connect,"estate",feature::world},
		native_rule{source::world,"ESTATE_DSM_USE_HINT",key::dsm_connect,"estate",feature::world},
		native_rule{source::script_hud,"ESTATE_DSM_USE_HINT_PC",key::dsm_connect,"estate",feature::world},
		native_rule{source::world,"ESTATE_DSM_USE_HINT_PC",key::dsm_connect,"estate",feature::world},
		native_rule{source::script_hud,"ESTATE_DSM_PICKUP_HINT",key::dsm_recover,"estate",feature::world},
		native_rule{source::world,"ESTATE_DSM_PICKUP_HINT",key::dsm_recover,"estate",feature::world},
		native_rule{source::script_hud,"ESTATE_DSM_PICKUP_HINT_PC",key::dsm_recover,"estate",feature::world},
		native_rule{source::world,"ESTATE_DSM_PICKUP_HINT_PC",key::dsm_recover,"estate",feature::world},
		native_rule{source::script_hud,"ESTATE_USE_CLAYMORE_HINT",key::claymore_place,"estate",feature::special},
		native_rule{source::script_hud,"CLAYMORE_CLAYMORE_PRESS_TO_SWITCH_TO_CLAYMORE",key::claymore_place,"estate",feature::special},
		native_rule{source::script_hud,"AF_CAVES_RAPPEL_HINT",key::rappel_attach,"af_caves",feature::world},
		native_rule{source::world,"AF_CAVES_RAPPEL_HINT",key::rappel_attach,"af_caves",feature::world},
		native_rule{source::script_hud,"AF_CAVES_RAPPEL_HINT_PC",key::rappel_attach,"af_caves",feature::world},
		native_rule{source::world,"AF_CAVES_RAPPEL_HINT_PC",key::rappel_attach,"af_caves",feature::world},
		native_rule{source::script_hud,"GULAG_SPIE_HINT",key::gulag_attach,"gulag",feature::world},
		native_rule{source::world,"GULAG_SPIE_HINT",key::gulag_attach,"gulag",feature::world},
		native_rule{source::script_hud,"GULAG_SPIE_HINT_PC",key::gulag_attach,"gulag",feature::world},
		native_rule{source::world,"GULAG_SPIE_HINT_PC",key::gulag_attach,"gulag",feature::world},
		native_rule{source::script_hud,"GULAG_HOLD_1_TO_SPIE",key::gulag_attach,"gulag",feature::world},
		native_rule{source::world,"GULAG_HOLD_1_TO_SPIE",key::gulag_attach,"gulag",feature::world},
		native_rule{source::script_hud,"ARCADIA_LASER_HINT",key::designator_draw,"arcadia",feature::designator},
		native_rule{source::script_hud,"ARCADIA_LASER_HINT_GOLFCOURSE",key::designator_draw,"arcadia",feature::designator},
		native_rule{source::script_hud,"ARCADIA_LASER_ATTACK_HINT",key::designator_target,"arcadia",feature::designator},
		native_rule{source::script_hud,"ARCADIA_PICK_UP_BRIEFCASE_HINT",key::briefcase_pickup,"arcadia",feature::world},
		native_rule{source::world,"ARCADIA_PICK_UP_BRIEFCASE_HINT",key::briefcase_pickup,"arcadia",feature::world},
		native_rule{source::script_hud,"ARCADIA_PICK_UP_BRIEFCASE_HINT_PC",key::briefcase_pickup,"arcadia",feature::world},
		native_rule{source::world,"ARCADIA_PICK_UP_BRIEFCASE_HINT_PC",key::briefcase_pickup,"arcadia",feature::world},
		native_rule{source::script_hud,"INVASION_DRONE_PICKUP",key::notebook_pickup,"invasion",feature::world},
		native_rule{source::world,"INVASION_DRONE_PICKUP",key::notebook_pickup,"invasion",feature::world},
		native_rule{source::script_hud,"INVASION_DRONE_PICKUP_PC",key::notebook_pickup,"invasion",feature::world},
		native_rule{source::world,"INVASION_DRONE_PICKUP_PC",key::notebook_pickup,"invasion",feature::world},
		native_rule{source::script_hud,"HELLFIRE_USE_DRONE",key::notebook_open,{},feature::notebook_device},
		native_rule{source::script_hud,"HELLFIRE_USE_DRONE_2",key::notebook_open,{},feature::notebook_device},
		native_rule{source::script_hud,"INVASION_USE_DRONE",key::notebook_open,{},feature::notebook_device},
		native_rule{source::script_hud,"HELLFIRE_BOOST_PROMPT",key::notebook_boost,{},feature::notebook},
		native_rule{source::script_hud,"HELLFIRE_CANCEL_PROMPT",key::notebook_control,{},feature::notebook},
		native_rule{source::script_hud,"HELLFIRE_CANCEL_PROMPT_PC",key::notebook_control,{},feature::notebook},
		native_rule{source::script_hud,"HELLFIRE_CANCEL_PROMPT_WITH_CLAYMORE_PC",key::notebook_control,{},feature::notebook},
		native_rule{source::script_hud,"CANCEL_PROMPT_WITH_CLAYMORE_PC",key::notebook_control,{},feature::notebook},
		native_rule{source::script_hud,"VARIABLE_SCOPE_SNIPER_ZOOM",key::fixed_sniper_zoom,"dc_burning",feature::fixed_sniper},
		native_rule{source::script_hud,"CLIFFHANGER_SWITCH_HEARTBEAT",key::heartbeat_fold,"cliffhanger",feature::heartbeat},
		native_rule{source::script_hud,"CLIFFHANGER_ACTIVATE_HEARTBEAT",key::heartbeat_fold,"cliffhanger",feature::heartbeat},
		native_rule{source::script_hud,"CLIFFHANGER_BOARD",key::snowmobile_board,"cliffhanger",feature::world},
		native_rule{source::world,"CLIFFHANGER_BOARD",key::snowmobile_board,"cliffhanger",feature::world},
		native_rule{source::script_hud,"CLIFFHANGER_BOARD_PRESS",key::snowmobile_board,"cliffhanger",feature::world},
		native_rule{source::world,"CLIFFHANGER_BOARD_PRESS",key::snowmobile_board,"cliffhanger",feature::world},
		native_rule{source::script_hud,"CHAR_MUSEUM_DO_NOT",key::museum_warning,"ending",feature::museum},
		native_rule{source::world,"CHAR_MUSEUM_DO_NOT",key::museum_warning,"ending",feature::museum},
		native_rule{source::script_hud,"CHAR_MUSEUM_DO_NOT_PC",key::museum_warning,"ending",feature::museum},
		native_rule{source::world,"CHAR_MUSEUM_DO_NOT_PC",key::museum_warning,"ending",feature::museum},
		native_rule{source::script_hud,"CHAR_MUSEUM_DO_NOT_PRESS",key::museum_warning,"ending",feature::museum},
		native_rule{source::world,"CHAR_MUSEUM_DO_NOT_PRESS",key::museum_warning,"ending",feature::museum},
		native_rule{source::script_hud,"CHAR_MUSEUM_PRESS_USE",key::museum_warning,"ending",feature::museum},
		native_rule{source::world,"CHAR_MUSEUM_PRESS_USE",key::museum_warning,"ending",feature::museum},
		native_rule{source::script_hud,"TRAINER_HINT_ATTACK",key::trainer_fire,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_INVERT_CONTROL",key::trainer_menu,"trainer"},
		native_rule{source::script_hud,"TRAINER_HINT_INVERT_CONTROL_PC",key::trainer_menu,"trainer"},
		native_rule{source::script_hud,"TRAINER_HINT_ATTACK_PC",key::trainer_fire,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_HIP_ATTACK",key::trainer_hip_fire,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_HIP_ATTACK_PC",key::trainer_hip_fire,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_ADS",key::trainer_ads,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_ADS_360",key::trainer_ads,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_ADS_THROW",key::trainer_ads,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_ADS_THROW_360",key::trainer_ads,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_ADS_TOGGLE",key::trainer_ads,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_ADS_TOGGLE_THROW",key::trainer_ads,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_STOP_ADS",key::trainer_stop_ads,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_STOP_ADS_THROW",key::trainer_stop_ads,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_STOP_ADS_TOGGLE",key::trainer_stop_ads,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_STOP_ADS_TOGGLE_THROW",key::trainer_stop_ads,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_ADS_SWITCH",key::trainer_next_target,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_ADS_SWITCH_SHOULDER",key::trainer_next_target,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_ADS_SWITCH_THROW",key::trainer_next_target,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_ADS_SWITCH_THROW_SHOULDER",key::trainer_next_target,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_SIDEARM_SWAP",key::trainer_sidearm,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_SIDEARM",key::trainer_sidearm,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_PRIMARY_SWAP",key::trainer_primary,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_RELOAD",key::trainer_reload,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_RELOAD_USE",key::trainer_reload,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_SIDEARM_RELOAD",key::trainer_reload,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_SIDEARM_RELOAD_USE",key::trainer_reload,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_MELEE",key::trainer_knife,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_MELEE_BREATH",key::trainer_knife,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_MELEE_BREATH_CLICK",key::trainer_knife,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_MELEE_CLICK",key::trainer_knife,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_CROUCH",key::trainer_crouch,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_CROUCH_HOLD",key::trainer_crouch,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_CROUCH_STANCE",key::trainer_crouch,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_CROUCH_TOGGLE",key::trainer_crouch,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_STAND",key::trainer_stand,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_STAND_STANCE",key::trainer_stand,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_STAND_UP",key::trainer_stand,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_PRONE",key::trainer_prone,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_PRONE_DOUBLE",key::trainer_prone,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_PRONE_HOLD",key::trainer_prone,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_PRONE_STANCE",key::trainer_prone,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_PRONE_TOGGLE",key::trainer_prone,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_JUMP",key::trainer_jump,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_JUMP_STAND",key::trainer_jump,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_MANTLE",key::trainer_mantle,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_SPRINT",key::trainer_sprint,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_SPRINT_PC",key::trainer_sprint,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_SPRINT_BREATH",key::trainer_sprint,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_SPRINT_BREATH_PC",key::trainer_sprint,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_FRAG",key::trainer_frag,"trainer",feature::carry},
		native_rule{source::script_hud,"TRAINER_HINT_FLASH",key::trainer_flash,"trainer",feature::carry},
		native_rule{source::script_hud,"SCRIPT_LEARN_GRENADE_LAUNCHER",key::m203_reload,"roadkill",feature::m203},
		native_rule{source::lui,"PLATFORM_HOLD_TO_SKIP",key::cinematic_skip,{}},
		native_rule{source::lui,"PLATFORM_HOLD_TO_SKIP_KEYBOARD",key::cinematic_skip,{}},
	};
	struct context {bool vr{};std::string_view map;feature available{};};
	constexpr const native_rule* identify(source producer,std::string_view name) noexcept
	{
		if(name.size()>128 || name.find('\0')!=name.npos)return nullptr;
		if(producer==source::lui && name.starts_with('@'))name.remove_prefix(1);
		for(const auto& rule:native_rules)if(rule.producer==producer && rule.native_key==name)return &rule;
		return nullptr;
	}
	constexpr std::optional<key> resolve(source producer,std::string_view name,const context& state) noexcept
	{
		const auto* rule=identify(producer,name);
		if(!state.vr || !rule || (!rule->map.empty() && rule->map!=state.map) ||
			!includes(state.available,rule->required))return {};
		return rule->message;
	}
	struct arguments {int hand{-1};bool shared{};std::string_view item;};
	struct message {game_text::locale language;game_text::runs parts;};
	enum class style { plain, native_colors };
	inline std::optional<message> compose(key id,game_text::locale requested,arguments args={})
	{
		const auto* entry=describe(id);
		if(!entry || args.item.size()>game_text::max_text_bytes || args.item.find('\0')!=args.item.npos)return {};
		key button=key::count;
		switch(entry->operation)
		{
		case control::none:break;
		case control::trigger:button=key::button_trigger;break;
		case control::right_stick_down:button=key::right_stick_down;break;
		case control::grip:
			if(!args.shared && args.hand!=0 && args.hand!=1)return {};
			button=game_text::grip_key(args.hand,args.shared);break;
		}
		// Exact locale only. Absence declines the override so the original game
		// producer retains the complete current-language text and native bindings.
		const auto sentence=game_text::translation(id,requested);if(!sentence)return {};
		const auto operation=button==key::count?std::optional<std::string_view>{std::string_view{}}:game_text::translation(button,requested);
		if(!operation)return {};
		const bool generic_item=args.item.empty() && (game_text::parameters(id)&2);
		const auto item=generic_item?game_text::translation(key::item_weapon,requested):std::optional<std::string_view>{args.item};
		if(!item)return {};
		auto parts=game_text::interpolate(*sentence,{{"button",*operation,true},{"item",*item}});
		if(parts.empty())return {};
		return message{requested,std::move(parts)};
	}
	// The stock cursor-hint producer selects use/pickup/resupply and binding
	// variants internally. Transfer that whole producer back to native when the
	// world-label family is incomplete; never mix it with partial VR labels.
	constexpr bool owns_world_text(game_text::locale language) noexcept
	{
		for(const auto id:{key::interaction_use,key::interaction_pickup,key::interaction_resupply,
			key::button_grip_left,key::button_grip_right,key::button_grip_either,key::item_weapon})
			if(!game_text::has_translation(id,language))return false;
		return true;
	}
	inline std::string text(const message& value,style presentation=style::plain)
	{
		if(value.parts.size()>game_text::max_runs)return {};
		std::string result;
		for(const auto& part:value.parts)
		{
			const bool highlight=presentation==style::native_colors && part.emphasized;
			const auto remaining=game_text::max_text_bytes-result.size();
			if(part.value.size()>remaining || (highlight && remaining-part.value.size()<4))return {};
			if(highlight)result+="^3";
			result+=part.value;
			if(highlight)result+="^7";
		}
		return result;
	}
	inline std::optional<std::string> replace(source producer,std::string_view name,const context& state,game_text::locale language)
	{
		const auto id=resolve(producer,name,state);if(!id)return {};
		const auto value=compose(*id,language,{-1,true,{}});if(!value)return {};
		auto result=text(*value,style::native_colors);if(result.empty())return {};
		return result;
	}
	// Runtime entry snapshots game language/context only for recognized keys.
	// No cache, script VM access, deferred jobs or renderer/gameplay callbacks.
	std::optional<std::string> replace(source,std::string_view native_key);
	std::optional<key> world_message(unsigned hint);
	bool native_cursor_required();
	// Native sethintstring uses 1..31 in its own config-string range.
	constexpr bool valid_world_hint(unsigned hint) noexcept {return hint>0 && hint<32;}

	static_assert([] {
		for(std::size_t i=0;i<definitions.size();++i)
		{
			const auto& entry=definitions[i];
			if(static_cast<std::size_t>(entry.message)>=game_text::key_count)return false;
			const auto parameters=game_text::parameters(entry.message);
			if(bool(parameters&1)!=(entry.operation!=control::none))return false;
			for(std::size_t j=0;j<i;++j)if(definitions[j].message==entry.message)return false;
		}
		for(std::size_t i=0;i<native_rules.size();++i)
		{
			const auto& rule=native_rules[i];const auto* entry=describe(rule.message);
			if(!entry || rule.native_key.empty() || rule.native_key.size()>128)return false;
			for(std::size_t j=0;j<i;++j)if(native_rules[j].producer==rule.producer && native_rules[j].native_key==rule.native_key)return false;
		}
		return true;
	}(),"HUD prompt rules must be unique and provide their complete argument contract");
}
