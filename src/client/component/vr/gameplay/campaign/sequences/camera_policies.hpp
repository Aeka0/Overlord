#pragma once
#include "../../../scripted_camera.hpp"

namespace vr::gameplay::sequences::scene_cameras
{
	// The scene comparison/editing surface. Identity and phase admission stay
	// in each scene adapter; all camera math stays in the portable camera_rig.
	using namespace game_view;
	inline constexpr camera_policy linked_gameplay{
	    camera_owner::player, head_rotation::free, head_translation::attenuated};
	inline constexpr camera_policy physical_ladder{
	    camera_owner::player, head_rotation::free, head_translation::tracked};
	inline constexpr auto rappel = camera_profiles::free;
	inline constexpr auto breach = camera_profiles::yaw;
	inline constexpr auto estate_drag = linked_gameplay;
	inline constexpr auto estate_ending = camera_profiles::align_on_entry(camera_profiles::authored);
	inline constexpr auto airport_boarding = camera_profiles::authored_full;
	inline constexpr camera_policy airport_shot{camera_owner::script,
	                                            head_rotation::fixed,
	                                            head_translation::fixed,
	                                            script_rotation::override_view,
	                                            script_axes::all,
	                                            rotation_source::tag};
	// Gulag's transient tag_turret aim helper is not a camera bank frame.
	// Retain heading motion until the remastered player rig owns the shot;
	// only that rig may consume its yaw/roll entry alignment.
	inline constexpr auto gulag_intro_legacy = camera_profiles::authored;
	inline constexpr auto gulag_intro = camera_profiles::authored_yaw_roll;
	inline constexpr auto gulag_later = camera_profiles::align_on_entry(camera_profiles::free);
	inline constexpr camera_policy gulag_evacuation{camera_owner::script,
	                                                head_rotation::free,
	                                                head_translation::fixed,
	                                                script_rotation::additive,
	                                                script_axes::yaw_roll,
	                                                rotation_source::tag,
	                                                camera_entry::align};
	inline constexpr auto sliding = camera_profiles::aligned;
	inline constexpr auto favela = camera_profiles::aligned;
	inline constexpr auto trainer = camera_profiles::free;
	inline constexpr auto roadkill = camera_profiles::free;
	inline constexpr auto dcemp_wakeup = camera_profiles::free;
	inline constexpr auto dcemp_space = camera_profiles::align_on_entry(
	    camera_policy{camera_owner::script, head_rotation::free, head_translation::tracked});
	inline constexpr auto dcemp_ground = camera_profiles::align_on_entry(linked_gameplay);
	inline constexpr auto cliffhanger_helper = camera_profiles::free;
	inline constexpr camera_policy cliffhanger_physical{
	    camera_owner::script, head_rotation::free, head_translation::tracked};
	inline constexpr auto cliffhanger_body = camera_profiles::authored;
	inline constexpr camera_policy cliffhanger_rescue{camera_owner::script,
	                                                  head_rotation::free,
	                                                  head_translation::attenuated,
	                                                  script_rotation::additive,
	                                                  script_axes::all,
	                                                  rotation_source::tag};
	inline constexpr auto ending_body = camera_profiles::authored;
	inline constexpr auto ending_wakeup = camera_profiles::authored_aligned;
	inline constexpr auto ending_wounded = camera_profiles::authored_aligned;
	inline constexpr auto ending_subdual = camera_profiles::authored_aligned;
	inline constexpr auto ending_approach = linked_gameplay;
	inline constexpr auto ending_unlinked = camera_profiles::free;
	inline constexpr camera_policy museum_credits{camera_owner::script,
	                                              head_rotation::free,
	                                              head_translation::tracked,
	                                              script_rotation::additive,
	                                              script_axes::yaw,
	                                              rotation_source::view,
	                                              camera_entry::align};
	inline constexpr auto vehicle = camera_profiles::vehicle;
	inline constexpr auto vehicle_exit = camera_profiles::aligned;
	inline constexpr auto oilrig_input = linked_gameplay;
	inline constexpr camera_policy oilrig_native{camera_owner::script,
	                                             head_rotation::fixed,
	                                             head_translation::attenuated,
	                                             script_rotation::override_view,
	                                             script_axes::all};
	inline constexpr auto oilrig_exit = camera_profiles::aligned;
	inline constexpr auto death = camera_profiles::aligned;
	inline constexpr camera_policy mounted{camera_owner::script,
	                                       head_rotation::free,
	                                       head_translation::tracked,
	                                       script_rotation::additive,
	                                       script_axes::yaw,
	                                       rotation_source::view,
	                                       camera_entry::align};
	inline constexpr auto thermal_scope = camera_profiles::fixed_scope;
}
