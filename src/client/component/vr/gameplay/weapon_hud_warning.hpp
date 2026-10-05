#pragma once
#include "detachable_magazine.hpp"
#include "underbarrel_feed.hpp"

namespace vr::gameplay::weapon_hud
{
	// Mechanical readiness only: grip/trigger leases and native shot cooldowns
	// must not turn a loaded, ready weapon into a chambering prompt.
	inline bool needs_chamber(const weapons::mechanics::rules& rules,const weapons::mechanics::state& ammo,bool action_held=false) noexcept
	{
		return !action_held && weapons::mechanics::valid(rules,ammo) && weapons::mechanics::native_ammo(ammo).loaded>0 &&
			!weapons::mechanics::ready(rules,ammo);
	}
	inline bool needs_chamber(weapons::tube::rules rules,const weapons::tube::state& ammo,bool action_held=false) noexcept
	{
		return !action_held && weapons::tube::valid(rules,ammo) && weapons::tube::native_ammo(ammo).loaded>0 &&
			!weapons::tube::ready(rules,ammo);
	}
	inline bool needs_chamber(const weapons::underbarrel::state& ammo,bool action_held=false) noexcept
	{
		return !action_held && weapons::underbarrel::valid(ammo) && ammo.loaded>0 && !weapons::underbarrel::ready(ammo);
	}
	class chamber_warning
	{
	public:
		bool visible(bool needed,std::uint64_t now_ms) noexcept
		{
			if (needed && (!active_ || now_ms<started_)) started_=now_ms;
			active_=needed;
			return active_ && ((now_ms-started_)/500)%2==0;
		}
	private:
		bool active_{};
		std::uint64_t started_{};
	};
	enum class status_line { none, chamber, quick_reload, magazine_empty, no_ammo, count };
	inline status_line select_status(bool chamber,bool quick_loading) noexcept
	{return quick_loading?status_line::quick_reload:chamber?status_line::chamber:status_line::none;}
	using warning_messages=std::array<status_line,2>;
	inline warning_messages select_messages(bool chamber,bool quick_loading,int loaded,int reserve) noexcept
	{
		if(loaded<0 || reserve<0)return {};
		// Native loaded includes the chamber and inserted magazine/tube. Reserve
		// exhaustion alone is not an empty weapon, and the empty captions exclude
		// one another. Keep quick-loading feedback while all ammunition is absent.
		const bool exhausted=loaded==0 && reserve==0;
		if(quick_loading)return {status_line::quick_reload,exhausted?status_line::no_ammo:status_line::none};
		return {loaded==0 ? (exhausted?status_line::no_ammo:status_line::magazine_empty) :
			select_status(chamber,false),status_line::none};
	}
	inline bool has_message(const warning_messages& messages) noexcept
	{return messages[0]!=status_line::none || messages[1]!=status_line::none;}
	inline constexpr std::array<std::uint8_t,3> status_color(status_line line) noexcept
	{
		return line==status_line::chamber?std::array<std::uint8_t,3>{255,215,0}:
			line==status_line::magazine_empty || line==status_line::no_ammo?std::array<std::uint8_t,3>{255,128,128}:
			std::array<std::uint8_t,3>{255,255,255};
	}
	inline bool status_visible(chamber_warning& blink,status_line line,std::uint64_t now_ms) noexcept
	{
		// Always clear the old blink phase during loading; a subsequent chamber
		// prompt starts visible rather than inheriting a hidden half-cycle.
		const bool chamber=blink.visible(line==status_line::chamber,now_ms);
		return line==status_line::quick_reload || line==status_line::magazine_empty || line==status_line::no_ammo || chamber;
	}
	inline constexpr float warning_height_meters=.014f,warning_gap_meters=.004f;
	inline constexpr float warning_row_meters=warning_height_meters+2*warning_gap_meters;
	inline float warning_clearance(float primary_height,float secondary_height,float separation) noexcept
	{
		return (std::max)(0.f,(primary_height+secondary_height)*.5f+warning_row_meters-separation);
	}
}
