#include <std_include.hpp>
#include "launcher/vr_settings_config.hpp"
#include "launcher/localization.hpp"
#include "launcher/game_language_catalog.hpp"
#include "launcher/html/html_argument.hpp"
#include "component/game_data.hpp"
#include <iostream>
#include <fstream>
#include <limits>

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
		VARIANT value;
		VariantInit(&value);
		const auto cleanup = gsl::finally([&] { VariantClear(&value); });
		html_argument argument(&value);
		const std::string chinese = "\xe7\xae\x80\xe4\xbd\x93\xe4\xb8\xad\xe6\x96\x87";
		for (const auto& text : {std::string{}, std::string("English"), chinese, chinese + std::string("\0end", 4)})
		{
			argument.set_string(text);
			require(argument.get_string() == text, "COM bridge preserves UTF-8 and embedded nulls");
		}
		bool rejected = false;
		try { argument.set_string("\xff"); } catch (const std::exception&) { rejected = true; }
		require(rejected, "Invalid UTF-8 must not be converted into mojibake");
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
		require(catalog.size()==vr::settings::choices.size(), "Choice catalog exposes every native setting");
		for(const auto& field:vr::settings::choices)
			for(std::size_t i=0;i<field.values.size() && field.values[i];++i)
				require(catalog[field.name][i]["value"]==field.values[i] && catalog[field.name][i]["labelKey"]==field.label_keys[i], "Frontend choices preserve native ordering and localized labels");
		const auto initial = defaults();
		require(validate(initial), "Shared defaults must be valid");
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
		require(initial[vr::settings::chambering_guide.name] == false && read_values("")[vr::settings::chambering_guide.name] == false,
			"Chambering guide defaults off, including existing profiles without the setting");
		for (const bool enabled : {false, true})
		{
			auto selected = initial;
			selected[vr::settings::quick_reload.name] = enabled;
			selected[vr::settings::chambering_guide.name] = enabled;
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
		const auto presets = controller_presets();
		require(presets.size() == 2 && presets[0]["id"] == "none" && presets[0]["label"] == "None" &&
			presets[1]["id"] == "meta_quest_3" && presets[1]["label"] == "Meta Quest 3", "Shared preset catalog");
		for (const auto& field : vr::settings::hand_alignment)
			require(initial[field.name] == 0 && presets[0]["values"][field.name] == 0,
				"New profiles and None must zero all six alignment fields");
		const json quest_values{{"vr_handOffsetInward", -.02}, {"vr_handOffsetBack", .12}, {"vr_handOffsetUp", -.1},
			{"vr_handAnglePitch", -20}, {"vr_handAngleYaw", 0}, {"vr_handAngleRoll", 0}};
		require(presets[1]["values"] == quest_values, "Quest 3 matches the accepted six-value calibration exactly");
		for (const auto& preset : presets)
		{
			auto candidate = initial;
			candidate.update(preset["values"]);
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
		auto quest_settings = initial;
		quest_settings.update(quest_values);
		const auto quest_config = update_config(original, quest_settings);
		require(quest_config.substr(0, preserved.size()) == preserved && read_values(quest_config) == quest_settings,
			"Preset values round trip without changing advanced wrist calibration or unrelated config");
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
			for (double value : {double(field.min), double(field.max)})
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
