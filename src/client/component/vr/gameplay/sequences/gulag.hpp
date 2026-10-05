#pragma once
#include "../scripted_sequences.hpp"

namespace vr::gameplay::sequences::gulag
{
	// Copied observations; the native adapter owns VM access and entity lifetime.
	struct cinematic_observation
	{
		bool alive{}, linked{};
		std::string_view rig;
		bool price_anchor{}, cafeteria{}, evacuation_rig{};
	};
	struct evacuation_observation
	{
		bool alive{}, linked{};
		std::string_view rig;
		bool begun{}, used{};
	};
	struct rope_observation
	{
		bool alive{}, evacuation_begun{}, linked{}, used{};
	};
	struct intro_observation
	{
		bool alive{}, linked{}, intro_controller{};
		game_view::scripted_camera_tag tag{game_view::scripted_camera_tag::player};
	};
	struct native_use_observation
	{
		bool weapon_permission{}, alive{}, linked{};
	};

	inline game_view::scripted_camera_tag intro_tag(unsigned parent,
	                                                unsigned camera,
	                                                unsigned animated) noexcept
	{
		if (!parent)
			return game_view::scripted_camera_tag::none;
		if (parent == animated)
			return game_view::scripted_camera_tag::player;
		return parent == camera ? game_view::scripted_camera_tag::aim : game_view::scripted_camera_tag::none;
	}
	inline bool intro_parent(unsigned parent, unsigned camera, unsigned animated) noexcept
	{
		return intro_tag(parent, camera, animated) != game_view::scripted_camera_tag::none;
	}
	inline bool same_entity(unsigned a, unsigned b) noexcept
	{
		return a && a == b;
	}
	inline view cinematic(const cinematic_observation& input) noexcept
	{
		if (!input.alive || !input.linked)
			return {};
		// Price's breach victim remains below price_breach_ent until unlink;
		// the downed worldbody is specific to the cafeteria stage. Evacuation
		// then replaces it with level.player_rig while Price removes the rock.
		if ((input.rig == "player_rig" && (input.price_anchor || input.evacuation_rig)) ||
		    (input.rig == "worldbody" && input.cafeteria))
		{
			auto result = free_look(scenario::gulag, phase::execution);
			result.camera = scene_cameras::gulag_later;
			result.rotation_tag = game_view::scripted_camera_tag::player;
			return result;
		}
		return {};
	}
	inline view evacuation(const evacuation_observation& input) noexcept
	{
		if (!input.alive || !input.linked || input.rig != "player_rig" || !input.begun || !input.used)
			return {};
		auto result = free_look(scenario::gulag, phase::execution);
		result.camera = scene_cameras::gulag_evacuation;
		result.rotation_tag = game_view::scripted_camera_tag::player;
		return result;
	}
	inline bool rope_ready(const rope_observation& input) noexcept
	{
		return input.alive && input.evacuation_begun && !input.linked && !input.used;
	}
	inline view rope_use(const rope_observation& input) noexcept
	{
		view result{};
		if (!rope_ready(input))
			return result;
		result.scene = scenario::gulag;
		result.stage = phase::scripted_use;
		// Native weapons stay disabled after the authored rig unlinks. The
		// now-interactive player still needs tracked empty hands to use the rope.
		result.independent_hands = true;
		result.allow_world_use = result.allow_movement = result.allow_turn = true;
		return result;
	}
	inline bool rope_target(std::string_view flag, std::string_view rig) noexcept
	{
		return flag == "player_uses_rig" || rig == "ending_rope1" || rig == "ending_rope";
	}
	inline bool retired_rope_target(std::string_view flag) noexcept
	{
		return flag == "player_ropes";
	}
	enum class use_kind
	{
		unknown,
		ordinary,
		rope,
		retired
	};
	struct use_witness
	{
		std::uint64_t timeline{}, generation{};
		use_kind kind{};
		use_kind current(std::uint64_t epoch, std::uint64_t serial) const noexcept
		{
			return epoch && timeline == epoch && generation == serial ? kind : use_kind::unknown;
		}
	};
	inline bool native_use_allowed(use_kind kind,
	                               const view& sequence,
	                               const native_use_observation& input) noexcept
	{
		if (!input.alive)
			return false;
		if (kind == use_kind::ordinary)
			return input.weapon_permission;
		return kind == use_kind::rope && !input.linked && sequence.epoch &&
		       sequence.scene == scenario::gulag && sequence.stage == phase::scripted_use &&
		       sequence.allow_world_use;
	}
	inline view classify(const intro_observation& input) noexcept
	{
		view result{};
		if (!input.alive || !input.linked || !input.intro_controller ||
		    (input.tag != game_view::scripted_camera_tag::player &&
		     input.tag != game_view::scripted_camera_tag::aim))
			return result;
		result.scene = scenario::gulag;
		result.stage = phase::scripted_combat;
		result.retain_weapon = true;
		result.camera = input.tag == game_view::scripted_camera_tag::aim ? scene_cameras::gulag_intro_legacy
		                                                                 : scene_cameras::gulag_intro;
		result.rotation_tag = input.tag;
		// Native yaw/roll comes from the animated tag, never from player view
		// clamps. Native pitch is excluded; physical head rotation remains free.
		// Retention is independent of the selected weapon: explicit stow/draw
		// and slot exchanges may temporarily leave native selection empty.
		result.allow_movement = result.allow_turn = true;
		return result;
	}
	bool supported();
	view observe();
	// Server-only; gates the same native targets for both hover and use leases.
	bool allows_use(int entity) noexcept;
	// Native G_PlayerUse runs before scheduler::pipeline::server callbacks.
	// Revalidate their value-only target identity without entering the script VM.
	bool allows_native_use(int entity, std::uint64_t generation) noexcept;
}
