#pragma once
#include "scripted_camera.hpp"

#include <array>
#include <cstdint>
#include <chrono>
#include "body_pose.hpp"
#include "pose_filter.hpp"

namespace vr::head_pose_bridge
{
	using vector3 = std::array<float, 3>;
	using matrix3 = std::array<vector3, 3>;

	struct tracking_pose
	{
		vector3 position_meters{};
		matrix3 orientation{};
	};

	struct status
	{
		bool target_matched{};
		bool enabled{};
		bool pose_available{};
		bool recenter_pending{};
		bool camera_applied{};
		float world_scale{};
		vector3 local_position_units{};
		matrix3 local_orientation{};
		std::uint64_t pose_publications{};
		std::uint64_t camera_applications{};
		std::uint64_t recenter_count{};
		std::uint64_t invalid_pose_count{};
		std::uint64_t composition_failure_count{};
		std::uint64_t yaw_from_left_count{};
		std::uint64_t yaw_extraction_failure_count{};
		float local_roll_degrees{};
		float reference_absolute_pitch_degrees{};
		float reference_absolute_roll_degrees{};
		float current_absolute_pitch_degrees{};
		float current_absolute_roll_degrees{};
		float input_pitch_degrees{};
		float base_yaw_degrees{};
		float output_pitch_degrees{};
		float input_horizon_roll_degrees{};
		float output_horizon_roll_degrees{};
		float max_abs_local_roll_degrees{};
		float max_abs_input_horizon_roll_degrees{};
		float max_abs_output_horizon_roll_degrees{};
		float min_local_roll_degrees{};
		float max_local_roll_degrees{};
		float min_input_horizon_roll_degrees{};
		float max_input_horizon_roll_degrees{};
		float min_output_horizon_roll_degrees{};
		float max_output_horizon_roll_degrees{};
		float input_basis_error{};
		float output_basis_error{};
		float max_output_basis_error{};
		std::uint64_t game_view_applications{};
		std::uint64_t game_view_history_misses{};
		float game_view_yaw_contribution{};
	};

	struct spatial_frame
	{
		tracking_pose reference{};
		vector3 world_origin{};
		matrix3 world_yaw_axis{};
		float units_per_meter{};
		std::uint64_t generation{};
		std::chrono::steady_clock::time_point captured_at{};
		// Applied center-eye camera, not native eye origin or an individual eye.
		vector3 head_position{};
		matrix3 head_yaw_axis{};
		body_pose::estimate body{}; // Shared equipment anchor, in the same world frame.
		vector3 head_forward{}; // Full center-eye direction, including pitch and roll.
		pose_filter::pose tracking_correction{};
		bool head_stabilized{};
		vector3 head_up{}; // Full center-eye up axis for head-mounted interactions.
	};
	inline spatial_frame body_slots_frame(spatial_frame frame) noexcept
	{
		if(frame.body.valid){frame.head_position=frame.body.position;frame.head_yaw_axis=frame.body.yaw_axis;}
		return frame;
	}

	struct world_pose
	{
		vector3 position{};
		matrix3 axis{}; // H2 forward, left, up rows
	};
	struct tracking_reference
	{
		tracking_pose origin{};
		matrix3 head_axis{}; // H2-local forward/left/up, without native camera motion.
		std::uint64_t generation{};
		vector3 head_position_meters{}; // H2-local physical translation before scripted attenuation.
	};
	// Available during native camera ownership too. Raw controller positions can
	// be interpreted without depending on rendered hands or a gameplay camera.
	[[nodiscard]] bool get_tracking_reference(tracking_reference& output) noexcept;
	inline bool tracking_position(const tracking_reference& frame,const vector3& meters,vector3& output) noexcept
	{
		if(!frame.generation || !pose_filter::valid({frame.origin.position_meters,frame.origin.orientation}))return false;
		for(float x:meters)if(!std::isfinite(x) || std::abs(x)>100000)return false;
		const auto local=pose_filter::rotate(pose_filter::transpose(frame.origin.orientation),pose_filter::sub(meters,frame.origin.position_meters));
		output={-local[2],-local[0],local[1]};return true;
	}

	// One copy per consumer transaction, shared by both hands. Uses the same
	// yaw-only native camera base and recenter reference as head composition.
	[[nodiscard]] bool get_spatial_frame(spatial_frame& output) noexcept;
	[[nodiscard]] bool tracking_to_world(const spatial_frame& frame, const tracking_pose& pose,
		world_pose& output) noexcept;

	void configure_target(bool matched) noexcept;
	void set_enabled(bool enabled) noexcept;
	void set_world_scale(float units_per_meter) noexcept;
	// The runtime adapter decides whether a sample may establish a new origin.
	// Retaining the reference leaves any initial/manual recenter request pending.
	enum class reference_policy
	{
		allow_recenter,
		retain_reference
	};
	void publish_tracking_pose(const tracking_pose& pose, std::uint64_t sample_id = 0,
		std::chrono::steady_clock::time_point sampled_at = {},
		reference_policy reference = reference_policy::allow_recenter) noexcept;
	void request_recenter(bool reset_view=false) noexcept;
	void invalidate_pose() noexcept;
	void reset() noexcept;
	// Spawn/save-load can reuse both player pointers and native command times.
	// Discard derived camera/input history without restarting tracking.
	void reset_game_view() noexcept;

	// Called on the command thread immediately before native FinishMove packs
	// angles. No runtime/engine calls are made while holding the bridge mutex.
	[[nodiscard]] bool apply_game_view(float* native_angles, const float* delta_angles,
		std::uint64_t scripted_epoch = 0, bool remote = false) noexcept;
	[[nodiscard]] bool apply_remote_control(std::array<std::int8_t,2>& command,std::array<float,2> rates,
		std::array<float,2> stick,int command_time,std::uint64_t epoch)noexcept;
	void suspend_remote_view()noexcept;
	void record_game_command(int command_time, int packed_pitch = 0, bool tracked = false) noexcept;
	[[nodiscard]] bool resolve_game_pitch(int command_time, int packed_pitch,
		float delta_pitch, float& pitch) noexcept;
	[[nodiscard]] bool apply_camera(float* origin, float (*axis)[3], int command_time = 0,
		const float* native_angles = nullptr, const game_view::camera_request& request = {},
		const game_view::scripted_rotation_reference* rotation_reference = nullptr) noexcept;
	[[nodiscard]] bool camera_was_applied() noexcept;
	[[nodiscard]] status get_status() noexcept;
}
