#pragma once
#include "component/vr/input_history.hpp"
#include "component/vr/controller_haptics.hpp"
#include "component/vr/native_hud_capture.hpp"
#include "component/vr/controller_pose_reference.hpp"
#include "component/vr/controller_calibration.hpp"
#include "component/vr/native_menu.hpp"
#include "component/vr/native_render_session.hpp"
#include "component/vr/native_stereo_source.hpp"

namespace vr::tests
{
	extern std::optional<engine_scene_resolution::extent> native_resolution_override;
	extern native_menu::state menu_state;
	extern std::shared_ptr<const native_menu::images> menu_images;
	extern native_menu::pointer menu_pointer;
} // namespace vr::tests

namespace openxr_adaptation_tests
{
	inline vr::controller_pose_reference::configuration grip_fixture;
	inline unsigned grip_fixture_calls{};
	inline vr::tests::mock::get_statistics_fn grip_statistics{};
	inline vr::controller_pose_reference::configuration query_grip_fixture()
	{
		++grip_fixture_calls;
		if (grip_statistics)
		{
			vr::tests::mock::statistics stats{};
			grip_statistics(&stats);
			vr::tests::require(stats.instances_created == 0, "metadata query overlapped an OpenXR instance");
		}
		return grip_fixture;
	}
	inline void wrist_reference_trajectory()
	{
		using namespace vr;
		grip_fixture = {};
		grip_fixture.target = controller_pose_reference::basis::calibration_frame;
		// Representative translated/rotated static components; test data are
		// deliberately distinct per hand and are not production controller presets.
		for (unsigned hand = 0; hand < 2; ++hand)
		{
			const pose_filter::pose device_from_grip{
			    {hand == 0 ? .007f : -.009f, -.002f, .102f},
			    controller_calibration::rotation({hand == 0 ? 20.6f : 17.f, 3.f, -2.f})};
			grip_fixture.hands[hand] = {.grip_from_calibration = pose_filter::inverse(device_from_grip),
			                            .reference_id = hand == 0 ? "test-left" : "test-right",
			                            .ready = true};
			const pose_filter::vec fixed_wrist{hand == 0 ? -.3f : .3f, 1.2f, -.5f};
			const pose_filter::vec lever{hand == 0 ? -.02f : .02f, 0, .12f};
			bool wrong_origin_moves = false;
			for (const controller_calibration::angles angles : {controller_calibration::angles{},
			                                                    {90.f, 0, 0},
			                                                    {0, 90.f, 0},
			                                                    {0, 0, 90.f},
			                                                    {-45.f, 30.f, 25.f}})
			{
				const auto rotation = controller_calibration::rotation(angles);
				const pose_filter::pose device{
				    pose_filter::sub(fixed_wrist, pose_filter::rotate(rotation, lever)), rotation};
				const auto sdk_grip = pose_filter::compose(device, device_from_grip);
				controller_input::hand_pose normalized{true, {sdk_grip.position, sdk_grip.orientation}};
				vr::tests::require(controller_pose_reference::normalize_grip(normalized, grip_fixture, hand),
				                   "grip reference conversion failed");
				const auto recovered_wrist =
				    pose_filter::add(normalized.tracking.position_meters,
				                     pose_filter::rotate(normalized.tracking.orientation, lever));
				vr::tests::require(pose_filter::length(pose_filter::sub(recovered_wrist, fixed_wrist)) <
				                       .00001f,
				                   "physical wrist wandered while rotating around a stationary wrist");
				const auto wrong_wrist =
				    pose_filter::add(sdk_grip.position, pose_filter::rotate(sdk_grip.orientation, lever));
				wrong_origin_moves |= pose_filter::length(pose_filter::sub(wrong_wrist, fixed_wrist)) > .03f;
			}
			vr::tests::require(wrong_origin_moves,
			                   "trajectory fixture did not exercise the displaced grip origin");
		}
		auto scaled = grip_fixture;
		scaled.hands[0].grip_from_calibration.orientation[0][0] *= 1.02f;
		vr::tests::require(!controller_pose_reference::valid_component(scaled.hands[0].grip_from_calibration),
		                   "non-rigid component metadata cannot use a transpose as its inverse");
		controller_input::hand_pose invalid{true, {}};
		auto missing = grip_fixture;
		missing.hands[0].ready = false;
		vr::tests::require(!controller_pose_reference::normalize_grip(invalid, missing, 0) && !invalid.valid,
		                   "missing grip metadata must reject the affected gameplay hand");
		invalid = {true, {}};
		auto reflected = grip_fixture;
		reflected.hands[0].grip_from_calibration.orientation[0][0] *= -1;
		vr::tests::require(!controller_pose_reference::normalize_grip(invalid, reflected, 0) &&
		                       !invalid.valid,
		                   "reflected component metadata must not enter gameplay tracking");
	}
	template <class Loader> void calibrated_grip_input(Loader& loader, const d3d11::device_snapshot& graphics)
	{
		configure(loader, graphics, vr::tests::mock::scenario::happy);
		wrist_reference_trajectory();
		grip_fixture_calls = 0;
		grip_statistics = loader.get_statistics;
		const auto reset_statistics = gsl::finally([] { grip_statistics = nullptr; });
		vr::openxr::runtime_backend runtime{query_grip_fixture};
		runtime.set_desired_enabled(true);
		runtime.set_scene_mode(vr::scene_mode::synthetic);
		vr::tests::require(runtime.initialize(graphics) && grip_fixture_calls == 1,
		                   "host grip metadata was not copied during initialization");
		loader.queue_session_state(XR_SESSION_STATE_READY);
		loader.queue_session_state(XR_SESSION_STATE_FOCUSED);
		runtime.on_present(graphics, 1);
		const auto input = vr::controller_input::latest();
		for (unsigned hand = 0; hand < 2; ++hand)
		{
			vr::tests::require(input.runtime_grip[hand].valid && input.runtime_aim[hand].valid,
			                   "normalized input lost grip or aim validity");
			vr::tests::require(std::abs(input.runtime_grip[hand].tracking.position_meters[2] -
			                            input.runtime_aim[hand].tracking.position_meters[2]) > .08f,
			                   "OpenXR gameplay grip still used the unconverted SDK origin");
			vr::tests::require(std::abs(input.runtime_aim[hand].tracking.orientation[0][0] - 1) < .00001f &&
			                       std::abs(input.runtime_aim[hand].tracking.orientation[1][1] - 1) < .00001f,
			                   "grip adaptation changed the SDK aim direction");
		}
		const auto status = runtime.get_status();
		vr::tests::require(status.controller_input_ready &&
		                       status.controller_reference_ids[0] == "test-left" &&
		                       status.controller_reference_ids[1] == "test-right",
		                   "calibration reference provenance was not reported");
		runtime.shutdown();
		configure(loader, graphics, vr::tests::mock::scenario::happy);
		grip_fixture.expected_runtime = "unrelated-runtime";
		vr::openxr::runtime_backend unrelated{query_grip_fixture};
		unrelated.set_desired_enabled(true);
		unrelated.set_scene_mode(vr::scene_mode::synthetic);
		vr::tests::require(unrelated.initialize(graphics) &&
		                       unrelated.get_status().controller_pose_reference == "openxr_grip" &&
		                       unrelated.get_status().controller_reference_ids[0].empty(),
		                   "foreign runtime metadata was applied to grip calibration");
		unrelated.shutdown();
	}

