#include <std_include.hpp>

#include "engine_stereo_view.hpp"
#include "stabilization.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace vr::engine_stereo_view
{
	namespace
	{
		constexpr std::size_t view_matrix_offset = 0x000;
		constexpr std::size_t projection_matrix_offset = 0x040;
		constexpr std::size_t view_projection_matrix_offset = 0x080;
		constexpr std::size_t inverse_view_projection_matrix_offset = 0x0C0;
		constexpr std::size_t origin_offset = 0x100;
		constexpr std::size_t axis_offset = 0x10C;
		constexpr std::size_t tan_half_x_offset = 0x140;
		constexpr std::size_t tan_half_y_offset = 0x144;
		constexpr std::size_t reverse_z_scale_offset = 0x148;
		constexpr std::size_t secondary_tan_half_x_offset = 0x150;
		constexpr std::size_t secondary_tan_half_y_offset = 0x154;
		constexpr std::size_t origin_size = 3 * sizeof(float);
		constexpr float maximum_eye_displacement_units = 1000.0f;
		// Keep shoulder-held geometry visible without moving an already closer
		// native near plane outward. Shared by rendered eyes and visibility.
		constexpr float maximum_vr_near_distance_units = 1.0f;
		// Culling-only screen-right margin in tangent space; optical FOV stays exact.
		constexpr float right_culling_tangent_margin = 0.1f;
		static_assert(origin_offset == h2_view_origin_offset);
		static_assert(h2_relative_eye_offset_offset + origin_size <=
			h2_scene_record_size);
		static_assert(view_projection_matrix_offset ==
			h2_current_view_projection_offset);
		static_assert(h2_temporal_history_view_projection_offset +
			sizeof(float) * 16 <= h2_scene_record_size);
		static_assert(h2_previous_view_projection_constant_offset +
			sizeof(float) * 16 <= h2_scene_record_size);
		static_assert(h2_previous_eye_position_constant_offset +
			sizeof(float) * 4 <= h2_scene_record_size);
		static_assert(h2_inverse_scene_projection_constant_offset +
			sizeof(float) * 4 <= h2_scene_record_size);
		static_assert(h2_draw_list_descriptor_base +
			h2_draw_list_descriptor_count * h2_draw_list_descriptor_size <=
			h2_scene_record_size);

		float read_float(const std::array<std::uint8_t, h2_view_slot_size>& bytes,
			const std::size_t offset) noexcept
		{
			float value{};
			std::memcpy(&value, bytes.data() + offset, sizeof(value));
			return value;
		}

		float vr_near_distance(const std::array<std::uint8_t, h2_view_slot_size>& bytes,float half_ipd) noexcept
		{
			const auto native = read_float(bytes, reverse_z_scale_offset);
			// Scripted cameras can request 0.01 units. At finite IPD that would
			// require a nearly 180-degree shared frustum and reject the stereo pair.
			// Keep ordinary close planes, but bound this singular stereo ratio.
			if(!std::isfinite(native) || native<=0)return native;
			const float minimum=half_ipd/8.f;
			return std::max(minimum,std::min(native,maximum_vr_near_distance_units));
		}

		void write_float(std::array<std::uint8_t, h2_view_slot_size>& bytes,
			const std::size_t offset, const float value) noexcept
		{
			std::memcpy(bytes.data() + offset, &value, sizeof(value));
		}

		bool finite_range(const std::array<std::uint8_t, h2_view_slot_size>& bytes,
			const std::size_t offset, const std::size_t count) noexcept
		{
			for (std::size_t index{}; index < count; ++index)
			{
				if (!std::isfinite(read_float(bytes, offset + index * sizeof(float))))
				{
					return false;
				}
			}
			return true;
		}

		bool coherent_configs(
			const std::array<engine_stereo_bridge::render_config, 2>& configs) noexcept
		{
			if (configs[0].pair_id == 0 || configs[0].pair_id != configs[1].pair_id ||
				configs[0].frame_id != configs[1].frame_id ||
				configs[0].publication != configs[1].publication ||
				configs[0].output_eye != 0 || configs[1].output_eye != 1 ||
				configs[0].view_eye >= 2 || configs[1].view_eye >= 2 ||
				configs[0].view_eye == configs[1].view_eye ||
				configs[0].half_eye_offset_units != configs[1].half_eye_offset_units ||
				!std::isfinite(configs[0].half_eye_offset_units) ||
				configs[0].half_eye_offset_units <= 0.0f ||
				configs[0].half_eye_offset_units > 1000.0f)
			{
				return false;
			}

			for (const auto& config : configs)
			{
				const auto& projection = config.eyes[config.view_eye];
				const auto width = projection.tan_right - projection.tan_left;
				const auto height = projection.tan_up - projection.tan_down;
				if (!std::isfinite(projection.tan_left) ||
					!std::isfinite(projection.tan_right) ||
					!std::isfinite(projection.tan_down) ||
					!std::isfinite(projection.tan_up) ||
					projection.tan_left >= 0.0f || projection.tan_right <= 0.0f ||
					projection.tan_down >= 0.0f || projection.tan_up <= 0.0f ||
					width < 0.2f || width > 20.0f || height < 0.2f || height > 20.0f)
				{
					return false;
				}
			}
			return true;
		}

		bool valid_axis(const std::array<std::uint8_t, h2_view_slot_size>& bytes) noexcept
		{
			std::array<std::array<float, 3>, 3> axis{};
			for (std::size_t row{}; row < axis.size(); ++row)
			{
				for (std::size_t column{}; column < axis[row].size(); ++column)
				{
					axis[row][column] = read_float(bytes,
						axis_offset + (row * 3 + column) * sizeof(float));
				}
				const auto length_squared = axis[row][0] * axis[row][0] +
					axis[row][1] * axis[row][1] + axis[row][2] * axis[row][2];
				if (!std::isfinite(length_squared) || length_squared < 0.81f ||
					length_squared > 1.21f)
				{
					return false;
				}
			}
			for (std::size_t left{}; left < axis.size(); ++left)
			{
				for (std::size_t right = left + 1; right < axis.size(); ++right)
				{
					const auto dot = axis[left][0] * axis[right][0] +
						axis[left][1] * axis[right][1] +
						axis[left][2] * axis[right][2];
					if (!std::isfinite(dot) || std::abs(dot) > 0.1f) return false;
				}
			}
			return true;
		}

		void rebuild_view_rotation(std::array<std::uint8_t, h2_view_slot_size>& bytes) noexcept
		{
			const std::array<float, 3> forward{
				read_float(bytes, axis_offset + 0x00),
				read_float(bytes, axis_offset + 0x04),
				read_float(bytes, axis_offset + 0x08),
			};
			const std::array<float, 3> right{
				read_float(bytes, axis_offset + 0x0C),
				read_float(bytes, axis_offset + 0x10),
				read_float(bytes, axis_offset + 0x14),
			};
			const std::array<float, 3> up{
				read_float(bytes, axis_offset + 0x18),
				read_float(bytes, axis_offset + 0x1C),
				read_float(bytes, axis_offset + 0x20),
			};
			std::array<float, 16> matrix{
				-right[0], up[0], forward[0], 0.0f,
				-right[1], up[1], forward[1], 0.0f,
				-right[2], up[2], forward[2], 0.0f,
				0.0f, 0.0f, 0.0f, 1.0f,
			};
			std::memcpy(bytes.data() + view_matrix_offset, matrix.data(), sizeof(matrix));
		}

		bool write_projection(std::array<std::uint8_t, h2_view_slot_size>& bytes,
			const float tan_left, const float tan_right, const float tan_down,
			const float tan_up, const float reverse_z_scale) noexcept
		{
			const auto width = tan_right - tan_left;
			const auto height = tan_up - tan_down;
			if (!std::isfinite(tan_left) || !std::isfinite(tan_right) ||
				!std::isfinite(tan_down) || !std::isfinite(tan_up) ||
				!std::isfinite(reverse_z_scale) || reverse_z_scale <= 0.0f ||
				tan_left >= 0.0f || tan_right <= 0.0f || tan_down >= 0.0f ||
				tan_up <= 0.0f || width < 0.2f || width > 40.0f ||
				height < 0.2f || height > 40.0f)
			{
				return false;
			}

			// H2 uses +Z forward, row vectors, and an infinite reverse-Z projection.
			// The off-axis terms are the +Z counterparts of OpenXR's canonical
			// D3D projection. H2's finalizer applies temporal jitter exactly once.
			std::array<float, 16> matrix{};
			matrix[0] = 2.0f / width;
			matrix[5] = 2.0f / height;
			matrix[8] = -(tan_right + tan_left) / width;
			matrix[9] = -(tan_up + tan_down) / height;
			matrix[11] = 1.0f;
			matrix[14] = reverse_z_scale;
			std::memcpy(bytes.data() + projection_matrix_offset,
				matrix.data(), sizeof(matrix));

			// Native Umbra (2AF550) and near-frustum consumers (779FA0)
			// reconstruct a symmetric frustum from these legacy scalars.
			// Half the off-axis width/height clips its longer side.
			const auto half_width = std::max(-tan_left, tan_right);
			const auto half_height = std::max(-tan_down, tan_up);
			write_float(bytes, tan_half_x_offset, half_width);
			write_float(bytes, tan_half_y_offset, half_height);
			write_float(bytes, secondary_tan_half_x_offset, half_width);
			write_float(bytes, secondary_tan_half_y_offset, half_height);
			write_float(bytes, reverse_z_scale_offset, reverse_z_scale);
			return true;
		}

		bool derive_eye(const std::array<std::uint8_t, h2_view_slot_size>& natural,
			const engine_stereo_bridge::render_config& config, eye_slot& output) noexcept
		{
			output = {};
			output.bytes = natural;
			output.pair_id = config.pair_id;
			output.publication = config.publication;
			output.output_eye = config.output_eye;
			output.view_eye = config.view_eye;

			// H2 viewaxis[1] points left. Eye 0 therefore moves along the positive
			// axis and eye 1 along the negative axis.
			const auto direction = config.view_eye == 0 ? 1.0f : -1.0f;
			const auto eye_offset = direction * config.half_eye_offset_units;
			for (std::size_t component{}; component < 3; ++component)
			{
				const auto origin = read_float(output.bytes,
					origin_offset + component * sizeof(float));
				const auto left = read_float(output.bytes,
					axis_offset + 0x0C + component * sizeof(float));
				write_float(output.bytes, origin_offset + component * sizeof(float),
					origin + left * eye_offset);
			}
			rebuild_view_rotation(output.bytes);

			const auto& projection = config.eyes[config.view_eye];
			const auto reverse_z_scale = vr_near_distance(natural,config.half_eye_offset_units);
			return write_projection(output.bytes, projection.tan_left,
				projection.tan_right, projection.tan_down, projection.tan_up,
				reverse_z_scale);
		}
	}

	bool derive(const void* const natural_slot,
		const std::array<engine_stereo_bridge::render_config, 2>& configs,
		slot_pair& output) noexcept
	{
		output = {};
		if (natural_slot == nullptr || !coherent_configs(configs)) return false;
		std::array<std::uint8_t, h2_view_slot_size> natural{};
		std::memcpy(natural.data(), natural_slot, natural.size());
		if (!finite_range(natural, origin_offset, 3) || !valid_axis(natural)) return false;
		std::memcpy(output.natural_camera.data(), natural.data() + origin_offset,
			sizeof(output.natural_camera));
		output.camera_sampled_at=std::chrono::steady_clock::now();
		output.stabilization_epoch=stabilization::epoch.load();
		for (std::size_t eye{}; eye < output.eyes.size(); ++eye)
		{
			if (!derive_eye(natural, configs[eye], output.eyes[eye]))
			{
				output = {};
				return false;
			}
		}
		return validate_derived(output);
	}

	bool derive_culling_union(const void* const natural_slot,
		const std::array<engine_stereo_bridge::render_config, 2>& configs,
		culling_union_slot& output,float screen_scope_aspect) noexcept
	{
		output = {};
		if (natural_slot == nullptr || !coherent_configs(configs)) return false;
		std::memcpy(output.bytes.data(), natural_slot, output.bytes.size());
		if (!finite_range(output.bytes, origin_offset, 3) ||
			!valid_axis(output.bytes)) return false;

		if(!std::isfinite(screen_scope_aspect) || screen_scope_aspect<0 || screen_scope_aspect>32)return false;
		const auto reverse_z_scale = screen_scope_aspect>0 ? read_float(output.bytes,reverse_z_scale_offset) :
			vr_near_distance(output.bytes,configs[0].half_eye_offset_units);
		const auto half_ipd = configs[0].half_eye_offset_units;
		if (!std::isfinite(reverse_z_scale) || reverse_z_scale <= 0.0f ||
			!std::isfinite(half_ipd) || half_ipd <= 0.0f) return false;

		const auto& first = configs[0].eyes[configs[0].view_eye];
		const auto& second = configs[1].eyes[configs[1].view_eye];
		output.near_distance_units = reverse_z_scale;
		output.horizontal_origin_expansion = half_ipd / reverse_z_scale;
		if (!std::isfinite(output.horizontal_origin_expansion) ||
			output.horizontal_origin_expansion <= 0.0f ||
			output.horizontal_origin_expansion > 20.0f) return false;

		// At center-forward distance z, the left eye's leftmost point is
		// -halfIPD + z*tanLeft and the right eye's rightmost point is
		// +halfIPD + z*tanRight. halfIPD/z is largest at the reverse-Z near
		// plane, so expanding by halfIPD/near encloses both translated frusta
		// for the complete visible depth range, including near geometry.
		output.tan_left = std::min(first.tan_left, second.tan_left) -
			output.horizontal_origin_expansion;
		output.tan_right = std::max(first.tan_right, second.tan_right) +
			output.horizontal_origin_expansion + right_culling_tangent_margin;
		output.tan_down = std::min(first.tan_down, second.tan_down);
		output.tan_up = std::max(first.tan_up, second.tan_up);
		if(screen_scope_aspect>0)
		{
			// This scene is completely covered by the fixed scope compositor. Its
			// sole visible world consumer is the native narrow optical view. Keep
			// native near clipping and a conservative envelope of its optical rays;
			// collecting unseen wide-angle geometry can overflow native SModel lists.
			const float y=read_float(output.bytes,tan_half_y_offset),x=y*screen_scope_aspect;
			if(!std::isfinite(x) || x<=0 || !std::isfinite(y) || y<=0)return false;
			output.tan_left=-std::max(.1f,x)-output.horizontal_origin_expansion;
			output.tan_right=std::max(.1f,x)+output.horizontal_origin_expansion+right_culling_tangent_margin;
			output.tan_down=-std::max(.1f,y);output.tan_up=std::max(.1f,y);
		}
		rebuild_view_rotation(output.bytes);
		if (!write_projection(output.bytes, output.tan_left, output.tan_right,
			output.tan_down, output.tan_up, reverse_z_scale))
		{
			output = {};
			return false;
		}
		return true;
	}

	bool apply_fx_culling_union(void* const camera,
		const std::array<engine_stereo_bridge::render_config, 2>& configs) noexcept
	{
		if (!camera || !coherent_configs(configs)) return false;
		auto* const bytes = static_cast<std::uint8_t*>(camera);
		std::uint32_t valid{}, count{};
		std::memcpy(&valid, bytes + 0x0C, sizeof(valid));
		std::memcpy(&count, bytes + 0x94, sizeof(count));
		if (valid != 1 || (count != 5 && count != 6)) return false;

		std::array<std::uint8_t, h2_view_slot_size> basis{};
		std::memcpy(basis.data() + origin_offset, bytes, 3 * sizeof(float));
		std::memcpy(basis.data() + axis_offset, bytes + 0x70, 9 * sizeof(float));
		if (!finite_range(basis, origin_offset, 3) || !valid_axis(basis)) return false;
		const auto& a = configs[0].eyes[configs[0].view_eye];
		const auto& b = configs[1].eyes[configs[1].view_eye];
		// Screen right is -H2 axis[1]; screen up is +axis[2]. Preserve H2's
		// plane order: right edge, left edge, bottom edge, top edge.
		const std::array<float, 4> slopes{
			std::max(a.tan_right, b.tan_right) + right_culling_tangent_margin,
			-std::min(a.tan_left, b.tan_left),
			-std::min(a.tan_down, b.tan_down), std::max(a.tan_up, b.tan_up)};
		std::array<std::array<float, 4>, 4> planes{};
		for (std::size_t plane{}; plane < planes.size(); ++plane)
		{
			const auto lateral_axis = plane < 2 ? 1U : 2U;
			const auto sign = (plane % 2 == 0) ? 1.0 : -1.0;
			std::array<double, 3> normal{};
			double length_squared{};
			for (std::size_t c{}; c < 3; ++c)
			{
				normal[c] = slopes[plane] * static_cast<double>(read_float(basis,
					axis_offset + c * 4)) + sign * read_float(basis,
					axis_offset + (lateral_axis * 3 + c) * 4);
				length_squared += normal[c] * normal[c];
			}
			if (!std::isfinite(length_squared) || length_squared <= 0.0) return false;
			const auto length = std::sqrt(length_squared);
			double center_distance{}, eye_distance{};
			for (std::size_t c{}; c < 3; ++c)
			{
				const auto n = planes[plane][c] = static_cast<float>(normal[c] / length);
				center_distance += static_cast<double>(n) * read_float(basis, origin_offset + c * 4);
				eye_distance += static_cast<double>(n) * read_float(basis, axis_offset + 12 + c * 4);
			}
			// H2 retains points with n.dot(p) >= D. Supporting the two eye
			// origins requires the smaller D, not a center-origin plane and not
			// an arbitrary FOV multiplier. This also covers near-eye particles.
			const auto distance = static_cast<float>(center_distance -
				std::abs(eye_distance) * configs[0].half_eye_offset_units);
			if (!std::isfinite(distance)) return false;
			planes[plane][3] = std::nextafter(distance, -std::numeric_limits<float>::infinity());
			if (!std::isfinite(planes[plane][3])) return false;
		}
		std::memcpy(bytes + 0x20, planes.data(), sizeof(planes));
		return true;
	}

	bool validate_derived(const slot_pair& value) noexcept
	{
		for (const auto component : value.natural_camera)
		{
			if (!std::isfinite(component)) return false;
		}
		for (std::size_t eye{}; eye < value.eyes.size(); ++eye)
		{
			const auto& slot = value.eyes[eye];
			if (slot.pair_id == 0 || slot.output_eye != eye || slot.view_eye >= 2 ||
				!finite_range(slot.bytes, view_matrix_offset, 16) ||
				!finite_range(slot.bytes, projection_matrix_offset, 16) ||
				!finite_range(slot.bytes, origin_offset, 3) || !valid_axis(slot.bytes))
			{
				return false;
			}
			const auto projection_x = read_float(slot.bytes, projection_matrix_offset);
			const auto projection_y = read_float(slot.bytes,
				projection_matrix_offset + 5 * sizeof(float));
			const auto perspective = read_float(slot.bytes,
				projection_matrix_offset + 11 * sizeof(float));
			const auto reverse_z = read_float(slot.bytes,
				projection_matrix_offset + 14 * sizeof(float));
			if (projection_x <= 0.0f || projection_y <= 0.0f || perspective != 1.0f ||
				reverse_z <= 0.0f)
			{
				return false;
			}
		}
		if (value.eyes[0].pair_id != value.eyes[1].pair_id ||
			value.eyes[0].publication != value.eyes[1].publication ||
			value.eyes[0].view_eye == value.eyes[1].view_eye)
		{
			return false;
		}
		return true;
	}

	bool validate_finalized(const slot_pair& value) noexcept
	{
		if (!validate_derived(value)) return false;
		for (const auto& slot : value.eyes)
		{
			if (!finite_range(slot.bytes, view_projection_matrix_offset, 16) ||
				!finite_range(slot.bytes, inverse_view_projection_matrix_offset, 16))
			{
				return false;
			}
			float view_projection_energy{};
			float inverse_energy{};
			for (std::size_t index{}; index < 16; ++index)
			{
				view_projection_energy += std::abs(read_float(slot.bytes,
					view_projection_matrix_offset + index * sizeof(float)));
				inverse_energy += std::abs(read_float(slot.bytes,
					inverse_view_projection_matrix_offset + index * sizeof(float)));
			}
			if (view_projection_energy < 0.1f || inverse_energy < 0.1f) return false;
		}
		// Before H2's finalizer runs these two ranges are still identical copies of
		// the natural center-eye slot. A successful stereo finalization must make
		// both the combined and inverse matrices eye-local.
		if (std::memcmp(value.eyes[0].bytes.data() + view_projection_matrix_offset,
			value.eyes[1].bytes.data() + view_projection_matrix_offset,
			16 * sizeof(float)) == 0 ||
			std::memcmp(value.eyes[0].bytes.data() + inverse_view_projection_matrix_offset,
				value.eyes[1].bytes.data() + inverse_view_projection_matrix_offset,
				16 * sizeof(float)) == 0)
		{
			return false;
		}
		return true;
	}

	bool read_projection(const eye_slot& slot, engine_stereo_bridge::eye_projection& output) noexcept
	{
		output = {};
		if (!slot.pair_id || !slot.publication || slot.output_eye >= 2 || slot.view_eye >= 2 ||
			!finite_range(slot.bytes, projection_matrix_offset, 16)) return false;
		std::array<float, 16> matrix{};
		std::memcpy(matrix.data(), slot.bytes.data() + projection_matrix_offset, sizeof(matrix));
		// Only the native axis-aligned +Z-forward perspective form is supported.
		if (matrix[0] <= 0 || matrix[5] <= 0 || matrix[11] != 1.0f || matrix[14] <= 0 ||
			matrix[1] != 0 || matrix[2] != 0 || matrix[3] != 0 || matrix[4] != 0 ||
			matrix[6] != 0 || matrix[7] != 0 || matrix[12] != 0 || matrix[13] != 0 || matrix[15] != 0)
			return false;
		const engine_stereo_bridge::eye_projection result{
			(-1.0f - matrix[8]) / matrix[0], (1.0f - matrix[8]) / matrix[0],
			(-1.0f - matrix[9]) / matrix[5], (1.0f - matrix[9]) / matrix[5]};
		if (!std::isfinite(result.tan_left) || !std::isfinite(result.tan_right) ||
			!std::isfinite(result.tan_down) || !std::isfinite(result.tan_up) ||
			result.tan_left >= 0 || result.tan_right <= 0 || result.tan_down >= 0 || result.tan_up <= 0)
			return false;
		output = result;
		return true;
	}

	bool validate_finalized_culling_union(const culling_union_slot& value) noexcept
	{
		if (!finite_range(value.bytes, view_matrix_offset, 16) ||
			!finite_range(value.bytes, projection_matrix_offset, 16) ||
			!finite_range(value.bytes, view_projection_matrix_offset, 16) ||
			!finite_range(value.bytes, inverse_view_projection_matrix_offset, 16) ||
			!finite_range(value.bytes, origin_offset, 3) || !valid_axis(value.bytes) ||
			!std::isfinite(value.tan_left) || !std::isfinite(value.tan_right) ||
			!std::isfinite(value.tan_down) || !std::isfinite(value.tan_up) ||
			!std::isfinite(value.near_distance_units) ||
			!std::isfinite(value.horizontal_origin_expansion) ||
			value.tan_left >= 0.0f || value.tan_right <= 0.0f ||
			value.tan_down >= 0.0f || value.tan_up <= 0.0f ||
			value.near_distance_units <= 0.0f ||
			value.horizontal_origin_expansion <= 0.0f)
		{
			return false;
		}
		float combined_energy{};
		float inverse_energy{};
		for (std::size_t index{}; index < 16; ++index)
		{
			combined_energy += std::abs(read_float(value.bytes,
				view_projection_matrix_offset + index * sizeof(float)));
			inverse_energy += std::abs(read_float(value.bytes,
				inverse_view_projection_matrix_offset + index * sizeof(float)));
		}
		return combined_energy >= 0.1f && inverse_energy >= 0.1f;
	}

	bool clone_scene_records(const void* const natural_record,
		const slot_pair& views, scene_record_pair& output) noexcept
	{
		output = {};
		if (natural_record == nullptr || !validate_finalized(views)) return false;
		std::memcpy(output.left.data(), natural_record, output.left.size());
		if (std::memcmp(output.left.data() + h2_view_origin_offset,
			views.natural_camera.data(), sizeof(views.natural_camera)) != 0)
		{
			output = {};
			return false;
		}
		// Both eye payloads come from the same CPU snapshot, not two independent
		// reads of a reusable arena. A matching address alone is not scene identity.
		output.right = output.left;
		std::memcpy(output.left.data(), views.eyes[0].bytes.data(), h2_view_slot_size);
		std::memcpy(output.right.data(), views.eyes[1].bytes.data(), h2_view_slot_size);

		// 0x14078B6B7..0x14078B701 copies record+0x2CC0+0x100 to the
		// backend eyeOffset used by rigid/skinned XModels. Natural H2 records prove
		// that this value is relative to a frontend rebase, while the leading origin
		// is absolute world space. Preserve the natural rebase and add only the
		// derived eye displacement; copying the absolute eye origin here would make
		// XModels subtract the world camera position a second time.
		const auto* const natural = output.left.data();
		std::array<float, 3> natural_rebase_origin{};
		std::memcpy(natural_rebase_origin.data(), natural + h2_relative_eye_offset_offset,
			sizeof(natural_rebase_origin));
		float displacement_energy[2]{};
		for (std::size_t eye{}; eye < 2; ++eye)
		{
			auto& record = eye == 0 ? output.left : output.right;
			// H2 0x14077EFE7..0x14077F02B derives this independent shader
			// constant from the finalized projection. Post-opaque fog reconstructs
			// view rays as (clip.xy * value.xz + value.yw) * linearDepth;
			// replacing only the view slot leaves a mono/FOV-mismatched ray field.
			// Use this eye's final matrix, including asymmetric center and jitter.
			const auto& slot = views.eyes[eye].bytes;
			const auto scale_x = read_float(slot, projection_matrix_offset);
			const auto scale_y = read_float(slot, projection_matrix_offset + 5 * sizeof(float));
			if (scale_x == 0.0f || scale_y == 0.0f)
			{
				output = {};
				return false;
			}
			const std::array<float, 4> inverse_projection{
				1.0f / scale_x,
				-read_float(slot, projection_matrix_offset + 8 * sizeof(float)) / scale_x,
				1.0f / scale_y,
				-read_float(slot, projection_matrix_offset + 9 * sizeof(float)) / scale_y,
			};
			for (const auto value : inverse_projection)
			{
				if (!std::isfinite(value))
				{
					output = {};
					return false;
				}
			}
			std::memcpy(record.data() + h2_inverse_scene_projection_constant_offset,
				inverse_projection.data(), sizeof(inverse_projection));
			for (std::size_t component{}; component < 3; ++component)
			{
				const auto natural_primary = views.natural_camera[component];
				const auto natural_rebase = natural_rebase_origin[component];
				float eye_primary{};
				std::memcpy(&eye_primary, views.eyes[eye].bytes.data() +
					h2_view_origin_offset + component * sizeof(float), sizeof(float));
				const auto displacement = eye_primary - natural_primary;
				const auto relative_eye = natural_rebase + displacement;
				if (!std::isfinite(natural_primary) || !std::isfinite(natural_rebase) ||
					!std::isfinite(eye_primary) || !std::isfinite(displacement) ||
					!std::isfinite(relative_eye))
				{
					output = {};
					return false;
				}
				displacement_energy[eye] += displacement * displacement;
				std::memcpy(record.data() + h2_relative_eye_offset_offset +
					component * sizeof(float), &relative_eye, sizeof(float));
			}
		}
		const auto maximum_energy = maximum_eye_displacement_units *
			maximum_eye_displacement_units;
		if (displacement_energy[0] <= 0.0f || displacement_energy[1] <= 0.0f ||
			displacement_energy[0] > maximum_energy ||
			displacement_energy[1] > maximum_energy)
		{
			output = {};
			return false;
		}
		output.pair_id = views.eyes[0].pair_id;
		output.publication = views.eyes[0].publication;
		return output.pair_id != 0 && output.publication != 0;
	}

	bool prepare_temporal_history(scene_record_pair& records,
		const std::uint64_t device_generation,
		const temporal_history_state& committed,
		temporal_history_preparation& output) noexcept
	{
		output = {};
		if (records.pair_id == 0 || device_generation == 0) return false;

		temporal_history_state current{};
		current.device_generation = device_generation;
		current.pair_id = records.pair_id;
		current.ready = true;
		std::array<float, 2> scales{};
		for (std::size_t eye{}; eye < 2; ++eye)
		{
			auto& record = eye == 0 ? records.left : records.right;
			std::memcpy(current.view_projection[eye].data(),
				record.data() + h2_current_view_projection_offset,
				sizeof(current.view_projection[eye]));
			std::memcpy(current.origin[eye].data(),
				record.data() + h2_view_origin_offset,
				sizeof(current.origin[eye]));
			std::memcpy(&scales[eye], record.data() +
				h2_previous_eye_position_constant_offset + sizeof(float) * 3,
				sizeof(scales[eye]));
			for (const auto value : current.view_projection[eye])
				if (!std::isfinite(value)) return false;
			for (const auto value : current.origin[eye])
				if (!std::isfinite(value)) return false;
			if (!std::isfinite(scales[eye]) || scales[eye] == 0.0f) return false;
		}

		const auto use_committed = committed.ready &&
			committed.device_generation == device_generation &&
			committed.pair_id != 0 && committed.pair_id < records.pair_id;
		for (std::size_t eye{}; eye < 2; ++eye)
		{
			auto& record = eye == 0 ? records.left : records.right;
			const auto& previous_view = use_committed ?
				committed.view_projection[eye] : current.view_projection[eye];
			const auto& previous_origin = use_committed ?
				committed.origin[eye] : current.origin[eye];
			std::memcpy(record.data() + h2_temporal_history_view_projection_offset,
				previous_view.data(), sizeof(previous_view));
			std::memcpy(record.data() + h2_previous_view_projection_constant_offset,
				previous_view.data(), sizeof(previous_view));
			std::array<float, 4> eye_delta{0.0f, 0.0f, 0.0f, scales[eye]};
			for (std::size_t component{}; component < 3; ++component)
			{
				eye_delta[component] = (current.origin[eye][component] -
					previous_origin[component]) * scales[eye];
				if (!std::isfinite(eye_delta[component])) return false;
			}
			std::memcpy(record.data() + h2_previous_eye_position_constant_offset,
				eye_delta.data(), sizeof(eye_delta));
		}
		output.current = current;
		output.valid = true;
		output.seeded = !use_committed;
		return true;
	}

	bool commit_temporal_history(temporal_history_state& state,
		const temporal_history_preparation& prepared) noexcept
	{
		if (!prepared.valid || !prepared.current.ready ||
			prepared.current.device_generation == 0 || prepared.current.pair_id == 0 ||
			(state.ready && state.device_generation == prepared.current.device_generation &&
				state.pair_id >= prepared.current.pair_id))
		{
			return false;
		}
		state = prepared.current;
		return true;
	}

	bool copy_eye_local_record_fields(void* const destination,
		const std::array<std::uint8_t, h2_scene_record_size>& source) noexcept
	{
		if (destination == nullptr) return false;
		auto* const bytes = static_cast<std::uint8_t*>(destination);
		std::memcpy(bytes, source.data(), h2_view_slot_size);
		std::memcpy(bytes + h2_inverse_scene_projection_constant_offset,
			source.data() + h2_inverse_scene_projection_constant_offset,
			sizeof(float) * 4);
		std::memcpy(bytes + h2_relative_eye_offset_offset,
			source.data() + h2_relative_eye_offset_offset, origin_size);
		std::memcpy(bytes + h2_temporal_history_view_projection_offset,
			source.data() + h2_temporal_history_view_projection_offset,
			sizeof(float) * 16);
		std::memcpy(bytes + h2_previous_view_projection_constant_offset,
			source.data() + h2_previous_view_projection_constant_offset,
			sizeof(float) * 16);
		std::memcpy(bytes + h2_previous_eye_position_constant_offset,
			source.data() + h2_previous_eye_position_constant_offset,
			sizeof(float) * 4);
		return true;
	}

	bool synchronize_backend_model_state(void* const backend_state,
		const void* const eye_record, backend_model_state_sync_result& output) noexcept
	{
		output = {};
		if (backend_state == nullptr || eye_record == nullptr)
		{
			output.failure = backend_model_state_sync_failure::invalid_argument;
			return false;
		}
		const auto record = reinterpret_cast<std::uintptr_t>(eye_record);
		output.expected_primary_source = record;
		if (record > std::numeric_limits<std::uintptr_t>::max() -
			h2_frontend_rebase_view_offset)
		{
			output.failure = backend_model_state_sync_failure::pointer_overflow;
			return false;
		}
		output.expected_rebase_source = record + h2_frontend_rebase_view_offset;

		auto* const state = static_cast<std::uint8_t*>(backend_state);
		std::memcpy(&output.primary_source,
			state + h2_backend_primary_source_pointer_offset,
			sizeof(output.primary_source));
		std::memcpy(&output.rebase_source,
			state + h2_backend_rebase_source_pointer_offset,
			sizeof(output.rebase_source));
		if (output.primary_source != output.expected_primary_source)
		{
			output.failure = backend_model_state_sync_failure::primary_source;
			return false;
		}
		if (output.rebase_source != output.expected_rebase_source)
		{
			output.failure = backend_model_state_sync_failure::rebase_source;
			return false;
		}

		const auto* const record_bytes = static_cast<const std::uint8_t*>(eye_record);
		std::memcpy(output.primary_origin.data(), state +
			h2_backend_primary_origin_offset, origin_size);
		std::memcpy(output.expected_primary_origin.data(), record_bytes +
			h2_view_origin_offset, origin_size);
		std::memcpy(output.relative_eye_offset.data(), state +
			h2_backend_relative_eye_offset, origin_size);
		std::memcpy(output.expected_relative_eye_offset.data(), record_bytes +
			h2_relative_eye_offset_offset, origin_size);
		for (std::size_t component{}; component < 3; ++component)
		{
			if (!std::isfinite(output.primary_origin[component]) ||
				!std::isfinite(output.expected_primary_origin[component]) ||
				!std::isfinite(output.relative_eye_offset[component]) ||
				!std::isfinite(output.expected_relative_eye_offset[component]))
			{
				output.failure = backend_model_state_sync_failure::non_finite;
				return false;
			}
		}
		if (std::memcmp(output.primary_origin.data(),
			output.expected_primary_origin.data(), origin_size) != 0)
		{
			output.failure = backend_model_state_sync_failure::primary_origin;
			return false;
		}
		if (std::memcmp(output.relative_eye_offset.data(),
			output.expected_relative_eye_offset.data(), origin_size) != 0)
		{
			output.failure = backend_model_state_sync_failure::relative_eye_offset;
			return false;
		}

		std::memcpy(&output.cache_before, state +
			h2_backend_xmodel_placement_cache_offset, sizeof(output.cache_before));
		if (output.cache_before != 0)
		{
			// 0x140788FE0 treats +0x3270 only as a borrowed placement identity:
			// null falls through to the native matrix rebuild, which writes the new
			// key and increments H2's own matrix versions. Multiple native batch paths
			// also clear this exact qword. Do not synthesize +0x800 or version fields.
			constexpr std::uintptr_t empty_cache{};
			std::memcpy(state + h2_backend_xmodel_placement_cache_offset,
				&empty_cache, sizeof(empty_cache));
			output.cache_invalidated = true;
		}
		output.matched = true;
		return true;
	}

	const char* to_string(const backend_model_state_sync_failure value) noexcept
	{
		switch (value)
		{
		case backend_model_state_sync_failure::none: return "none";
		case backend_model_state_sync_failure::invalid_argument: return "invalid_argument";
		case backend_model_state_sync_failure::pointer_overflow: return "pointer_overflow";
		case backend_model_state_sync_failure::primary_source: return "primary_source";
		case backend_model_state_sync_failure::rebase_source: return "rebase_source";
		case backend_model_state_sync_failure::primary_origin: return "primary_origin";
		case backend_model_state_sync_failure::relative_eye_offset:
			return "relative_eye_offset";
		case backend_model_state_sync_failure::non_finite: return "non_finite";
		default: return "unknown";
		}
	}

	backend_depth_hack_projection_outcome restore_backend_depth_hack_projection(
		void* const backend_state,
		backend_depth_hack_projection_result& output) noexcept
	{
		output = {};
		if (backend_state == nullptr)
			return backend_depth_hack_projection_outcome::contract_mismatch;
		auto* const state = static_cast<std::uint8_t*>(backend_state);

		std::uint32_t depth_hack_flags{};
		std::uint16_t cache_version{};
		std::uint16_t input_version{};
		std::memcpy(&depth_hack_flags, state + h2_backend_depth_hack_flags_offset,
			sizeof(depth_hack_flags));
		std::memcpy(&cache_version,
			state + h2_backend_projection_cache_version_offset,
			sizeof(cache_version));
		std::memcpy(&input_version,
			state + h2_backend_projection_input_version_offset,
			sizeof(input_version));
		if ((depth_hack_flags & 1u) == 0)
		{
			return backend_depth_hack_projection_outcome::not_applicable;
		}
		if (cache_version != input_version)
		{
			return backend_depth_hack_projection_outcome::contract_mismatch;
		}

		constexpr std::size_t projection_float_count = 16;
		constexpr std::array<std::size_t, 4> diagnostic_indices{0, 5, 8, 9};
		std::array<float, projection_float_count> eye_projection{};
		std::array<float, projection_float_count> cached_projection{};
		std::memcpy(eye_projection.data(), state + h2_backend_eye_projection_offset,
			sizeof(eye_projection));
		std::memcpy(cached_projection.data(), state + h2_backend_projection_cache_offset,
			sizeof(cached_projection));
		for (const auto value : eye_projection)
		{
			if (!std::isfinite(value))
				return backend_depth_hack_projection_outcome::contract_mismatch;
		}
		for (const auto value : cached_projection)
		{
			if (!std::isfinite(value))
				return backend_depth_hack_projection_outcome::contract_mismatch;
		}
		float depth_hack_near{};
		std::memcpy(&depth_hack_near, state + h2_backend_depth_hack_near_offset,
			sizeof(depth_hack_near));
		if (!std::isfinite(depth_hack_near) || depth_hack_near <= 0.0f ||
			eye_projection[0] <= 0.0f || eye_projection[5] <= 0.0f ||
			std::abs(eye_projection[11] - 1.0f) > 0.001f)
		{
			return backend_depth_hack_projection_outcome::contract_mismatch;
		}

		for (std::size_t index{}; index < diagnostic_indices.size(); ++index)
		{
			output.previous_terms[index] = cached_projection[diagnostic_indices[index]];
			output.restored_terms[index] = eye_projection[diagnostic_indices[index]];
		}
		output.depth_hack_near = depth_hack_near;
		std::memcpy(state + h2_backend_projection_cache_offset,
			eye_projection.data(), sizeof(eye_projection));
		std::memcpy(state + h2_backend_projection_cache_offset + 0x38,
			&depth_hack_near, sizeof(depth_hack_near));
		output.matched = true;
		return backend_depth_hack_projection_outcome::restored;
	}

	bool rebase_camera_model_list_origins(void* const eye_record,
		const std::array<float, 3>& natural_center,
		camera_model_origin_update& output) noexcept
	{
		output = {};
		if (eye_record == nullptr) return false;
		for (const auto component : natural_center)
		{
			if (!std::isfinite(component)) return false;
		}

		auto* const record = static_cast<std::uint8_t*>(eye_record);
		std::array<float, 3> eye_origin{};
		std::memcpy(eye_origin.data(), record + h2_view_origin_offset, origin_size);
		for (const auto component : eye_origin)
		{
			if (!std::isfinite(component)) return false;
		}
		for (const auto index : h2_camera_model_list_indices)
		{
			auto* const descriptor = record + h2_draw_list_descriptor_base +
				index * h2_draw_list_descriptor_size;
			std::uint32_t active_type{};
			std::memcpy(&active_type, descriptor + h2_draw_list_active_type_offset,
				sizeof(active_type));
			if (active_type == 0)
			{
				++output.inactive;
				continue;
			}
			auto* const origin = descriptor + h2_draw_list_origin_offset;
			if (std::memcmp(origin, natural_center.data(), origin_size) == 0)
			{
				std::memcpy(origin, eye_origin.data(), origin_size);
				++output.rewritten;
			}
			else if (std::memcmp(origin, eye_origin.data(), origin_size) == 0)
			{
				++output.already_eye;
			}
			else
			{
				++output.foreign;
			}
		}
		return output.rewritten + output.already_eye + output.inactive + output.foreign ==
			h2_camera_model_list_indices.size();
	}

	bool census_camera_model_list_origins(const void* const eye_record,
		camera_model_origin_census& output) noexcept
	{
		output = {};
		if (eye_record == nullptr) return false;
		const auto* const record = static_cast<const std::uint8_t*>(eye_record);
		std::array<float, 3> eye_origin{};
		std::memcpy(eye_origin.data(), record + h2_view_origin_offset, origin_size);
		for (const auto component : eye_origin)
		{
			if (!std::isfinite(component)) return false;
		}
		for (const auto index : h2_camera_model_list_indices)
		{
			const auto* const descriptor = record + h2_draw_list_descriptor_base +
				index * h2_draw_list_descriptor_size;
			std::uint32_t active_type{};
			std::memcpy(&active_type, descriptor + h2_draw_list_active_type_offset,
				sizeof(active_type));
			if (active_type == 0)
			{
				++output.inactive;
				continue;
			}
			const auto* const origin = descriptor + h2_draw_list_origin_offset;
			if (std::memcmp(origin, eye_origin.data(), origin_size) == 0)
			{
				++output.eye;
			}
			else
			{
				++output.other;
			}
		}
		return output.eye + output.inactive + output.other ==
			h2_camera_model_list_indices.size();
	}
}
