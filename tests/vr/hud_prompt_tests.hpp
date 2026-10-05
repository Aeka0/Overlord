#pragma once
#include "component/vr/hud_prompts.hpp"

template<class Check>void hud_prompt_tests(Check& check)
{
	using namespace vr::hud_prompts;using game_text::locale;
	const auto en=locale::english,zh=locale::simplified_chinese;
	const context estate{true,"estate"},favela{true,"favela"},flare{true,"dc_whitehouse",feature::signal_flare};
	for(const auto* name:{"ESTATE_LEARN_PRONE","ESTATE_LEARN_PRONE_TOGGLE","ESTATE_LEARN_PRONE_HOLDDOWN"})
	{
		check(resolve(source::script_hud,name,estate)==key::mine_prone,"every native stance binding variant selects one semantic mine instruction");
		check(!resolve(source::script_hud,name,favela) && !resolve(source::lui,name,estate),"mission and producer are part of prompt identity");
		const std::string native_french="^3Ctrl^7 : instruction originale, parametre 42";
		const auto replacement=replace(source::script_hud,name,estate,locale::czech);
		check(!replacement && replacement.value_or(native_french)==native_french,
			"missing locale preserves the complete current native text, glyphs and parameters instead of supplying English");
	}
	for(const auto* name:{"FAVELA_DUCK_HINT","FAVELA_DUCK_HINT_KEYBOARD"})
	{
		check(resolve(source::script_hud,name,favela)==key::vehicle_duck,"vehicle controller and keyboard aliases share one instruction");
		const auto french=replace(source::script_hud,name,favela,locale::french);
		check(french && french->find("Stick droit vers le bas")!=std::string::npos && french->find("Right Stick")==std::string::npos,
			"translated action and translated sentence are composed in the same locale");
	}
	for(const auto* name:{"SCRIPT_PLATFORM_HINTSTR_POPFLARE","SCRIPT_PLATFORM_HINTSTR_POPFLARE_KEYBOARD"})
	{
		check(resolve(source::script_hud,name,flare)==key::signal_flare &&
			!resolve(source::script_hud,name,{true,"dc_whitehouse"}) && !resolve(source::script_hud,name,estate),
			"flare text requires its map and physical mission adapter, not only a familiar key");
		check(replace(source::script_hud,name,flare,zh)==game_text::utf8(u8"从^3装备位^7取出信号弹，移除^3盖子^7点燃"),
			"central route preserves the approved flare instruction exactly");
	}
	for(const auto* name:{"Ctrl","[{+stance}]","MENU_CROUCH","FAVELA_DUCK_HINT_OTHER","Press Ctrl to go prone"})
		check(!resolve(source::script_hud,name,favela),"glyphs, translated prose and key substrings cannot replace unrelated text");
	check(!resolve(source::script_hud,game_text::text(key::mine_prone,zh),estate),"translated template is not accepted as native identity");
	check(!identify(source::lui,std::string(100000,'x')) &&
		!identify(source::lui,std::string_view("PLATFORM_HOLD_TO_SKIP\0junk",25)),"oversized or embedded-NUL source keys fail before formatting");
	check(replace(source::lui,"@PLATFORM_HOLD_TO_SKIP_KEYBOARD",{true,{}},en)=="Hold ^3Trigger^7 or ^3Enter^7 to skip",
		"LUI normalization and generated HUD composition use the same registry");
	check(!replace(source::lui,"PLATFORM_HOLD_TO_SKIP",{false,{}},en) &&
		!replace(source::lui,"MENU_SP_OFFENSIVE_SKIP_NOW",{true,{}},en),"native menu fallthrough preserves flat gameplay and other skip warnings");
	for(const auto& rule:native_rules)
	{
		check(!resolve(rule.producer,rule.native_key,{false,rule.map,rule.required}),"all registered overrides preserve disabled VR behavior");
		for(unsigned l=0;l<unsigned(locale::count);++l)
		{
			const auto value=replace(rule.producer,rule.native_key,{true,rule.map,rule.required},locale(l));
			if(game_text::has_translation(rule.message,locale(l)))
				check(value && !value->empty() && value->size()<=game_text::max_text_bytes && value->find('{')==value->npos,
					"complete existing native-rule translations produce bounded text in their own locale");
			else check(!value,"native-rule translation holes always decline the override");
		}
	}
	for(const auto& entry:definitions)for(unsigned l=0;l<unsigned(locale::count);++l)
	{
		const auto result=compose(entry.message,locale(l),{0,false,"M4A1"});
		if(l!=unsigned(locale::czech) && l!=unsigned(locale::turkish))
			check(result && !text(*result).empty(),"Every official game locale composes every existing VR instruction");
		if(result)check(result->language==locale(l) && !text(*result,style::native_colors).empty(),"generated instructions can never cross-fallback into another MOD language");
		if(!game_text::has_translation(entry.message,locale(l)))check(!result,"generated translation holes return ownership to the native producer");
	}
	const auto melee=compose(key::story_melee,zh);
	check(melee && text(*melee,style::native_colors)==game_text::utf8(u8"按下^3扳机键^7刺杀哨兵"),
		"scripted instructions resolve and highlight their control without renderer-specific assembly");
	const auto pickup=compose(key::interaction_pickup,en,{0,false,"AK {button}"});
	check(pickup && text(*pickup)=="Hold Left Grip to pick up AK {button}","native item names are inserted literally, never reinterpreted as templates");
	const auto shared=compose(key::interaction_resupply,zh,{-1,true,{}});
	check(shared && text(*shared)==game_text::utf8(u8"按住左／右手握持键补充弹药"),"either-hand prompts use one complete localized operation label");
	check(!compose(key::interaction_use,en) && !compose(key::interaction_use,en,{9,false,{}}),"invalid hand cannot silently become a right-hand instruction");
	check(!compose(key::interaction_pickup,en,{1,false,std::string(game_text::max_text_bytes+1,'x')}),"large native labels cannot cause unbounded prompt assembly");
	check(!compose(key::interaction_pickup,en,{1,false,std::string_view("M4\0A1",5)}),"embedded NUL cannot split native measurement from rendered text");
	const auto unnamed=compose(key::interaction_pickup,zh,{1,false,{}});
	check(unnamed && text(*unnamed)==game_text::utf8(u8"按住右手握持键拾取武器"),"missing native item name uses the sentence locale's generic noun");
	check(!compose(key::weapon_needs_chamber,zh) && !compose(static_cast<key>(999),zh),"non-action captions and invalid keys cannot accidentally become native prompt overrides");
	const auto invalid_locale=compose(key::signal_flare,static_cast<locale>(999));
	check(!invalid_locale,"invalid game language preserves native text instead of guessing English");
	check(owns_world_text(en) && owns_world_text(zh) && owns_world_text(locale::french) && !owns_world_text(locale::czech) && !owns_world_text(locale::count),
		"world cursor text returns to its original native producer when the VR sentence family is incomplete");
	const std::string original_skip{game_text::utf8(u8"Maintenez ^3Entree^7 pour passer")};
	check(!replace(source::lui,"PLATFORM_HOLD_TO_SKIP",{true,{}},locale::czech) &&
		replace(source::lui,"PLATFORM_HOLD_TO_SKIP",{true,{}},locale::czech).value_or(original_skip)==original_skip,
		"LUI missing translation invokes its original formatter instead of substituting an English skip sentence");
	const auto a=replace(source::script_hud,"ESTATE_LEARN_PRONE",estate,zh);
	const auto b=replace(source::script_hud,"ESTATE_LEARN_PRONE",estate,en);
	const auto c=replace(source::script_hud,"ESTATE_LEARN_PRONE",estate,zh);
	check(a && b && c && *a!=*b && *a==*c,"language changes need no previous translated-text cache or level restart");
	message too_large{en,{{std::string(game_text::max_text_bytes,'x'),true}}};
	check(text(too_large).size()==game_text::max_text_bytes && text(too_large,style::native_colors).empty(),"native color markup is included in the final byte limit");
	check(game_text::template_parameters(u8"{item}: {button}, {button}")==3 &&
		game_text::template_parameters(u8"{botton}")==game_text::invalid_parameters &&
		game_text::template_parameters(u8"{button")==game_text::invalid_parameters &&
		game_text::template_parameters(u8"button}")==game_text::invalid_parameters,"catalog validation accepts reordering but rejects misspelled or malformed placeholders");
	const context trainer{true,"trainer",feature::carry};
	const context oilrig{true,"oilrig",feature::special};
	check(replace(source::script_hud,"SCRIPT_PLATFORM_OILRIG_HINT_STEALTH_KILL",oilrig,zh)==
		game_text::utf8(u8"按下^3扳机键^7刺杀哨兵") &&
		!resolve(source::script_hud,"SCRIPT_PLATFORM_OILRIG_HINT_STEALTH_KILL",estate),
		"Oilrig native guard hint uses the old text in its original lifecycle and cannot escape the mission");
	check(resolve(source::script_hud,"OILRIG_HINT_C4_SWITCH",oilrig)==key::c4_draw &&
		resolve(source::script_hud,"OILRIG_HINT_C4_DETONATE",oilrig)==key::c4_detonate &&
		!resolve(source::script_hud,"OILRIG_HINT_C4_SWITCH",{true,"oilrig"}),
		"Oilrig C4 hints require the physical abdominal equipment adapter");
	const context roadkill{true,"roadkill",feature::m203};
	const auto reload=replace(source::script_hud,"SCRIPT_LEARN_GRENADE_LAUNCHER",roadkill,zh);
	check(reload && reload->find(game_text::utf8(u8"发射后"))!=std::string::npos &&
		reload->find(game_text::utf8(u8"^3向前推^7"))!=std::string::npos &&
		reload->find(game_text::utf8(u8"^3腰间^7"))!=std::string::npos &&
		reload->find(game_text::utf8(u8"^3向后拉^7"))!=std::string::npos && std::count(reload->begin(),reload->end(),'\n')==2,
		"Team Player M203 prompt explains reload with emphasized controls, locations and directions");
	check(!replace(source::script_hud,"SCRIPT_LEARN_GRENADE_LAUNCHER",{true,"dc_burning",feature::m203},zh) &&
		!replace(source::script_hud,"SCRIPT_LEARN_GRENADE_LAUNCHER",{true,"roadkill",feature::carry},zh) &&
		!replace(source::lui,"SCRIPT_LEARN_GRENADE_LAUNCHER",roadkill,zh) &&
		!replace(source::script_hud,"SCRIPT_LEARN_GRENADE_LAUNCHER",roadkill,locale::czech),
		"M203 replacement requires the exact mission, producer, active feature and a complete translation");
	check(resolve(source::script_hud,"HELLFIRE_USE_DRONE",{true,"contingency",feature::notebook_device})==key::notebook_open &&
		resolve(source::script_hud,"HELLFIRE_CANCEL_PROMPT_PC",{true,"contingency",feature::notebook})==key::notebook_control &&
		!resolve(source::script_hud,"HELLFIRE_USE_DRONE",{true,"contingency",feature::special}),
		"shared AGM prompts follow notebook ownership in Contingency as well as invasion, never an unrelated abdominal device");
	check(replace(source::script_hud,"TRAINER_HINT_SIDEARM_SWAP",trainer,zh)==
		game_text::utf8(u8"步枪可以收纳至^3背后^7") &&
		replace(source::script_hud,"TRAINER_HINT_PRIMARY_SWAP",trainer,zh)==game_text::utf8(u8"你可以将手枪挂在^3腰间^7或^3背后^7"),
		"trainer storage instructions follow the VR action order without moving physical holsters");
	const auto frag=replace(source::script_hud,"TRAINER_HINT_FRAG",trainer,zh);
	check(frag&&frag->starts_with(game_text::utf8(u8"先将步枪收纳至^3背后^7\n从^3胸前^7")),"grenade instruction first frees hands by stowing the rifle on its own line");
	const auto pause=replace(source::script_hud,"TRAINER_HINT_INVERT_CONTROL_PC",trainer,zh);
	check(pause&&pause->find("ESC")==std::string::npos&&pause->find(game_text::utf8(u8"左手柄 X 键"))!=std::string::npos,
		"trainer menu instruction names the actual left-controller pause key");
	check(replace(source::script_hud,"TRAINER_HINT_SIDEARM_SWAP",trainer,en).has_value() &&
		!replace(source::script_hud,"TRAINER_HINT_SIDEARM_SWAP",{true,"trainer"},zh) &&
		!replace(source::script_hud,"TRAINER_HINT_SIDEARM_SWAP",{true,"estate",feature::carry},zh),
		"Translated trainer instructions cannot escape their physical carry mission");
	const context dsm{true,"estate",feature::world};
	check(resolve(source::world,"ESTATE_DSM_USE_HINT_PC",dsm)==key::dsm_connect &&
		resolve(source::world,"ESTATE_DSM_PICKUP_HINT_PC",dsm)==key::dsm_recover &&
		!replace(source::world,"ESTATE_DSM_USE_HINT_PC",dsm,locale::czech),
		"native target hint identity distinguishes connect and recover, with exact-locale fallthrough");
	const auto connect=compose(key::dsm_connect,zh,{0,false,{}});
	check(connect && text(*connect,style::native_colors)==game_text::utf8(u8"对准^3黄色标记^7，按住^3左手握持键^7连接 DSM"),
		"world prompts emphasize both the location and the actual interacting hand");
	check(valid_world_hint(1) && valid_world_hint(31) && !valid_world_hint(0) && !valid_world_hint(32) && !valid_world_hint(0xffffffffu),
		"world identities cannot cross into the script HUD config-string namespace");
	for(const auto& entry:definitions)
	{
		const auto value=compose(entry.message,zh,{0,false,{}});
		check(value && text(*value).find(game_text::utf8(u8"。"))==std::string::npos && text(*value).find("<em>")==std::string::npos,
			"every Chinese action prompt is free of sentence periods and raw formatting tags");
	}
	for(const auto* pending:{"TRAINER_HINT_EQUIP_C4","TRAINER_HINT_FIREMODE","CLIFFHANGER_HOW_TO_CLIMB_PC","SCRIPT_PLATFORM_STEER_DRONE","AF_CHASE_PRESS_USE"})
		check(!identify(source::script_hud,pending),"unreviewed interaction chains do not acquire invented instructions");
	check(!resolve(source::script_hud,"VARIABLE_SCOPE_SNIPER_ZOOM",{true,"dc_burning"}) &&
		resolve(source::script_hud,"VARIABLE_SCOPE_SNIPER_ZOOM",{true,"dc_burning",feature::fixed_sniper})==key::fixed_sniper_zoom &&
		!resolve(source::world,"CHAR_MUSEUM_DO_NOT_PC",{true,"ending",feature::world}),
		"shared scope and ending map are insufficient without the exact interaction owner");
}
