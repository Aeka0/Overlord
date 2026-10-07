#pragma once
#include "scripted_sequence_policy.hpp"
#include "../scripted_arms_policy.hpp"
#include "../vehicles/policy.hpp"
#include "component/game_text.hpp"
#include "../../scripted_camera.hpp"
#include "sequences/camera_policies.hpp"
#include <optional>
#include <string>

namespace vr::gameplay::sequences
{
	struct view
	{
		phase stage{};
		std::uint64_t epoch{};
		int command_time{};
		int started_time{};
		std::uintptr_t player{};
		scenario scene{};
		vehicles::kind vehicle{};
		game_view::camera_policy camera{};
		game_view::scripted_camera_tag rotation_tag{};
		bool allow_movement{},allow_turn{},suspend_weapons{};
		std::uint64_t position_epoch{};
		int linked_entity{-1};
		unsigned linked_object{}; // Diagnostic VM wrapper only; not native lifetime.
		std::uint64_t linked_generation{};
		bool retain_weapon{},hide_body_arms{},dsm_progress{};
		scripted_arms::request arms{};
		bool allow_world_use{}; // Explicit script use while native firearms remain disabled.
		melee_delivery melee_mode{};
        bool independent_hands{},block_carry{},hide_chest_equipment{};
		std::array<int,4> hidden_entities{{-1,-1,-1,-1}};
		std::optional<game_text::key> instruction{};
	};
	inline bool same_position_owner(const view& previous,const view& next,std::uintptr_t player,int time,bool same_level) noexcept
	{
		// Temporary native helpers can lose their last VM reference between
		// observations. Recreating that wrapper is not a new camera shot.
		return previous.position_epoch && next.linked_entity>0 && previous.linked_entity==next.linked_entity &&
			previous.linked_generation==next.linked_generation && previous.player==player && same_level && time>=previous.command_time;
	}
	// Both command and render adapters use this priority and ownership contract.
	inline game_view::camera_request camera_request_for(const view& v,std::uint64_t scope=0,game_view::camera_request mounted={},std::uint64_t remote=0) noexcept
	{
		if(remote)return {game_view::camera_profiles::remote_control,remote|(1ull<<62)};
		if(scope)return {scene_cameras::thermal_scope,scope|(1ull<<63)};
		if(mounted.epoch)
		{
			if(mounted.policy.translation==game_view::head_translation::attenuated)
				mounted.policy.translation_gain=v.camera.translation_gain;
			return mounted;
		}
        // A player-owned, fully tracked camera keeps its gameplay reference
        // even when movement uses a native link (physical ladder carrier).
        // Re-anchoring here cancels the pre-grasp head offset and shifts entry.
        const auto position=v.camera.owner==game_view::camera_owner::player &&
            v.camera.translation==game_view::head_translation::tracked ? 0:v.position_epoch;
        // Gulag's real intro camera and rope attachment align once per shot.
        // The legacy intro aim helper preserves entry and cannot consume it;
        // after alignment, replacing a parent only rebases the delta source.
        const bool gulag_shot=v.scene==scenario::gulag &&
            (v.stage==phase::scripted_combat || v.camera==scene_cameras::gulag_evacuation);
        const auto entry=gulag_shot?v.epoch:v.position_epoch;
        return {v.camera,v.camera.owns_rotation()?v.epoch:0,position,entry};
	}
	// Rotation-only ownership: native scripts retain their existing weapon,
	// locomotion and body-animation authority, including early input notices.
	inline view free_look(scenario scene,phase stage=phase::transport)noexcept
	{
		view result{};result.scene=scene;result.stage=stage;
		result.camera=game_view::camera_profiles::aligned;
		result.allow_movement=result.allow_turn=true;return result;
	}
	inline view death_view() noexcept
	{
		auto result=free_look(scenario::death,phase::death);
		result.camera=scene_cameras::death;result.suspend_weapons=true;result.allow_movement=result.allow_turn=false;
		// Interactive arms are suppressed by weapon permission. Preserve the
		// separate native death body, including its authored arms and props.
		return result;
	}
	// Script data is read only on the server pipeline. Consumers receive values,
	// never VM references or zone-owned pointers.
	view latest() noexcept;
	view for_player(const void* player_state) noexcept;
	bool owns_body(const void* player_state) noexcept;
	bool owns_arms(const void* player_state) noexcept;
    bool independent_hands(const void* player_state) noexcept;
    bool chest_equipment_visible() noexcept;
	std::string prompt_status();
}
