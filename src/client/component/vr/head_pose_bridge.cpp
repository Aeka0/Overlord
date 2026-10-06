#include <std_include.hpp>

#include "head_pose_bridge.hpp"
#include "camera_rig.hpp"
#include "game_view.hpp"
#include "remote_view.hpp"
#include "continuous_view_angles.hpp"
#include "stabilization.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>

namespace vr::head_pose_bridge
{
	namespace
	{
		constexpr float default_world_scale = 39.3700787f;
		constexpr matrix3 identity_matrix{{
			{1.0f, 0.0f, 0.0f},
			{0.0f, 1.0f, 0.0f},
			{0.0f, 0.0f, 1.0f},
		}};

		std::mutex state_mutex;
		game_view::remote_view remote_view_state;
		bool target_matched{};
		bool enabled{};
		bool pose_available{};
		bool recenter_pending{true};
		bool camera_applied{};
		float world_scale{default_world_scale};
		tracking_pose reference_pose{};
		spatial_frame camera_frame{};
		body_pose::estimator body_estimator;
		bool camera_frame_available{};
		pose_filter::filter head_filter;
		pose_filter::pose tracking_correction;
		bool head_stabilized{};
		std::uint64_t filter_epoch{};
		vector3 local_position_units{};
		matrix3 local_orientation{identity_matrix};
		std::chrono::steady_clock::time_point pose_sampled_at{};
		game_view::state game_view_state;
		game_view::camera_rig scripted_camera;
		game_view::continuous_angles absolute_angles, local_angles;
		game_view::horizontal_heading local_heading;
		std::uint64_t game_view_applications{};
		std::uint64_t game_view_history_misses{};
		float game_view_yaw_contribution{};
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

		float dot(const vector3& left, const vector3& right) noexcept
		{
			return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
		}

		bool finite_vector(const vector3& value) noexcept
		{
			return std::all_of(value.begin(), value.end(), [](const float element)
			{
				return std::isfinite(element);
			});
		}

		bool valid_rotation(const matrix3& value) noexcept
		{
			for (const auto& row : value)
			{
				if (!finite_vector(row) || std::abs(dot(row, row) - 1.0f) > 0.05f)
				{
					return false;
				}
			}
			if (std::abs(dot(value[0], value[1])) > 0.05f ||
				std::abs(dot(value[0], value[2])) > 0.05f ||
				std::abs(dot(value[1], value[2])) > 0.05f)
			{
				return false;
			}
			const float determinant =
				value[0][0] * (value[1][1] * value[2][2] - value[1][2] * value[2][1]) -
				value[0][1] * (value[1][0] * value[2][2] - value[1][2] * value[2][0]) +
				value[0][2] * (value[1][0] * value[2][1] - value[1][1] * value[2][0]);
			return std::abs(determinant - 1.0f) <= 0.05f;
		}

		float determinant(const matrix3& value) noexcept
		{
			return value[0][0] * (value[1][1] * value[2][2] -
				value[1][2] * value[2][1]) -
				value[0][1] * (value[1][0] * value[2][2] -
					value[1][2] * value[2][0]) +
				value[0][2] * (value[1][0] * value[2][1] -
					value[1][1] * value[2][0]);
		}

		float basis_error(const matrix3& value) noexcept
		{
			float error = std::abs(determinant(value) - 1.0f);
			for (std::size_t row{}; row < value.size(); ++row)
			{
				error = std::max(error, std::abs(dot(value[row], value[row]) - 1.0f));
				for (std::size_t other = row + 1; other < value.size(); ++other)
				{
					error = std::max(error, std::abs(dot(value[row], value[other])));
				}
			}
			return error;
		}

		vector3 cross(const vector3& left, const vector3& right) noexcept
		{
			return {
				left[1] * right[2] - left[2] * right[1],
				left[2] * right[0] - left[0] * right[2],
				left[0] * right[1] - left[1] * right[0],
			};
		}

