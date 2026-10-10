#include <std_include.hpp>

#include "component/vr/engine_stereo_bridge.hpp"
#include "component/vr/engine_stereo_binding.hpp"
#include "component/vr/engine_stereo_backend_target.hpp"
#include "component/vr/engine_stereo_backend_view.hpp"
#include "component/vr/engine_stereo_probe.hpp"
#include "component/vr/engine_stereo_owner_pass.hpp"
#include "component/vr/engine_stereo_view.hpp"
#include "component/vr/head_pose_bridge.hpp"
#include "component/vr/scene_compositor.hpp"
#include "test_support.hpp"
#include "game_view_tests.hpp"
#include "roomscale_origin_tests.hpp"
#include <csetjmp>
#include "game/structs.hpp"
#include "component/vr/gameplay/mounted_turret_policy.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace
{
	std::atomic_bool target_select_entered{};
	std::atomic_bool target_select_release{};

	void blocking_h2_target_select(void* const opaque, const std::uint32_t target)
	{
		target_select_entered.store(true, std::memory_order_release);
		while (!target_select_release.load(std::memory_order_acquire))
		{
			std::this_thread::yield();
		}
		std::uintptr_t state{};
		std::memcpy(&state, static_cast<std::uint8_t*>(opaque) +
			vr::engine_stereo_backend_target::context_state_pointer_offset,
			sizeof(state));
		std::memcpy(reinterpret_cast<void*>(state +
			vr::engine_stereo_backend_target::current_target_offset),
			&target, sizeof(target));
	}

	vr::engine_stereo_probe::target_descriptor matching_target()
	{
		return {
			"9D554376FBE78248A423F6B776634453142FAFA1218645CD64B2499967512AAE",
			"58CB70D63E05DC239997420C3ED91F04D3D324AF0BDF8A9623F1012164E0ED9D",
			0x6023ED08,
			0x11E6BC00,
			true,
			true,
		};
	}

	vr::engine_stereo_probe::shared_camera_sample valid_camera()
	{
		return {
			90.0f,
			60.0f,
			{1.0f, 2.0f, 3.0f},
			{{{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}},
			{0.0f, 45.0f, 0.0f},
		};
	}

	void configure_bridge(const bool matched = true)
	{
		using namespace vr::engine_stereo_bridge;
		set_enabled(false);
		reset();
		configure_target(matched);
		set_world_scale(100.0f);
		set_swap_eyes(false);
		set_render_hook_installed(true);
		set_enabled(true);
	}

	void publish_valid_bridge_views()
	{
		vr::engine_stereo_bridge::publish_views(
			{-0.032f, 0.0f, 0.0f}, {0.032f, 0.0f, 0.0f},
			{{{-1.1f, 1.0f, -1.2f, 1.1f}, {-1.3f, 1.2f, -1.4f, 1.3f}}});
	}

	void expect_target_gate()
	{
		using namespace vr::engine_stereo_probe;
		reset();
		set_mode(mode::observe);
		configure_target({"bad", "bad", 0, 0, true, true});
		auto state = get_status();
		vr::tests::require(state.gate == target_gate::fingerprint_mismatch && !state.active,
			"target fingerprint mismatch unexpectedly activated probe");

		auto unavailable_target = matching_target();
		unavailable_target.renderer_boundary_executable = false;
		configure_target(unavailable_target);
		state = get_status();
		vr::tests::require(state.gate == target_gate::renderer_boundary_not_executable &&
			!state.active && state.abi == abi_state::unverified,
			"non-executable renderer boundary unexpectedly activated probe");

		unavailable_target = matching_target();
		unavailable_target.scene_hook_installed = false;
		configure_target(unavailable_target);
		state = get_status();
		vr::tests::require(state.gate == target_gate::scene_hook_not_installed &&
			!state.active && state.abi == abi_state::unverified,
			"missing scene hook unexpectedly activated probe");

		configure_target(matching_target());
		state = get_status();
		vr::tests::require(state.gate == target_gate::matched && state.active &&
			state.abi == abi_state::observation_only,
			"matching target did not activate observation-only probe");
		set_mode(mode::off);
		vr::tests::require(!get_status().active &&
			get_status().abi == abi_state::unverified,
			"off mode retained an active ABI state");
		set_mode(mode::observe);
	}

	void expect_camera_and_correlation()
	{
		using namespace vr::engine_stereo_probe;
		reset();
		configure_target(matching_target());
		set_mode(mode::observe);
		const auto frame = begin_renderer_frame(valid_camera());
		vr::tests::require(static_cast<bool>(frame), "valid camera did not begin renderer frame");
		record_renderer_boundary(frame, boundary_phase::before_original);
		end_renderer_frame(frame);
		record_present(7, 3);
		auto state = get_status();
		vr::tests::require(state.renderer_frame_count == 1 && state.present_count == 1 &&
			state.correlation_count == 1 && state.correlation_miss_count == 0 &&
			state.renderer_boundary_count == 3 && state.last_camera_valid &&
			state.last_present_frame_index == 7 && state.device_generation == 3,
			"valid renderer/present correlation was not recorded");

		record_resize();
		record_present(8, 3);
		state = get_status();
		vr::tests::require(state.resize_epoch == 1 && state.correlation_miss_count == 1,
			"resize did not invalidate the previous renderer correlation");
	}

	void expect_invalid_camera_and_trace()
	{
		using namespace vr::engine_stereo_probe;
		reset();
		configure_target(matching_target());
		set_mode(mode::observe);
		set_trace_enabled(true);
		auto camera = valid_camera();
		camera.fov_x = NAN;
		const auto frame = begin_renderer_frame(camera);
		vr::tests::require(static_cast<bool>(frame),
			"invalid camera unexpectedly suppressed renderer observation");
		end_renderer_frame(frame);
		auto state = get_status();
		vr::tests::require(state.invalid_camera_count == 1 && !state.last_camera_valid,
			"invalid camera was not recorded");
		std::array<trace_event, 8> events{};
		const auto count = read_recent_events(events.data(), events.size());
		vr::tests::require(count >= 2, "probe trace did not publish renderer boundary events");

		stop();
		vr::tests::require(!get_status().active &&
			get_status().abi == abi_state::unverified &&
			!begin_renderer_frame(valid_camera()),
			"stopped probe still accepted renderer frames");
	}

	void expect_controller_spatial_frame()
	{
		using namespace vr::head_pose_bridge;
		constexpr matrix3 identity{{{1,0,0},{0,1,0},{0,0,1}}};
		const auto close = [](const vector3& a, const vector3& b)
		{
			for (size_t i=0;i<3;++i) if (std::abs(a[i]-b[i])>0.001f) return false;
			return true;
		};
		reset();
		spatial_frame frame{};
		vr::tests::require(!get_spatial_frame(frame), "spatial frame requires active camera");
		configure_target(true);
		set_enabled(true);
		set_world_scale(40);
		publish_tracking_pose({{1,1.7f,2},identity});
		vr::tests::require(!get_spatial_frame(frame), "head publication alone cannot invent world origin");
		float origin[3]{100,200,300};
		float axis[3][3]{{0,1,0},{-1,0,0},{0,0,1}};
		vr::tests::require(apply_camera(origin,axis) && get_spatial_frame(frame), "camera publishes spatial frame");
		vr::tests::require(frame.generation==get_status().recenter_count, "spatial reference generation");
		vr::tests::require(close(frame.head_position, {100,200,300}) && close(frame.head_yaw_axis[0], {0,1,0}),
			"spatial frame includes applied center-eye head position and heading");
		vr::tests::require(close(frame.head_forward, {axis[0][0],axis[0][1],axis[0][2]}),
			"aiming forward belongs to the captured center-eye camera");
		world_pose world{};
		vr::tests::require(tracking_to_world(frame,{{1.25f,1.2f,1.5f},identity},world), "controller world conversion");
		vr::tests::require(close(world.position,{110,220,280}) && close(world.axis[0],{0,1,0}),
			"native yaw, tracking axes and world scale applied once");
		publish_tracking_pose({{1.1f,1.7f,2},identity});
		vr::tests::require(get_spatial_frame(frame), "same-generation native base survives next tracking publication");
		vr::tests::require(close(frame.head_position, {100,200,300}), "head anchor stays with captured camera transaction");
		for (const auto& rotation : std::array<matrix3, 3>{
			matrix3{{{1,0,0},{0,0.8f,-0.6f},{0,0.6f,0.8f}}},
			matrix3{{{0.8f,-0.6f,0},{0.6f,0.8f,0},{0,0,1}}},
			matrix3{{{1,0,0},{0,0,-1},{0,1,0}}}})
		{
			publish_tracking_pose({{1.1f,1.4f,2},rotation});
			float translated_origin[3]{100,200,300};
			float yaw_axis[3][3]{{0,1,0},{-1,0,0},{0,0,1}};
			vr::tests::require(apply_camera(translated_origin,yaw_axis) && get_spatial_frame(frame) &&
				close(frame.head_position, {104,200,288}), "shoulder head origin includes HMD translation once");
			vr::tests::require(close(frame.head_yaw_axis[0], {0,1,0}) && close(frame.head_yaw_axis[1], {-1,0,0}) &&
				close(frame.head_yaw_axis[2], {0,0,1}), "body heading removes pitch and roll including vertical pitch");
		}
		const auto generation=frame.generation;
		request_recenter();
		vr::tests::require(!get_spatial_frame(frame), "recenter invalidates spatial frame immediately");
		publish_tracking_pose({{2,1.7f,3},identity});
		vr::tests::require(!get_spatial_frame(frame), "new reference cannot reuse old native camera frame");
		float next_origin[3]{10,20,30};
		float next_axis[3][3]{{1,0,0},{0,1,0},{0,0,1}};
		vr::tests::require(apply_camera(next_origin,next_axis) && get_spatial_frame(frame) && frame.generation>generation,
			"new camera refreshes recentered frame");
		vr::tests::require(tracking_to_world(frame,{{2,1.7f,3},identity},world) && close(world.position,{10,20,30}),
			"recenter origin is shared with controller");
		const auto saved=world;
		auto invalid=frame;
		invalid.units_per_meter=std::numeric_limits<float>::infinity();
		vr::tests::require(!tracking_to_world(invalid,{{},identity},world) && world.position==saved.position && world.axis==saved.axis,
			"invalid world scale is transactional");
		vr::tests::require(!tracking_to_world(frame,{{},{}},world), "malformed orientation rejected");
		set_enabled(false);
		vr::tests::require(!get_spatial_frame(frame), "disabled head tracking cannot drive hands");
		reset();
	}

	void expect_head_pose_camera_transform()
	{
		using namespace vr::head_pose_bridge;
		constexpr matrix3 identity{{
			{1.0f, 0.0f, 0.0f},
			{0.0f, 1.0f, 0.0f},
			{0.0f, 0.0f, 1.0f},
		}};
		const auto approximately_equal_test = [](const float left, const float right)
		{
			return std::abs(left - right) < 0.0001f;
		};
		const auto multiply_matrix_test = [](const matrix3& left, const matrix3& right)
		{
			matrix3 result{};
			for (std::size_t row{}; row < 3; ++row)
			{
				for (std::size_t column{}; column < 3; ++column)
				{
					for (std::size_t inner{}; inner < 3; ++inner)
					{
						result[row][column] += left[row][inner] * right[inner][column];
					}
				}
			}
			return result;
		};
		const auto h2_camera_axis = [&](const float yaw_degrees,
			const float pitch_degrees, const float roll_degrees)
		{
			constexpr auto degrees_to_radians = 0.017453292519943295f;
			const auto yaw = yaw_degrees * degrees_to_radians;
			const auto pitch = pitch_degrees * degrees_to_radians;
			const auto roll = roll_degrees * degrees_to_radians;
			const matrix3 yaw_axis{{
				{std::cos(yaw), std::sin(yaw), 0.0f},
				{-std::sin(yaw), std::cos(yaw), 0.0f},
				{0.0f, 0.0f, 1.0f},
			}};
			const matrix3 pitch_axis{{
				{std::cos(pitch), 0.0f, std::sin(pitch)},
				{0.0f, 1.0f, 0.0f},
				{-std::sin(pitch), 0.0f, std::cos(pitch)},
			}};
			const matrix3 roll_axis{{
				{1.0f, 0.0f, 0.0f},
				{0.0f, std::cos(roll), std::sin(roll)},
				{0.0f, -std::sin(roll), std::cos(roll)},
			}};
			return multiply_matrix_test(roll_axis,
				multiply_matrix_test(pitch_axis, yaw_axis));
		};
		const auto matrix_approximately_equal = [&](const matrix3& left,
			const matrix3& right)
		{
			for (std::size_t row{}; row < 3; ++row)
			{
				for (std::size_t column{}; column < 3; ++column)
				{
					if (!approximately_equal_test(left[row][column],
						right[row][column])) return false;
				}
			}
			return true;
		};
		const auto apply_test_camera = [&](const matrix3& input_axis,
			const vector3& input_origin, matrix3& result_axis,
			vector3& result_origin)
		{
			float raw_origin[3]{input_origin[0], input_origin[1], input_origin[2]};
			float raw_axis[3][3]{};
			for (std::size_t row{}; row < 3; ++row)
			{
				for (std::size_t column{}; column < 3; ++column)
				{
					raw_axis[row][column] = input_axis[row][column];
				}
			}
			if (!apply_camera(raw_origin, raw_axis)) return false;
			for (std::size_t row{}; row < 3; ++row)
			{
				result_origin[row] = raw_origin[row];
				for (std::size_t column{}; column < 3; ++column)
				{
					result_axis[row][column] = raw_axis[row][column];
				}
			}
			return true;
		};

		reset();
		configure_target(true);
		set_world_scale(10.0f);
		set_enabled(true);
		publish_tracking_pose({{0.0f, 1.6f, 0.0f}, identity});
		auto state = get_status();
		vr::tests::require(state.pose_available && state.recenter_count == 1 &&
			state.pose_publications == 1,
			"first valid HMD pose did not establish the neutral tracking origin");

		publish_tracking_pose({{0.1f, 1.7f, -0.2f}, identity});
		state = get_status();
		vr::tests::require(approximately_equal_test(state.local_position_units[0], 2.0f) &&
			approximately_equal_test(state.local_position_units[1], -1.0f) &&
			approximately_equal_test(state.local_position_units[2], 1.0f),
			"tracking-space translation was not converted to H2 forward/left/up units");
		float origin[3]{100.0f, 200.0f, 300.0f};
		float axis[3][3]{
			{1.0f, 0.0f, 0.0f},
			{0.0f, 1.0f, 0.0f},
			{0.0f, 0.0f, 1.0f},
		};
		vr::tests::require(apply_camera(origin, axis) && approximately_equal_test(origin[0], 102.0f) &&
			approximately_equal_test(origin[1], 199.0f) && approximately_equal_test(origin[2], 301.0f),
			"room-scale translation was not applied in camera-local coordinates");

		constexpr auto roll_radians = 20.0f * 0.017453292519943295f;
		const matrix3 tracking_roll_20{{
			{std::cos(roll_radians), -std::sin(roll_radians), 0.0f},
			{std::sin(roll_radians), std::cos(roll_radians), 0.0f},
			{0.0f, 0.0f, 1.0f},
		}};
		publish_tracking_pose({{0.0f, 1.6f, 0.0f}, tracking_roll_20});
		constexpr auto yaw_radians = 35.0f * 0.017453292519943295f;
		float rolled_origin[3]{10.0f, 20.0f, 30.0f};
		float yawed_axis[3][3]{
			{std::cos(yaw_radians), std::sin(yaw_radians), 0.0f},
			{-std::sin(yaw_radians), std::cos(yaw_radians), 0.0f},
			{0.0f, 0.0f, 1.0f},
		};
		vr::tests::require(apply_camera(rolled_origin, yawed_axis),
			"valid HMD roll could not be composed with a yawed H2 camera");
		state = get_status();
		vr::tests::require(std::abs(state.local_roll_degrees + 20.0f) < 0.01f &&
			std::abs(state.input_horizon_roll_degrees) < 0.01f &&
			std::abs(state.output_horizon_roll_degrees + 20.0f) < 0.01f &&
			state.max_abs_local_roll_degrees >= 19.99f &&
			state.max_abs_output_horizon_roll_degrees >= 19.99f &&
			state.min_local_roll_degrees <= -19.99f &&
			state.min_output_horizon_roll_degrees <= -19.99f &&
			state.output_basis_error < 0.0001f &&
			state.composition_failure_count == 0,
			"roll diagnostics did not separate HMD-relative, base-camera and composed roll");

		const matrix3 yaw_right_90{{
			{0.0f, 0.0f, 1.0f},
			{0.0f, 1.0f, 0.0f},
			{-1.0f, 0.0f, 0.0f},
		}};
		publish_tracking_pose({{0.0f, 1.6f, 0.0f}, yaw_right_90});
		state = get_status();
		vr::tests::require(approximately_equal_test(state.local_orientation[0][0], 0.0f) &&
			approximately_equal_test(state.local_orientation[0][1], 1.0f) &&
			approximately_equal_test(state.local_orientation[1][0], -1.0f) &&
			approximately_equal_test(state.local_orientation[2][2], 1.0f),
			"HMD yaw was not converted to the H2 forward/left/up camera basis");

		request_recenter();
		vr::tests::require(!get_status().pose_available,
			"recenter request left a stale pose applicable to the camera");
		publish_tracking_pose({{0.0f, 1.6f, 0.0f}, yaw_right_90});
		state = get_status();
		vr::tests::require(state.recenter_count == 2 && approximately_equal_test(state.local_orientation[0][0], 1.0f) &&
			approximately_equal_test(state.local_orientation[1][1], 1.0f) &&
			approximately_equal_test(state.local_orientation[2][2], 1.0f),
			"recenter did not make the current HMD orientation neutral");
		publish_tracking_pose({{-0.2f, 1.6f, 0.0f}, yaw_right_90});
		state = get_status();
		vr::tests::require(approximately_equal_test(state.local_position_units[0], 2.0f) &&
			approximately_equal_test(state.local_position_units[1], 0.0f),
			"translation was not resolved relative to the recentered HMD orientation");

		auto invalid = tracking_pose{{NAN, 0.0f, 0.0f}, identity};
		publish_tracking_pose(invalid);
		state = get_status();
		vr::tests::require(!state.pose_available && state.invalid_pose_count == 1,
			"invalid HMD pose was not rejected");
		set_enabled(false);
		vr::tests::require(!apply_camera(origin, axis), "disabled head tracking still mutated the camera");

		// Recenter owns translation and yaw only. A tilted first/recenter pose must
		// retain its gravity-relative pitch and roll instead of becoming identity.
		reset();
		configure_target(true);
		set_world_scale(10.0f);
		set_enabled(true);
		constexpr auto reference_yaw_radians = 35.0f * 0.017453292519943295f;
		constexpr auto reference_pitch_radians = 15.0f * 0.017453292519943295f;
		constexpr auto reference_roll_radians = 20.0f * 0.017453292519943295f;
		const matrix3 reference_yaw{{
			{std::cos(reference_yaw_radians), 0.0f, std::sin(reference_yaw_radians)},
			{0.0f, 1.0f, 0.0f},
			{-std::sin(reference_yaw_radians), 0.0f, std::cos(reference_yaw_radians)},
		}};
		const matrix3 reference_pitch{{
			{1.0f, 0.0f, 0.0f},
			{0.0f, std::cos(reference_pitch_radians), -std::sin(reference_pitch_radians)},
			{0.0f, std::sin(reference_pitch_radians), std::cos(reference_pitch_radians)},
		}};
		const matrix3 reference_roll{{
			{std::cos(reference_roll_radians), -std::sin(reference_roll_radians), 0.0f},
			{std::sin(reference_roll_radians), std::cos(reference_roll_radians), 0.0f},
			{0.0f, 0.0f, 1.0f},
		}};
		const auto tilted_reference = multiply_matrix_test(reference_yaw,
			multiply_matrix_test(reference_pitch, reference_roll));
		constexpr vector3 reference_position{1.0f, 1.6f, 2.0f};
		publish_tracking_pose({reference_position, tilted_reference});
		state = get_status();
		vr::tests::require(state.pose_available && state.recenter_count == 1 &&
			approximately_equal_test(state.local_orientation[0][2],
				std::sin(reference_pitch_radians)) &&
			std::abs(state.local_roll_degrees + 20.0f) < 0.01f &&
			std::abs(state.reference_absolute_pitch_degrees - 15.0f) < 0.01f &&
			std::abs(state.reference_absolute_roll_degrees + 20.0f) < 0.01f &&
			std::abs(state.current_absolute_pitch_degrees - 15.0f) < 0.01f &&
			std::abs(state.current_absolute_roll_degrees + 20.0f) < 0.01f,
			"first tilted HMD pose lost physical pitch/roll while establishing yaw-only recenter");
		float tilted_origin[3]{};
		float tilted_axis[3][3]{
			{1.0f, 0.0f, 0.0f},
			{0.0f, 1.0f, 0.0f},
			{0.0f, 0.0f, 1.0f},
		};
		vr::tests::require(apply_camera(tilted_origin, tilted_axis) &&
			approximately_equal_test(tilted_axis[0][2],
				std::sin(reference_pitch_radians)),
			"first yaw-only recenter pose was not immediately applicable to the H2 camera");

		request_recenter();
		publish_tracking_pose({reference_position, tilted_reference});
		state = get_status();
		vr::tests::require(state.recenter_count == 2 &&
			approximately_equal_test(state.local_orientation[0][2],
				std::sin(reference_pitch_radians)) &&
			std::abs(state.local_roll_degrees + 20.0f) < 0.01f,
			"explicit recenter absorbed physical HMD pitch/roll");

		auto translated_position = reference_position;
		translated_position[0] += 0.1f;
		publish_tracking_pose({translated_position, tilted_reference});
		state = get_status();
		vr::tests::require(std::abs(state.local_position_units[0] +
			std::sin(reference_yaw_radians)) < 0.0001f &&
			std::abs(state.local_position_units[1] +
				std::cos(reference_yaw_radians)) < 0.0001f &&
			std::abs(state.local_position_units[2]) < 0.0001f,
			"recentered translation was contaminated by reference pitch/roll");

		// The H2 base camera contributes world yaw only. Mouse pitch and any
		// non-tracking roll must not rotate the VR world or room-scale motion.
		reset();
		configure_target(true);
		set_world_scale(10.0f);
		set_enabled(true);
		constexpr vector3 yaw_only_reference_position{0.0f, 1.6f, 0.0f};
		publish_tracking_pose({yaw_only_reference_position, identity});
		publish_tracking_pose({{0.1f, 1.6f, -0.2f}, identity});
		const auto h2_pitch_up = h2_camera_axis(35.0f, 60.0f, 0.0f);
		const auto h2_pitch_down = h2_camera_axis(35.0f, -45.0f, 0.0f);
		const auto expected_base_yaw = h2_camera_axis(35.0f, 0.0f, 0.0f);
		constexpr vector3 shared_origin{100.0f, 200.0f, 300.0f};
		matrix3 pitch_up_output{};
		matrix3 pitch_down_output{};
		vector3 pitch_up_origin{};
		vector3 pitch_down_origin{};
		vr::tests::require(apply_test_camera(h2_pitch_up, shared_origin,
			pitch_up_output, pitch_up_origin) &&
			apply_test_camera(h2_pitch_down, shared_origin,
				pitch_down_output, pitch_down_origin) &&
			matrix_approximately_equal(pitch_up_output, expected_base_yaw) &&
			matrix_approximately_equal(pitch_down_output, expected_base_yaw) &&
			matrix_approximately_equal(pitch_up_output, pitch_down_output) &&
			std::equal(pitch_up_origin.begin(), pitch_up_origin.end(),
				pitch_down_origin.begin(), approximately_equal_test),
			"H2 pitch leaked into the VR camera orientation or room-scale translation");
		state = get_status();
		vr::tests::require(std::abs(state.input_pitch_degrees + 45.0f) < 0.01f &&
			std::abs(state.base_yaw_degrees - 35.0f) < 0.01f &&
			std::abs(state.output_pitch_degrees) < 0.01f &&
			state.yaw_from_left_count == 0 &&
			state.yaw_extraction_failure_count == 0,
			"yaw-only base camera diagnostics did not expose the suppressed H2 pitch");

		// Fixed HMD pitch/roll must produce the same view for every H2 pitch and
		// remain visible in the composed output diagnostics.
		reset();
		configure_target(true);
		set_enabled(true);
		publish_tracking_pose({yaw_only_reference_position, identity});
		const auto hmd_pitch_roll = multiply_matrix_test(reference_pitch,
			reference_roll);
		publish_tracking_pose({yaw_only_reference_position, hmd_pitch_roll});
		matrix3 tracked_pitch_up_output{};
		matrix3 tracked_pitch_down_output{};
		vector3 tracked_pitch_up_origin{};
		vector3 tracked_pitch_down_origin{};
		vr::tests::require(apply_test_camera(h2_pitch_up, {},
			tracked_pitch_up_output, tracked_pitch_up_origin) &&
			apply_test_camera(h2_pitch_down, {},
				tracked_pitch_down_output, tracked_pitch_down_origin) &&
			matrix_approximately_equal(tracked_pitch_up_output,
				tracked_pitch_down_output),
			"suppressing H2 pitch also changed the physical HMD orientation");
		state = get_status();
		vr::tests::require(std::abs(state.input_pitch_degrees + 45.0f) < 0.01f &&
			std::abs(state.base_yaw_degrees - 35.0f) < 0.01f &&
			std::abs(state.output_pitch_degrees - 15.0f) < 0.01f &&
			std::abs(state.output_horizon_roll_degrees + 20.0f) < 0.01f,
			"yaw-only base camera did not preserve HMD pitch/roll in the output");

		// H2 roll is not a planar input axis and must never become VR horizon roll.
		reset();
		configure_target(true);
		set_enabled(true);
		publish_tracking_pose({yaw_only_reference_position, identity});
		const auto h2_rolled = h2_camera_axis(35.0f, 0.0f, 20.0f);
		matrix3 stripped_roll_output{};
		vector3 stripped_roll_origin{};
		vr::tests::require(apply_test_camera(h2_rolled, {},
			stripped_roll_output, stripped_roll_origin) &&
			matrix_approximately_equal(stripped_roll_output, expected_base_yaw),
			"H2 roll leaked into the VR horizon");
		state = get_status();
		vr::tests::require(std::abs(state.input_horizon_roll_degrees - 20.0f) < 0.01f &&
			std::abs(state.output_horizon_roll_degrees) < 0.01f,
			"roll diagnostics did not distinguish stripped H2 roll from HMD roll");

		// Exact vertical pitch has no forward XY projection. The orthogonal H2
		// left axis must deterministically preserve yaw through that singularity.
		const auto h2_vertical = h2_camera_axis(35.0f, 90.0f, 0.0f);
		matrix3 vertical_output{};
		vector3 vertical_origin{};
		vr::tests::require(apply_test_camera(h2_vertical, {},
			vertical_output, vertical_origin) &&
			matrix_approximately_equal(vertical_output, expected_base_yaw),
			"vertical H2 pitch lost its yaw-only camera base");
		state = get_status();
		vr::tests::require(state.yaw_from_left_count == 1 &&
			state.yaw_extraction_failure_count == 0 &&
			std::abs(state.base_yaw_degrees - 35.0f) < 0.01f,
			"vertical-pitch yaw extraction was not diagnosed exactly once");
	}

	void expect_optical_projection_remap()
	{
		const auto approximately_equal_test = [](const float left, const float right)
		{
			return std::abs(left - right) < 0.0001f;
		};
		vr::scene_uv_mapping mapping;
		vr::tests::require(!vr::calculate_projection_uv_mapping({}, mapping),
			"empty projection unexpectedly produced an optical remap");
		const vr::engine_stereo_bridge::eye_projection projection{
			-0.8f, 1.2f, -1.1f, 0.9f};
		vr::tests::require(vr::calculate_projection_uv_mapping(projection, mapping),
			"valid asymmetric projection did not produce an optical remap");
		vr::tests::require(approximately_equal_test(mapping.scale_u, 2.0f / 2.4f) &&
			approximately_equal_test(mapping.offset_u, 0.4f / 2.4f) &&
			approximately_equal_test(mapping.scale_v, 2.0f / 2.2f) &&
			approximately_equal_test(mapping.offset_v, 0.2f / 2.2f) &&
			approximately_equal_test(projection.optical_aspect(), 1.0f),
			"asymmetric destination rays were not mapped into the symmetric source frustum");
	}

	void expect_bridge_gate_and_views()
	{
		using namespace vr::engine_stereo_bridge;
		configure_bridge(false);
		publish_valid_bridge_views();
		vr::tests::require(!is_active() && !get_status().enabled,
			"target mismatch unexpectedly activated engine stereo bridge");

		configure_bridge();
		vr::tests::require(!is_active(), "bridge activated before OpenXR views were published");
		publish_valid_bridge_views();
		publish_views({NAN, 0.0f, 0.0f}, {0.032f, 0.0f, 0.0f},
			{{{1.0f, 1.0f, -1.0f, 1.0f}, {-1.0f, 1.0f, -1.0f, 1.0f}}});
		vr::tests::require(!is_active(), "invalid OpenXR positions activated bridge");
		publish_valid_bridge_views();
		const auto state = get_status();
		vr::tests::require(is_active() && state.views_available && state.ipd_meters == 0.064f &&
			state.world_scale == 100.0f && state.half_eye_offset_units == 3.2f &&
			state.eyes[0].symmetric_tan_half_x() == 1.1f &&
			state.eyes[1].symmetric_tan_half_y() == 1.4f,
			"valid OpenXR positions did not configure bridge scale and eye offset");
	}

	void expect_explicit_eye_render_config_and_counters()
	{
		using namespace vr::engine_stereo_bridge;
		configure_bridge();
		publish_valid_bridge_views();
		render_config config;
		vr::tests::require(get_render_config_for_eye(0, config) &&
			std::abs(config.half_eye_offset_units - 3.2f) < 0.0001f &&
			config.eyes[0].symmetric_tan_half_x() == 1.1f && !config.swap_eyes &&
			config.output_eye == 0 && config.view_eye == 0,
			"bridge did not publish the explicitly selected left-eye configuration");
		record_stereo_eye(config, 2560, 1261, 2560, 1261);
		set_swap_eyes(true);
		vr::tests::require(get_render_config_for_eye(1, config) && config.swap_eyes &&
			config.output_eye == 1 && config.view_eye == 0,
			"swap-eyes was not applied to an explicitly selected right-eye view");
		record_stereo_eye(config, 2560, 1261, 2560, 1261);
		const auto tag = consume_capture_tag();
		record_invalid_viewport();
		record_restore_conflict();
		const auto state = get_status();
		vr::tests::require(state.stereo_frames == 1 && state.scene_calls == 2 &&
			state.invalid_viewports == 1 && state.restore_conflicts == 1 &&
			state.last_full_width == 2560 && state.last_left_width == 2560 &&
			tag.valid && tag.eye_index == 1,
			"explicit-eye stereo renderer diagnostics were not recorded");
	}

	void expect_same_frame_view_family_contract()
	{
		using namespace vr::engine_stereo_bridge;
		configure_bridge();
		vr::tests::require(publish_view_family(42,
			{-0.032f, 0.0f, 0.0f}, {0.032f, 0.0f, 0.0f},
			{{{-1.1f, 1.0f, -1.2f, 1.1f}, {-1.3f, 1.2f, -1.4f, 1.3f}}}),
			"view family publication was rejected");

		view_family family;
		vr::tests::require(get_view_family(family) && family.frame_id == 42 &&
			family.publication == 1,
			"published view family was not an immutable frame-level snapshot");

		render_config left;
		render_config right;
		std::array<render_config, 2> pair{};
		vr::tests::require(get_render_config_for_eye(0, left) &&
			get_render_config_for_eye(1, right) && left.frame_id == right.frame_id &&
			left.pair_id == right.pair_id && left.output_eye == 0 && right.output_eye == 1,
			"same-frame eye configs did not share one view-family id");
		vr::tests::require(get_render_configs(pair) &&
			pair[0].frame_id == 42 && pair[1].frame_id == 42 &&
			pair[0].publication == pair[1].publication &&
			pair[0].pair_id == pair[1].pair_id &&
			pair[0].output_eye == 0 && pair[1].output_eye == 1,
			"atomic stereo config snapshot did not retain one immutable view family");
		record_stereo_eye(left, 2560, 1261, 1280, 1261);
		record_stereo_eye(right, 2560, 1261, 1280, 1261);
		const auto state = get_status();
		vr::tests::require(state.coherent_stereo_pairs == 1 && state.incoherent_stereo_pairs == 0,
			"same-frame eye submissions were classified as incoherent");
	}

	void expect_render_target_observation_contract()
	{
		using namespace vr::engine_stereo_bridge;
		configure_bridge();
		vr::tests::require(publish_view_family(77,
			{-0.032f, 0.0f, 0.0f}, {0.032f, 0.0f, 0.0f},
			{{{-1.1f, 1.0f, -1.2f, 1.1f}, {-1.3f, 1.2f, -1.4f, 1.3f}}}),
			"target observation view family publication was rejected");

		render_config left;
		render_config right;
		vr::tests::require(get_render_config_for_eye(0, left) &&
			get_render_config_for_eye(1, right),
			"target observation test could not acquire same-frame eyes");
		record_render_target_observation(left, target_observation_phase::before_scene,
			0x1000, 2048, 2048, 28, 1);
		record_render_target_observation(right, target_observation_phase::before_scene,
			0x2000, 2048, 2048, 28, 1);
		record_render_target_observation(left, target_observation_phase::after_scene,
			0x1000, 2048, 2048, 28, 1);
		record_render_target_observation(right, target_observation_phase::after_scene,
			0x2000, 2048, 2048, 28, 1);
		auto mismatched_frame = right;
		mismatched_frame.frame_id++;
		record_render_target_observation(mismatched_frame,
			target_observation_phase::after_scene, 0x3000, 2048, 2048, 28, 1);
		record_missing_render_target(right, target_observation_phase::after_scene);
		const auto state = get_status();
		vr::tests::require(state.target_observations == 5 && state.target_misses == 1 &&
			state.distinct_target_pairs == 2 && state.aliased_target_pairs == 0 &&
			state.native_target_candidate_frame == 77 &&
			!state.last_target.valid && state.last_target.output_eye == 1,
			"render-target observations did not preserve the same-frame native contract");
	}

	void expect_record_local_asymmetric_eye_slots()
	{
		using namespace vr::engine_stereo_bridge;
		configure_bridge();
		vr::tests::require(publish_view_family(91,
			{-0.0315f, 0.0f, 0.0f}, {0.0315f, 0.0f, 0.0f},
			{{{-1.376f, 0.839f, -1.428f, 0.966f},
				{-0.839f, 1.376f, -1.428f, 0.966f}}}),
			"asymmetric eye-slot test could not publish a view family");
		std::array<render_config, 2> configs{};
		vr::tests::require(get_render_configs(configs),
			"asymmetric eye-slot test could not acquire one stereo config snapshot");

		alignas(16) std::array<std::uint8_t,
			vr::engine_stereo_view::h2_view_slot_size> natural{};
		auto write_float = [&natural](const std::size_t offset, const float value)
		{
			std::memcpy(natural.data() + offset, &value, sizeof(value));
		};
		write_float(0x100, 10.0f);
		write_float(0x104, 20.0f);
		write_float(0x108, 30.0f);
		// H2 view axes: forward, left, up.
		write_float(0x10C, 1.0f);
		write_float(0x11C, 1.0f);
		write_float(0x12C, 1.0f);
		write_float(0x148, 4.0f);

		vr::engine_stereo_view::slot_pair pair{};
		vr::tests::require(vr::engine_stereo_view::derive(natural.data(), configs, pair) &&
			vr::engine_stereo_view::validate_derived(pair),
			"record-local asymmetric eye-slot derivation failed");
		vr::tests::require(std::memcmp(pair.natural_camera.data(),
			natural.data() + vr::engine_stereo_view::h2_view_origin_offset,
			sizeof(pair.natural_camera)) == 0,
			"camera witness did not retain the exact pre-eye origin and axis");
		vr::tests::require(!vr::engine_stereo_view::validate_finalized(pair),
			"derived slots were accepted before H2 finalized eye-local matrices");
		auto read_float = [](const auto& bytes, const std::size_t offset)
		{
			float value{};
			std::memcpy(&value, bytes.data() + offset, sizeof(value));
			return value;
		};
		const auto& left = pair.eyes[0].bytes;
		const auto& right = pair.eyes[1].bytes;
		vr::tests::require(std::abs(read_float(left, 0x104) - 23.15f) < 0.001f &&
			std::abs(read_float(right, 0x104) - 16.85f) < 0.001f,
			"eye origins were not separated along H2's camera-left axis");
		vr::tests::require(read_float(left, 0x40 + 8 * 4) > 0.2f &&
			read_float(right, 0x40 + 8 * 4) < -0.2f &&
			read_float(left, 0x40 + 9 * 4) > 0.19f &&
			read_float(right, 0x40 + 9 * 4) > 0.19f &&
			read_float(left, 0x40) > 0.0f && read_float(right, 0x40) > 0.0f,
			"mirrored asymmetric projection centers were not preserved");
		const auto expect_edge = [&](const auto& bytes, const float tangent,
			const std::size_t scale_index, const std::size_t center_index,
			const float expected)
		{
			const auto ndc = tangent * read_float(bytes, 0x40 + scale_index * 4) +
				read_float(bytes, 0x40 + center_index * 4);
			vr::tests::require(std::abs(ndc - expected) < 0.001f,
				"an OpenVR frustum edge did not map to the matching H2 NDC edge");
		};
		expect_edge(left, -1.376f, 0, 8, -1.0f);
		expect_edge(left, 0.839f, 0, 8, 1.0f);
		expect_edge(left, -1.428f, 5, 9, -1.0f);
		expect_edge(left, 0.966f, 5, 9, 1.0f);
		vr::tests::require(pair.eyes[0].pair_id == 91 && pair.eyes[1].pair_id == 91 &&
			pair.eyes[0].publication == pair.eyes[1].publication,
			"derived eye slots lost their immutable view-family identity");

		vr::engine_stereo_view::culling_union_slot culling_union{};
		vr::tests::require(vr::engine_stereo_view::derive_culling_union(
			natural.data(), configs, culling_union),
			"conservative frontend culling union could not be derived");
		vr::tests::require(std::abs(culling_union.near_distance_units - 1.0f) < 0.0001f &&
			std::abs(culling_union.horizontal_origin_expansion - 3.15f) < 0.0001f &&
			std::abs(culling_union.tan_left + 4.526f) < 0.0001f &&
			std::abs(culling_union.tan_right - 4.626f) < 0.0001f &&
			std::abs(culling_union.tan_down + 1.428f) < 0.0001f &&
			std::abs(culling_union.tan_up - 0.966f) < 0.0001f &&
			std::abs(read_float(culling_union.bytes, 0x100) - 10.0f) < 0.0001f &&
			std::abs(read_float(culling_union.bytes, 0x104) - 20.0f) < 0.0001f &&
			std::abs(read_float(culling_union.bytes, 0x108) - 30.0f) < 0.0001f,
			"culling union did not contain translated near-eye and asymmetric optical bounds");
		const auto expect_union_edge = [&](const float tangent,
			const std::size_t scale_index, const std::size_t center_index,
			const float expected)
		{
			const auto ndc = tangent * read_float(culling_union.bytes,
				0x40 + scale_index * 4) + read_float(culling_union.bytes,
				0x40 + center_index * 4);
			vr::tests::require(std::abs(ndc - expected) < 0.001f,
				"a conservative culling-union edge did not map to the H2 NDC boundary");
		};
		expect_union_edge(culling_union.tan_left, 0, 8, -1.0f);
		expect_union_edge(culling_union.tan_right, 0, 8, 1.0f);
		expect_union_edge(culling_union.tan_down, 5, 9, -1.0f);
		expect_union_edge(culling_union.tan_up, 5, 9, 1.0f);
		vr::tests::require(read_float(culling_union.bytes, 0x140) >= culling_union.tan_right &&
			read_float(culling_union.bytes, 0x144) >= -culling_union.tan_down &&
			read_float(right, 0x140) >= 1.376f && read_float(right, 0x144) >= 1.428f,
			"legacy symmetric frusta enclose the longer optical side");
		{
			using namespace vr::engine_stereo_tessellation;
			shared_view upload{culling_union.bytes, left};
			// Identity view fixture: copy projection into VP to exercise the
			// actual main-view constant ranges used by the native GPU pass.
			std::memcpy(upload.rendered.data() + 0x80, upload.rendered.data() + 0x40, 64);
			std::memcpy(upload.culling.data() + 0x80, upload.culling.data() + 0x40, 64);
			std::array<std::uint8_t, constants_size> constants{};
			constants.fill(0x5a);
			std::memcpy(constants.data() + 0x20, upload.rendered.data() + 0x80, 64);
			std::memcpy(constants.data() + 0x8e0, upload.rendered.data() + 0x40, 64);
			std::memcpy(constants.data() + 0x11a0, upload.rendered.data() + 0x100, 12);
			const auto original = constants;
			vr::tests::require(replace_main_view(constants.data(), constants.size(), upload),
				"shared GPU tessellation replaces left-eye culling with frontend union");
			const auto clip_x = [&](const auto& data) { return 1.2f * read_float(data, 0x20) + read_float(data, 0x40); };
			vr::tests::require(clip_x(original) > 1 && std::abs(clip_x(constants)) < 1,
				"terrain visible only at right-eye outer edge survives shared tessellation");
			for (std::size_t i = 0; i < constants.size(); ++i)
			{
				const bool main = (i >= 0x20 && i < 0x60) || (i >= 0x8e0 && i < 0x920) ||
					(i >= 0x11a0 && i < 0x11ac);
				vr::tests::require(main || constants[i] == original[i],
					"shadow views, tessellation parameters and origin W stay intact");
			}
			constants = original; constants[0x20] ^= 1;
			const auto foreign = constants;
			vr::tests::require(!replace_main_view(constants.data(), constants.size(), upload) && constants == foreign,
				"foreign upload is rejected before any constant is changed");
			vr::tests::require(!replace_main_view(constants.data(), constants.size() - 1, upload),
				"truncated native constant buffer is rejected");
		}

		const std::array<std::uint8_t, vr::engine_stereo_view::h2_view_slot_size>*
			near_slots[]{&left, &right, &culling_union.bytes};
		for (const auto* bytes : near_slots)
		{
			vr::tests::require(read_float(*bytes, 0x148) == 1.0f &&
				read_float(*bytes, 0x78) == 1.0f,
				"VR near distance disagrees with reverse-Z projection or visibility");
		}
		// A point just outside the right optical edge must survive both shared
		// visibility paths, including at the new near plane and far away.
		alignas(16) std::array<std::uint8_t, 0xC0> fx_camera{};
		std::memcpy(fx_camera.data(), natural.data() + 0x100, 12);
		std::memcpy(fx_camera.data() + 0x70, natural.data() + 0x10C, 36);
		const std::uint32_t fx_valid = 1, fx_plane_count = 5;
		std::memcpy(fx_camera.data() + 0x0C, &fx_valid, 4);
		std::memcpy(fx_camera.data() + 0x94, &fx_plane_count, 4);
		vr::tests::require(vr::engine_stereo_view::apply_fx_culling_union(
			fx_camera.data(), configs), "FX culling union derivation failed");
		for (const float distance : {1.0f, 4.0f, 10000.0f})
		{
			const auto screen_right = 3.15f + distance * (1.376f + 0.09f);
			vr::tests::require(screen_right / distance < culling_union.tan_right,
				"right-edge guard point was rejected by frontend visibility");
			const auto plane_distance = read_float(fx_camera, 0x20) * (10.0f + distance) +
				read_float(fx_camera, 0x24) * (20.0f - screen_right) +
				read_float(fx_camera, 0x28) * 30.0f - read_float(fx_camera, 0x2C);
			vr::tests::require(plane_distance >= 0.0f,
				"right-edge guard point was rejected by FX visibility");
		}
		write_float(0x148, 0.5f);
		vr::engine_stereo_view::slot_pair closer_pair{};
		vr::tests::require(vr::engine_stereo_view::derive(natural.data(), configs, closer_pair) &&
			read_float(closer_pair.eyes[0].bytes, 0x78) == 0.5f &&
			read_float(closer_pair.eyes[1].bytes, 0x148) == 0.5f,
			"VR moved an already closer native near plane outward");
		write_float(0x148,.01f);
		vr::tests::require(vr::engine_stereo_view::derive(natural.data(),configs,closer_pair) &&
			vr::engine_stereo_view::derive_culling_union(natural.data(),configs,culling_union) &&
			read_float(closer_pair.eyes[0].bytes,0x148)==culling_union.near_distance_units && culling_union.horizontal_origin_expansion<=8,
			"scripted 0.01 near plane cannot reject stereo publication or use inconsistent render/culling clipping");
		write_float(0x148,100.f);write_float(0x144,.04f);
		vr::tests::require(vr::engine_stereo_view::derive_culling_union(natural.data(),configs,culling_union,16.f/9) &&
			culling_union.near_distance_units==100 && culling_union.tan_right<.25f && culling_union.tan_left>-.15f,
			"fully covered story scope collects only the native optical scene rather than invisible wide-angle static models");
		for (const float invalid : {0.0f, -1.0f, std::numeric_limits<float>::infinity(),
			std::numeric_limits<float>::quiet_NaN()})
		{
			write_float(0x148, invalid);
			vr::tests::require(!vr::engine_stereo_view::derive(natural.data(), configs, closer_pair) &&
				!vr::engine_stereo_view::derive_culling_union(natural.data(), configs, culling_union),
				"invalid native near plane was hidden by the VR near cap");
		}
		write_float(0x148, 4.0f);

		auto incoherent = configs;
		++incoherent[1].pair_id;
		vr::tests::require(!vr::engine_stereo_view::derive(natural.data(), incoherent, pair),
			"eye-slot derivation accepted configs from different stereo families");
	}

	vr::engine_stereo_view::slot_pair make_finalized_binding_pair(
		const std::array<float, 3>& source_origin = {},
		const std::array<vr::engine_stereo_bridge::eye_projection, 2>& projections =
			{{{-1.376f, 0.839f, -0.966f, 1.428f},
				{-0.839f, 1.376f, -0.966f, 1.428f}}})
	{
		using namespace vr::engine_stereo_bridge;
		configure_bridge();
		vr::tests::require(publish_view_family(191,
			{-0.0315f, 0.0f, 0.0f}, {0.0315f, 0.0f, 0.0f},
			projections),
			"binding test could not publish a view family");
		std::array<render_config, 2> configs{};
		vr::tests::require(get_render_configs(configs),
			"binding test could not acquire one stereo config snapshot");

		alignas(16) std::array<std::uint8_t,
			vr::engine_stereo_view::h2_view_slot_size> natural{};
		auto write_float = [](auto& bytes, const std::size_t offset, const float value)
		{
			std::memcpy(bytes.data() + offset, &value, sizeof(value));
		};
		write_float(natural, 0x10C, 1.0f);
		std::memcpy(natural.data() + vr::engine_stereo_view::h2_view_origin_offset,
			source_origin.data(), sizeof(source_origin));
		write_float(natural, 0x11C, 1.0f);
		write_float(natural, 0x12C, 1.0f);
		write_float(natural, 0x148, 4.0f);

		vr::engine_stereo_view::slot_pair pair{};
		vr::tests::require(vr::engine_stereo_view::derive(natural.data(), configs, pair),
			"binding test could not derive eye slots");
		for (std::size_t eye{}; eye < pair.eyes.size(); ++eye)
		{
			auto& bytes = pair.eyes[eye].bytes;
			std::memcpy(bytes.data() + 0x80, bytes.data() + 0x40, 16 * sizeof(float));
			for (std::size_t diagonal{}; diagonal < 4; ++diagonal)
			{
				write_float(bytes, 0xC0 + diagonal * 5 * sizeof(float), 1.0f);
			}
			write_float(bytes, 0xC0 + 12 * sizeof(float),
				eye == 0 ? -0.0315f : 0.0315f);
		}
		vr::tests::require(vr::engine_stereo_view::validate_finalized(pair),
			"binding test pair did not satisfy the finalized slot contract");
		return pair;
	}

	void expect_symmetric_eye_finalization()
	{
		// Issue #34: VDXR can report identical symmetric FOVs. H2's combined
		// matrix is camera-relative; its inverse also contains the eye origin.
		const auto pair = make_finalized_binding_pair({10.0f, 20.0f, 30.0f},
			{{{-1.2795f, 1.2795f, -1.2795f, 1.2795f},
				{-1.2795f, 1.2795f, -1.2795f, 1.2795f}}});
		constexpr auto matrix_size = 16 * sizeof(float);
		vr::tests::require(std::memcmp(pair.eyes[0].bytes.data() + 0x80,
			pair.eyes[1].bytes.data() + 0x80, matrix_size) == 0,
			"symmetric-FOV fixture did not retain identical camera-relative matrices");
		vr::engine_stereo_view::scene_record_pair records{};
		alignas(16) std::array<std::uint8_t,
			vr::engine_stereo_view::h2_scene_record_size> natural{};
		std::memcpy(natural.data() + vr::engine_stereo_view::h2_view_origin_offset,
			pair.natural_camera.data(), sizeof(pair.natural_camera));
		vr::tests::require(vr::engine_stereo_view::clone_scene_records(
			natural.data(), pair, records),
			"symmetric-FOV stereo views were rejected by backend scene cloning");

		auto invalid = pair;
		std::memcpy(invalid.eyes[1].bytes.data() + 0xC0,
			invalid.eyes[0].bytes.data() + 0xC0, matrix_size);
		vr::tests::require(!vr::engine_stereo_view::validate_finalized(invalid) &&
			!vr::engine_stereo_view::clone_scene_records(natural.data(), invalid, records),
			"copied center/other-eye inverse matrices passed finalization");

		std::array<vr::engine_stereo_bridge::render_config, 2> configs{};
		vr::tests::require(vr::engine_stereo_bridge::get_render_configs(configs) &&
			vr::engine_stereo_view::derive(pair.eyes[0].bytes.data(), configs, invalid) &&
			!vr::engine_stereo_view::validate_finalized(invalid),
			"derived slots retaining nonzero source matrices passed without finalization");

		for (const auto offset : {0x80u, 0xC0u})
		{
			for (const auto non_finite : {std::numeric_limits<float>::quiet_NaN(),
				std::numeric_limits<float>::infinity()})
			{
				invalid = pair;
				std::memcpy(invalid.eyes[1].bytes.data() + offset, &non_finite, sizeof(float));
				vr::tests::require(!vr::engine_stereo_view::validate_finalized(invalid),
					"symmetric-FOV finalization accepted a non-finite matrix");
			}
			invalid = pair;
			std::memset(invalid.eyes[1].bytes.data() + offset, 0, matrix_size);
			vr::tests::require(!vr::engine_stereo_view::validate_finalized(invalid),
				"symmetric-FOV finalization accepted an empty matrix");
		}
		invalid = pair;
		++invalid.eyes[1].publication;
		vr::tests::require(!vr::engine_stereo_view::validate_finalized(invalid),
			"symmetric-FOV finalization accepted mixed publications");
	}

	void expect_isolated_backend_scene_record_clones()
	{
		const auto views = make_finalized_binding_pair();
		alignas(16) std::array<std::uint8_t,
			vr::engine_stereo_view::h2_scene_record_size> natural{};
		for (std::size_t index{}; index < natural.size(); ++index)
		{
			natural[index] = static_cast<std::uint8_t>((index * 37u + 11u) & 0xFFu);
		}
		const auto write_record_float = [](auto& record, const std::size_t offset,
			const float value)
		{
			std::memcpy(record.data() + offset, &value, sizeof(value));
		};
		const std::array<float, 3> natural_primary{0.0f, 0.0f, 0.0f};
		const std::array<float, 3> natural_rebase{120.0f, -80.0f, 12.0f};
		std::memcpy(natural.data() + vr::engine_stereo_view::h2_view_origin_offset,
			views.natural_camera.data(), sizeof(views.natural_camera));
		for (std::size_t component{}; component < 3; ++component)
		{
			write_record_float(natural,
				vr::engine_stereo_view::h2_view_origin_offset + component * sizeof(float),
				natural_primary[component]);
			write_record_float(natural,
				vr::engine_stereo_view::h2_relative_eye_offset_offset +
					component * sizeof(float), natural_rebase[component]);
		}
		vr::engine_stereo_view::scene_record_pair records{};
		vr::tests::require(vr::engine_stereo_view::clone_scene_records(
			natural.data(), views, records),
			"backend scene-record clone rejected one finalized stereo family");
		vr::tests::require(records.pair_id == views.eyes[0].pair_id &&
			records.publication == views.eyes[0].publication,
			"backend scene-record clone lost its immutable family identity");
		vr::tests::require(std::memcmp(records.left.data(),
			views.eyes[0].bytes.data(), vr::engine_stereo_view::h2_view_slot_size) == 0 &&
			std::memcmp(records.right.data(), views.eyes[1].bytes.data(),
				vr::engine_stereo_view::h2_view_slot_size) == 0,
			"backend scene-record clone did not install both finalized eye views");

		constexpr auto relative_eye_offset =
			vr::engine_stereo_view::h2_relative_eye_offset_offset;
		constexpr auto origin_size = 3 * sizeof(float);
		const auto read_record_float = [](const auto& record,
			const std::size_t offset)
		{
			float value{};
			std::memcpy(&value, record.data() + offset, sizeof(value));
			return value;
		};
		for (std::size_t eye{}; eye < 2; ++eye)
		{
			const auto& record = eye == 0 ? records.left : records.right;
			for (std::size_t component{}; component < 3; ++component)
			{
				const auto eye_primary = read_record_float(record,
					vr::engine_stereo_view::h2_view_origin_offset +
						component * sizeof(float));
				const auto relative_eye = read_record_float(record,
					relative_eye_offset + component * sizeof(float));
				const auto expected = natural_rebase[component] +
					(eye_primary - natural_primary[component]);
				vr::tests::require(std::abs(relative_eye - expected) < 0.0001f,
					"backend XModel eyeOffset used an absolute origin instead of a relative eye delta");
			}
		}
		const auto relative_dx = read_record_float(records.right, relative_eye_offset) -
			read_record_float(records.left, relative_eye_offset);
		const auto relative_dy = read_record_float(records.right,
			relative_eye_offset + 4) - read_record_float(records.left,
				relative_eye_offset + 4);
		const auto relative_dz = read_record_float(records.right,
			relative_eye_offset + 8) - read_record_float(records.left,
				relative_eye_offset + 8);
		const auto relative_ipd = std::sqrt(relative_dx * relative_dx +
			relative_dy * relative_dy + relative_dz * relative_dz);
		vr::tests::require(std::abs(relative_ipd - 6.3f) < 0.0001f,
			"relative backend eyeOffset did not retain the configured H2-space IPD");

		constexpr auto inverse_projection_offset =
			vr::engine_stereo_view::h2_inverse_scene_projection_constant_offset;
		for (const auto* record : {&records.left, &records.right})
		{
			for (std::size_t offset = vr::engine_stereo_view::h2_view_slot_size;
				offset < natural.size(); ++offset)
			{
				if ((offset >= relative_eye_offset && offset < relative_eye_offset + origin_size) ||
					(offset >= inverse_projection_offset && offset < inverse_projection_offset + 16)) continue;
				vr::tests::require((*record)[offset] == natural[offset],
					"backend clone changed bytes outside the proven eye-local ranges");
			}
			// Project and reconstruct the same world direction at several rolls.
			// The old inherited mono constant cannot invert either off-axis eye.
			for (const auto roll : {-0.7f, 0.0f, 0.7f})
			{
				const auto x = 0.3f * std::cos(roll) + 0.4f * std::sin(roll);
				const auto y = -0.3f * std::sin(roll) + 0.4f * std::cos(roll);
				const auto clip_x = x * read_record_float(*record, 0x40) +
					read_record_float(*record, 0x60);
				const auto clip_y = y * read_record_float(*record, 0x54) +
					read_record_float(*record, 0x64);
				const auto reconstructed_x = clip_x * read_record_float(*record, inverse_projection_offset) +
					read_record_float(*record, inverse_projection_offset + 4);
				const auto reconstructed_y = clip_y * read_record_float(*record, inverse_projection_offset + 8) +
					read_record_float(*record, inverse_projection_offset + 12);
				vr::tests::require(std::abs(reconstructed_x - x) < 0.00001f &&
					std::abs(reconstructed_y - y) < 0.00001f,
					"fog ray reconstruction did not invert the selected eye projection under roll");
			}
		}
		vr::tests::require(std::memcmp(records.left.data() +
			vr::engine_stereo_view::h2_frontend_rebase_view_offset + 0x40,
			natural.data() + vr::engine_stereo_view::h2_frontend_rebase_view_offset + 0x40,
			4 * sizeof(float) * 4) == 0 &&
			std::memcmp(records.right.data() +
				vr::engine_stereo_view::h2_frontend_rebase_view_offset + 0x40,
				natural.data() + vr::engine_stereo_view::h2_frontend_rebase_view_offset + 0x40,
				4 * sizeof(float) * 4) == 0,
			"frontend rebase projection was replaced by an eye projection");

		// Eye 0 runs H2's arena record, not records.left itself. Verify the exact
		// installation helper carries both proven eye-local ranges into that path
		// without copying the rest of the private clone.
		auto arena_record = natural;
		vr::tests::require(vr::engine_stereo_view::copy_eye_local_record_fields(
			arena_record.data(), records.left),
			"left eye-local fields could not be installed in the arena record");
		vr::tests::require(std::memcmp(arena_record.data() + inverse_projection_offset,
			records.left.data() + inverse_projection_offset, sizeof(float) * 4) == 0,
			"left arena record retained the mono fog reconstruction constant");
		// Exclude the separately asserted inverse projection from the old byte guard.
		std::memcpy(arena_record.data() + inverse_projection_offset,
			natural.data() + inverse_projection_offset, sizeof(float) * 4);
		vr::tests::require(std::memcmp(arena_record.data(), records.left.data(),
			vr::engine_stereo_view::h2_view_slot_size) == 0 &&
			std::memcmp(arena_record.data() + relative_eye_offset,
				records.left.data() + relative_eye_offset, origin_size) == 0 &&
			std::memcmp(arena_record.data() +
				vr::engine_stereo_view::h2_view_slot_size, natural.data() +
				vr::engine_stereo_view::h2_view_slot_size, relative_eye_offset -
				vr::engine_stereo_view::h2_view_slot_size) == 0 &&
			std::memcmp(arena_record.data() + relative_eye_offset + origin_size,
				natural.data() + relative_eye_offset + origin_size,
				natural.size() - relative_eye_offset - origin_size) == 0,
			"arena eye-field installation changed bytes outside its proven contracts");

		auto jittered_views = views;
		for (auto& eye : jittered_views.eyes)
		{
			write_record_float(eye.bytes, 0x60, read_record_float(eye.bytes, 0x60) + 0.001f);
			write_record_float(eye.bytes, 0x64, read_record_float(eye.bytes, 0x64) - 0.002f);
		}
		vr::engine_stereo_view::scene_record_pair jittered_records{};
		vr::tests::require(vr::engine_stereo_view::clone_scene_records(
			natural.data(), jittered_views, jittered_records),
			"fog reconstruction rejected finite finalized projection jitter");
		for (std::size_t eye{}; eye < 2; ++eye)
		{
			const auto& before = eye == 0 ? records.left : records.right;
			const auto& after = eye == 0 ? jittered_records.left : jittered_records.right;
			vr::tests::require(std::abs(read_record_float(after, inverse_projection_offset + 4) -
				read_record_float(before, inverse_projection_offset + 4) +
				0.001f / read_record_float(after, 0x40)) < 0.00001f &&
				std::abs(read_record_float(after, inverse_projection_offset + 12) -
				read_record_float(before, inverse_projection_offset + 12) -
				0.002f / read_record_float(after, 0x54)) < 0.00001f,
				"fog inverse projection dropped or double-applied finalized jitter");
		}
		write_record_float(jittered_views.eyes[1].bytes, 0x54, 0.0f);
		vr::tests::require(!vr::engine_stereo_view::clone_scene_records(
			natural.data(), jittered_views, jittered_records) && jittered_records.pair_id == 0,
			"singular fog projection published a partially prepared stereo pair");
	}

	void expect_scene_clone_rejects_mixed_camera_generations()
	{
		namespace view = vr::engine_stereo_view;
		// Source-center correction alone was insufficient: pair 1343 combined an
		// older eye publication with a record and descriptors from the next scene.
		const std::array<float, 3> center{-14479.73828125f, 24503.9453125f, -16218.791015625f};
		const std::array<float, 3> later_center{center[0] + 0.880859375f,
			center[1] - 0.27734375f, center[2] - 0.083984375f};
		auto views = make_finalized_binding_pair(center);
		std::array<std::uint8_t, view::h2_scene_record_size> natural{};
		std::memcpy(natural.data() + view::h2_view_origin_offset,
			views.natural_camera.data(), sizeof(views.natural_camera));
		std::memcpy(natural.data() + view::h2_relative_eye_offset_offset,
			center.data(), sizeof(center));
		for (std::size_t index = 2; index <= 12; ++index)
		{
			const auto offset = view::h2_draw_list_descriptor_base +
				index * view::h2_draw_list_descriptor_size;
			const std::uint32_t type = 13;
			std::memcpy(natural.data() + offset + view::h2_draw_list_active_type_offset,
				&type, sizeof(type));
			std::memcpy(natural.data() + offset + view::h2_draw_list_origin_offset,
				center.data(), sizeof(center));
		}
		view::scene_record_pair records{};
		std::memcpy(natural.data() + view::h2_view_origin_offset,
			later_center.data(), sizeof(later_center));
		const auto advanced = natural;
		vr::tests::require(!view::clone_scene_records(natural.data(), views, records) &&
			records.pair_id == 0 && natural == advanced,
			"stale camera publication was overlaid on a newer scene payload");
		std::memcpy(natural.data() + view::h2_view_origin_offset,
			views.natural_camera.data(), sizeof(views.natural_camera));
		const auto before = natural;
		vr::tests::require(view::clone_scene_records(natural.data(), views, records),
			"matching source camera was rejected");
		auto wrong_reference = records.right;
		view::camera_model_origin_update wrong_update{};
		view::camera_model_origin_census wrong_census{};
		vr::tests::require(view::rebase_camera_model_list_origins(wrong_reference.data(),
			later_center, wrong_update) && wrong_update.rewritten == 0 && wrong_update.foreign == 11 &&
			view::census_camera_model_list_origins(wrong_reference.data(), wrong_census) &&
			wrong_census.other == 11,
			"regression fixture did not reproduce the old center mismatch rejection");
		for (auto* record : {&records.left, &records.right})
		{
			vr::tests::require(std::memcmp(record->data() + view::h2_view_origin_offset,
				record->data() + view::h2_relative_eye_offset_offset, sizeof(center)) == 0,
				"body motion leaked from the later arena camera into XModel eyeOffset");
			view::camera_model_origin_update update{};
			vr::tests::require(view::rebase_camera_model_list_origins(record->data(),
				views.source_origin(), update) && update.rewritten == 11 && update.foreign == 0,
				"same-scene model descriptors were treated as foreign after body movement");
			view::camera_model_origin_census census{};
			vr::tests::require(view::census_camera_model_list_origins(record->data(), census) &&
				census.eye == 11 && census.inactive == 16 && census.other == 0,
				"published-center rebase did not satisfy the existing strict eye contract");
		}
		vr::tests::require(natural == before, "private model rebasing changed the source arena");
		natural[view::h2_view_origin_offset + sizeof(center)] ^= 1;
		vr::tests::require(!view::clone_scene_records(natural.data(), views, records),
			"axis-only camera-generation mismatch was accepted");
		natural = before;
		views.natural_camera[0] = std::numeric_limits<float>::quiet_NaN();
		vr::tests::require(!view::clone_scene_records(natural.data(), views, records),
			"non-finite published scene center was accepted");
	}

	void expect_owner_pass_target4_resource_contract()
	{
		// Only the two canonical completed-owner tuples can authorize PostFX.
		constexpr std::array<std::uint32_t, 7> ids{0, 3, 4, 5, 6, 304, 0xffffffffu};
		for (const auto final_target : ids)
		for (const auto first : ids)
		for (const auto spare : ids)
		for (const auto selector : ids)
		{
			const auto route = vr::native_display_contract::resolve(final_target, first, spare, selector);
			const bool expected = selector == 0 && final_target == first &&
				((first == 4 && spare == 5) || (first == 5 && spare == 4));
			vr::tests::require(bool(route) == expected && (!expected ||
				(route.source == final_target && route.destination == spare)),
				"native display route accepted an unfinished/alias/unknown target or lost thermal routing");
		}
		D3D11_TEXTURE2D_DESC description{};
		description.Width = 2560;
		description.Height = 1361;
		description.MipLevels = 1;
		description.ArraySize = 1;
		description.Format = DXGI_FORMAT_R11G11B10_FLOAT;
		description.SampleDesc = {1, 0};
		description.Usage = D3D11_USAGE_DEFAULT;
		description.BindFlags = D3D11_BIND_RENDER_TARGET |
			D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		vr::tests::require(vr::engine_stereo_owner_pass::supports_readback_source(description),
			"owner pass rejected H2 target 4's observed HDR resource contract");
		auto display = description;
		display.Width = 2528;
		display.Height = 2704;
		// Captured by GetDesc in the 2026-09-05 target-5 rejection. A missing
		// registry UAV view does not remove the resource's UAV bind capability.
		display.BindFlags = 0xA8;
		vr::tests::require(vr::native_display_contract::accepts(display) &&
			vr::engine_stereo_owner_pass::supports_readback_source(display),
			"actual target-5 descriptor rejected; scene/display identity is not encoded in BindFlags");
		auto missing_uav_capability = display;
		missing_uav_capability.BindFlags &= ~D3D11_BIND_UNORDERED_ACCESS;
		vr::tests::require(!vr::native_display_contract::accepts(missing_uav_capability),
			"incorrect non-UAV assumption still accepted as the target-5 contract");
		for (const auto format : {DXGI_FORMAT_R8G8B8A8_UNORM,
			DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R16G16B16A16_FLOAT})
		{
			auto invalid = display;
			invalid.Format = format;
			vr::tests::require(!vr::native_display_contract::accepts(invalid),
				"unproven display format was accepted");
		}
		auto multisampled = display;
		multisampled.SampleDesc.Count = 2;
		vr::tests::require(!vr::native_display_contract::accepts(multisampled),
			"multisampled display source was accepted");
		display.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
		vr::tests::require(!vr::native_display_contract::accepts(display),
			"foreign shared display source was accepted");

		description.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
		vr::tests::require(!vr::engine_stereo_owner_pass::supports_readback_source(description),
			"owner pass admitted a different output-merger resource as target 4");
	}

	void expect_camera_model_list_origin_rebase_contract()
	{
		alignas(16) std::array<std::uint8_t,
			vr::engine_stereo_view::h2_scene_record_size> record{};
		const std::array<float, 3> center{10.0f, 20.0f, 30.0f};
		const std::array<float, 3> eye{11.25f, 19.5f, 30.125f};
		const std::array<float, 3> foreign{-7.0f, 8.0f, 9.0f};
		std::memcpy(record.data() + vr::engine_stereo_view::h2_view_origin_offset,
			eye.data(), 3 * sizeof(float));
		const auto origin_at = [&](const std::size_t index)
		{
			return record.data() + vr::engine_stereo_view::h2_draw_list_descriptor_base +
				index * vr::engine_stereo_view::h2_draw_list_descriptor_size +
				vr::engine_stereo_view::h2_draw_list_origin_offset;
		};
		const auto activate = [&](const std::size_t index)
		{
			const std::uint32_t type = 13;
			std::memcpy(record.data() +
				vr::engine_stereo_view::h2_draw_list_descriptor_base +
				index * vr::engine_stereo_view::h2_draw_list_descriptor_size +
				vr::engine_stereo_view::h2_draw_list_active_type_offset,
				&type, sizeof(type));
		};
		for (std::size_t index{}; index < 21; ++index)
		{
			activate(index);
			std::memcpy(origin_at(index), center.data(), 3 * sizeof(float));
		}
		for (const auto index : {24u, 25u})
		{
			activate(index);
			std::memcpy(origin_at(index), eye.data(), 3 * sizeof(float));
		}
		// A matching origin without H2's nonzero +0x100 discriminator is lazy,
		// not authorized for mutation.
		std::memcpy(origin_at(21), center.data(), 3 * sizeof(float));
		activate(29);
		std::memcpy(origin_at(29), foreign.data(), 3 * sizeof(float));
		const auto before = record;

		vr::engine_stereo_view::camera_model_origin_update update{};
		vr::tests::require(vr::engine_stereo_view::rebase_camera_model_list_origins(
			record.data(), center, update) && update.rewritten == 21 &&
			update.already_eye == 2 && update.inactive == 3 && update.foreign == 1,
			"camera-model list rebasing did not classify the proven 27-list subset");
		for (std::size_t byte{}; byte < record.size(); ++byte)
		{
			bool may_change{};
			for (std::size_t index{}; index < 21; ++index)
			{
				const auto origin = vr::engine_stereo_view::h2_draw_list_descriptor_base +
					index * vr::engine_stereo_view::h2_draw_list_descriptor_size +
					vr::engine_stereo_view::h2_draw_list_origin_offset;
				may_change = may_change || (byte >= origin && byte < origin +
					3 * sizeof(float));
			}
			vr::tests::require(may_change || record[byte] == before[byte],
				"camera-model list rebasing changed bytes outside exact origin fields");
		}

		vr::engine_stereo_view::camera_model_origin_census census{};
		vr::tests::require(vr::engine_stereo_view::census_camera_model_list_origins(
			record.data(), census) && census.eye == 23 && census.inactive == 3 &&
			census.other == 1,
			"camera-model end census lost a foreign non-camera origin");
		std::memcpy(origin_at(29), eye.data(), 3 * sizeof(float));
		vr::tests::require(vr::engine_stereo_view::census_camera_model_list_origins(
			record.data(), census) && census.eye == 24 && census.inactive == 3 &&
			census.other == 0,
			"camera-model end census rejected a fully eye-local nonzero subset");
	}

	void expect_backend_model_eye_state_contract()
	{
		alignas(16) std::array<std::uint8_t,
			vr::engine_stereo_view::h2_scene_record_size> record{};
		alignas(16) std::array<std::uint8_t, 0x3280> backend{};
		const auto record_address = reinterpret_cast<std::uintptr_t>(record.data());
		const auto rebase_address = record_address +
			vr::engine_stereo_view::h2_frontend_rebase_view_offset;
		std::memcpy(backend.data() +
			vr::engine_stereo_view::h2_backend_primary_source_pointer_offset,
			&record_address, sizeof(record_address));
		std::memcpy(backend.data() +
			vr::engine_stereo_view::h2_backend_rebase_source_pointer_offset,
			&rebase_address, sizeof(rebase_address));

		const std::array<float, 3> primary{1000.0f, -2000.0f, 3000.0f};
		const std::array<float, 3> relative{1.25f, -0.5f, 0.125f};
		std::memcpy(record.data() + vr::engine_stereo_view::h2_view_origin_offset,
			primary.data(), 3 * sizeof(float));
		std::memcpy(record.data() +
			vr::engine_stereo_view::h2_relative_eye_offset_offset,
			relative.data(), 3 * sizeof(float));
		std::memcpy(backend.data() +
			vr::engine_stereo_view::h2_backend_primary_origin_offset,
			primary.data(), 3 * sizeof(float));
		std::memcpy(backend.data() +
			vr::engine_stereo_view::h2_backend_relative_eye_offset,
			relative.data(), 3 * sizeof(float));
		const std::uintptr_t cached_placement = 0x12345678u;
		std::memcpy(backend.data() +
			vr::engine_stereo_view::h2_backend_xmodel_placement_cache_offset,
			&cached_placement, sizeof(cached_placement));
		const auto before = backend;

		vr::engine_stereo_view::backend_model_state_sync_result result{};
		vr::tests::require(vr::engine_stereo_view::synchronize_backend_model_state(
			backend.data(), record.data(), result) && result.matched &&
			result.cache_invalidated && result.cache_before == cached_placement &&
			result.primary_origin == primary && result.relative_eye_offset == relative,
			"exact backend eye-state contract did not invalidate the placement cache");
		std::uintptr_t cache_after{1};
		std::memcpy(&cache_after, backend.data() +
			vr::engine_stereo_view::h2_backend_xmodel_placement_cache_offset,
			sizeof(cache_after));
		vr::tests::require(cache_after == 0,
			"backend model placement cache was not cleared to H2's native empty value");
		vr::tests::require(std::memcmp(backend.data(), before.data(),
			vr::engine_stereo_view::h2_backend_xmodel_placement_cache_offset) == 0 &&
			std::memcmp(backend.data() +
				vr::engine_stereo_view::h2_backend_xmodel_placement_cache_offset +
					sizeof(cache_after), before.data() +
				vr::engine_stereo_view::h2_backend_xmodel_placement_cache_offset +
					sizeof(cache_after), backend.size() -
				vr::engine_stereo_view::h2_backend_xmodel_placement_cache_offset -
					sizeof(cache_after)) == 0,
			"backend eye-state synchronization changed bytes outside the proven cache field");

		auto mismatch = before;
		const float wrong_relative = relative[0] + 1.0f;
		std::memcpy(mismatch.data() +
			vr::engine_stereo_view::h2_backend_relative_eye_offset,
			&wrong_relative, sizeof(wrong_relative));
		const auto mismatch_before = mismatch;
		vr::tests::require(!vr::engine_stereo_view::synchronize_backend_model_state(
			mismatch.data(), record.data(), result) && result.failure ==
				vr::engine_stereo_view::backend_model_state_sync_failure::
					relative_eye_offset &&
			result.relative_eye_offset[0] == wrong_relative &&
			result.expected_relative_eye_offset == relative &&
			std::memcmp(mismatch.data(), mismatch_before.data(), mismatch.size()) == 0,
			"mismatched backend eye state mutated H2's placement cache");

		auto rebase_mismatch = before;
		const auto wrong_rebase = rebase_address + 0x80;
		std::memcpy(rebase_mismatch.data() +
			vr::engine_stereo_view::h2_backend_rebase_source_pointer_offset,
			&wrong_rebase, sizeof(wrong_rebase));
		const auto rebase_mismatch_before = rebase_mismatch;
		vr::tests::require(!vr::engine_stereo_view::synchronize_backend_model_state(
			rebase_mismatch.data(), record.data(), result) && result.failure ==
				vr::engine_stereo_view::backend_model_state_sync_failure::rebase_source &&
			result.rebase_source == wrong_rebase &&
			result.expected_rebase_source == rebase_address &&
			std::memcmp(rebase_mismatch.data(), rebase_mismatch_before.data(),
				rebase_mismatch.size()) == 0,
			"mismatched backend rebase source was not diagnosed without mutation");
	}

	void expect_backend_depth_hack_projection_contract()
	{
		alignas(16) std::array<std::uint8_t, 0x3350> backend{};
		const std::array<float, 16> eye_projection{
			1.25f, 0.0f, 0.0f, 0.0f,
			0.0f, 0.835f, 0.0f, 0.0f,
			0.242708f, 0.192820f, 0.0f, 1.0f,
			0.0f, 0.0f, 4.0f, 0.0f,
		};
		const std::array<float, 16> symmetric_depth_hack{
			0.902738f, 0.0f, 0.0f, 0.0f,
			0.0f, 0.835479f, 0.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f,
			0.0f, 0.0f, 0.1f, 0.0f,
		};
		constexpr float depth_hack_near = 0.1f;
		constexpr std::uint32_t depth_hack_flags = 1;
		constexpr std::uint16_t projection_version = 73;
		std::memcpy(backend.data() +
			vr::engine_stereo_view::h2_backend_eye_projection_offset,
			eye_projection.data(), sizeof(eye_projection));
		std::memcpy(backend.data() +
			vr::engine_stereo_view::h2_backend_projection_cache_offset,
			symmetric_depth_hack.data(), sizeof(symmetric_depth_hack));
		std::memcpy(backend.data() +
			vr::engine_stereo_view::h2_backend_depth_hack_near_offset,
			&depth_hack_near, sizeof(depth_hack_near));
		std::memcpy(backend.data() +
			vr::engine_stereo_view::h2_backend_depth_hack_flags_offset,
			&depth_hack_flags, sizeof(depth_hack_flags));
		std::memcpy(backend.data() +
			vr::engine_stereo_view::h2_backend_projection_cache_version_offset,
			&projection_version, sizeof(projection_version));
		std::memcpy(backend.data() +
			vr::engine_stereo_view::h2_backend_projection_input_version_offset,
			&projection_version, sizeof(projection_version));
		const auto before = backend;

		vr::engine_stereo_view::backend_depth_hack_projection_result result{};
		vr::tests::require(
			vr::engine_stereo_view::restore_backend_depth_hack_projection(
				backend.data(), result) == vr::engine_stereo_view::
					backend_depth_hack_projection_outcome::restored && result.matched &&
			result.previous_terms == std::array<float, 4>{
				symmetric_depth_hack[0], symmetric_depth_hack[5], 0.0f, 0.0f} &&
			result.restored_terms == std::array<float, 4>{
				eye_projection[0], eye_projection[5], eye_projection[8], eye_projection[9]} &&
			result.depth_hack_near == depth_hack_near,
			"depth-hack projection did not restore the complete asymmetric eye matrix");
		auto expected_projection = eye_projection;
		expected_projection[14] = depth_hack_near;
		vr::tests::require(std::memcmp(backend.data() +
			vr::engine_stereo_view::h2_backend_projection_cache_offset,
			expected_projection.data(), sizeof(expected_projection)) == 0,
			"depth-hack projection did not preserve only H2's dedicated near clip");
		for (std::size_t byte{}; byte < backend.size(); ++byte)
		{
			const auto cache_begin =
				vr::engine_stereo_view::h2_backend_projection_cache_offset;
			const auto cache_end = cache_begin + sizeof(expected_projection);
			vr::tests::require((byte >= cache_begin && byte < cache_end) ||
				backend[byte] == before[byte],
				"depth-hack projection changed bytes outside the exact matrix cache");
		}

		auto unarmed = before;
		constexpr std::uint32_t no_depth_hack{};
		std::memcpy(unarmed.data() +
			vr::engine_stereo_view::h2_backend_depth_hack_flags_offset,
			&no_depth_hack, sizeof(no_depth_hack));
		const auto unarmed_before = unarmed;
		vr::tests::require(
			vr::engine_stereo_view::restore_backend_depth_hack_projection(
				unarmed.data(), result) == vr::engine_stereo_view::
					backend_depth_hack_projection_outcome::not_applicable &&
			!result.matched &&
			std::memcmp(unarmed.data(), unarmed_before.data(), unarmed.size()) == 0,
			"non-depth-hack projection was not a clean no-op");

		auto stale = before;
		constexpr std::uint16_t stale_version = projection_version - 1;
		std::memcpy(stale.data() +
			vr::engine_stereo_view::h2_backend_projection_cache_version_offset,
			&stale_version, sizeof(stale_version));
		const auto stale_before = stale;
		vr::tests::require(
			vr::engine_stereo_view::restore_backend_depth_hack_projection(
				stale.data(), result) == vr::engine_stereo_view::
					backend_depth_hack_projection_outcome::contract_mismatch &&
			!result.matched &&
			std::memcmp(stale.data(), stale_before.data(), stale.size()) == 0,
			"stale depth-hack projection cache was mutated before H2 published it");
	}

	void expect_backend_local_stereo_binding_ring()
	{
		namespace binding = vr::engine_stereo_binding;
		vr::tests::require(binding::reset(), "stereo binding ring did not reset");
		const auto pair = make_finalized_binding_pair();
		constexpr std::uintptr_t frontend = 0x11110000;
		constexpr std::uintptr_t record = 0x22220000;

		auto invalid = binding::frontend_publication{};
		invalid.frontend = frontend;
		invalid.record = record;
		invalid.frontend_epoch = 1;
		invalid.frontend_transaction_id = 1;
		invalid.views = pair;
		invalid.views.eyes[1].pair_id++;
		vr::tests::require(!binding::publish(invalid),
			"binding ring accepted incoherent eye identities");

		for (std::size_t index{}; index < binding::publication_capacity; ++index)
		{
			binding::frontend_publication publication{};
			publication.frontend = frontend;
			publication.record = record;
			publication.record_index = static_cast<std::uint32_t>(index & 3);
			publication.record_type = 4;
			publication.frontend_epoch = index + 10;
			publication.frontend_transaction_id = index + 100;
			publication.views = pair;
			vr::tests::require(binding::publish(publication),
				"binding ring rejected an available publication slot");
		}
		binding::frontend_publication overflow{};
		overflow.frontend = frontend;
		overflow.record = record;
		overflow.frontend_epoch = 999;
		overflow.frontend_transaction_id = 999;
		overflow.views = pair;
		vr::tests::require(!binding::publish(overflow),
			"full binding ring silently overwrote an unclaimed publication");
		vr::tests::require(!binding::acquire(record, frontend + 8),
			"binding ring matched record address without exact frontend identity");

		for (std::size_t index{}; index < binding::publication_capacity; ++index)
		{
			auto claim = binding::acquire(record, frontend);
			vr::tests::require(claim && claim.frontend_epoch == index + 10 &&
				claim.frontend_transaction_id == index + 100 &&
				claim.record_index == (index & 3) &&
				claim.views.eyes[0].pair_id == pair.eyes[0].pair_id,
				"binding ring did not claim the oldest exact publication");
			if (index == 0)
			{
				vr::tests::require(!binding::reset(),
					"binding reset cleared an active backend claim");
			}
			binding::release(claim);
			vr::tests::require(!claim, "binding release retained a live claim token");
		}

		const auto state = binding::get_status();
		vr::tests::require(state.publications == binding::publication_capacity &&
			state.claims == binding::publication_capacity &&
			state.releases == binding::publication_capacity &&
			state.publication_drops == 1 && state.mapping_misses == 1 &&
			state.invalid_publications == 1 && state.release_mismatches == 0 &&
			state.active_claims == 0 && state.maximum_active_claims == 1,
			"binding ring accounting lost an ownership transition");
		vr::tests::require(binding::reset(),
			"binding ring did not reset after every claim was released");
	}

	void expect_current_scene_binding()
	{
		namespace binding = vr::engine_stereo_binding;
		namespace view = vr::engine_stereo_view;
		vr::tests::require(binding::reset(), "current scene binding reset failed");
		const auto old_views = make_finalized_binding_pair({10.0f, 20.0f, 30.0f});
		auto current_views = make_finalized_binding_pair({12.0f, 21.0f, 30.5f});
		for (auto& eye : current_views.eyes) { eye.pair_id += 1; eye.publication += 1; }
		std::array<std::uint8_t, view::h2_view_slot_size> natural{};
		std::memcpy(natural.data() + view::h2_view_origin_offset,
			current_views.natural_camera.data(), sizeof(current_views.natural_camera));
		const auto untouched = natural;
		const auto address = reinterpret_cast<std::uintptr_t>(natural.data());
		constexpr std::uintptr_t frontend = 0x12340000;
		std::uint64_t epoch{};
		const auto publish = [&](const view::slot_pair& views, const std::uintptr_t owner)
		{
			binding::frontend_publication publication{};
			publication.frontend = owner;
			publication.record = address;
			publication.record_type = 4;
			publication.frontend_epoch = ++epoch;
			publication.frontend_transaction_id = epoch;
			publication.views = views;
			vr::tests::require(binding::publish(publication), "current scene publish failed");
		};
		publish(old_views, frontend);
		publish(old_views, frontend + 8);
		publish(current_views, frontend);
		auto claim = binding::acquire_current(natural.data(), frontend);
		vr::tests::require(claim && claim.frontend_epoch == 3 &&
			claim.views.eyes[0].pair_id == current_views.eyes[0].pair_id &&
			claim.views.eyes[1].pair_id == current_views.eyes[1].pair_id,
			"reused record address consumed an older eye family");
		binding::observe_owner_camera(claim, natural.data());
		binding::release(claim);
		auto foreign = binding::acquire(address, frontend + 8);
		vr::tests::require(foreign && foreign.frontend_epoch == 2,
			"current scene acquisition consumed another frontend's publication");
		binding::release(foreign);
		// Static cameras still select the newest publication, not the oldest match.
		publish(current_views, frontend);
		publish(current_views, frontend);
		claim = binding::acquire_current(natural.data(), frontend);
		vr::tests::require(claim && claim.frontend_epoch == 5,
			"static camera retained a superseded frame identity");
		binding::release(claim);
		// Do not fall back to an older matching camera when the newest one differs.
		publish(current_views, frontend);
		publish(old_views, frontend);
		vr::tests::require(!binding::acquire_current(natural.data(), frontend),
			"mismatched newest scene silently fell back to an older publication");
		// Axis differences matter even when the origin is unchanged.
		publish(current_views, frontend);
		natural[view::h2_view_origin_offset + 3 * sizeof(float)] ^= 1;
		vr::tests::require(!binding::acquire_current(natural.data(), frontend),
			"axis-only source mismatch was accepted");
		natural = untouched;
		vr::tests::require(!binding::acquire_current(nullptr, frontend) &&
			!binding::acquire_current(natural.data(), 0), "invalid current source accepted");
		const auto observed = binding::get_status();
		vr::tests::require(natural == untouched && observed.current_scene_matches == 2 &&
			observed.current_scene_camera_mismatches == 2 && observed.current_scene_misses == 4 &&
			observed.superseded_publications == 3 && observed.active_claims == 0 &&
			observed.publications == observed.claims && observed.claims == observed.releases &&
			observed.release_mismatches == 0 && observed.owner_camera_differences == 0,
			"current scene binding lost ownership, source immutability or mismatch evidence");
		vr::tests::require(binding::reset(), "current scene reset failed after drain");
		// Slot reuse breaks index order: the newest publication can be in slot 0
		// while an older generation of the same record remains in a higher slot.
		publish(old_views, frontend);
		publish(old_views, frontend + 8);
		publish(old_views, frontend);
		auto retired = binding::acquire(address, frontend);
		binding::release(retired);
		publish(current_views, frontend);
		claim = binding::acquire_current(natural.data(), frontend);
		vr::tests::require(claim && claim.frontend_epoch == epoch,
			"physical ring slot order replaced publication sequence order");
		binding::release(claim);
		foreign = binding::acquire(address, frontend + 8);
		binding::release(foreign);
		vr::tests::require(binding::reset(), "slot reuse test retained a claim");
		for (std::size_t i{}; i < binding::publication_capacity; ++i)
			publish(current_views, frontend);
		claim = binding::acquire_current(natural.data(), frontend);
		vr::tests::require(claim && claim.frontend_epoch == epoch &&
			binding::get_status().superseded_publications == binding::publication_capacity - 1,
			"bounded full-ring drain did not select the current scene");
		binding::release(claim);
		vr::tests::require(binding::reset() && binding::get_status().current_scene_matches == 0 &&
			binding::get_status().superseded_publications == 0,
			"current scene reset retained previous evidence");
	}

	void expect_owner_camera_identity_witness()
	{
		namespace binding = vr::engine_stereo_binding;
		vr::tests::require(binding::reset(), "camera witness could not reset");
		auto pair = make_finalized_binding_pair();
		std::array<std::uint8_t, vr::engine_stereo_view::h2_view_slot_size> natural{};
		std::memcpy(natural.data() + vr::engine_stereo_view::h2_view_origin_offset,
			pair.natural_camera.data(), sizeof(pair.natural_camera));
		const auto original = natural;
		binding::frontend_publication publication{};
		publication.frontend = 0x12340000;
		publication.record = reinterpret_cast<std::uintptr_t>(natural.data());
		publication.record_type = 4;
		publication.frontend_epoch = 37;
		publication.frontend_transaction_id = 41;
		publication.views = pair;
		vr::tests::require(binding::publish(publication), "camera witness publish failed");
		// The caller's later mutation must not change the ring's immutable witness.
		publication.views.natural_camera[0] = 999.0f;
		auto claim = binding::acquire(publication.record, publication.frontend);
		vr::tests::require(claim && claim.views.natural_camera == pair.natural_camera,
			"camera identity was not preserved across publication/acquisition");
		binding::observe_owner_camera(claim, natural.data());
		vr::tests::require(natural == original,
			"matching camera observation changed H2 bytes");
		// H2 can finalize/change matrices without changing this camera identity.
		natural[0x80] ^= 0x55;
		binding::observe_owner_camera(claim, natural.data());
		const auto changed_projection = natural;
		pair.natural_camera[2] = 1.5f;
		std::memcpy(natural.data() + vr::engine_stereo_view::h2_view_origin_offset,
			pair.natural_camera.data(), sizeof(pair.natural_camera));
		const auto moved = natural;
		binding::observe_owner_camera(claim, natural.data());
		vr::tests::require(natural == moved,
			"mismatching camera observation changed H2 bytes");
		const auto first = binding::get_status().first_owner_camera_difference;
		vr::tests::require(first.publication_sequence == claim.publication_sequence &&
			first.pair_id == claim.views.eyes[0].pair_id && first.frontend_epoch == 37 &&
			first.frontend_transaction_id == 41 && first.record == publication.record &&
			first.published[2] == 0.0f && first.consumed[2] == 1.5f,
			"movement difference lost the exact camera/publication identity");
		// A later axis-only difference must not overwrite the first event.
		natural = changed_projection;
		const float changed_axis = 0.99f;
		std::memcpy(natural.data() + 0x10C, &changed_axis, sizeof(changed_axis));
		binding::observe_owner_camera(claim, natural.data());
		binding::observe_owner_camera(claim, nullptr);
		binding::observe_owner_camera(claim, original.data()); // Wrong identity, no read.
		const auto observed = binding::get_status();
		vr::tests::require(observed.owner_camera_matches == 2 &&
			observed.owner_camera_differences == 2 && observed.owner_camera_invalid == 2 &&
			observed.first_owner_camera_difference.consumed == first.consumed &&
			observed.claims == 1 && observed.releases == 0 && observed.active_claims == 1,
			"camera witness overwrote first evidence or changed ring ownership");
		binding::release(claim);
		vr::tests::require(binding::reset(), "camera witness did not release its claim");
		const auto cleared = binding::get_status();
		vr::tests::require(cleared.owner_camera_matches == 0 &&
			cleared.owner_camera_differences == 0 && cleared.owner_camera_invalid == 0 &&
			cleared.first_owner_camera_difference.publication_sequence == 0,
			"quiescent reset retained evidence from a previous test session");
	}

	void expect_one_shot_backend_eye_view_copy()
	{
		namespace binding = vr::engine_stereo_binding;
		namespace backend_view = vr::engine_stereo_backend_view;
		vr::tests::require(binding::reset(),
			"backend eye-view test could not reset the binding ring");
		vr::tests::require(backend_view::reset(),
			"backend eye-view test could not arm its one-shot gate");

		const auto pair = make_finalized_binding_pair();
		auto natural = pair.eyes[0].bytes;
		natural[0] ^= 0xA5;
		binding::frontend_publication publication{};
		publication.frontend = 0x11110000;
		publication.record = reinterpret_cast<std::uintptr_t>(natural.data());
		publication.record_type = 4;
		publication.frontend_epoch = 7;
		publication.frontend_transaction_id = 9;
		publication.views = pair;
		vr::tests::require(binding::publish(publication),
			"backend eye-view test could not publish a finalized pair");
		auto claim = binding::acquire(publication.record, publication.frontend);
		vr::tests::require(static_cast<bool>(claim),
			"backend eye-view test could not claim its exact record");

		backend_view::transaction transaction{};
		vr::tests::require(backend_view::begin(transaction, claim,
			publication.record), "backend eye-view one-shot did not begin");
		vr::tests::require(!backend_view::reset(),
			"backend eye-view gate reset an active transaction");

		std::array<std::uint8_t, backend_view::source_pointer_offset +
			sizeof(std::uintptr_t)> backend_state{};
		auto source = publication.record;
		std::memcpy(backend_state.data() + backend_view::source_pointer_offset,
			&source, sizeof(source));
		const auto h2_copy = +[](void* const opaque)
		{
			auto* const state = static_cast<std::uint8_t*>(opaque);
			std::uintptr_t input{};
			std::memcpy(&input, state + backend_view::source_pointer_offset,
				sizeof(input));
			std::memcpy(state + backend_view::copied_view_offset,
				reinterpret_cast<const void*>(input),
				vr::engine_stereo_view::h2_view_slot_size);
		};

		backend_view::invoke_copy(transaction, backend_state.data(), h2_copy);
		std::uintptr_t restored{};
		std::memcpy(&restored, backend_state.data() +
			backend_view::source_pointer_offset, sizeof(restored));
		vr::tests::require(restored == publication.record &&
			std::memcmp(backend_state.data() + backend_view::copied_view_offset,
				pair.eyes[0].bytes.data(), pair.eyes[0].bytes.size()) == 0,
			"backend eye-view copy did not restore H2's source pointer exactly");

		auto foreign = natural;
		foreign[0] ^= 0x5A;
		source = reinterpret_cast<std::uintptr_t>(foreign.data());
		std::memcpy(backend_state.data() + backend_view::source_pointer_offset,
			&source, sizeof(source));
		backend_view::invoke_copy(transaction, backend_state.data(), h2_copy);
		vr::tests::require(std::memcmp(backend_state.data() +
			backend_view::copied_view_offset, foreign.data(), foreign.size()) == 0,
			"backend eye-view gate modified a foreign H2 copy source");

		source = publication.record;
		std::memcpy(backend_state.data() + backend_view::source_pointer_offset,
			&source, sizeof(source));
		backend_view::invoke_copy(transaction, backend_state.data(), h2_copy);
		backend_view::end(transaction, natural.data(), true);
		const auto state = backend_view::get_status();
		vr::tests::require(state.state == backend_view::gate_state::complete &&
			state.attempts == 1 && state.completions == 1 && state.failures == 0 &&
			state.copy_calls == 3 && state.substitutions == 2 &&
			state.foreign_copy_bypasses == 1 && state.destination_mismatches == 0 &&
			state.record_mutations == 0 && state.dispatch_misses == 0,
			"backend eye-view gate lost an exact copy/restore transition");

		binding::release(claim);
		vr::tests::require(binding::reset() && backend_view::reset(),
			"backend eye-view test did not leave both ownership gates quiescent");
	}

	void expect_one_shot_backend_target_route()
	{
		namespace binding = vr::engine_stereo_binding;
		namespace backend_target = vr::engine_stereo_backend_target;
		vr::tests::require(binding::reset(),
			"backend target-route test could not reset the binding ring");
		vr::tests::require(backend_target::reset(),
			"backend target-route test could not arm its one-shot gate");
		backend_target::on_present_post(41, 7, 77, 0);

		const auto pair = make_finalized_binding_pair();
		alignas(16) std::array<std::uint8_t,
			vr::engine_stereo_view::h2_view_slot_size> record{};
		binding::frontend_publication publication{};
		publication.frontend = 0x33330000;
		publication.record = reinterpret_cast<std::uintptr_t>(record.data());
		publication.record_type = 4;
		publication.frontend_epoch = 11;
		publication.frontend_transaction_id = 13;
		publication.views = pair;
		vr::tests::require(binding::publish(publication),
			"backend target-route test could not publish a finalized pair");
		auto claim = binding::acquire(publication.record, publication.frontend);
		vr::tests::require(static_cast<bool>(claim),
			"backend target-route test could not claim its exact record");

		backend_target::transaction transaction{};
		vr::tests::require(backend_target::begin(transaction, claim,
			publication.record), "backend target-route one-shot did not begin");
		vr::tests::require(!backend_target::reset(),
			"backend target-route gate reset an active transaction");

		std::array<std::uint8_t, backend_target::current_target_offset +
			sizeof(std::uint32_t)> backend_state{};
		auto current = backend_target::target_capacity;
		std::memcpy(backend_state.data() + backend_target::current_target_offset,
			&current, sizeof(current));
		std::array<std::uintptr_t, 2> context{
			0x44440000,
			reinterpret_cast<std::uintptr_t>(backend_state.data()),
		};
		std::array<std::uint8_t, backend_target::target_capacity *
			backend_target::target_entry_size> registry{};
		for (const auto target : {1u, 4u, 5u, 13u, 14u, 15u})
		{
			auto* const entry = registry.data() + static_cast<std::size_t>(target) *
				backend_target::target_entry_size;
			entry[0] = static_cast<std::uint8_t>(0x80 + target);
			const std::uint16_t width = 2528;
			const std::uint16_t height = 2704;
			std::memcpy(entry + 0x30, &width, sizeof(width));
			std::memcpy(entry + 0x32, &height, sizeof(height));
		}
		const auto h2_select = +[](void* const opaque, const std::uint32_t target)
		{
			std::uintptr_t state{};
			std::memcpy(&state, static_cast<std::uint8_t*>(opaque) +
				vr::engine_stereo_backend_target::context_state_pointer_offset,
				sizeof(state));
			std::memcpy(reinterpret_cast<void*>(state +
				vr::engine_stereo_backend_target::current_target_offset),
				&target, sizeof(target));
		};

		const backend_target::selection_scope active_scope{
			19,
			claim.publication_sequence,
			publication.record,
			publication.record_type,
			true,
			true,
		};
		backend_target::record_view_copy(transaction, 0x14078A404, active_scope,
			1, 1, 0, true);
		backend_target::invoke_select(transaction, context.data(), 13, 0x1407B0483,
			active_scope, registry.data(), backend_target::target_capacity, h2_select);
		backend_target::invoke_select(transaction, context.data(), 14, 0x140296FF4,
			active_scope, registry.data(), backend_target::target_capacity, h2_select);
		backend_target::invoke_select(transaction, context.data(), 15, 0x14029706E,
			active_scope, registry.data(), backend_target::target_capacity, h2_select);
		backend_target::record_view_copy(transaction, 0x14078A404, active_scope,
			2, 2, 0, true);
		backend_target::invoke_select(transaction, context.data(), 1, 0x1407A8451,
			active_scope, registry.data(), backend_target::target_capacity, h2_select);
		backend_target::invoke_select(transaction, context.data(), 1, 0x1407A6C5A,
			active_scope, registry.data(), backend_target::target_capacity, h2_select);
		backend_target::enter_dispatch(transaction);
		backend_target::leave_dispatch(transaction);
		backend_target::end(transaction, true);
		backend_target::transaction outside_transaction{};
		backend_target::invoke_select(outside_transaction, context.data(), 4,
			0x140003000, {}, registry.data(), backend_target::target_capacity, h2_select);
		backend_target::invoke_select(outside_transaction, context.data(), 5,
			0x140003100, {}, registry.data(), backend_target::target_capacity, h2_select);
		backend_target::on_present_pre(42, 7, 88);

		const auto state = backend_target::get_status();
		vr::tests::require(state.state == backend_target::gate_state::complete &&
			state.attempts == 1 && state.completions == 1 && state.failures == 0 &&
			state.select_calls == 5 && state.dispatch_select_calls == 0 &&
			state.applied_transitions == 4 && state.retained_targets == 1 &&
			state.unique_targets == 4 && state.invalid_observations == 0 &&
			state.application_mismatches == 0 && state.entry_mutations == 0 &&
			state.route_overflows == 0,
			"backend target-route gate lost an exact native target transition");
		backend_target::report report{};
		vr::tests::require(backend_target::read_report(report) &&
			report.event_count == 5 && report.unique_target_count == 4 &&
			report.events[0].phase == backend_target::route_phase::before_dispatch &&
			report.events[4].phase == backend_target::route_phase::before_dispatch &&
			report.events[4].current_after == 1 && report.targets[1].seen &&
			report.targets[13].seen && report.targets[14].seen &&
			report.targets[15].seen,
			"backend target-route report did not preserve its bounded call sequence");
		const auto frame_state = backend_target::get_frame_status();
		auto frame_report = std::make_unique<backend_target::frame_report>();
		vr::tests::require(frame_state.state == backend_target::gate_state::complete &&
			frame_state.attempts == 1 && frame_state.completions == 1 &&
			frame_state.failures == 0 && frame_state.select_calls == 7 &&
			frame_state.view_copy_events == 2 &&
			frame_state.unique_targets == 6 && frame_state.invalid_observations == 0 &&
			frame_state.application_mismatches == 0 && frame_state.entry_mutations == 0 &&
			frame_state.route_overflows == 0 && frame_state.start_present_post_frame == 41 &&
			frame_state.end_present_pre_frame == 42 &&
			backend_target::read_frame_report(*frame_report) &&
			frame_report->event_count == 9 && frame_report->view_copy_event_count == 2 &&
			frame_report->unique_target_count == 6 &&
			frame_report->events[0].kind == backend_target::frame_event_kind::view_copy &&
			frame_report->events[0].view_copy_ordinal == 1 &&
			frame_report->events[1].phase == backend_target::route_phase::before_dispatch &&
			frame_report->events[7].phase == backend_target::route_phase::outside_transaction &&
			frame_report->events[8].target_id == 5,
			"backend target frame-route did not preserve the claim-to-Present interval");

		// A Present boundary may arrive on a different thread while the H2 target
		// selector is still executing. The frame gate must close without waiting and
		// let the final writer publish the immutable report after the original call.
		vr::tests::require(backend_target::reset(),
			"backend target frame-route did not reset for its cross-thread boundary test");
		backend_target::on_present_post(50, 7, 77, 0);
		backend_target::transaction racing_transaction{};
		vr::tests::require(backend_target::begin(racing_transaction, claim,
			publication.record), "backend target frame-route race did not begin");
		backend_target::record_view_copy(racing_transaction, 0x14078A404,
			active_scope, 1, 1, 0, true);
		target_select_entered.store(false, std::memory_order_relaxed);
		target_select_release.store(false, std::memory_order_relaxed);
		std::thread selector([&]
		{
			backend_target::invoke_select(racing_transaction, context.data(), 13,
				0x1407B0483, active_scope, registry.data(),
				backend_target::target_capacity, blocking_h2_target_select);
		});
		bool selector_entered{};
		for (std::size_t attempt{}; attempt < 2000; ++attempt)
		{
			if (target_select_entered.load(std::memory_order_acquire))
			{
				selector_entered = true;
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		if (!selector_entered)
		{
			target_select_release.store(true, std::memory_order_release);
			selector.join();
			vr::tests::require(false,
				"backend target frame-route race never entered the original selector");
		}
		backend_target::on_present_pre(51, 7, 88);
		const auto closing_state = backend_target::get_frame_status();
		vr::tests::require(closing_state.state == backend_target::gate_state::closing &&
			closing_state.active_writers == 1,
			"Present boundary did not leave the in-flight target observation owned");
		target_select_release.store(true, std::memory_order_release);
		selector.join();
		backend_target::enter_dispatch(racing_transaction);
		backend_target::leave_dispatch(racing_transaction);
		backend_target::end(racing_transaction, true);
		const auto raced_state = backend_target::get_frame_status();
		vr::tests::require(raced_state.state == backend_target::gate_state::complete &&
			raced_state.select_calls == 1 && raced_state.view_copy_events == 1 &&
			raced_state.active_writers == 0 &&
			raced_state.maximum_active_writers == 1 &&
			raced_state.start_present_post_frame == 50 &&
			raced_state.end_present_pre_frame == 51,
			"final target writer did not complete the cross-thread Present boundary");

		binding::release(claim);
		vr::tests::require(binding::reset() && backend_target::reset(),
			"backend target-route test did not leave both ownership gates quiescent");
	}
}

int main()
{
	try
	{
		// Live mounted witness: PS+0x54 is zero, PS+0x58 is 0x3002.
		// Movement and entity flags must never alias in the native declaration.
		game::playerState_s mounted_state{};
		const std::uint32_t entity_flags=0x3002;
		std::memcpy(reinterpret_cast<std::byte*>(&mounted_state)+0x58,&entity_flags,sizeof(entity_flags));
		if (mounted_state.pm_flags!=0 || !vr::gameplay::mounted::attached(mounted_state.e_flags))
			throw std::runtime_error("native mounted flag offset rejected");
		mounted_state.pm_flags=0x3000;mounted_state.e_flags=0;
		if (vr::gameplay::mounted::attached(mounted_state.e_flags))
			throw std::runtime_error("movement flags must not create turret ownership");
		expect_target_gate();
		expect_camera_and_correlation();
		expect_invalid_camera_and_trace();
		expect_head_pose_camera_transform();
		expect_native_game_view();
		expect_roomscale_turn_origin();
		expect_controller_spatial_frame();
		expect_optical_projection_remap();
		expect_bridge_gate_and_views();
		expect_explicit_eye_render_config_and_counters();
		expect_same_frame_view_family_contract();
		expect_render_target_observation_contract();
		expect_record_local_asymmetric_eye_slots();
		expect_symmetric_eye_finalization();
		expect_isolated_backend_scene_record_clones();
		expect_scene_clone_rejects_mixed_camera_generations();
		expect_owner_pass_target4_resource_contract();
		expect_camera_model_list_origin_rebase_contract();
		expect_backend_model_eye_state_contract();
		expect_backend_depth_hack_projection_contract();
		expect_backend_local_stereo_binding_ring();
		expect_current_scene_binding();
		expect_owner_camera_identity_witness();
		expect_one_shot_backend_eye_view_copy();
		expect_one_shot_backend_target_route();
		std::cout << "engine-stereo-probe-smoke: PASS\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "engine-stereo-probe-smoke: FAIL: " << error.what() << '\n';
		return 1;
	}
}
