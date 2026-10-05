#pragma once
#include <array>
#include <initializer_list>
#include <string_view>

namespace game_text
{
	enum class locale
	{
		english, simplified_chinese, traditional_chinese, french, german, italian, spanish, russian, polish, portuguese, japanese, arabic, czech, spanish_latin_america, korean, turkish, count
	};
	enum class key
	{
		button_trigger, button_grip_left, button_grip_right, button_grip_either,
		item_weapon, interaction_use, interaction_pickup, rappel_brake, story_melee, right_stick_down, mine_prone, c4_draw, c4_detonate,
		recording_preview, interaction_resupply, weapon_needs_chamber, vehicle_duck, weapon_quick_loading, cinematic_skip, signal_flare,
		nightvision_on, nightvision_off, breach_place, dsm_connect, dsm_recover, claymore_place, rappel_attach, gulag_attach, designator_draw, designator_target, briefcase_pickup, notebook_pickup, notebook_open, notebook_boost, notebook_control, fixed_sniper_zoom, fixed_sniper_aim, fixed_sniper_controls, heartbeat_fold, snowmobile_board, vehicle_drive, vehicle_reload, museum_warning, trainer_fire, trainer_hip_fire, trainer_ads, trainer_stop_ads, trainer_next_target, trainer_sidearm, trainer_primary, trainer_reload, trainer_knife, trainer_crouch, trainer_stand, trainer_prone, trainer_jump, trainer_mantle, trainer_sprint, trainer_frag, trainer_flash, trainer_menu, m203_reload, weapon_magazine_empty, weapon_no_ammo, ending_knife_pull, count
	};
	inline constexpr std::size_t key_count=static_cast<std::size_t>(key::count);
	using catalog=std::array<std::u8string_view,key_count>;


	// Language-independent argument schema: Chinese-only additions do not
	// manufacture English translations just to provide a reference template.
	constexpr unsigned parameters(key id) noexcept
	{
		switch(id)
		{
		case key::interaction_use:
		case key::rappel_brake:
		case key::story_melee:
		case key::mine_prone:
		case key::interaction_resupply:
		case key::vehicle_duck:
		case key::breach_place:
		case key::dsm_connect:
		case key::dsm_recover:
		case key::rappel_attach:
		case key::gulag_attach:
		case key::briefcase_pickup:
		case key::notebook_pickup:
		case key::snowmobile_board:
		case key::museum_warning:
			return 1;
		case key::interaction_pickup:return 3;
		default:return 0;
		}
	}

	struct catalog_entry {key id;std::u8string_view text;};
	// Locale files use named entries, independent of enum order. All catalogs
	// are constexpr, so duplicate or invalid keys fail compilation.
	constexpr catalog make_catalog(std::initializer_list<catalog_entry> entries)
	{
		catalog out{};std::array<bool,key_count> seen{};
		for(const auto& entry:entries)
		{
			const auto index=static_cast<std::size_t>(entry.id);
			if(index>=key_count || seen[index])throw "Invalid or duplicate game text key";
			seen[index]=true;out[index]=entry.text;
		}
		return out;
	}
}