		float horizon_roll_degrees(const matrix3& axis) noexcept
		{
			constexpr vector3 world_up{0.0f, 0.0f, 1.0f};
			const auto& forward = axis[0];
			const auto& up = axis[2];
			const auto vertical = dot(world_up, forward);
			vector3 reference_up{
				world_up[0] - vertical * forward[0],
				world_up[1] - vertical * forward[1],
				world_up[2] - vertical * forward[2],
			};
			const auto length_squared = dot(reference_up, reference_up);
			if (!std::isfinite(length_squared) || length_squared < 0.0001f)
			{
				return std::numeric_limits<float>::quiet_NaN();
			}
			const auto inverse_length = 1.0f / std::sqrt(length_squared);
			for (auto& component : reference_up) component *= inverse_length;
			constexpr auto radians_to_degrees = 57.29577951308232f;
			return std::atan2(dot(cross(reference_up, up), forward),
				dot(reference_up, up)) * radians_to_degrees;
		}

		float pitch_degrees(const matrix3& axis) noexcept
		{
			constexpr auto radians_to_degrees = 57.29577951308232f;
			return std::asin(std::clamp(axis[0][2], -1.0f, 1.0f)) * radians_to_degrees;
		}

		void update_peak(const float value, float& peak) noexcept
		{
			if (std::isfinite(value)) peak = std::max(peak, std::abs(value));
		}

		void update_range(const float value, float& minimum, float& maximum) noexcept
		{
			if (!std::isfinite(value)) return;
			minimum = std::min(minimum, value);
			maximum = std::max(maximum, value);
		}

		vector3 multiply(const matrix3& matrix, const vector3& vector) noexcept
		{
			return {
				dot(matrix[0], vector),
				dot(matrix[1], vector),
				dot(matrix[2], vector),
			};
		}

		matrix3 transpose(const matrix3& matrix) noexcept
		{
			return {{
				{matrix[0][0], matrix[1][0], matrix[2][0]},
				{matrix[0][1], matrix[1][1], matrix[2][1]},
				{matrix[0][2], matrix[1][2], matrix[2][2]},
			}};
		}

		matrix3 multiply(const matrix3& left, const matrix3& right) noexcept
		{
			const auto right_transposed = transpose(right);
			matrix3 result{};
			for (std::size_t row{}; row < 3; ++row)
			{
				for (std::size_t column{}; column < 3; ++column)
				{
					result[row][column] = dot(left[row], right_transposed[column]);
				}
			}
			return result;
		}

		bool extract_h2_yaw_only(const matrix3& axis, matrix3& output,
			float& yaw_degrees, bool& used_left_axis) noexcept
		{
			// H2 camera axes are rows in +X forward, +Y left, +Z up space.
			// Mouse pitch scales the horizontal forward vector but does not change
			// its heading, so its normalized XY projection is the exact world yaw.
			float cosine = axis[0][0];
			float sine = axis[0][1];
			auto horizontal_length_squared = cosine * cosine + sine * sine;
			used_left_axis = false;
			if (!std::isfinite(horizontal_length_squared) ||
				horizontal_length_squared < 0.0001f)
			{
				// At vertical pitch the forward heading is singular, while H2's
				// left axis still carries the same yaw. A valid rotation cannot have
				// both orthogonal axes parallel to world up.
				cosine = axis[1][1];
				sine = -axis[1][0];
				horizontal_length_squared = cosine * cosine + sine * sine;
				used_left_axis = true;
			}
			if (!std::isfinite(horizontal_length_squared) ||
				horizontal_length_squared < 0.0001f)
			{
				return false;
			}

			const auto inverse_length = 1.0f / std::sqrt(horizontal_length_squared);
			cosine *= inverse_length;
			sine *= inverse_length;
			output = {{
				{cosine, sine, 0.0f},
				{-sine, cosine, 0.0f},
				{0.0f, 0.0f, 1.0f},
			}};
			constexpr auto radians_to_degrees = 57.29577951308232f;
			yaw_degrees = std::atan2(sine, cosine) * radians_to_degrees;
			return std::isfinite(yaw_degrees);
		}

		// OpenXR/OpenVR tracking coordinates are +X right, +Y up, -Z forward.
		// H2 camera-local coordinates are +X forward, +Y left, +Z up.
		vector3 tracking_to_h2(const vector3& value) noexcept
		{
			return {-value[2], -value[0], value[1]};
		}

