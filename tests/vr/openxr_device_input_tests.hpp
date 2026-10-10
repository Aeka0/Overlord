#pragma once

namespace openxr_device_input_tests
{
	template <class Loader> void analog_input(Loader& loader, const d3d11::device_snapshot& graphics)
	{
		using namespace vr;
		using namespace controller_input;
		for (const bool standard : {false, true})
		for (unsigned profile = 0; profile < 3; ++profile)
		{
			configure(loader, graphics, tests::mock::scenario::happy);
			openxr::runtime_backend runtime{standard ? openxr_adaptation_tests::standard_startup : openxr_adaptation_tests::legacy_startup};
			runtime.set_desired_enabled(true);
			runtime.set_scene_mode(scene_mode::synthetic);
			tests::require(runtime.initialize(graphics), "analog input initialization failed");
			const char* paths[]{"/interaction_profiles/oculus/touch_controller",
				"/interaction_profiles/valve/index_controller", "/interaction_profiles/htc/vive_controller"};
			loader.set_interaction_profile(1, paths[profile]); // Left hand remains Touch.
			loader.queue_session_state(XR_SESSION_STATE_READY);
			loader.queue_session_state(XR_SESSION_STATE_FOCUSED);
			if (profile == 1)
			{
				// HUD identity and left policy queries fail independently. The
				// right-hand Index policy must still be admitted on this sample.
				loader.fail_once(tests::mock::failure_point::get_current_interaction_profile, XR_ERROR_RUNTIME_FAILURE);
				loader.fail_once(tests::mock::failure_point::get_current_interaction_profile, XR_ERROR_RUNTIME_FAILURE);
			}
			std::uint64_t pair{};
			const auto sample = [&](float trigger, float squeeze)
			{
				loader.set_action_value("right_trigger", trigger, 0);
				loader.set_action_value("right_squeeze", squeeze, 0);
				loader.set_action_value("left_squeeze", .2f, 0);
				runtime.on_present(graphics, ++pair);
				return controller_input::latest();
			};
			const auto trigger = profile == 1 ? index_trigger : value_button;
			const auto squeeze = profile == 1 ? index_squeeze : profile == 2 ? click_button : value_button;
			sample(0, 0);
			const auto pressed = sample(trigger.press, profile == 2 ? 1.f : squeeze.press);
			tests::require(pressed.trigger[1].down && pressed.squeeze[1].down && !pressed.squeeze[0].down &&
				pressed.trigger_analog[1].active && pressed.squeeze_analog[1].source == squeeze.source &&
				pressed.squeeze_analog[1].value == (profile == 2 ? 1.f : squeeze.press),
				"profile/action analog policy or physical hand identity was not preserved");
			const auto held = sample((trigger.press + trigger.release) / 2,
				profile == 2 ? 1.f : (squeeze.press + squeeze.release) / 2);
			tests::require(held.trigger[1].down && held.squeeze[1].down &&
				held.trigger[1].presses == pressed.trigger[1].presses && held.squeeze[1].releases == pressed.squeeze[1].releases,
				"analog jitter emitted a false edge inside the hysteresis band");
			const auto released = sample(0, 0);
			tests::require(!released.trigger[1].down && !released.squeeze[1].down &&
				released.trigger[1].releases == pressed.trigger[1].releases + 1 &&
				released.squeeze[1].releases == pressed.squeeze[1].releases + 1,
				"analog release did not reach the digital event counters exactly once");
			sample(1, 1);
			loader.queue_session_state(XR_SESSION_STATE_VISIBLE);
			const auto lost = sample(1, 1);
			tests::require(!lost.trigger[1].active && !lost.trigger_analog[1].active && !lost.squeeze[1].down,
				"focus loss retained an analog latch or available sample");
			loader.queue_session_state(XR_SESSION_STATE_FOCUSED);
			const auto recovery = sample(0, 0);
			tests::require(recovery.trigger[1].generation > released.trigger[1].generation && !recovery.trigger[1].down,
				"analog recovery reused the old digital generation");
			const auto bad = sample(std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity());
			tests::require(!bad.trigger[1].active && !bad.squeeze_analog[1].active && !bad.squeeze[1].down,
				"non-finite analog input retained a digital latch");
			const auto after_bad = sample(0, 0);
			tests::require(after_bad.squeeze[1].generation > recovery.squeeze[1].generation,
				"invalid analog action recovery did not establish a new generation");
			const auto valid = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
			const auto tracked = valid | XR_SPACE_LOCATION_POSITION_TRACKED_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
			loader.set_hand_flags(1, valid | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT);
			const auto inferred = sample(0, 1);
			const auto admitted = controller_input::latest_interaction();
			tests::require(inferred.grip[1].valid && inferred.sdk_grip[1].valid &&
				!inferred.grip[1].quality.position_tracked && inferred.target_display_time != 0 &&
				inferred.continuity_generation != after_bad.continuity_generation &&
				!admitted.grip[1].valid && admitted.grip[0].valid && admitted.squeeze[1].down,
				"inferred pose lost visual/button ownership or remained eligible for precision interaction");
			loader.set_hand_flags(1, tracked);
			const auto restored = sample(0, 1);
			tests::require(interaction_ready(restored.grip[1]) &&
				restored.continuity_generation != inferred.continuity_generation &&
				restored.squeeze[1].presses == inferred.squeeze[1].presses,
				"tracking quality recovery reused motion history or synthesized a grip press");
			runtime.shutdown();
		}
	}
	template <class Loader> void failed_view_publication(Loader& loader, const d3d11::device_snapshot& graphics)
	{
		using namespace vr;
		for (unsigned failure = 0; failure < 3; ++failure)
		{
			configure(loader, graphics, tests::mock::scenario::happy);
			head_pose_bridge::reset();
			head_pose_bridge::configure_target(true);
			head_pose_bridge::set_enabled(true);
			const auto reset = gsl::finally([] { head_pose_bridge::reset(); });
			openxr::runtime_backend runtime;
			runtime.set_desired_enabled(true);
			runtime.set_scene_mode(scene_mode::synthetic);
			tests::require(runtime.initialize(graphics), "view failure fixture initialization failed");
			loader.queue_session_state(XR_SESSION_STATE_READY);
			loader.queue_session_state(XR_SESSION_STATE_FOCUSED);
			loader.set_action_value("right_squeeze", 1, 0);
			runtime.on_present(graphics, 1);
			const auto before = controller_input::latest();
			tests::require(head_pose_bridge::get_status().pose_available && before.grip[1].valid,
				"view failure fixture lacked valid prior publications");
			if (failure == 0) loader.fail_once(tests::mock::failure_point::locate_views, XR_ERROR_RUNTIME_FAILURE);
			else loader.set_scenario(failure == 1 ? tests::mock::scenario::view_count_mismatch : tests::mock::scenario::invalid_eye_pose);
			runtime.on_present(graphics, 2);
			const auto invalid = controller_input::latest();
			const auto stats = loader.statistics();
			tests::require(!head_pose_bridge::get_status().pose_available && !invalid.focused &&
				!invalid.grip[1].valid && invalid.continuity_generation != before.continuity_generation,
				"rejected view sample left a stale valid head/input publication");
			tests::require(stats.frames_begun == stats.frames_ended && stats.zero_layer_frames == 1,
				"rejected view sample did not close the SDK frame exactly once");
			loader.set_scenario(tests::mock::scenario::happy);
			runtime.on_present(graphics, 3);
			const auto recovered = controller_input::latest();
			tests::require(head_pose_bridge::get_status().pose_available && recovered.grip[1].valid &&
				recovered.squeeze[1].generation > before.squeeze[1].generation &&
				loader.statistics().frames_begun == loader.statistics().frames_ended,
				"view failure recovery stranded a frame or reused old button history");
			runtime.shutdown();
		}
	}
}
