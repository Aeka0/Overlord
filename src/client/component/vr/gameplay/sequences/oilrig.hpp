#pragma once
#include "../scripted_sequences.hpp"
#include <algorithm>
#include <cmath>
#include <optional>

namespace scripting {class entity;}

namespace vr::gameplay::sequences::oilrig
{
	struct evidence
	{
		bool alive{},linked{},attached{},surfaced{},swim_done{},looking_at_guard{},
			kill_started{},leaving_water{},out_of_water{},objective_complete{},kill_rig{},weapon_recovery{};
	};
	inline phase classify(const evidence& e) noexcept
	{
		if(!e.alive)return phase::none;
		if(e.weapon_recovery && e.kill_started)return phase::recovery;
		if(!e.linked || e.out_of_water || e.objective_complete)return phase::none;
		if(!e.attached && !e.surfaced && !e.swim_done && !e.kill_started)return phase::none;
		if(e.kill_started || e.leaving_water || (e.swim_done && e.kill_rig))return phase::execution;
		if(e.swim_done && e.looking_at_guard)return phase::melee;
		if(e.surfaced || e.swim_done)return phase::swim;
		return e.attached ? phase::transport : phase::none;
	}
	inline bool moving(phase p) noexcept {return p==phase::swim || p==phase::melee || p==phase::recovery;}
	inline bool turning(phase p) noexcept {return p==phase::transport || p==phase::swim || p==phase::melee;}
	inline constexpr float yaw_extension=45.f;
	inline float expanded_yaw(unsigned argument,float value,bool admitted) noexcept
	{
		// Native arguments 3/4 are yaw bounds, 5/6 are pitch. Do not accumulate
		// onto player state: each invocation starts from its authored parameter.
		return admitted && (argument==3 || argument==4) && std::isfinite(value) && value>=0 && value<=180 ?
			std::min(value+yaw_extension,180.f) : value;
	}
	// Keep both native inventory entries intact; only the active mission item
	// participates in abdominal display, acquisition and physical carry.
	enum class equipment { unrestricted, unavailable, detonator, claymore };
	inline bool permits_equipment(equipment active,std::string_view weapon) noexcept
	{
		if(weapon=="c4")return active==equipment::unrestricted || active==equipment::detonator;
		if(weapon=="claymore")return active==equipment::unrestricted || active==equipment::claymore;
		return true;
	}
	inline bool can_detonate(bool planted,bool triggered,bool native_c4,bool weapons_enabled,bool empty_feed) noexcept
	{return planted && !triggered && native_c4 && weapons_enabled && empty_feed;}
	inline view evacuation(bool alive,unsigned parent,unsigned worldbody,bool boarded,bool landed,bool boarding_helper=false) noexcept
	{
		if(!alive || !parent || !worldbody || (!boarded && !landed) ||
			(parent!=worldbody && !(boarding_helper && landed && !boarded)))return {};
		auto result=free_look(scenario::oilrig,phase::scripted_combat);result.camera=scene_cameras::oilrig_exit;
		result.retain_weapon=true;
		return result;
	}
	bool supported();
	phase observe();
	view observe_evacuation(const scripting::entity& parent);
	equipment current_equipment() noexcept;
	// Called only by the native VM parameter adapter, not by render/input code.
	bool underwater_entry();
	std::string angle_status();
}