		matrix3 h2_orientation(const matrix3& relative_tracking_rotation) noexcept
		{
			constexpr vector3 forward{0.0f, 0.0f, -1.0f};
			constexpr vector3 left{-1.0f, 0.0f, 0.0f};
			constexpr vector3 up{0.0f, 1.0f, 0.0f};
			return {{
				tracking_to_h2(multiply(relative_tracking_rotation, forward)),
				tracking_to_h2(multiply(relative_tracking_rotation, left)),
				tracking_to_h2(multiply(relative_tracking_rotation, up)),
			}};
		}

		void invalidate_locked() noexcept
		{
			if(pose_available)stabilization::invalidate();
			head_filter.reset();tracking_correction={};head_stabilized=false;
			body_estimator.reset();
			camera_frame_available = false;
			pose_available = false;
			camera_applied = false;
			local_position_units = {};
			local_orientation = identity_matrix;
			local_roll_degrees = 0.0f;
		}
	}

	void configure_target(const bool matched) noexcept
	{
		const std::lock_guard lock(state_mutex);
		target_matched = matched;
		if (!matched)
		{
			enabled = false;
			scripted_camera = {};
			invalidate_locked();
		}
	}

	void set_enabled(const bool requested) noexcept
	{
		const std::lock_guard lock(state_mutex);
		const bool applied = requested && target_matched;
		if (enabled == applied)
		{
			return;
		}
		enabled = applied;
		if (!applied) {scripted_camera = {};}
		invalidate_locked();
		if (applied)
		{
			recenter_pending = true;
		}
	}

	void set_world_scale(const float units_per_meter) noexcept
	{
		if (!std::isfinite(units_per_meter) || units_per_meter <= 0.0f || units_per_meter > 10000.0f)
		{
			return;
		}
		const std::lock_guard lock(state_mutex);
		world_scale = units_per_meter;
	}

	void publish_tracking_pose(const tracking_pose& pose, const std::uint64_t sample_id,
		const std::chrono::steady_clock::time_point sampled_at, const reference_policy reference) noexcept
	{
		const bool valid = finite_vector(pose.position_meters) && valid_rotation(pose.orientation);
		const std::lock_guard lock(state_mutex);
		if (!valid)
		{
			++invalid_pose_count;
			invalidate_locked();
			return;
		}
		if (!target_matched || !enabled)
		{
			return;
		}

		if (recenter_pending && reference == reference_policy::retain_reference)
		{
			invalidate_locked();
			return;
		}
		++pose_publications;
		const auto absolute_orientation = h2_orientation(pose.orientation);
		if (!absolute_angles.update(absolute_orientation)) return;
		current_absolute_pitch_degrees = pitch_degrees(absolute_orientation);
		current_absolute_roll_degrees = horizon_roll_degrees(absolute_orientation);
		// Temporary publication loss does not erase the established origin.
		if (recenter_pending)
		{
			// An upright recovery must not inherit the alternate Euler yaw. Keep
			// recenter translation aligned with horizontal forward (not fused yaw,
			// which couples simultaneous pitch/roll). Over-pitch retains its branch.
			auto reference_heading = absolute_angles.yaw;
			if (absolute_orientation[2][2] > 0 &&
				std::hypot(absolute_orientation[0][0], absolute_orientation[0][1]) > .01f)
				reference_heading = std::atan2(absolute_orientation[0][1], absolute_orientation[0][0]) * 57.29577951308232f;
			const auto radians = reference_heading * 0.017453292519943295f;
			const auto c = std::cos(radians), s = std::sin(radians);
			const matrix3 reference_yaw{{{c, 0, s}, {0, 1, 0}, {-s, 0, c}}};
			reference_pose = {pose.position_meters, reference_yaw};
			local_angles = {absolute_angles.pitch, 0, absolute_angles.roll};
			local_heading = {};
			reference_absolute_pitch_degrees = current_absolute_pitch_degrees;
			reference_absolute_roll_degrees = current_absolute_roll_degrees;
			recenter_pending = false;
			++recenter_count;
		}

		const auto now=sampled_at==std::chrono::steady_clock::time_point{}?std::chrono::steady_clock::now():sampled_at;
		const auto epoch=stabilization::epoch.load();
		if(epoch!=filter_epoch){head_filter.reset();filter_epoch=epoch;}
		const float amount=stabilization::gameplay.load()?stabilization::settings().head.amount():0.f;
		const pose_filter::pose raw{pose.position_meters,pose.orientation};
		const auto filtered=head_filter.update(raw,sample_id?sample_id:pose_publications,recenter_count,now,amount,pose_filter::head);
		head_stabilized=std::isfinite(amount) && amount>0;
		tracking_correction=head_stabilized?pose_filter::correction(raw,filtered):pose_filter::pose{};
		const auto& effective_position=head_stabilized?filtered.position:pose.position_meters;
		const auto& effective_orientation=head_stabilized?filtered.orientation:pose.orientation;
		const auto inverse_reference = transpose(reference_pose.orientation);
		const vector3 tracking_delta{
			effective_position[0] - reference_pose.position_meters[0],
			effective_position[1] - reference_pose.position_meters[1],
			effective_position[2] - reference_pose.position_meters[2],
		};
		const auto reference_local_delta = multiply(inverse_reference, tracking_delta);
		local_position_units = tracking_to_h2(reference_local_delta);
		for (auto& element : local_position_units)
		{
			element *= world_scale;
		}
		local_orientation = h2_orientation(multiply(inverse_reference, effective_orientation));
		(void)local_angles.update(local_orientation);
		(void)local_heading.update(local_orientation);
		local_roll_degrees = horizon_roll_degrees(local_orientation);
		update_peak(local_roll_degrees, max_abs_local_roll_degrees);
		update_range(local_roll_degrees, min_local_roll_degrees,
			max_local_roll_degrees);
		pose_available = true;
		pose_sampled_at = now;
		camera_applied = false;
	}

