#pragma once
#include "weapon_carry.hpp"
#include "weapon_scene.hpp"
#include "holster_presentation.hpp"
#include "support_carry_rotation.hpp"
#include "hands/pose_solver.hpp"
#include "arm_geometry.hpp"
#include "../controller_input.hpp"
#include "weapon_ejection.hpp"
#include "weapon_runtime_lifecycle.hpp"
namespace vr::gameplay::weapons {struct profile;}

namespace vr::gameplay::weapons::carry
{
	std::optional<hands::quat> support_basis(const hold&,std::uint64_t space) noexcept;
	bool active() noexcept;
	hold current_hold() noexcept;
	// Empty-hand query only. Specific combinations use hand_interaction::permits.
	bool hand_available(hand) noexcept;
	// Render ownership excludes temporary interaction leases (use/magazine/bolt).
	bool hand_has_weapon(hand) noexcept;
	const profile* held_profile(hand) noexcept;
	void publish_scene(const scene&) noexcept;
	// Exact solved wrist positions and gun-frame aim rotations for this render
	// sample. Secondary weapons share the primary arm solver's reach limits.
	void publish_hands(const std::array<hands::anchor,2>&,const std::array<hands::arm_geometry,2>&,const controller_input::frame&,
		std::uintptr_t object,std::uintptr_t matrices,std::uint32_t epoch,const hands::vec& view_offset) noexcept;
	hold held(identity) noexcept;
	bool contains(identity) noexcept;
	runtime_lifecycle::ownership_snapshot scene_ownership() noexcept;
	bool request_abdominal(std::uint32_t,hand)noexcept;
	bool abdominal_active(std::uint32_t)noexcept;
	bool abdominal_request_valid(std::uint32_t)noexcept;
	// Includes cancelled draws until the native startup has drained and unlocked.
	std::uint32_t pending_abdominal_weapon()noexcept;
	bool restore_native_projection()noexcept;
	inventory capture_inventory(std::span<const owned_instance>,std::uint32_t selected);
	bool restore_inventory(std::span<const instance>,identity selected);
	// Definition-only native callers must use this explicit, unambiguous projection.
	identity native_identity(std::uint32_t weapon) noexcept;
	std::array<instance,2> held_instances() noexcept;
	std::array<instance,visible_instance_capacity> visible_instances(bool include_held=true) noexcept;
	struct storage_scene
	{
		instance item{};holster_layout layout{};int native_loaded{-1};
		std::uint64_t reference{};controller_input::clock::time_point at{};
	};
	// Copied server inventory/counts only. Renderers never query or mutate native ammo.
	storage_scene stored_scene(identity) noexcept;
	scene firing_scene(identity) noexcept;
	// Diagnostics use the same transactions and native adapter as controller input.
	bool debug_draw(location,hand);
	bool debug_release(location,bool drop);
}