	template <class Loader>
	void focused_startup_height(Loader& loader, const d3d11::device_snapshot& graphics)
	{
		using namespace vr;
		configure(loader, graphics, tests::mock::scenario::happy);
		head_pose_bridge::reset();
		head_pose_bridge::configure_target(true);
		head_pose_bridge::set_enabled(true);
		head_pose_bridge::set_world_scale(100);
		const auto reset_head = gsl::finally([] { head_pose_bridge::reset(); });
		openxr::runtime_backend runtime;
		runtime.set_desired_enabled(true);
		runtime.set_scene_mode(scene_mode::synthetic);
		tests::require(runtime.initialize(graphics), "height reference initialization failed");
		loader.set_head_height(.25f);
		loader.queue_session_state(XR_SESSION_STATE_READY);
		loader.queue_session_state(XR_SESSION_STATE_VISIBLE);
		runtime.on_present(graphics, 1);
		tests::require(head_pose_bridge::get_status().recenter_pending &&
		                   head_pose_bridge::get_status().recenter_count == 0,
		               "unfocused startup pose established the height reference");
		const auto valid = XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
		const auto tracked =
		    valid | XR_VIEW_STATE_POSITION_TRACKED_BIT | XR_VIEW_STATE_ORIENTATION_TRACKED_BIT;
		loader.set_view_flags(valid);
		loader.set_head_height(.5f);
		loader.queue_session_state(XR_SESSION_STATE_FOCUSED);
		runtime.on_present(graphics, 2);
		tests::require(head_pose_bridge::get_status().recenter_pending &&
		                   !head_pose_bridge::get_status().pose_available,
		               "inferred startup pose established the height reference");
		loader.set_view_flags(tracked);
		loader.set_head_height(1.65f);
		runtime.on_present(graphics, 3);
		auto head = head_pose_bridge::get_status();
		tests::require(head.pose_available && head.recenter_count == 1 &&
		                   std::abs(head.local_position_units[2]) < .0001f,
		               "first focused tracked pose did not establish neutral height");
		loader.set_head_height(1.75f);
		runtime.on_present(graphics, 4);
		tests::require(std::abs(head_pose_bridge::get_status().local_position_units[2] - 10) < .0001f,
		               "established reference suppressed real head height movement");
		loader.set_view_flags(0);
		runtime.on_present(graphics, 5);
		loader.set_head_height(1.85f);
		loader.set_view_flags(tracked);
		runtime.on_present(graphics, 6);
		head = head_pose_bridge::get_status();
		tests::require(head.recenter_count == 1 && std::abs(head.local_position_units[2] - 20) < .0001f,
		               "temporary tracking loss silently changed the height reference");
		head_pose_bridge::request_recenter();
		loader.set_view_flags(valid);
		runtime.on_present(graphics, 7);
		tests::require(head_pose_bridge::get_status().recenter_pending,
		               "manual recenter accepted an inferred pose");
		loader.set_view_flags(tracked);
		runtime.on_present(graphics, 8);
		head = head_pose_bridge::get_status();
		tests::require(head.recenter_count == 2 && std::abs(head.local_position_units[2]) < .0001f,
		               "manual recenter failed to establish a fresh tracked height");
		// Recreating LOCAL may change its origin even with the same graphics device.
		tests::require(runtime.initialize(graphics), "height reference session recreation failed");
		loader.set_head_height(2.35f);
		loader.queue_session_state(XR_SESSION_STATE_READY);
		loader.queue_session_state(XR_SESSION_STATE_FOCUSED);
		runtime.on_present(graphics, 9);
		head = head_pose_bridge::get_status();
		tests::require(head.recenter_count == 3 && std::abs(head.local_position_units[2]) < .0001f,
		               "new LOCAL space inherited the previous session height origin");
		runtime.shutdown();
	}