	void request_recenter(bool reset_view) noexcept
	{
		const std::lock_guard lock(state_mutex);
		stabilization::invalidate();
		recenter_pending = true;
		if(reset_view){game_view_state={};remote_view_state={};scripted_camera={};}
		invalidate_locked();
	}

	void invalidate_pose() noexcept
	{
		const std::lock_guard lock(state_mutex);
		invalidate_locked();
	}

	void reset() noexcept
	{
		const std::lock_guard lock(state_mutex);
		head_filter.reset();tracking_correction={};head_stabilized=false;
		enabled = false;
		pose_available = false;
		recenter_pending = true;
		camera_applied = false;
		world_scale = default_world_scale;
		reference_pose = {};
		camera_frame_available = false;
		camera_frame = {};
		body_estimator.reset();
		pose_sampled_at = {};
		game_view_state = {};remote_view_state={};
		scripted_camera = {};
		absolute_angles = local_angles = {};
		local_heading = {};
		game_view_applications = game_view_history_misses = 0;
		game_view_yaw_contribution = 0;
		local_position_units = {};
		local_orientation = identity_matrix;
		pose_publications = 0;
		camera_applications = 0;
		recenter_count = 0;
		invalid_pose_count = 0;
		composition_failure_count = 0;
		yaw_from_left_count = 0;
		yaw_extraction_failure_count = 0;
		local_roll_degrees = 0.0f;
		reference_absolute_pitch_degrees = 0.0f;
		reference_absolute_roll_degrees = 0.0f;
		current_absolute_pitch_degrees = 0.0f;
		current_absolute_roll_degrees = 0.0f;
		input_pitch_degrees = 0.0f;
		base_yaw_degrees = 0.0f;
		output_pitch_degrees = 0.0f;
		input_horizon_roll_degrees = 0.0f;
		output_horizon_roll_degrees = 0.0f;
		max_abs_local_roll_degrees = 0.0f;
		max_abs_input_horizon_roll_degrees = 0.0f;
		max_abs_output_horizon_roll_degrees = 0.0f;
		min_local_roll_degrees = 0.0f;
		max_local_roll_degrees = 0.0f;
		min_input_horizon_roll_degrees = 0.0f;
		max_input_horizon_roll_degrees = 0.0f;
		min_output_horizon_roll_degrees = 0.0f;
		max_output_horizon_roll_degrees = 0.0f;
		input_basis_error = 0.0f;
		output_basis_error = 0.0f;
		max_output_basis_error = 0.0f;
	}

	void reset_game_view() noexcept
	{
		const std::lock_guard lock(state_mutex);
		stabilization::invalidate();
		head_filter.reset();tracking_correction={};head_stabilized=false;
		game_view_state={};remote_view_state={};scripted_camera={};camera_frame_available=false;camera_applied=false;
		body_estimator.reset();
	}

