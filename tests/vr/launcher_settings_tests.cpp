#include <std_include.hpp>
#include "launcher/vr_settings_config.hpp"
#include "launcher/risk_settings_config.hpp"
#include "launcher/localization.hpp"
#include "launcher/game_language_catalog.hpp"
#include "launcher/bridge_protocol.hpp"
#include "campaign_unlock_request.hpp"
#include "launcher/preflight_policy.hpp"
#include "component/game_data.hpp"
#include <iostream>
#include <fstream>
#include <limits>
#include <sstream>
#include <shellapi.h>

void test_native_melee_setting();

namespace
{
	void require(bool condition, const char* message)
	{
		if (!condition) throw std::runtime_error(message);
	}
}

int main(int argc, char** argv)
{
	using namespace launcher_vr_settings;
	try
	{
		test_native_melee_setting();
		campaign_unlock::request disabled_request("0");
		require(!disabled_request.take(true), "Off never changes an existing campaign unlock");
		campaign_unlock::request unlock_request("1");
		require(!unlock_request.take(false), "Gameplay cannot consume a frontend profile request");
		require(unlock_request.take(true), "On requests an explicit unlock");
		require(!unlock_request.take(true), "Menu reopening or VM restart cannot replay the request, even after failure");
		for (const auto action : {"", "true", "unlock;quit", "restore", "UNLOCK"})
		{
			bool rejected = false;
			try { campaign_unlock::parse(action); } catch (const std::exception&) { rejected = true; }
			require(rejected, "Only allowlisted campaign actions can become startup arguments");
		}
		const std::string chinese = "\xe7\xae\x80\xe4\xbd\x93\xe4\xb8\xad\xe6\x96\x87";
		for (const auto& text : {std::string{}, std::string("English"), chinese, chinese + std::string("\0end", 4)})
		{
			const auto payload = nlohmann::json{{"id", 1}, {"session", "session-1"}, {"method", "settings.save"}, {"params", {{"text", text}}}}.dump();
			require(launcher_bridge::parse(payload).params["text"] == text, "JSON bridge preserves UTF-8 and embedded nulls");
		}
		bool rejected = false;
		try { launcher_bridge::parse(std::string("\xff")); } catch (const std::exception&) { rejected = true; }
		require(rejected, "Invalid UTF-8 must not be converted into mojibake");
		require(launcher_bridge::trusted_source("https://launcher.invalid/index.html") && !launcher_bridge::trusted_source("https://launcher.invalid.evil/index.html")
			&& !launcher_bridge::trusted_source("file:///index.html"), "Only the exact launcher origin receives native access");
		for (const auto& invalid : {std::string(launcher_bridge::max_message_bytes + 1, ' '),
			std::string(20, '[') + std::string(20, ']'), std::string(R"({"id":1,"session":"session","method":"shell.execute","params":{}})"),
			std::string(R"({"id":-1,"session":"session","method":"bootstrap","params":{}})")})
		{
			rejected = false; try { launcher_bridge::parse(invalid); } catch (const std::exception&) { rejected = true; }
			require(rejected, "Oversized, nested and unsupported native requests fail closed");
		}
		for (const auto& argument : {std::wstring{}, std::wstring(L"directory with spaces\\"), std::wstring(L"quoted\"value"), std::wstring(L"plain")})
		{
			const auto command = L"program.exe " + launcher_bridge::quote_argument(argument);
			int count{}; auto* values = CommandLineToArgvW(command.c_str(), &count);
			const auto cleanup = gsl::finally([&] { LocalFree(values); });
			require(values && count == 2 && argument == values[1], "Child process arguments preserve spaces, quotes and trailing slashes");
		}
		using launcher_localization::preference;
		for (const auto& language : launcher_game_language::languages)
			require(launcher_game_language::official(language.name), "Official game languages admitted by exact identity");
		for (const auto* value : {"czech", "turkish", "fr", "English", "../english", "english;quit", ""})
			require(!launcher_game_language::official(value), "Custom languages, aliases and malformed input cannot select a game pack");
		using launcher_localization::match_system_locale;
		for (const auto* tag : {"zh-TW", "zh-HK", "zh-MO", "zh-Hant", "zh-Hant-CN"})
			require(match_system_locale(tag) == "zh-TW", "Traditional Chinese UI language matching");
		for (const auto* tag : {"zh-CN", "zh-SG", "zh-Hans-HK", "ZH_cn"})
			require(match_system_locale(tag) == "zh-CN", "Simplified Chinese UI language matching");
		for (const auto& pair : {std::pair{"fr-CA", "fr"}, {"de-CH", "de"}, {"es-MX", "es"},
			{"ru-RU", "ru"}, {"ja-JP", "ja"}, {"ko-KR", "ko"}, {"en-GB", "en"}, {"it-IT", "en"}, {"", "en"}})
			require(match_system_locale(pair.first) == pair.second, "System UI region variants and English fallback");
		for (const auto& locale : launcher_localization::locales)
			require(preference(nlohmann::json{{"language", locale.id}}.dump()) == locale.id, "Every launcher locale persists independently of system language");
		require(preference("") == "en" && preference("{}") == "en", "Launcher language defaults to English");
		require(preference(R"({"language":"zh-CN"})") == "zh-CN" &&
			preference(R"({"language":"en"})") == "en", "Supported launcher languages round trip");
		for (const auto text : {"invalid JSON", "[]", R"({"language":true})", R"({"language":"zh-cn"})",
			R"({"language":"__proto__"})", R"({"language":"../../english"})"})
			require(preference(text) == "en", "Invalid launcher preferences fall back to English");
		require(preference(std::string(launcher_localization::max_preferences_bytes + 1, ' ')) == "en",
			"Oversized launcher preferences are bounded");
		require(!launcher_localization::supported("unknown") && !launcher_localization::supported(""),
			"Unsupported locale IDs cannot be saved");
		const auto catalog = choice_catalog();
		{
			const auto& field = vr::settings::tracer_style;
			require(catalog[field.name].size() == 7 && read_values("")[field.name] == "native",
				"Seven tracer presets retain the unmodified native default");
			for (unsigned i = 0; i < 7; ++i)
			{
				auto values = defaults(); values[field.name] = field.values[i];
				require(read_values(update_config("bind X vr_recenter\n", values)) == values,
					"Every tracer preset round trips through the saved profile");
				require(read_values(std::string("seta ")+field.name+" "+std::to_string(i))[field.name] == field.values[i],
					"Native numeric enum saves reload as the same tracer preset");
			}
			require(read_values("seta vr_tracerStyle 1776\n")[field.name] == "native" &&
				read_values("seta vr_tracerStyle independence_1776\n")[field.name] == "independence_1776",
				"1776 display name stays distinct from enum ordinals");
		}
		require(catalog.size()==vr::settings::choices.size(), "Choice catalog exposes every native setting");
		for(const auto& field:vr::settings::choices)
			for(std::size_t i=0;i<field.values.size() && field.values[i];++i)
				require(catalog[field.name][i]["value"]==field.values[i] && catalog[field.name][i]["labelKey"]==field.label_keys[i], "Frontend choices preserve native ordering and localized labels");
		const auto initial = defaults();
		require(validate(initial), "Shared defaults must be valid");
		{
			// Exercise the native writer's hashed names, not just our own output.
			const auto alias = [](const char* name)
			{
				std::ostringstream out;
				out << "0x" << std::hex << static_cast<std::uint32_t>(dvars::generate_hash(name));
				return out.str();
			};
			const auto check_native = [&](const char* name, const json& value, const std::string& text)
			{
				const auto hashed = alias(name);
				const auto profile = "bind F10 togglemenu\nseta " + std::string(name) + " \"invalid\"\nseta " + hashed + " \"" + text + "\"\n";
				const auto loaded = read_values(profile);
				require(loaded[name] == value, "Last native hashed assignment must survive launcher reload");
				const auto saved = update_config(profile, loaded);
				require(saved.starts_with("bind F10 togglemenu\n") && saved.find(hashed) == std::string::npos,
					"Launcher merge removes old hash aliases and preserves bindings");
				require(read_values(saved) == loaded && update_config(saved, loaded) == saved,
					"Native profiles stay stable after repeated launcher saves");
			};
			for (const auto& field : vr::settings::choices)
				for (std::size_t i{}; i < field.values.size() && field.values[i]; ++i)
				{
					check_native(field.name, field.values[i], field.values[i]);
					check_native(field.name, field.values[i], std::to_string(i));
				}
			for (const auto& field : vr::settings::toggles)
				for (bool enabled : {false, true}) check_native(field.name, enabled, enabled ? "1" : "0");
			for (const auto& field : vr::settings::numbers)
				check_native(field.name, setting_number(field.max), json(setting_number(field.max)).dump());
			require(has_vr_configuration("seta 0x2BA94576 \"baton\"\n"), "Hashed melee profile is existing VR configuration");
			require(!has_vr_configuration("seta 0x70BF1633 \"1\"\n"), "Native graphics alone is not VR onboarding");
			require(read_values("seta 0x2BA94576 \"baton\"\n")[vr::settings::melee_weapon.name] == "baton",
				"Reported engine-generated baton assignment must load without resetting");
		}
		{
			const auto* name=vr::settings::melee_weapon.name;
			require(initial[name]=="default","Melee choice defaults to native Ghost Dance with cheat substitution");
			for(const auto* value:{"default","ghost","baton","karambit","ending","bloody"})
			{
				auto selected=initial;selected[name]=value;
				require(validate(selected),"Every named melee choice is admitted by native and launcher catalogs");
				const auto saved=update_config("",selected);
				require(read_values(saved)[name]==value,"Melee selections persist through config serialization");
			}
			auto invalid=initial;invalid[name]="sas";
			require(!validate(invalid),"Unregistered SAS and arbitrary asset names cannot enter melee selection");
		}
		const auto* body_mode = vr::settings::body_pose_mode.name;
		require(initial[body_mode] == "inferred" && read_values("")[body_mode] == "inferred",
		        "Body positioning preserves inference by default");
		for (const auto* mode : {"inferred", "head_locked"})
		{
			auto selected = initial;
			selected[body_mode] = mode;
			require(read_values(update_config("bind X vr_recenter\n", selected)) == selected,
			        "Body modes persist through the shared config bridge");
		}
		require(read_values("seta vr_bodyPoseMode 0\n")[body_mode] == "inferred" &&
		        read_values("seta vr_bodyPoseMode 1\n")[body_mode] == "head_locked",
		        "Native body mode indices match the camera adapter");
		for (const auto& bad : {json("invalid"), json("head_locked;quit"), json(true), json(2)})
		{
			auto selected = initial;
			selected[body_mode] = bad;
			require(!validate(selected), "Malformed body modes cannot be saved");
		}
		require(read_values("seta vr_bodyPoseMode invalid\n")[body_mode] == "inferred",
		        "Invalid saved body modes fall back to inference");
		const auto* handedness = vr::settings::handedness.name;
		require(initial[handedness] == "right" && read_values("")[handedness] == "right",
		        "New and existing profiles default to right-hand automatic equipment");
		for (const auto* hand : {"right", "left"})
		{
			auto selected = initial;
			selected[handedness] = hand;
			require(read_values(update_config("bind X vr_recenter\n", selected)) == selected,
			        "Handedness persists through the shared config bridge");
		}
		require(read_values("seta vr_handedness 0\n")[handedness] == "right" &&
		        read_values("seta vr_handedness 1\n")[handedness] == "left",
		        "Native handedness indices match the runtime hand mapping");
		for (const auto& bad : {json("invalid"), json("left;quit"), json(true), json(2)})
		{
			auto selected = initial;
			selected[handedness] = bad;
			require(!validate(selected), "Invalid handedness cannot be saved");
		}
		const auto* desktop_eye = vr::settings::desktop_eye.name;
		require(initial[desktop_eye] == "right" && read_values("")[desktop_eye] == "right",
		        "New and existing profiles retain the right-eye desktop preview by default");
		for (const auto* eye : {"left", "right"})
			for (bool stabilized : {false, true})
			{
				auto selected = initial;
				selected[desktop_eye] = eye;
				selected[vr::settings::stabilization_toggles[0]] = stabilized;
				require(read_values(update_config("bind X vr_recenter\n", selected)) == selected,
				        "Desktop eye selection persists independently of desktop stabilization");
			}
		require(read_values("seta vr_desktopEye 0\n")[desktop_eye] == "left" &&
		        read_values("seta vr_desktopEye 1\n")[desktop_eye] == "right",
		        "Desktop eye enum indices preserve left-zero and right-one ordering");
		for (const auto& bad : {json("invalid"), json("left;quit"), json(true), json(2)})
		{
			auto selected = initial;
			selected[desktop_eye] = bad;
			require(!validate(selected), "Invalid desktop eye selections cannot be saved");
		}
		require(read_values("seta vr_desktopEye 2\n")[desktop_eye] == "right",
		        "Unknown saved desktop eye values retain the default right eye");
		const auto* grip_mode = vr::settings::weapon_grip_mode.name;
		require(initial[grip_mode] == "hold" && read_values("")[grip_mode] == "hold",
		        "New and existing profiles retain hold-to-grip by default");
		for (const auto* mode : {"hold", "toggle"})
		{
			auto selected = initial;
			selected[grip_mode] = mode;
			require(read_values(update_config("bind X vr_recenter\n", selected)) == selected,
			        "Weapon grip modes persist through the shared config bridge");
		}
		require(read_values("seta vr_weaponGripMode 1\n")[grip_mode] == "toggle",
		        "Native grip enum index reloads as the launcher choice");
		{
			auto invalid = initial;
			invalid[grip_mode] = "invalid";
			require(!validate(invalid), "Unknown grip modes cannot be saved");
		}
		require(initial[vr::settings::subtitles.name] == true, "Dialogue subtitles default on");
		const auto* support_lock = vr::settings::support_grip_lock.name;
		require(initial[support_lock] == true && read_values("")[support_lock] == true,
		        "Support grip lock defaults on for new and existing profiles");
		for (const auto* mode : {"hold", "toggle"})
			for (bool locked : {false, true})
			{
				auto selected = initial;
				selected[grip_mode] = mode;
				selected[support_lock] = locked;
				require(read_values(update_config("bind X vr_recenter\n", selected)) == selected,
				        "Support retention and button modes persist independently, including explicit opt-out");
			}
		for (bool enabled : {false, true})
		{
			auto selected = initial;
			selected[vr::settings::subtitles.name] = enabled;
			require(read_values(update_config("bind F10 togglemenu\n", selected)) == selected,
				"Subtitle preference round trips independently of native profile initialization");
		}
		for (const auto& field : {vr::settings::depth_of_field, vr::settings::motion_blur})
		{
			require(initial[field.name] == false, "Native comfort effects default off");
			std::ostringstream alias;
			alias << "0x" << std::hex << static_cast<std::uint32_t>(dvars::generate_hash(field.name));
			const std::string other = "bind F10 togglemenu\nseta r_dof_enable 1\nseta r_mbFastEnable 1\n";
			for (int quality = 0; quality <= field.max_enabled_value; ++quality)
			{
				const auto profile = "\xef\xbb\xbf" + other + "seta " + field.name + " 3\nseta " + alias.str() + " " + std::to_string(quality) + "\n";
				const auto loaded = read_values(profile);
				require(loaded[field.name] == (quality != 0), "Last hashed quality limit controls the launcher switch");
				const auto merged = update_config(profile, loaded);
				require(merged.starts_with("\xef\xbb\xbf" + other) && merged.find(alias.str()) == std::string::npos,
					"Comfort saves preserve BOM, bindings and subordinate renderer settings, and remove aliases");
				require(merged.find(std::string("seta ") + field.name + " \"" + std::to_string(quality) + "\"") != std::string::npos,
					"Unrelated saves preserve native Low/Medium/High quality exactly");
				require(read_values(merged) == loaded && update_config(merged, loaded) == merged,
					"Native comfort settings round trip and repeated saves are stable");
				auto selected = loaded;
				selected[field.name] = false;
				const auto disabled = update_config(profile, selected);
				require(disabled.find(std::string("seta ") + field.name + " \"0\"") != std::string::npos,
					"Disabling any native effect quality writes Off");
				selected[field.name] = true;
				require(update_config(disabled, selected).find(std::string("seta ") + field.name + " \"3\"") != std::string::npos,
					"Enabling an Off native effect selects High quality");
			}
			for (const auto text : {"-1", "4", "true", "1.5", "999999999999999999999", "1;quit"})
				require(read_values(std::string("seta ") + field.name + " " + text)[field.name] == false,
					"Malformed or out-of-range native quality remains disabled");
		}
		const auto* killfeed_name = vr::settings::killfeed_style.name;
		require(initial[killfeed_name] == "mw2019", "Hit and kill audio defaults to MW2019");
		for (std::size_t i{}; i < vr::settings::killfeed_style.values.size() && vr::settings::killfeed_style.values[i]; ++i)
		{
			auto selected = initial;
			selected[killfeed_name] = vr::settings::killfeed_style.values[i];
			require(read_values(update_config("seta vr_recoil 1\nbind X vr_recenter\n", selected)) == selected,
				"All feedback styles persist alongside VR settings and bindings");
			require(read_values(std::string("seta ") + killfeed_name + " " + std::to_string(i))[killfeed_name] == selected[killfeed_name],
				"Native feedback enum indices reload as launcher choices");
		}
		for (const auto& bad : {json(-1), json(4), json(true), json("invalid"), json("mw2019;quit")})
		{
			auto selected = initial; selected[killfeed_name] = bad;
			require(!validate(selected), "Invalid feedback selections cannot be saved");
		}
		require(initial[vr::settings::ads_comfort.name] == true,
			"Near-eye sight attraction retains the existing enabled default");
		require(initial[vr::settings::ads_sprint.name] == true,
			"ADS sprint defaults on for existing and new configurations");
		for (const bool enabled : {false, true})
		{
			auto selected = initial;
			selected[vr::settings::ads_sprint.name] = enabled;
			const auto saved = update_config("seta vr_autoAds 1\nseta vr_scopeZoom 1\n", selected);
			require(read_values(saved) == selected && read_values(saved)[vr::settings::ads_comfort.name] == true &&
				saved.find("seta vr_autoAds 1") != std::string::npos && saved.find("seta vr_scopeZoom 1") != std::string::npos,
				"ADS sprint persists independently of comfort, automatic ADS and scope zoom");
		}
		for (const bool enabled : {false, true})
		{
			auto selected = initial;
			selected[vr::settings::ads_comfort.name] = enabled;
			const auto saved = update_config("seta vr_autoAds 1\nseta vr_scopeZoom 1\nbind X vr_recenter\n", selected);
			require(read_values(saved) == selected && saved.find("seta vr_autoAds 1") != std::string::npos &&
				saved.find("seta vr_scopeZoom 1") != std::string::npos && saved.find("bind X vr_recenter") != std::string::npos,
				"Sight attraction persists independently of automatic ADS, zoom and bindings");
		}
		require(initial[vr::settings::controller_pose_mode.name] == "legacy", "Controller pipeline defaults to Legacy");
		require(read_values("seta vr_controllerPoseMode standard\n")[vr::settings::controller_pose_mode.name] == "standard",
			"An explicitly saved Standard selection remains available");
		auto rollback = initial;
		rollback[vr::settings::controller_pose_mode.name] = "legacy";
		rollback[vr::settings::hand_pitch.name] = -17;
		rollback[vr::settings::standard_hand_alignment[3].name] = 12;
		const auto saved_banks = update_config("seta vr_wristPivotUp -0.075\n", rollback);
		require(read_values(saved_banks) == rollback && saved_banks.find("seta vr_wristPivotUp -0.075") != std::string::npos,
			"Rollback preserves both independent calibration banks and advanced legacy pivots");
		for (const auto& preset : controller_presets(true))
		{
			auto candidate = rollback;
			candidate.update(preset["values"]);
			require(validate(candidate) && candidate[vr::settings::hand_pitch.name] == -17,
				"Standard presets must never overwrite legacy calibration");
		}
		vr::controller_pose_pipeline::initialize(vr::controller_pose_pipeline::mode::legacy);
		vr::controller_pose_pipeline::initialize(vr::controller_pose_pipeline::mode::standard);
		require(vr::controller_pose_pipeline::selected() == vr::controller_pose_pipeline::mode::legacy,
			"Controller coordinate selection is frozen for the process");
		const auto* backend_name = vr::settings::runtime_backend.name;
		require(initial[backend_name] == "openxr", "Launcher backend defaults to OpenXR");
		for (const auto* backend : {"openxr", "openvr"})
		{
			auto edited = initial; edited[backend_name] = backend;
			require(read_values(update_config("bind F10 togglemenu\n", edited)) == edited, "Backend preference round trips without losing binds");
			require(backend_environment_update(edited, startup_source::launcher, true) == backend, "Launcher selection wins in the current process even with an inherited override");
			require(backend_environment_update(edited, startup_source::direct, false) == backend, "Direct startup uses the saved backend when no override is present");
			require(!backend_environment_update(edited, startup_source::direct, true), "Direct startup preserves an explicit diagnostic override");
		}
		require(read_values("\xef\xbb\xbfseta vr_runtimeBackend openvr\n")[backend_name] == "openvr", "UTF-8 BOM cannot hide the startup backend");
		require(read_values("seta vr_runtimeBackend 1\n")[backend_name] == "openvr", "Native enum indices reload as launcher choices");
		for (const auto* invalid : {"unknown", "openvr;quit", "../../openvr"})
		{
			auto edited = initial; edited[backend_name] = invalid; require(!validate(edited), "Malformed backend payload cannot be saved");
		}

		const auto exo=vr::settings::exo_punch.name;
		const auto hei=vr::settings::hei_round.name;
		require(initial[hei]==false,"HEI rounds default off");
		{auto selected=initial;selected[hei]=true;
		 require(read_values(update_config("",selected))==selected,"HEI Bool round trips through launcher configuration");}
		require(initial[exo]==false,"EXO punch defaults off");
		for(bool enabled:{false,true})
		{
			auto selected=initial;selected[exo]=enabled;
			const auto config=update_config("seta sf_use_ragdoll_mode 1\nseta ragdoll_enable 1\n",selected);
			require(read_values(config)==selected && config.find("seta sf_use_ragdoll_mode 1")!=std::string::npos,
				"EXO punch round trips without changing the independent native Ragdoll Impact request");
		}
		for(const auto& bad:{json(1),json("true"),json("on;quit"),json(nullptr)})
		{auto selected=initial;selected[exo]=bad;require(!validate(selected),"EXO punch accepts only a Bool bridge value");}
		for(const auto& field:{vr::settings::cheat_health,vr::settings::cheat_notarget,vr::settings::cheat_ammo})
		{
			require(initial[field.name]=="off","Launcher cheats default off");
			for(std::size_t i=0;i<field.values.size() && field.values[i];++i)
			{
				auto selected=initial;selected[field.name]=field.values[i];
				require(read_values(update_config("seta player_sustainAmmo 1\n",selected))==selected,
					"Cheat choices persist without changing independent official config");
				require(read_values(std::string("seta ")+field.name+" "+std::to_string(i))[field.name]==field.values[i],
					"Native enum indices reload as launcher choices");
			}
			for(const auto& bad:{json(-1),json(3),json(true),json("invalid"),json("on;god")})
			{auto selected=initial;selected[field.name]=bad;require(!validate(selected),"Malformed cheat choices rejected at bridge boundary");}
			require(read_values(std::string("seta ")+field.name+" 99")[field.name]=="off","Malformed saved cheats stay disabled");
		}
		for (const auto* text : {"", "// seta vr_turnMode snap\n", "seta cg_fov 85\n", "bind X vr_recenter\n"})
			require(!has_vr_configuration(text), "Non-VR profiles and comments remain eligible for first use");
		for (const auto* text : {"seta vr_turnMode smooth\n", "SET VR_ENABLE 0\n", "seta vr_footballThrowSpeedScale 1.5",
			"\xef\xbb\xbfseta vr_handOffsetUp 0\n"})
			require(has_vr_configuration(text), "Existing VR settings including advanced dvars suppress first use");
		{
			const auto previous = std::filesystem::current_path();
			const auto fixture = std::filesystem::temp_directory_path() /
				("h2vr-first-use-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
			std::filesystem::create_directory(fixture);
			const auto cleanup_fixture = gsl::finally([&] {
				std::filesystem::current_path(previous);
				std::filesystem::remove_all(fixture);
			});
			std::filesystem::current_path(fixture);
			require(!game_data::is_game_directory_available(), "An empty folder cannot trigger OOBE");
			{
				std::ofstream file("h2_sp64_bnet_ship.exe", std::ios::binary);
				file << "MZ";
			}
			require(!game_data::is_game_directory_available(), "A truncated game executable is not available");
			{
				std::ofstream file("h2_sp64_bnet_ship.exe", std::ios::binary);
				file << "MZ";
				file.seekp(game_data::supported_binary_size - 1);
				file.put('\0');
			}
			require(game_data::is_game_directory_available(), "The supported executable admits first-use inspection");
			std::filesystem::rename("h2_sp64_bnet_ship.exe", "MW2CR.exe");
			require(game_data::is_game_directory_available() && game_data::get_game_binary_path() == "MW2CR.exe",
				"Both native game executable names are supported");
		}
		require(initial[vr::settings::discard_ammo_penalty.name]==false && read_values("")[vr::settings::discard_ammo_penalty.name]==false,
			"Discard ammo penalty defaults off for new and existing profiles");
		for(bool enabled:{false,true})
		{
			auto selected=initial;selected[vr::settings::discard_ammo_penalty.name]=enabled;
			require(read_values(update_config("seta unrelated 7\n",selected))==selected,"Discard penalty persists both values without changing other settings");
		}
		require(initial[vr::settings::enemy_melee_damage.name] == .5 &&
			initial[vr::settings::disable_dog_pounce.name] == true,
			"Enemy melee defaults to half damage and dog knockdowns default off");
		for (const double scale : {.1, .5, 1., 2.}) for (const bool disabled : {false, true})
		{
			auto selected = initial;
			selected[vr::settings::enemy_melee_damage.name] = scale;
			selected[vr::settings::disable_dog_pounce.name] = disabled;
			require(read_values(update_config("seta unrelated 7\n", selected)) == selected,
				"Enemy combat settings persist independently at all boundaries");
		}
		const auto* throw_name=vr::settings::grenade_throw_speed.name;
		const auto* size_name=vr::settings::first_person_scale.name;
		require(initial[size_name]==1.0,"First-person size keeps the original default");
		for (double size:{.2,.8,1.0})
		{
			auto edited=initial;edited[size_name]=size;
			const auto saved=read_values(update_config("",edited));
			require(validate(edited) && saved[size_name]==size && saved[vr::settings::world_scale.name]==initial[vr::settings::world_scale.name],
				"First-person size persists independently of world scale");
		}
		const auto* world_name = vr::settings::world_scale.name;
		require(initial[world_name] == setting_number(vr::settings::world_scale.default_value),
			"World size defaults to the existing native spatial scale");
		for (const double scale : {.01, 32.8083989167, 39.370079, 10000.})
		{
			auto edited = initial;
			edited[world_name] = scale;
			const auto saved = update_config("seta vr_worldScale 40\nseta VR_WORLDSCALE 50\nseta unrelated 7\n", edited);
			require(read_values(saved) == edited && saved.find("seta unrelated 7\n") == 0,
				"World scale persists native precision and replaces duplicates without changing other settings");
		}
		require(initial[throw_name]==2.5 && !initial.contains(vr::settings::football_throw_speed.name),
			"Launcher exposes general throw gain while leaving football tuning private");
		const std::string private_gain="seta vr_footballThrowSpeedScale \"1.8\"\n";
		for(double gain:{.25,1.75,2.5,10.0})
		{
			auto edited=initial;edited[throw_name]=gain;
			const auto saved=update_config(private_gain,edited);
			require(validate(edited) && read_values(saved)[throw_name]==gain && saved.find(private_gain)==0,
				"Throw gain saves exact decimal boundaries without changing private football tuning");
		}
		require(update_config(private_gain,initial).find(private_gain)==0,
			"Resetting visible defaults preserves the hidden football multiplier");
		require(initial[vr::settings::quick_reload.name] == true && read_values("")[vr::settings::quick_reload.name] == true,
			"Quick reload defaults on for new and existing profiles without the setting");
		require(initial[vr::settings::smart_ammo_selection.name] == true && read_values("")[vr::settings::smart_ammo_selection.name] == true,
			"Smart ammo selection defaults on for new and existing profiles without the setting");
		require(initial[vr::settings::chambering_guide.name] == false && read_values("")[vr::settings::chambering_guide.name] == false,
			"Chambering guide defaults off, including existing profiles without the setting");
		for (const bool enabled : {false, true})
		{
			auto selected = initial;
			selected[vr::settings::quick_reload.name] = enabled;
			selected[vr::settings::chambering_guide.name] = enabled;
			selected[vr::settings::smart_ammo_selection.name] = enabled;
			require(read_values(update_config("", selected)) == selected, "Quick reload toggle persists both values");
		}
		for (const auto* name : vr::debug_options::names)
		{
			require(initial[name] == false, "Expensive diagnostics must default off");
			require(read_values(std::string("seta ") + name + " invalid\n")[name] == false,
				"Malformed saved probes must not enable diagnostics");
		}
		// Cover every switch independently in both directions without an
		// exponential matrix as optional diagnostic modules are added.
		constexpr auto probe_count=vr::debug_options::names.size();
		for (std::size_t variant{}; variant < 2+probe_count*2; ++variant)
		{
			auto selected = initial;
			vr::debug_options::selection expected{};
			for (std::size_t i{}; i < probe_count; ++i)
			{
				expected[i]=variant==1 || (variant>=2 &&
					(variant<2+probe_count ? i==variant-2 : i!=variant-2-probe_count));
				selected[vr::debug_options::names[i]] = expected[i];
			}
			const auto round_trip = read_values(update_config("seta cg_fov 90\n", selected));
			require(round_trip == selected, "Every debug selection must round trip independently");
			const auto loaded = debug_selection(round_trip);
			require(loaded == expected, "Runtime must load exactly the saved probes");
		}
		require(debug_selection(json{{vr::debug_options::names[0], "true"}}) ==
			vr::debug_options::selection{}, "Runtime probe selection rejects string booleans");
		require(initial.dump().size() < max_payload_bytes, "Complete settings fit the bounded bridge payload");
		require(initial[vr::settings::camera_bob] == true, "Movement camera bob defaults on");
		require(initial[vr::settings::recording_mode.name] == false, "Recording guide defaults off in launcher and runtime");
		require(initial[vr::settings::recording_dim.name] == 65, "Recording exterior defaults to stronger 65 percent dimming");
		for (const double strength : {0.,65.,100.})
		{
			auto values=initial;values[vr::settings::recording_dim.name]=strength;
			const auto saved=update_config("seta VR_RECORDINGDIM 35\nseta vr_recordingDim 50\n",values);
			require(validate(values) && read_values(saved)==values && saved.find("VR_RECORDINGDIM")==std::string::npos,
				"Dimming boundaries round trip without duplicate assignments");
		}
		require(read_values("seta VR_RECORDINGMODE 1\n")[vr::settings::recording_mode.name] == true,
			"Existing console-enabled guide loads in launcher");
		require(read_values("seta vr_recordingMode invalid\n") == initial, "Malformed guide value stays off");
		for (bool enabled : {true,false})
		{
			auto values=initial;values[vr::settings::recording_mode.name]=enabled;
			const auto saved=update_config("seta vr_recordingMode 1\nseta VR_RECORDINGMODE 0\n",values);
			require(read_values(saved)==values && saved.find("VR_RECORDINGMODE")==std::string::npos,
				"Launcher guide enable and disable replace old duplicate assignments");
		}
		require(initial[vr::settings::recoil] == true && initial[vr::settings::recoil_penalty] == "long",
			"Muzzle recoil defaults on and single-hand penalty defaults to long weapons");
		require(read_values("seta vr_recoil \"0\"\nseta vr_recoilPenalty \"all\"\n")[vr::settings::recoil] == false &&
			read_values("seta vr_recoilPenalty \"2\"\n")[vr::settings::recoil_penalty] == "off",
			"Saved recoil controls and native numeric enum values load");
		for (const auto* invalid : {"", "pistol", "off;quit"})
		{
			auto bad=initial;bad[vr::settings::recoil_penalty]=invalid;
			require(!validate(bad), "Unknown recoil penalty modes are rejected");
		}
		require(read_values("seta VR_CAMERABOB 0\n")[vr::settings::camera_bob] == false,
			"Console camera bob disable loads case-insensitively");
		require(read_values("seta vr_cameraBob invalid\n") == initial, "Malformed camera bob keeps defaults");
		for (const auto& toggle : vr::settings::toggles)
		{
			auto candidate = initial;
			candidate[toggle.name] = 1;
			require(!validate(candidate), "Every toggle rejects numeric JSON");
			candidate.erase(toggle.name);
			require(!validate(candidate), "Every toggle is required in a save payload");
		}
		require(read_values("") == initial, "A new profile must load defaults");
		require(read_values("seta vr_standardHandOffsetBack -0.096073\n")["vr_standardHandOffsetBack"] == -.096073,
			"Changing defaults must not overwrite a saved custom point");
		for (bool standard : {false, true})
		{
			const auto presets = controller_presets(standard);
			require(presets.size() == 1 && presets[0]["id"] == "none" && presets[0]["label"] == "None",
				"Both calibration banks expose only the shared None preset");
			const auto& fields = standard ? vr::settings::standard_hand_alignment : vr::settings::hand_alignment;
			for (const auto& field : fields)
				require(initial[field.name] == 0 && presets[0]["values"][field.name] == 0,
					"New profiles and None must zero all six alignment fields in both banks");
			auto candidate = initial;
			candidate.update(presets[0]["values"]);
			require(validate(candidate), "Every compiled preset must pass the normal save validation");
		}
		require(initial[vr::settings::aim_assist_strength.name] == 0, "VR aim assist must default off");
		require(vr::settings::aim_assist_degrees(0) == 0 && vr::settings::aim_assist_degrees(50) == 5 &&
			vr::settings::aim_assist_degrees(100) == 10, "Strength must map linearly to a ten-degree cone");
		auto changed = initial;
		changed[vr::settings::turn_mode] = "snap";
		changed[vr::settings::disable_lens_flare] = true;
		changed[vr::settings::camera_bob] = false;
		changed[vr::settings::recoil] = false;
		changed[vr::settings::recoil_penalty] = "all";
		changed[vr::settings::hand_up.name] = -.125;
		changed[vr::settings::aim_assist_strength.name] = 100;
		changed[vr::settings::hand_pitch.name] = -7.5;
		changed[vr::settings::hand_yaw.name] = 2;
		changed[vr::settings::hand_roll.name] = -3;
		const std::string preserved = "// profile\r\nbind W \"+forward\"\r\nseta name \"\xe7\x8e\xa9\xe5\xae\xb6\"\r\n"
			"seta cg_fov \"90\"\r\nseta vr_wristPivotUp \"-0.075\"\r\n";
		const auto original = preserved + "seta vr_turnMode \"smooth\"\r\n"
			"seta vr_handOffsetUp \"0.1\"\r\nseta VR_HANDOFFSETUP \"-.2\" // calibrated\r\n";
		const auto default_config = update_config(original, initial);
		require(default_config.substr(0, preserved.size()) == preserved && read_values(default_config) == initial,
			"Reset to zero defaults preserves retired advanced fields and unrelated config");
		require(read_values(original)[vr::settings::hand_up.name] == -.2, "Last recognized assignment wins");
		const auto updated = update_config(original, changed);
		require(updated.substr(0, preserved.size()) == preserved, "Unrelated config and UTF-8 must survive verbatim");
		require(read_values(updated) == changed, "Saved values must round trip");
		require(read_values("seta vr_handAnglePitch \"-8\"\nseta VR_HANDANGLEPITCH \"-6.5\"\n")[vr::settings::hand_pitch.name] == -6.5,
			"Live console angle calibration loads through the same persistent settings path");
		require(update_config(updated, changed) == updated, "Repeated saves must not grow the config");
		require(updated.find("VR_HANDOFFSETUP") == std::string::npos, "Old case-insensitive duplicates must be removed");
		require(read_values("set vr_turnMode 1\n")[vr::settings::turn_mode] == "snap", "Native numeric enum values supported");
		require(read_values("seta vr_aimAssistStrength \"50\"\n")[vr::settings::aim_assist_strength.name] == 50,
			"Console-saved aim assist strength must load in the launcher");
		require(read_values("seta vr_aimAssistStrength \"100\"\nseta VR_AIMASSISTSTRENGTH \"0\"\n") == initial,
			"A console disable must override an older launcher value");
		require(read_values("seta vr_aimAssistStrength \"101\"\nseta vr_aimAssistStrength \"nan\"\n") == initial,
			"Invalid stored aim assist must not enable assistance");

		for (const auto& field : vr::settings::numbers)
		{
			for (double value : {setting_number(field.min), setting_number(field.max)})
			{
				auto candidate = initial;
				candidate[field.name] = value;
				require(validate(candidate), "Range boundaries must be accepted");
			}
			for (double value : {double(field.min) - 1, double(field.max) + 1,
				std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
			{
				auto candidate = initial;
				candidate[field.name] = value;
				require(!validate(candidate), "Out-of-range and non-finite values must be rejected");
			}
			auto candidate = initial;
			candidate[field.name] = "0";
			require(!validate(candidate), "String values must not bypass numeric validation");
			candidate.erase(field.name);
			require(!validate(candidate), "Partial saves must be rejected");
		}
		auto invalid = initial;
		invalid[vr::settings::turn_mode] = "snap\"; quit";
		require(!validate(invalid), "Commands must not enter through enum values");
		invalid = initial;
		invalid["unknown"] = 1;
		require(!validate(invalid), "Unknown fields must be rejected");
		invalid = initial;
		invalid[vr::settings::disable_lens_flare] = 1;
		require(!validate(invalid), "Checkbox must be boolean");
		require(read_values("seta vr_handOffsetUp \"nan\"\nseta vr_turnSpeed \"1e999\"\n") == initial,
			"Malformed stored values must fall back safely");

		const std::string compound = "seta vr_turnMode \"smooth\"; seta cg_fov \"85\"\n";
		require(update_config(compound, changed).find(compound) == 0, "Do not erase unrelated inline commands");
		const std::string long_bind = "bind X \"" + std::string(100000, 'a') + "\"\n";
		require(update_config(long_bind, initial).find(long_bind) == 0, "Long unrelated bindings must survive");
		require(read_values(update_config("// no final newline", changed)) == changed,
			"Appended settings must not join an existing comment");
		const std::string graphics_profile = "\xef\xbb\xbfseta r_ssaaSamples \"16\"\r\n"
			"seta 0x70BF1633 \"4\"\nset R_SSAASAMPLES 2\n"
			"seta r_preloadShadersFrontendAllow 1\nseta r_preloadShaders 1\n"
			"seta sm_cacheSunShadow Enabled\nseta sm_cacheSpotShadows Enabled\n"
			"// Keep these settings and bindings\nseta vr_turnSpeed 123\n"
			"seta r_preloadShadersAfterCinematic 1\nseta sm_enable 1\nbind F \"+activate\"\n";
		const auto safe_graphics = disable_risk_settings(graphics_profile);
		{
			using namespace launcher_preflight;
			auto issues = json::array();
			append_risks(issues, safe_graphics, true);
			require(issues.empty(), "Safe native graphics settings pass preflight");
			const std::string unsafe = "\xef\xbb\xbfseta r_ssaaSamples 1\nseta 0x70BF1633 4\nseta r_preloadShadersFrontendAllow 1\nseta r_preloadShaders 0\nseta sm_cacheSunShadow Enabled\nseta sm_cacheSpotShadows 0\nseta r_fill_texture_memory 0\nbind F2 togglemenu\n";
			const auto values = read_risk_settings(unsafe);
			require(values[0] == "4", "Last hashed assignment wins alongside named assignments and BOM");
			append_risks(issues, unsafe, true);
			require(issues.size() == 3 && issues[0]["severity"] == "error" && issues[1]["severity"] == "warning" && issues[2]["severity"] == "warning",
				"SSAA blocks startup while shader/shadow risk uses warning severity");
			require(!launch_allowed(report(issues, true), json::array({"ssaa", "shaders", "shadows"})), "Acknowledgements cannot override a blocking error");
			const auto repaired = disable_risk_settings(unsafe, risk_group("ssaa"));
			const auto after = read_risk_settings(repaired);
			require(after[0] == "1" && after[1] == "1" && after[3] == "Enabled" && repaired.find("bind F2 togglemenu") != std::string::npos,
				"A row repair removes duplicate SSAA assignments without changing other risks or bindings");
			issues = json::array(); append_risks(issues, repaired, true);
			const auto warnings = report(issues, true);
			require(!launch_allowed(warnings, json::array()) && !launch_allowed(warnings, json::array({"shaders"})) && launch_allowed(warnings, json::array({"shaders", "shadows"})),
				"Every currently present warning requires acknowledgement on this launch");
			issues = json::array(); append_risks(issues, "", false);
			require(issues.size() == 4 && issues[0]["severity"] == "error" && issues[0]["fixable"] == false,
				"Unknown values are explicit and game-profile repair is unavailable outside a game directory");
			require(risk_value(0, std::string("1.0")) == risk_state::safe && risk_value(0, std::string("garbage")) == risk_state::unknown,
				"SSAA compares native numeric meaning and malformed values cannot pass");
			bool invalid_fix = false; try { risk_group("../config"); } catch (const std::exception&) { invalid_fix = true; }
			require(invalid_fix, "Repair targets are a fixed allowlist rather than user-supplied paths");
			issues = json::array(); append_risks(issues, "seta r_ssaaSamples \xff\n", true);
			require(!issues.dump().empty(), "Malformed non-UTF-8 risk values are classified without leaking invalid text to the UI");
			for (const auto* value : {"1", "true", "TRUE", "0", "false", "False", "invalid"})
			{
				issues = json::array();
				const auto profile = safe_graphics + "seta r_fill_texture_memory " + value + "\n";
				append_risks(issues, profile, true);
				const bool safe = same_name(value, "false") || std::string_view(value) == "0";
				require(issues.empty() == safe, "Fill memory accepts native boolean text and saved numeric values");
				if (safe) continue;
				require(issues.size() == 1 && issues[0]["id"] == "fillMemory" && issues[0]["severity"] == "warning" && issues[0]["fixable"] == true,
					"Fill memory is a repairable warning, including unrecognized values");
				require(issues[0]["detailKey"] == (std::string_view(value) == "invalid" ? "preflight.unknownSetting" : "preflight.fillMemoryDetail"),
					"Enabled fill memory shows its performance warning");
				require(!launch_allowed(report(issues, true), json::array()) && launch_allowed(report(issues, true), json::array({"fillMemory"})),
					"Fill memory warning requires acknowledgement for this launch");
			}
			std::ostringstream fill_hash;
			fill_hash << "0x" << std::hex << static_cast<std::uint32_t>(dvars::generate_hash("r_fill_texture_memory"));
			const auto fill_profile = unsafe + "seta r_fill_texture_memory true\nseta " + fill_hash.str() + " 0\n";
			require(read_risk_settings(fill_profile)[5] == "0", "Last hashed fill-memory assignment wins");
			const auto fill_repaired = disable_risk_settings(fill_profile, risk_group("fillMemory"));
			const auto fill_values = read_risk_settings(fill_repaired);
			require(fill_values[5] == "0" && fill_values[0] == "4" && fill_values[1] == "1" && fill_values[3] == "Enabled" &&
				fill_repaired.find(fill_hash.str()) == std::string::npos && fill_repaired.find("bind F2 togglemenu") != std::string::npos,
				"Fill-memory repair removes named/hashed duplicates and preserves other risks and bindings");
			require(disable_risk_settings(fill_repaired, risk_group("fillMemory")) == fill_repaired,
				"Fill-memory repair is idempotent");
		}
		require(safe_graphics.substr(0, 3) == "\xef\xbb\xbf", "Offline repair preserves UTF-8 BOM");
		require(safe_graphics.find("0x70BF1633") == std::string::npos && safe_graphics.find("R_SSAASAMPLES") == std::string::npos,
			"Offline repair removes duplicate hashed and named SSAA assignments");
		require(safe_graphics.find("// Keep these settings and bindings\nseta vr_turnSpeed 123\n"
			"seta r_preloadShadersAfterCinematic 1\nseta sm_enable 1\nbind F \"+activate\"\n") != std::string::npos,
			"Offline repair preserves VR options, shadow rendering, preload timing and bindings");
		require(safe_graphics.find("seta r_ssaaSamples \"1\"\r\nseta r_preloadShadersFrontendAllow \"0\"\r\n"
			"seta r_preloadShaders \"0\"\r\nseta sm_cacheSunShadow \"Disabled\"\r\n"
			"seta sm_cacheSpotShadows \"Disabled\"\r\n") != std::string::npos,
			"Offline repair uses native Off values for SSAA, shaders and both shadow caches");
		require(disable_risk_settings(safe_graphics) == safe_graphics, "Repeated offline repair must be idempotent");
		require(read_values(safe_graphics) == read_values(graphics_profile), "Offline repair leaves VR settings unchanged");
		require(update_config(safe_graphics, changed).find("seta r_ssaaSamples \"1\"\r\nseta r_preloadShadersFrontendAllow \"0\"\r\n"
			"seta r_preloadShaders \"0\"\r\nseta sm_cacheSunShadow \"Disabled\"\r\n"
			"seta sm_cacheSpotShadows \"Disabled\"\r\n") != std::string::npos,
			"Saving a VR draft cannot restore risk settings");
		require(disable_risk_settings("").find("seta r_ssaaSamples \"1\"") == 0,
			"Offline repair also works before the first game start");
		require(disable_risk_settings(long_bind).find(long_bind) == 0, "Offline repair preserves long unrelated lines");
		require(disable_risk_settings("// no final newline").find("// no final newline\r\nseta ") == 0,
			"Offline repair terminates the final comment before appending settings");
		if (argc > 1)
		{
			// Optional read-only check against a real engine-generated profile.
			std::ifstream file(argv[1], std::ios::binary);
			require(bool(file), "Could not open sample profile");
			const std::string profile{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
			const auto loaded = read_values(profile);
			const auto merged = update_config(profile, loaded);
			require(read_values(merged) == loaded, "Engine-generated settings must round trip");
			require(update_config(merged, loaded) == merged, "Engine-generated profile merge must be stable");
			std::cout << "Engine-generated profile checked in memory; source file unchanged\n";
		}
		std::cout << "Launcher VR settings tests passed\n";
		return 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