	template <class Loader>
	void virtualdesktop_grip_reference(Loader& loader, const d3d11::device_snapshot& graphics)
	{
		using namespace vr;
		configure(loader, graphics, tests::mock::scenario::happy);
		loader.set_runtime_name("VirtualDesktopXR");
		openxr::runtime_backend runtime;
		runtime.set_desired_enabled(true);
		runtime.set_scene_mode(scene_mode::synthetic);
		tests::require(runtime.initialize(graphics) &&
		                   runtime.get_status().controller_pose_reference == "touch_legacy_reference",
		               "VDXR did not select the canonical Touch reference");
		loader.queue_session_state(XR_SESSION_STATE_READY);
		loader.queue_session_state(XR_SESSION_STATE_FOCUSED);
		runtime.on_present(graphics, 1);
		auto input = controller_input::latest();
		for (unsigned hand = 0; hand < 2; ++hand)
		{
			tests::require(input.runtime_grip[hand].valid && input.runtime_aim[hand].valid,
			               "active VDXR Touch hands did not reach common input");
			tests::require(std::abs(input.runtime_grip[hand].tracking.position_meters[2] + .396074f) <
			                       .00002f &&
			                   std::abs(input.runtime_grip[hand].tracking.position_meters[0] -
			                            (hand == 0 ? -.207f : .207f)) < .00001f,
			               "VDXR grip failed to recover the legacy calibration origin");
			tests::require(input.runtime_aim[hand].tracking.position_meters[2] == -.3f &&
			                   input.runtime_aim[hand].tracking.orientation[1][1] == 1,
			               "VDXR normalization changed the SDK aim witness");
		}
		loader.set_interaction_profile(1, "/interaction_profiles/valve/index_controller");
		runtime.on_present(graphics, 2);
		input = controller_input::latest();
		tests::require(
		    input.runtime_grip[0].valid && !input.runtime_grip[1].valid && input.runtime_aim[1].valid &&
		        runtime.get_status().controller_pose_reference_error.find("right") != std::string::npos,
		    "profile mismatch applied Touch calibration to an unrelated hand");
		loader.set_interaction_profile(0, "");
		loader.set_interaction_profile(1, "");
		runtime.on_present(graphics, 3);
		input = controller_input::latest();
		tests::require(!input.runtime_grip[0].valid && !input.runtime_grip[1].valid,
		               "controller disconnect retained the cached calibration admission");
		for (unsigned hand = 0; hand < 2; ++hand)
			loader.set_interaction_profile(hand, "/interaction_profiles/oculus/touch_controller");
		runtime.on_present(graphics, 4);
		input = controller_input::latest();
		tests::require(input.runtime_grip[0].valid && input.runtime_grip[1].valid &&
		                   runtime.get_status().controller_pose_reference_error.empty(),
		               "Touch reconnection did not restore profile-scoped calibration");
		runtime.shutdown();
	}