	bool apply_game_view(float* const native_angles, const float* const delta_angles, const std::uint64_t scripted_epoch, const bool remote) noexcept
	{
		if (!native_angles || !delta_angles) return false;
		const std::lock_guard lock(state_mutex);
		const auto now = std::chrono::steady_clock::now();
		if (!target_matched || !enabled || !pose_available || recenter_pending ||
			pose_sampled_at > now || now - pose_sampled_at > std::chrono::milliseconds(150)) {remote_view_state.suspend();return false;}
		if(remote)return remote_view_state.apply(native_angles[0],native_angles[1],local_angles,scripted_epoch,recenter_count,
			std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
		if(remote_view_state.active())
		{
			// Native UAV cleanup restores the player's original facing. Do not
			// hand back the missile's heading or replay remote head deltas on foot.
			remote_view_state={};scripted_camera={};
			game_view_state.rebase(recenter_count,local_angles.yaw,local_heading.yaw);
		}
		// Script camera constraints must never feed HMD yaw back into its anchor.
		if (scripted_epoch) return false;
		if (scripted_camera.restore_command(scripted_epoch,recenter_count,native_angles[1],delta_angles[1],local_heading.yaw,local_angles.yaw,local_orientation))
			game_view_state.rebase(recenter_count,local_angles.yaw,local_heading.yaw);
		if (!game_view_state.apply(native_angles[0], native_angles[1], delta_angles[0],
			local_angles.pitch, local_angles.yaw, recenter_count,local_heading.yaw)) return false;
		++game_view_applications;
		return true;
	}

	void suspend_remote_view()noexcept
	{const std::lock_guard lock(state_mutex);remote_view_state.suspend();}
	bool apply_remote_control(std::array<std::int8_t,2>& command,std::array<float,2> rates,
		std::array<float,2> stick,int command_time,std::uint64_t epoch)noexcept
	{
		const std::lock_guard lock(state_mutex);const auto now=std::chrono::steady_clock::now();
		if(!target_matched || !enabled || !pose_available || recenter_pending || now<pose_sampled_at || now-pose_sampled_at>std::chrono::milliseconds(150))
		{remote_view_state.suspend();return false;}
		return remote_view_state.apply_control(command,local_angles,epoch,recenter_count,
			std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count(),command_time,rates,stick);
	}
	void record_game_command(const int command_time, const int packed_pitch, const bool tracked) noexcept
	{
		const std::lock_guard lock(state_mutex);
		game_view_state.record(command_time, packed_pitch, tracked);
		remote_view_state.record(command_time);
		if (tracked) scripted_camera.record(command_time);
	}

	bool resolve_game_pitch(const int command_time, const int packed_pitch,
		const float delta_pitch, float& pitch) noexcept
	{
		const std::lock_guard lock(state_mutex);
		return target_matched && enabled && pose_available && !recenter_pending &&
			game_view_state.resolve_pitch(command_time, packed_pitch, recenter_count, delta_pitch, pitch);
	}

	bool apply_camera(float* const origin, float (*const axis)[3], const int command_time,
		const float* const native_angles,const game_view::camera_request& request,
		const game_view::scripted_rotation_reference* rotation_reference) noexcept
	{
		if (origin == nullptr || axis == nullptr)
		{
			return false;
		}

		const std::lock_guard lock(state_mutex);
		if (!target_matched || !enabled || !pose_available)
		{
			camera_applied = false;
			return false;
		}

		matrix3 original_axis{};
		for (std::size_t row{}; row < 3; ++row)
		{
			for (std::size_t column{}; column < 3; ++column)
			{
				original_axis[row][column] = axis[row][column];
			}
		}
		const vector3 original_origin{origin[0], origin[1], origin[2]};
		if (!finite_vector(original_origin) || !valid_rotation(original_axis))
		{
			camera_applied = false;
			return false;
		}
		matrix3 base_yaw_axis{};
		bool used_left_axis{};
		float extracted_yaw_degrees{};
		if (native_angles && std::isfinite(native_angles[1]))
			extracted_yaw_degrees = native_angles[1];
		else if (!extract_h2_yaw_only(original_axis, base_yaw_axis,
			extracted_yaw_degrees, used_left_axis))
		{
			camera_applied = false;
			++yaw_extraction_failure_count;
			return false;
		}
		if (used_left_axis) ++yaw_from_left_count;
		vector3 head_meters{};for(unsigned i=0;i<3;++i)head_meters[i]=local_position_units[i]/world_scale;
		const auto scripted_epoch=request.policy.owns_rotation()?request.epoch:0;
		float command_head_yaw{};
		if (!game_view_state.resolve(command_time, recenter_count, command_head_yaw) &&
			!scripted_epoch && !scripted_camera.owns_camera())
		{
			camera_applied = false;camera_frame_available = false;
			++game_view_history_misses;return false;
		}
		game_view_yaw_contribution = command_head_yaw;
		const auto head_axis=request.policy.head==game_view::head_rotation::command_delta ?
			remote_view_state.correction(request.epoch,recenter_count,command_time,local_orientation):local_orientation;
		const auto composed=scripted_camera.compose({original_axis,head_axis,head_meters,
			extracted_yaw_degrees,local_heading.yaw,command_head_yaw,command_time,recenter_count},request,rotation_reference);
		extracted_yaw_degrees=composed.base_heading;base_yaw_axis=composed.base_axis;
		auto head_offset=composed.head_offset;for(auto& x:head_offset)x*=world_scale;
		vector3 composed_origin=original_origin;
		for(unsigned world_axis=0;world_axis<3;++world_axis)for(unsigned i=0;i<3;++i)
			composed_origin[world_axis]+=head_offset[i]*base_yaw_axis[i][world_axis];
		const auto& composed_axis=composed.axis;
		if (!finite_vector(composed_origin) || !valid_rotation(composed_axis))
		{
			camera_applied = false;
			++composition_failure_count;
			return false;
		}

		input_pitch_degrees = pitch_degrees(original_axis);
		base_yaw_degrees = extracted_yaw_degrees;
		output_pitch_degrees = pitch_degrees(composed_axis);
		input_horizon_roll_degrees = horizon_roll_degrees(original_axis);
		output_horizon_roll_degrees = horizon_roll_degrees(composed_axis);
		input_basis_error = basis_error(original_axis);
		output_basis_error = basis_error(composed_axis);
		update_peak(input_horizon_roll_degrees,
			max_abs_input_horizon_roll_degrees);
		update_peak(output_horizon_roll_degrees,
			max_abs_output_horizon_roll_degrees);
		update_range(input_horizon_roll_degrees,
			min_input_horizon_roll_degrees, max_input_horizon_roll_degrees);
		update_range(output_horizon_roll_degrees,
			min_output_horizon_roll_degrees, max_output_horizon_roll_degrees);
		max_output_basis_error = std::max(max_output_basis_error, output_basis_error);
		for (std::size_t component{}; component < 3; ++component)
		{
			origin[component] = composed_origin[component];
			for (std::size_t column{}; column < 3; ++column)
			{
				axis[component][column] = composed_axis[component][column];
			}
		}
		camera_applied = true;
		if(!composed.spatial){camera_frame_available=false;++camera_applications;return true;}
		// Translate the tracking frame as a whole: hands retain 1:1 motion relative
		// to the head instead of drifting away from a scaled cinematic camera.
		auto tracking_origin=original_origin;
		for(unsigned axis=0;axis<3;++axis)for(unsigned i=0;i<3;++i)
			tracking_origin[axis]+=(head_offset[i]-local_position_units[i])*base_yaw_axis[i][axis];
		camera_frame = {reference_pose, tracking_origin, base_yaw_axis, world_scale, recenter_count,
			std::chrono::steady_clock::now()};
		camera_frame.head_position = composed_origin;
		camera_frame.head_forward = composed_axis[0];
		camera_frame.head_up = composed_axis[2];
		camera_frame.tracking_correction=tracking_correction;
		camera_frame.head_stabilized=head_stabilized;
		const auto head_radians = composed.world_heading * 0.017453292519943295f;
		const auto hc = std::cos(head_radians), hs = std::sin(head_radians);
		camera_frame.head_yaw_axis = {{{hc, hs, 0}, {-hs, hc, 0}, {0, 0, 1}}};
		const auto estimated=body_estimator.update(head_meters,local_orientation,local_heading.yaw,recenter_count,pose_sampled_at);
		camera_frame.body=body_pose::to_world(estimated,tracking_origin,base_yaw_axis,world_scale);
		camera_frame_available = true;
		++camera_applications;
		return true;
	}

	bool camera_was_applied() noexcept
	{
		const std::lock_guard lock(state_mutex);
		return camera_applied;
	}

	bool get_spatial_frame(spatial_frame& output) noexcept
	{
		const std::lock_guard lock(state_mutex);
		if (!enabled || !pose_available || recenter_pending || !camera_frame_available ||
			camera_frame.generation != recenter_count ||
			std::chrono::steady_clock::now() - camera_frame.captured_at > std::chrono::milliseconds(150)) return false;
		output = camera_frame;
		return true;
	}
	bool get_tracking_reference(tracking_reference& output) noexcept
	{
		const std::lock_guard lock(state_mutex);
		const auto now=std::chrono::steady_clock::now();
		if(!enabled || !pose_available || recenter_pending || now<pose_sampled_at || now-pose_sampled_at>std::chrono::milliseconds(150))return false;
		output={reference_pose,local_orientation,recenter_count};
		for(unsigned i=0;i<3;++i)output.head_position_meters[i]=local_position_units[i]/world_scale;
		return true;
	}

	bool tracking_to_world(const spatial_frame& frame, const tracking_pose& pose, world_pose& output) noexcept
	{
		if (!finite_vector(pose.position_meters) || !valid_rotation(pose.orientation) ||
			!finite_vector(frame.reference.position_meters) || !valid_rotation(frame.reference.orientation) ||
			!finite_vector(frame.world_origin) || !valid_rotation(frame.world_yaw_axis) ||
			!std::isfinite(frame.units_per_meter) || frame.units_per_meter <= 0 || frame.units_per_meter > 10000) return false;
		const auto corrected=frame.head_stabilized ? pose_filter::compose(frame.tracking_correction,
			{pose.position_meters,pose.orientation}) : pose_filter::pose{pose.position_meters,pose.orientation};
		const auto inverse = transpose(frame.reference.orientation);
		vector3 delta{};
		for (std::size_t i = 0; i < 3; ++i) delta[i] = corrected.position[i] - frame.reference.position_meters[i];
		const auto local = tracking_to_h2(multiply(inverse, delta));
		const auto orientation = h2_orientation(multiply(inverse, corrected.orientation));
		world_pose result{};
		result.position = frame.world_origin;
		for (std::size_t world = 0; world < 3; ++world)
		{
			for (std::size_t i = 0; i < 3; ++i)
			{
				result.position[world] += local[i] * frame.units_per_meter * frame.world_yaw_axis[i][world];
				for (std::size_t row = 0; row < 3; ++row)
					result.axis[row][world] += orientation[row][i] * frame.world_yaw_axis[i][world];
			}
		}
		if (!finite_vector(result.position) || !valid_rotation(result.axis)) return false;
		output = result;
		return true;
	}

	status get_status() noexcept
	{
		const std::lock_guard lock(state_mutex);
		return {
			target_matched,
			enabled,
			pose_available,
			recenter_pending,
			camera_applied,
			world_scale,
			local_position_units,
			local_orientation,
			pose_publications,
			camera_applications,
			recenter_count,
			invalid_pose_count,
			composition_failure_count,
			yaw_from_left_count,
			yaw_extraction_failure_count,
			local_roll_degrees,
			reference_absolute_pitch_degrees,
			reference_absolute_roll_degrees,
			current_absolute_pitch_degrees,
			current_absolute_roll_degrees,
			input_pitch_degrees,
			base_yaw_degrees,
			output_pitch_degrees,
			input_horizon_roll_degrees,
			output_horizon_roll_degrees,
			max_abs_local_roll_degrees,
			max_abs_input_horizon_roll_degrees,
			max_abs_output_horizon_roll_degrees,
			min_local_roll_degrees,
			max_local_roll_degrees,
			min_input_horizon_roll_degrees,
			max_input_horizon_roll_degrees,
			min_output_horizon_roll_degrees,
			max_output_horizon_roll_degrees,
			input_basis_error,
			output_basis_error,
			max_output_basis_error,
			game_view_applications,
			game_view_history_misses,
			game_view_yaw_contribution,
		};
	}
}
