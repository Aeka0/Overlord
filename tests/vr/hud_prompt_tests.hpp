#pragma once
#include "component/vr/hud_prompts.hpp"

template<class Check>void hud_prompt_tests(Check& check)
{
	using namespace vr::hud_prompts;using game_text::locale;
	const auto en=locale::english,zh=locale::simplified_chinese;
	for(const auto language:{locale::english,locale::simplified_chinese,locale::french})
	{
		for(const auto id:{key::trainer_menu,key::fixed_sniper_controls,key::trainer_objectives,key::trainer_recenter})
		{
			const auto touch=compose(id,language);
			const auto index=compose(id,language,{},true);
			check(touch && index && text(*touch)!=text(*index),"controller-specific face-button instructions follow the active profile");
		}
	}
	const auto menu_index=compose(key::trainer_menu,en,{},true);
	check(menu_index && text(*menu_index)=="Press A on the left controller to open the menu and set vertical look inversion",
		"Index menu preserves the Touch sentence and substitutes only A for X");
	for(const auto& entry:definitions)
	{
		if(uses_controller_profile(entry.message))continue;
		const auto touch=compose(entry.message,en,{0,false,"weapon"});
		const auto index=compose(entry.message,en,{0,false,"weapon"},true);
		check(touch && index && text(*touch)==text(*index),"all other Index instructions preserve original text including skip");
	}

	const auto boarding = compose(key::snowmobile_board, zh);
	check(boarding && text(*boarding) == game_text::utf8(u8"雪地摩托停稳后，靠近即可自动上车"),
		"snowmobile boarding explains the story stop and proximity without a hand or button argument");
	const context snowmobile{true,"cliffhanger",feature::snowmobile};
	for(const auto* name:{"SCRIPT_PLATFORM_SNOWMOBILE_DRIVE","SCRIPT_PLATFORM_SNOWMOBILE_DRIVE_KEYBOARD"})
		check(replace(source::script_hud,name,snowmobile,zh)==
			game_text::utf8(u8"用^3手柄^7驾驶，握住^3车把^7微调方向"),
			"snowmobile drive aliases explain controller driving and handlebar fine adjustment");
	for(const auto* name:{"SCRIPT_PLATFORM_SNOWMOBILE_ATTACK","SCRIPT_PLATFORM_SNOWMOBILE_ATTACK_KEYBOARD"})
		check(replace(source::script_hud,name,snowmobile,zh)==game_text::utf8(
			u8"从^3胸挂^7取出武器，扣动^3持枪手扳机^7开火\n卸下^3弹匣^7后，将武器停留在^3胸前^7自动装弹"),
			"snowmobile attack aliases combine chest-rig draw and empty-well chest auto reload in two lines");
	for(const auto* name:{"SCRIPT_PLATFORM_SNOWMOBILE_REVERSE","SCRIPT_PLATFORM_SNOWMOBILE_REVERSE_KEYBOARD",
		"SCRIPT_PLATFORM_SNOWMOBILE_REVERSE_BUTTON_INVERSED"})
		check(replace(source::script_hud,name,snowmobile,zh)==game_text::utf8(u8"向后推动^3摇杆^7倒车"),
			"every snowmobile reverse binding variant uses the approved backward stick instruction");
	for(const auto* name:{"SCRIPT_PLATFORM_SNOWMOBILE_DRIVE","SCRIPT_PLATFORM_SNOWMOBILE_DRIVE_KEYBOARD",
		"SCRIPT_PLATFORM_SNOWMOBILE_ATTACK","SCRIPT_PLATFORM_SNOWMOBILE_ATTACK_KEYBOARD",
		"SCRIPT_PLATFORM_SNOWMOBILE_REVERSE","SCRIPT_PLATFORM_SNOWMOBILE_REVERSE_KEYBOARD",
		"SCRIPT_PLATFORM_SNOWMOBILE_REVERSE_BUTTON_INVERSED"})
		check(!replace(source::script_hud,name,{true,"cliffhanger"},zh) &&
			!replace(source::script_hud,name,{false,"cliffhanger",feature::snowmobile},zh) &&
			!replace(source::script_hud,name,{true,"af_chase",feature::snowmobile},zh) &&
			!replace(source::world,name,snowmobile,zh) && !replace(source::script_hud,name,snowmobile,locale::czech),
			"snowmobile reminders require their physical driver, map, VR and script HUD producer and preserve native fallback");
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
	for(const auto* name:{"FAVELA_ESCAPE_HINT_SPRINT","FAVELA_ESCAPE_HINT_SPRINT_PC"})
	{
		check(replace(source::script_hud,name,{true,"favela_escape"},zh)==
			game_text::utf8(u8"前推^3左摇杆^7移动，按下^3左摇杆^7冲刺"),
			"Hornet's Nest native sprint aliases reuse the localized locomotion instruction without weapon carry");
		check(!replace(source::script_hud,name,favela,zh) &&
			!replace(source::script_hud,name,{false,"favela_escape"},zh) &&
			!replace(source::lui,name,{true,"favela_escape"},zh) &&
			!replace(source::script_hud,name,{true,"favela_escape"},locale::czech),
			"sprint hints retain mission, VR, producer and exact-locale boundaries");
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
	const context rappel{true,"af_caves",feature::rappel};
	check(replace(source::script_hud,"AF_CAVES_DESCEND",rappel,zh)==game_text::utf8(u8"按住^3扳机键^7减速") &&
		replace(source::script_hud,"SCRIPT_PLATFORM_OILRIG_HINT_STEALTH_KILL",rappel,zh)==
			game_text::utf8(u8"按下^3扳机键^7刺杀哨兵"),
		"af_caves native descent and reused guard keys preserve the existing VR sentences through the shared producer");
	const auto* rappel_guard=identify(source::script_hud,"SCRIPT_PLATFORM_OILRIG_HINT_STEALTH_KILL","af_caves");
	const auto* oilrig_guard=identify(source::script_hud,"SCRIPT_PLATFORM_OILRIG_HINT_STEALTH_KILL","oilrig");
	check(rappel_guard && oilrig_guard && rappel_guard->required==feature::rappel && oilrig_guard->required==feature::none &&
		!identify(source::script_hud,"SCRIPT_PLATFORM_OILRIG_HINT_STEALTH_KILL","estate"),
		"shared native key selects the matching disjoint mission scope instead of the first registry row");
	for(const auto* name:{"AF_CAVES_DESCEND","SCRIPT_PLATFORM_OILRIG_HINT_STEALTH_KILL"})
		check(!replace(source::script_hud,name,{true,"af_caves"},zh) &&
			!replace(source::script_hud,name,{false,"af_caves",feature::rappel},zh) &&
			!replace(source::world,name,rappel,zh) && !replace(source::lui,name,rappel,zh) &&
			!replace(source::script_hud,name,rappel,locale::czech),
			"rappel override requires its adapter, VR and script HUD identity, with exact-locale native fallthrough");
	check(!replace(source::script_hud,"AF_CAVES_DESCEND",oilrig,zh) &&
		!replace(source::script_hud,"SCRIPT_PLATFORM_OILRIG_HINT_STEALTH_KILL",{true,"estate",feature::rappel},zh),
		"rappel feature cannot grant a hint on an unrelated map or change Oilrig descent instructions");
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
	const context physical_grenades{true,"roadkill",feature::grenades};
	check(replace(source::script_hud,"SCRIPT_PLATFORM_HINT_FLASH",physical_grenades,zh)==
		game_text::utf8(u8"从^3胸挂^7取出闪光弹，用另一只手按住^3扳机键^7拔出^3拉环^7，再松开^3握持键^7投出") &&
		resolve(source::script_hud,"SCRIPT_PLATFORM_HINT_FLASH",physical_grenades)==key::trainer_flash,
		"Team Player reuses the chest-rig flash instruction with emphasized pin-pull and release controls");
	check(!replace(source::script_hud,"SCRIPT_PLATFORM_HINT_FLASH",{true,"roadkill",feature::carry},zh) &&
		!replace(source::script_hud,"SCRIPT_PLATFORM_HINT_FLASH",{true,"trainer",feature::grenades},zh) &&
		!replace(source::world,"SCRIPT_PLATFORM_HINT_FLASH",physical_grenades,zh),
		"Team Player flash override requires its exact mission, script producer and physical grenade capability");
	const auto cooking=replace(source::script_hud,"SCRIPT_GRENADE_SUICIDE_LINE2",physical_grenades,zh);
	check(cooking==game_text::utf8(u8"拔出^3拉环^7后，按下^3持雷手柄上较远的按键^7或^3同手扳机键^7解除保险") &&
		replace(source::script_hud,"SCRIPT_GRENADE_SUICIDE_LINE2",{true,"trainer",feature::grenades},zh)==cooking &&
		replace(source::script_hud,"SCRIPT_GRENADE_SUICIDE_LINE2",physical_grenades,zh,true)==cooking,
		"shared grenade death instruction preserves the approved future-trigger wording without face-button names or a held weapon dependency");
	check(!replace(source::script_hud,"SCRIPT_GRENADE_SUICIDE_LINE2",{true,"roadkill"},zh) &&
		!replace(source::script_hud,"SCRIPT_GRENADE_SUICIDE_LINE2",{false,"roadkill",feature::grenades},zh) &&
		!replace(source::script_hud,"SCRIPT_GRENADE_SUICIDE_LINE2",physical_grenades,locale::czech) &&
		!identify(source::script_hud,"SCRIPT_GRENADE_SUICIDE_LINE1"),
		"grenade replacement retains native fallback and leaves the original death-reason line alone");
	const auto pause=replace(source::script_hud,"TRAINER_HINT_INVERT_CONTROL_PC",trainer,zh);
	check(pause&&pause->find("ESC")==std::string::npos&&pause->find(game_text::utf8(u8"左手柄 X 键"))!=std::string::npos,
		"trainer menu instruction names the actual left-controller pause key");
	// Independently list the native producer aliases: iterating native_rules
	// alone cannot catch an omitted keyboard/gamepad variant.
	for(const auto* name:{"TRAINER_HINT_CHECK_OBJECTIVES_PAUSED","TRAINER_HINT_CHECK_OBJECTIVES_SCORES",
		"TRAINER_HINT_WEAPON_INSPECT","TRAINER_HINT_WEAPON_INSPECT_PC"})
	{
		const bool objectives=std::string_view(name).starts_with("TRAINER_HINT_CHECK_OBJECTIVES_");
		const auto id=objectives?key::trainer_objectives:key::trainer_recenter;
		check(resolve(source::script_hud,name,{true,"trainer"})==id &&
			replace(source::script_hud,name,{true,"trainer"},zh)==game_text::utf8(objectives?
				u8"按下^3左手柄 X 键^7打开菜单":u8"按住^3左手柄 X 键^7重置视角"),
			"trainer objective and inspect producers show the approved tap/hold instructions without a weapon dependency");
		check(replace(source::script_hud,name,{true,"trainer"},zh,true)==game_text::utf8(objectives?
				u8"按下^3左手柄 A 键^7打开菜单":u8"按住^3左手柄 A 键^7重置视角"),
			"Index uses its shared left A menu/recenter binding for both trainer instructions");
		check(!replace(source::script_hud,name,{false,"trainer"},zh) &&
			!replace(source::script_hud,name,{true,"estate"},zh) &&
			!replace(source::lui,name,{true,"trainer"},zh) &&
			!replace(source::script_hud,name,{true,"trainer"},locale::czech),
			"new trainer instructions retain VR, mission, producer and exact-locale boundaries");
	}
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
