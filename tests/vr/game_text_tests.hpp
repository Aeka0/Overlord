#pragma once
#include "component/game_text.hpp"
#include "component/game_text_native.hpp"

template<class Check> void game_text_tests(Check check)
{
	using namespace game_text;
	const auto en=locale::english,zh=locale::simplified_chinese;
	check(text(key::weapon_magazine_empty,zh)==utf8(u8"弹匣为空") && text(key::weapon_no_ammo,zh)==utf8(u8"无弹药") &&
		text(key::weapon_magazine_empty,en)=="Magazine empty" && text(key::weapon_no_ammo,en)=="No ammo",
		"empty-magazine and reserve warnings preserve the requested labels");
	check(text(key::weapon_quick_loading,zh)==utf8(u8"正在快捷装弹") &&
		text(key::weapon_quick_loading,en)=="Quick reloading" &&
		text(key::weapon_quick_loading,locale::traditional_chinese)==utf8(u8"正在快捷裝彈"),
		"quick reload caption preserves the requested Chinese text and shared locale fallbacks");
	check(format(key::vehicle_duck,zh,{{"button","^3"+std::string(text(key::right_stick_down,zh))+"^7"}})==
		utf8(u8"^3下推右摇杆^7，低头躲避"),
		"car hint highlights the complete right-stick operation and restores ordinary text color");
	check(effective_locale(key::mine_prone,locale::czech)==en && effective_locale(key::vehicle_duck,locale::czech)==locale::czech,
		"partially translated messages keep fallback sentences and embedded control labels in one language");
	for (int language=0;language<game::LANGUAGE_COUNT;++language)
	{
		const auto selected=native_locale(language);
		check(effective_locale(key::vehicle_duck,selected)==selected && effective_locale(key::right_stick_down,selected)==selected,
			"every game language has both the vehicle hint and its control label");
		const auto duck=format(key::vehicle_duck,selected,{{"button","^3"+std::string(text(key::right_stick_down,selected))+"^7"}});
		check(duck.find("^3")!=std::string::npos && duck.find("^7")!=std::string::npos && duck.find("{button}")==std::string::npos,
			"translated car hints preserve yellow highlighting without unresolved placeholders");
		check(!catalogs[static_cast<std::size_t>(selected)][static_cast<std::size_t>(key::weapon_needs_chamber)].empty(),
			"every native game language has a chamber warning translation");
		if (language!=game::LANGUAGE_ENGLISH && language!=game::LANGUAGE_ENGLISH_SAFE)
			check(selected!=en,"non-English game languages are not collapsed to English");
	}
	check(native_locale(game::LANGUAGE_JAPANESE_PARTIAL)==locale::japanese &&
		native_locale(game::LANGUAGE_RUSSIAN_PARTIAL)==locale::russian &&
		native_locale(game::LANGUAGE_ENGLISH_SAFE)==en,"content/voice editions share their written language");
	check(native_locale(-1)==en && native_locale(game::LANGUAGE_COUNT)==en,"invalid native language falls back safely");
	check(text(key::button_trigger,locale::czech)==text(key::button_trigger,en),
		"partial catalogs preserve existing per-key English fallback");
	check(text(key::weapon_needs_chamber,zh)==utf8(u8"需要上膛") &&
		text(key::weapon_needs_chamber,en)=="Chamber round","chamber warning preserves requested Chinese and English fallback");
	check(format(key::interaction_resupply,en,{{"button","Left Grip",true}})=="Hold Left Grip to resupply ammo" &&
		format(key::interaction_resupply,zh,{{"button",text(key::button_grip_right,zh),true}})==
		utf8(u8"按住右手握持键补充弹药"),"resupply prompts name the action and preserve localized controller labels");
	check(text(key::recording_preview,en)=="Live stream preview mode - Disable in the launcher's VR Settings",
		"English game language selects the English recording preview caption");
	check(text(key::recording_preview,zh)==utf8(u8"直播画面预览模式 - 可在启动器 VR 设置页关闭"),
		"Chinese game language retains the requested recording preview caption");
	check(text(key::recording_preview,static_cast<locale>(99))==text(key::recording_preview,en),
		"Unimplemented recording preview locales follow the shared English fallback");
	const auto mine=format(key::mine_prone,zh,{{"button","^3"+std::string(text(key::right_stick_down,zh))+"^7"}});
	check(mine.find("\x5e\x33\xe4\xb8\x8b\xe6\x8e\xa8\xe5\x8f\xb3\xe6\x91\x87\xe6\x9d\x86\x5e\x37")!=std::string::npos && mine.find("Ctrl")==std::string::npos,"mine prompt emphasizes the VR stick direction in native yellow and restores normal text color");
	for(std::size_t i=0;i<key_count;++i)
	{
		const auto id=static_cast<key>(i);
		for(const auto language : {en,zh,locale::traditional_chinese,locale::french,locale::german,
			locale::spanish,locale::russian,locale::japanese,locale::korean,locale::italian,
			locale::polish,locale::portuguese,locale::arabic,locale::spanish_latin_america})
			check(has_translation(id,language) && !format(id,language,{{"button","[BUTTON]",true},{"item","[ITEM]"}}).empty(),
				"Every official game locale covers and formats every existing semantic key");
		check(!simplified_chinese[i].empty(),"Chinese authoring catalog covers every semantic key");
		const auto a=format(id,en,{{"button","[BUTTON]",true},{"item","[ITEM]"}});
		const auto b=format(id,zh,{{"button","[BUTTON]",true},{"item","[ITEM]"}});
		check(!b.empty() && (english[i].empty() ? a.empty() : !a.empty()),"Chinese-only keys do not manufacture English translations");
		for(const auto token:{"[BUTTON]","[ITEM]"})
			check(english[i].empty() || (a.find(token)!=std::string::npos)==(b.find(token)!=std::string::npos),"translations retain the same argument contract");
	}
	check(text(grip_key(0,false),en)=="Left Grip" && text(grip_key(1,false),en)=="Right Grip" &&
		text(grip_key(0,true),en)=="Left/Right Grip","all highlighted grip labels share the catalog");
	check(format(key::interaction_pickup,en,{{"button","Left Grip",true},{"item","M4A1"}})==
		"Hold Left Grip to pick up M4A1","English template preserves spaces across colored spans");
	check(format(key::interaction_pickup,zh,{{"button",text(key::button_grip_left,zh),true},{"item","M4A1"}})==
		utf8(u8"按住左手握持键拾取M4A1"),"Chinese remains UTF-8 with no invented English spacing");
	const auto colored=format_runs(key::interaction_pickup,zh,{{"button",text(key::button_grip_either,zh),true},{"item","M4A1"}});
	std::size_t emphasized{};
	for(const auto& value:colored)if(value.emphasized){++emphasized;check(value.value==text(key::button_grip_either,zh),"whole localized button is highlighted");}
	check(emphasized==1,"template order never determines highlight identity");
	check(text(key::button_trigger,static_cast<locale>(99))=="Trigger","unimplemented locales fall back to English");
	check(!text(static_cast<key>(999),zh).empty(),"invalid keys have a visible diagnostic fallback");
	check(format(key::story_melee,zh,{{"button",text(key::button_trigger,zh)}})==
		utf8(u8"按下扳机键刺杀哨兵"),"story instructions do not require native translation keys");
	check(format(key::rappel_brake,en,{{"button","^3Trigger^7"}})=="Hold ^3Trigger^7 to slow down","native renderer color markup remains intact");
	check(format(key::interaction_pickup,en,{{"button","Grip"}}).empty(),"missing required arguments never leak unresolved placeholders");
	check(interpolate("{item}",{{"item","{button}"}})[0].value=="{button}","argument text is never interpreted as another template");
	check(interpolate("{button}",{{"button","a"},{"button","b"}}).empty(),"ambiguous duplicate arguments are rejected");
	check(interpolate("{unfinished",{}).empty(),"malformed catalog template is rejected");
	check(format(key::interaction_pickup,en,{{"button","Grip"},{"item",std::string(max_text_bytes+1,'x')}}).empty(),"oversized text is bounded without cutting UTF-8");
	const auto emphasis=interpolate("a<em>place {button}</em>z",{{"button","<em>literal</em>",true}});
	check(emphasis.size()==3 && emphasis[1].emphasized && emphasis[1].value=="place <em>literal</em>" && !emphasis[2].emphasized,
		"catalog emphasis merges spans and never interprets native arguments as markup");
	for(const auto pattern:{"<em>open","</em>close","<em><em>nested</em></em>","<color>unknown</color>","<em>{button</em>"})
		check(interpolate(pattern,{{"button","Grip"}}).empty(),"invalid emphasis and placeholders fail as a complete sentence");
	check(template_parameters(u8"<em>{item}</em> {button}")==3 &&
		template_parameters(u8"<em>unclosed")==invalid_parameters &&
		template_parameters(u8"</em>unopened")==invalid_parameters &&
		template_parameters(u8"<em><em>nested</em></em>")==invalid_parameters,
		"compile-time validation and runtime emphasis use the same bounded grammar");
	std::string fragmented;for(unsigned i=0;i<max_runs+1;++i)fragmented+="<em>x</em>y";
	check(interpolate(fragmented,{}).empty(),"too many emphasis spans cannot return a partial instruction");
}