	inline vr::native_stereo_source::proof renderer_proof;
	inline vr::native_stereo_source::proof current_proof()
	{
		return renderer_proof;
	}
	inline d3d11::present_event present(const d3d11::device_snapshot& graphics, std::uint64_t frame)
	{
		return {.graphics = graphics, .frame_index = frame};
	}
	inline void bridge()
	{
		vr::engine_stereo_bridge::configure_target(true);
		vr::engine_stereo_bridge::set_render_hook_installed(true);
		vr::engine_stereo_bridge::set_enabled(true);
	}
	template <class Loader> void native_pair(Loader& loader, const d3d11::device_snapshot& graphics)
	{
		configure(loader, graphics, vr::tests::mock::scenario::happy);
		loader.set_synthetic_checks(FALSE);
		loader.set_eye_extent(256, 256);
		vr::tests::native_resolution_override = vr::engine_scene_resolution::extent{256, 256};
		const auto reset = gsl::finally(
		    []
		    {
			    vr::tests::native_resolution_override.reset();
			    vr::engine_stereo_bridge::set_enabled(false);
		    });
		renderer_proof = {
		    .state = vr::native_stereo_source::phase::ready,
		    .source = {256,
		               256,
		               1,
		               1,
		               DXGI_FORMAT_R11G11B10_FLOAT,
		               {1, 0},
		               D3D11_USAGE_DEFAULT,
		               D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
		               0,
		               0},
		    .generation = graphics.generation,
		    .context = reinterpret_cast<std::uintptr_t>(graphics.context.Get()),
		    .owner_thread = GetCurrentThreadId()};
		auto source = vr::native_stereo_source::register_query(current_proof);
		vr::openxr::runtime_backend runtime;
		runtime.set_desired_enabled(true);
		runtime.set_scene_mode(vr::scene_mode::engine_stereo);
		vr::tests::require(runtime.initialize(graphics), "native transfer initialization failed");
		bridge();
		loader.queue_session_state(XR_SESSION_STATE_READY);
		loader.queue_session_state(XR_SESSION_STATE_FOCUSED);
		const auto first = present(graphics, 1);
		runtime.on_present(first);
		runtime.on_present_post(first, S_OK);
		vr::tests::require(runtime.get_status().native_renderer_ready &&
		                       vr::native_render_session::active().accepts_pair(1),
		                   std::format("native admission state={} ready={} error={} native_error={}",
		                               vr::to_string(runtime.get_status().state),
		                               runtime.get_status().native_renderer_ready,
		                               runtime.get_status().last_error,
		                               runtime.get_status().native_renderer_error));
		Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
		vr::tests::require(
		    SUCCEEDED(graphics.device->CreateTexture2D(&renderer_proof.source, nullptr, &texture)) &&
		        SUCCEEDED(graphics.device->CreateRenderTargetView(texture.Get(), nullptr, &target)),
		    "native HDR source allocation failed");
		// H2 reuses one physical target for its two owner passes. Each pass
		// copies completed pixels into a distinct leased native eye target.
		const float left[]{.9f, .1f, .8f, 1};
		graphics.context->ClearRenderTargetView(target.Get(), left);
		vr::tests::require(
		    vr::native_render_session::active().copy_eye(1, 0, texture.Get(), 4, graphics.context.Get()),
		    "left native copy failed");
		const auto pending = present(graphics, 2);
		runtime.on_present(pending);
		runtime.on_present_post(pending, S_OK);
		vr::tests::require(loader.statistics().frames_waited == 1 && loader.statistics().images_acquired == 0,
		                   "partial stereo must not acquire images, resample poses "
		                   "or submit a substitute");
		const float right[]{.1f, .9f, .8f, 1};
		graphics.context->ClearRenderTargetView(target.Get(), right);
		vr::tests::require(
		    vr::native_render_session::active().copy_eye(1, 1, texture.Get(), 4, graphics.context.Get()),
		    "right native copy failed");
		const auto complete = present(graphics, 3);
		runtime.on_present(complete);
		vr::tests::require(runtime.get_status().submitted_frames == 1 &&
		                       loader.statistics().projection_frames == 1,
		                   std::format("native submit state={} frames={} xr={} error={}",
		                               vr::to_string(runtime.get_status().state),
		                               runtime.get_status().submitted_frames,
		                               runtime.get_status().last_xr_result_name,
		                               runtime.get_status().last_error));
		vr::tests::require(!vr::native_render_session::active().pair_published(1),
		                   "native source must retire after same-queue transfer");
		const auto pixels = loader.statistics().last_projection_pixels;
		vr::tests::require((pixels[0] & 255) > 200 && ((pixels[1] >> 8) & 255) > 200 &&
		                       pixels[0] != pixels[1],
		                   "native eye colors or linear/sRGB transfer changed");
		runtime.on_present_post(complete, S_OK);
		const auto wrong_pre = present(graphics, 4), wrong_post = present(graphics, 5);
		runtime.on_present(wrong_pre);
		runtime.on_present_post(wrong_post, S_OK);
		vr::tests::require(runtime.get_status().state == vr::runtime_state::fatal_for_vr &&
		                       loader.statistics().frames_waited == 2,
		                   "mismatched Present must not open another XR prediction");
		runtime.shutdown();
		const auto statistics = loader.statistics();
		vr::tests::require(!runtime.get_status().loader_loaded &&
		                       statistics.action_sets_created == statistics.action_sets_destroyed &&
		                       statistics.actions_created == statistics.actions_destroyed &&
		                       statistics.spaces_created == statistics.spaces_destroyed,
		                   "native path leaked action or space ownership during shutdown");
	}
	template <class Loader> void focused_input(Loader& loader, const d3d11::device_snapshot& graphics)
	{
		configure(loader, graphics, vr::tests::mock::scenario::happy);
		vr::openxr::runtime_backend runtime;
		runtime.set_desired_enabled(true);
		runtime.set_scene_mode(vr::scene_mode::synthetic);
		vr::tests::require(runtime.initialize(graphics), "input initialization failed");
		loader.set_action_value("move", .3f, .4f);
		loader.set_action_value("right_trigger", 1, 0);
		loader.set_action_value("menu_recenter", 1, 0);
		loader.set_action_value("right_primary", 1, 0);
		loader.queue_session_state(XR_SESSION_STATE_READY);
		loader.queue_session_state(XR_SESSION_STATE_FOCUSED);
		runtime.on_present(graphics, 1);
		const auto first = vr::controller_input::latest();
		const auto history_before = vr::controller_input::get_input_history();
		vr::tests::require(
		    first.source.backend == vr::controller_input::input_backend::openxr &&
		        first.source.runtime_focus_known && first.source.runtime_focus &&
		        first.source.gate.reason == vr::controller_input::input_reason::none,
		    "focused OpenXR input must record successful action synchronization separately from runtime focus");
		vr::tests::require(first.focused && first.move_active && std::abs(first.move[0] - .3f) < .001f &&
		                       first.trigger[1].down && first.menu_recenter.down && !first.primary[0].down &&
		                       first.primary[1].down && first.runtime_grip[0].valid &&
		                       first.runtime_aim[1].valid,
		                   "OpenXR input did not preserve hand, menu or pose semantics");
		vr::controller_haptics::request(1,
		                                {.seconds = .02f,
		                                 .frequency = 120,
		                                 .amplitude = .5f,
		                                 .reference = first.reference_generation,
		                                 .at = vr::controller_input::clock::now()});
		runtime.on_present(graphics, 2);
		vr::tests::require(loader.statistics().haptic_events == 1,
		                   "haptic mailbox was not delivered through OpenXR");
		loader.queue_session_state(XR_SESSION_STATE_VISIBLE);
		runtime.on_present(graphics, 3);
		const auto unfocused = vr::controller_input::latest();
		const auto history_lost = vr::controller_input::get_input_history();
		const auto focus_channel = vr::controller_input::index(vr::controller_input::input_channel::focus);
		vr::tests::require(unfocused.source.gate.reason ==
		                           vr::controller_input::input_reason::input_unavailable &&
		                       history_lost.channels[focus_channel].losses ==
		                           history_before.channels[focus_channel].losses + 1,
		                   "OpenXR focus loss must retain a distinct availability transition");
		vr::tests::require(!unfocused.focused && !unfocused.trigger[1].active && !unfocused.grip[0].valid &&
		                       !unfocused.move_active,
		                   "focus loss must neutralize actions and tracking");
		loader.queue_session_state(XR_SESSION_STATE_FOCUSED);
		runtime.on_present(graphics, 4);
		vr::tests::require(vr::controller_input::latest().trigger[1].generation > first.trigger[1].generation,
		                   "focus recovery must fence old button history");
		vr::tests::require(
		    vr::controller_input::get_input_history().channels[focus_channel].last_loss.reason ==
		        vr::controller_input::input_reason::input_unavailable,
		    "OpenXR recovery must preserve the preceding loss reason");
		runtime.shutdown();
	}
	template <class Loader>
	void frontend_menu(Loader& loader, const d3d11::device_snapshot& graphics, bool curved)
	{
		configure(loader, graphics, vr::tests::mock::scenario::happy);
		loader.set_synthetic_checks(FALSE);
		loader.set_cylinder_supported(curved);
		const auto reset = gsl::finally(
		    []
		    {
			    vr::tests::menu_state = {};
			    vr::tests::menu_images.reset();
		    });
		vr::tests::menu_state = {.session = 71,
		                         .revision = 3,
		                         .timestamp = GetTickCount64(),
		                         .enabled = true,
		                         .frontend = true,
		                         .count = 1};
		vr::tests::menu_state.menus[0].id = 11;
		auto images = std::make_shared<vr::native_menu::images>();
		images->owner = vr::tests::menu_state;
		auto image = std::make_shared<vr::native_hud_capture::frame>();
		image->width = 64;
		image->height = 32;
		image->generation = graphics.generation;
		image->timestamp = GetTickCount64();
		image->sequence = 1;
		D3D11_TEXTURE2D_DESC description{64,
		                                 32,
		                                 1,
		                                 1,
		                                 DXGI_FORMAT_R8G8B8A8_UNORM,
		                                 {1, 0},
		                                 D3D11_USAGE_DEFAULT,
		                                 D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
		                                 0,
		                                 0};
		vr::tests::require(
		    SUCCEEDED(graphics.device->CreateTexture2D(&description, nullptr, &image->texture)) &&
		        SUCCEEDED(
		            graphics.device->CreateRenderTargetView(image->texture.Get(), nullptr, &image->target)),
		    "menu source allocation failed");
		const float ink[]{.25f, .1f, 0, .5f};
		graphics.context->ClearRenderTargetView(image->target.Get(), ink);
		images->layers[0] = image;
		vr::tests::menu_images = images;
		vr::openxr::runtime_backend runtime;
		runtime.set_desired_enabled(true);
		runtime.set_scene_mode(vr::scene_mode::synthetic);
		vr::tests::require(runtime.initialize(graphics), "menu initialization failed");
		runtime.set_scene_mode(vr::scene_mode::engine_stereo);
		loader.queue_session_state(XR_SESSION_STATE_READY);
		loader.queue_session_state(XR_SESSION_STATE_FOCUSED);
		const auto first = present(graphics, 1);
		runtime.on_present(first);
		runtime.on_present_post(first, S_OK);
		const auto shown = present(graphics, 2);
		runtime.on_present(shown);
		vr::tests::require((curved ? loader.statistics().cylinder_frames : loader.statistics().quad_frames) ==
		                           1 &&
		                       loader.statistics().projection_frames == 0 && vr::tests::menu_pointer.ready &&
		                       vr::tests::menu_pointer.hit,
		                   "frontend must present and hit-test native UI without a world source");
		const auto pixel = loader.statistics().last_menu_pixel;
		vr::tests::require((pixel & 255) > 85 && (pixel & 255) < 100 && ((pixel >> 24) & 255) >= 127 &&
		                       ((pixel >> 24) & 255) <= 128,
		                   "encoded premultiplied UI must decode before linear composition");
		runtime.on_present_post(shown, S_OK);
		runtime.shutdown();
	}
	template <class Loader> void canted_native_views(Loader& loader, const d3d11::device_snapshot& graphics)
	{
		configure(loader, graphics, vr::tests::mock::scenario::canted_views);
		vr::openxr::runtime_backend runtime;
		runtime.set_desired_enabled(true);
		runtime.set_scene_mode(vr::scene_mode::synthetic);
		vr::tests::require(runtime.initialize(graphics), "canted-view fixture initialization failed");
		runtime.set_scene_mode(vr::scene_mode::engine_stereo);
		loader.queue_session_state(XR_SESSION_STATE_READY);
		loader.queue_session_state(XR_SESSION_STATE_FOCUSED);
		for (unsigned frame = 1; frame <= 2; ++frame)
		{
			const auto event = present(graphics, frame);
			runtime.on_present(event);
			runtime.on_present_post(event, S_OK);
		}
		const auto status = runtime.get_status();
		vr::tests::require(
		    status.state == vr::runtime_state::fatal_for_vr &&
		        status.last_error.find("canted eye orientations") != std::string::npos &&
		        loader.statistics().frames_waited == 1 && loader.statistics().zero_layer_frames == 1,
		    "unrepresentable native eye rotations must stop without submitting a flattened view");
		runtime.shutdown();
	}

} // namespace openxr_adaptation_tests
