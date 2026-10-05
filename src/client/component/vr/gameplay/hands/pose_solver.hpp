#pragma once

#include "../../spatial_math.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace vr::gameplay::hands
{
	using spatial_math::add;
	using spatial_math::anchor;
	using spatial_math::conjugate;
	using spatial_math::cross;
	using spatial_math::dot;
	using spatial_math::from_axis;
	using spatial_math::from_to;
	using spatial_math::length;
	using spatial_math::multiply;
	using spatial_math::normalize;
	using spatial_math::quat;
	using spatial_math::rotate;
	using spatial_math::scale;
	using spatial_math::sub;
	using spatial_math::unit;
	using spatial_math::vec;

	inline constexpr float max_arm_stretch_ratio = 1.25f;
	struct bone
	{
		quat rotation{};
		vec position{};
		float weight{};
	};
	struct arm
	{
		int shoulder{-1}, elbow{-1}, wrist{-1};
	};
	struct rig
	{
		std::array<int, 256> parent{};
		std::array<arm, 2> arms{};
		std::array<bool, 256> weapon_bones{};
		int gun{-1}, weapon_tag{-1}, count{};
		int muzzle{-1}, rear_grip_wrist{-1}, laser{-1};
	};
	inline bool descendant(int index, int root, const rig& r)
	{
		for (int n = 0; n < r.count && index >= 0; ++n)
		{
			if (index == root)
				return true;
			index = r.parent[index];
		}
		return false;
	}
	inline bone transformed(const bone& old, const vec pivot, const vec target, const quat delta)
	{
		return {normalize(multiply(delta, normalize(old.rotation))),
		        add(target, rotate(delta, sub(old.position, pivot))),
		        2.0f};
	}
	inline vec arm_plane_normal(vec upper, vec lower, quat shoulder_rotation) noexcept
	{
		const auto axis = unit(upper);
		auto normal = cross(axis, unit(lower));
		if (length(normal) < 1e-4f)
		{
			// A straight source arm has no geometric bend plane. Choose its
			// authored anatomical frame, never a changing world-axis fallback.
			normal = rotate(normalize(shoulder_rotation), {0, 1, 0});
			normal = sub(normal, scale(axis, dot(normal, axis)));
			if (length(normal) < 1e-4f)
			{
				normal = rotate(normalize(shoulder_rotation), {0, 0, 1});
				normal = sub(normal, scale(axis, dot(normal, axis)));
			}
		}
		return unit(normal);
	}
	inline quat arm_segment_frame(vec direction, vec normal) noexcept
	{
		const auto forward = unit(direction), side = unit(cross(normal, forward));
		return from_axis({forward, side, unit(cross(forward, side))});
	}
	struct arm_rotations
	{
		quat upper, lower;
	};
	inline arm_rotations orient_arm(
	    vec old_upper, vec old_lower, vec upper, vec lower, quat old_shoulder) noexcept
	{
		// Direction alone leaves roll unconstrained. Independent shortest-arc
		// rotations can spin an elbow through 360 degrees when a segment circles
		// the opposite of its bind direction, even while the hand barely moves.
		// Carry BOTH segments through the same oriented bend plane instead.
		const auto old_normal = arm_plane_normal(old_upper, old_lower, old_shoulder);
		auto normal = cross(unit(upper), unit(lower));
		if (length(normal) < 1e-4f)
		{
			// Degenerate target: retain the source plane as far as the new axis
			// permits. Ordinary IK keeps a small bend, so this is a fallback for
			// straight external arm constraints only.
			normal = sub(old_normal, scale(unit(upper), dot(old_normal, unit(upper))));
			if (length(normal) < 1e-4f)
				normal = arm_plane_normal(upper, lower, old_shoulder);
		}
		normal = unit(normal);
		return {normalize(multiply(arm_segment_frame(upper, normal),
		                           conjugate(arm_segment_frame(old_upper, old_normal)))),
		        normalize(multiply(arm_segment_frame(lower, normal),
		                           conjugate(arm_segment_frame(old_lower, old_normal))))};
	}

	// Swing a mirrored, outward/down rest bend from body-forward to the reach
	// direction. Projecting an animated elbow (or a fixed world hint) directly
	// becomes singular when the hand points along that hint. Swing transport does
	// not have that singularity in the ordinary front/side/overhead reach domain.
	inline vec elbow_pole(const vec direction, const std::array<vec, 3>& body_axis, int hand)
	{
		const auto rest = unit(sub(scale(body_axis[1], hand == 0 ? 0.6f : -0.6f), body_axis[2]));
		// Exactly behind the body is an ambiguous 180-degree swing. Use the bend
		// itself as its half-turn axis, identically mirrored for the two arms.
		const auto swing = dot(body_axis[0], direction) < -0.9999f ? quat{rest[0], rest[1], rest[2], 0}
		                                                           : from_to(body_axis[0], direction);
		const auto transported = rotate(swing, rest);
		return unit(sub(transported, scale(direction, dot(transported, direction))));
	}
	struct limb_solution
	{
		vec elbow{}, wrist{};
		bool valid{}, limited{};
	};
	inline limb_solution solve_limb(float upper,
	                                float lower,
	                                vec wrist,
	                                vec shoulder,
	                                const std::array<vec, 3>& body_axis,
	                                int hand,
	                                const vec* elbow_hint = nullptr,
	                                float stretch_limit = max_arm_stretch_ratio) noexcept
	{
		if (!std::isfinite(stretch_limit) || stretch_limit < 1.f || stretch_limit > max_arm_stretch_ratio)
			return {};
		if (hand < 0 || hand > 1 || !std::isfinite(upper) || !std::isfinite(lower) || upper < .1f ||
		    lower < .1f || upper > 100 || lower > 100)
			return {};
		for (const auto row : body_axis)
		{
			for (float x : row)
				if (!std::isfinite(x))
					return {};
			if (std::abs(dot(row, row) - 1) > .001f)
				return {};
		}
		if (std::abs(dot(body_axis[0], body_axis[1])) > .001f ||
		    length(sub(cross(body_axis[0], body_axis[1]), body_axis[2])) > .001f)
			return {};
		for (const auto p : {wrist, shoulder})
			for (float x : p)
				if (!std::isfinite(x))
					return {};
		const auto requested = sub(wrist, shoulder);
		const float distance = length(requested);
		if (!std::isfinite(distance) || distance > 200)
			return {};
		const float stretch = std::clamp((distance + .001f) / (upper + lower), 1.f, stretch_limit);
		upper *= stretch;
		lower *= stretch;
		const float reach = std::clamp(distance, std::abs(upper - lower) + .001f, upper + lower - .001f);
		const auto direction = distance > 1e-5f ? unit(requested) : body_axis[0];
		auto pole = elbow_pole(direction, body_axis, hand);
		if (elbow_hint)
		{
			for (float x : *elbow_hint)
				if (!std::isfinite(x))
					return {};
			const auto hint = sub(*elbow_hint, shoulder),
			           projected = sub(hint, scale(direction, dot(hint, direction)));
			if (length(projected) > .001f)
				pole = unit(projected);
		}
		const float along = (upper * upper - lower * lower + reach * reach) / (2 * reach);
		return {add(shoulder,
		            add(scale(direction, along),
		                scale(pole, std::sqrt(std::max(0.f, upper * upper - along * along))))),
		        add(shoulder, scale(direction, reach)),
		        true,
		        std::abs(reach - distance) > .01f};
	}

	// Transactional: never write a partially solved pose. Native finger and part
	// transforms survive as wrist-/gun-relative transforms, not copied assets.
	inline bool solve_pose(const rig& r,
	                       std::span<const bone> source,
	                       const std::array<anchor, 2>& targets,
	                       const std::array<vec, 2>& shoulders,
	                       const std::array<vec, 3>& body_axis,
	                       int holding_hand,
	                       std::span<bone> output,
	                       std::array<bool, 2>& limited,
	                       const vec* authored_rear_offset,
	                       bool attach_weapon,
	                       const vec* controlling_elbow = nullptr,
	                       float stretch_limit = max_arm_stretch_ratio,
	                       std::array<const vec*, 2> elbow_hints = {}) noexcept
	{
		if (attach_weapon &&
		    (holding_hand < 0 || holding_hand > 1 || r.rear_grip_wrist < 0 || r.rear_grip_wrist >= r.count))
			return false;
		if (authored_rear_offset)
			for (auto x : *authored_rear_offset)
				if (!std::isfinite(x))
					return false;
		for (const auto& row : body_axis)
		{
			for (auto x : row)
				if (!std::isfinite(x))
					return false;
			if (std::abs(dot(row, row) - 1) > 0.001f)
				return false;
		}
		if (std::abs(dot(body_axis[0], body_axis[1])) > 0.001f ||
		    length(sub(cross(body_axis[0], body_axis[1]), body_axis[2])) > 0.001f)
			return false;
		if (r.count <= 0 || r.count > 256 || source.size() < static_cast<size_t>(r.count) ||
		    output.size() < static_cast<size_t>(r.count))
			return false;
		if (attach_weapon && (r.gun < 0 || r.gun >= r.count || r.weapon_tag < 0 || r.weapon_tag >= r.count ||
		                      !r.weapon_bones[r.gun] || r.weapon_bones[r.weapon_tag]))
			return false;
		for (int i = 0; i < r.count; ++i)
		{
			if (r.parent[i] < -1 || r.parent[i] >= i)
				return false;
			for (auto x : source[i].position)
				if (!std::isfinite(x))
					return false;
			float norm{};
			for (auto x : source[i].rotation)
			{
				if (!std::isfinite(x))
					return false;
				norm += x * x;
			}
			if (norm < 0.5f || norm > 1.5f)
				return false;
		}
		for (const auto& a : r.arms)
			if (a.shoulder < 0 || a.elbow < 0 || a.wrist < 0 || a.wrist >= r.count || a.elbow >= a.wrist ||
			    a.shoulder >= a.elbow || !descendant(a.wrist, a.elbow, r) ||
			    !descendant(a.elbow, a.shoulder, r))
				return false;
		for (const auto& a : r.arms)
			if (r.weapon_bones[a.shoulder] || r.weapon_bones[a.elbow] || r.weapon_bones[a.wrist])
				return false;
		if (descendant(r.arms[0].shoulder, r.arms[1].shoulder, r) ||
		    descendant(r.arms[1].shoulder, r.arms[0].shoulder, r) ||
		    (attach_weapon && (descendant(r.weapon_tag, r.arms[0].shoulder, r) ||
		                       descendant(r.weapon_tag, r.arms[1].shoulder, r))))
			return false;
		std::array<bone, 256> result{};
		std::array<bool, 2> reach_limited{};
		std::copy_n(source.begin(), r.count, result.begin());
		for (int hand = 0; hand < 2; ++hand)
		{
			for (auto x : shoulders[hand])
				if (!std::isfinite(x))
					return false;
			for (auto x : targets[hand].position)
				if (!std::isfinite(x))
					return false;
			float norm{};
			for (auto x : targets[hand].rotation)
			{
				if (!std::isfinite(x))
					return false;
				norm += x * x;
			}
			if (norm < 0.5f || norm > 1.5f)
				return false;
			const auto a = r.arms[hand];
			const auto native_shoulder = source[a.shoulder].position, elbow = source[a.elbow].position,
			           wrist = source[a.wrist].position;
			const auto shoulder = shoulders[hand];
			const float native_upper = length(sub(elbow, native_shoulder)),
			            native_lower = length(sub(wrist, elbow));
			const auto limb =
			    solve_limb(native_upper,
			               native_lower,
			               targets[hand].position,
			               shoulder,
			               body_axis,
			               hand,
			               attach_weapon && hand == holding_hand ? controlling_elbow : elbow_hints[hand],
			               stretch_limit);
			if (!limb.valid)
				return false;
			reach_limited[hand] = limb.limited;
			const auto end = limb.wrist, joint = limb.elbow;
			const auto rotations = orient_arm(sub(elbow, native_shoulder),
			                                  sub(wrist, elbow),
			                                  sub(joint, shoulder),
			                                  sub(end, joint),
			                                  source[a.shoulder].rotation);
			const auto upper_delta = rotations.upper, lower_delta = rotations.lower;
			const auto hand_delta =
			    normalize(multiply(normalize(targets[hand].rotation),
			                       conjugate(normalize(source[attach_weapon ? r.gun : a.wrist].rotation))));
			for (int i = 0; i < r.count; ++i)
			{
				if (r.weapon_bones[i])
					continue;
				if (descendant(i, a.wrist, r))
					result[i] = transformed(source[i], wrist, end, hand_delta);
				else if (descendant(i, a.elbow, r))
					result[i] = transformed(source[i], elbow, joint, lower_delta);
				else if (descendant(i, a.shoulder, r))
					result[i] = transformed(source[i], native_shoulder, shoulder, upper_delta);
			}
			if (attach_weapon && hand == holding_hand)
			{
				// Native rear-grip relation is separate from the selected owner's
				// animated wrist. A left hand's old foregrip pose is not a rear grip.
				const auto rear_wrist =
				    authored_rear_offset
				        ? add(source[r.gun].position,
				              rotate(normalize(source[r.gun].rotation), *authored_rear_offset))
				        : source[r.rear_grip_wrist].position;
				result[r.weapon_tag] = transformed(source[r.weapon_tag], rear_wrist, end, hand_delta);
				for (int i = 0; i < r.count; ++i)
					if (r.weapon_bones[i])
						result[i] = transformed(source[i], rear_wrist, end, hand_delta);
			}
		}
		std::copy_n(result.begin(), r.count, output.begin());
		limited = reach_limited;
		return true;
	}
	// Absolute anatomical wrist targets; no receiver, muzzle or controlling hand
	// is needed. The same transactional arm/reach solver serves both paths.
	inline bool solve_arms(const rig& r,
	                       std::span<const bone> source,
	                       const std::array<anchor, 2>& targets,
	                       const std::array<vec, 2>& shoulders,
	                       const std::array<vec, 3>& body_axis,
	                       std::span<bone> output,
	                       std::array<bool, 2>& limited,
	                       float stretch_limit = max_arm_stretch_ratio,
	                       std::array<const vec*, 2> elbow_hints = {}) noexcept
	{
		return solve_pose(r,
		                  source,
		                  targets,
		                  shoulders,
		                  body_axis,
		                  -1,
		                  output,
		                  limited,
		                  nullptr,
		                  false,
		                  nullptr,
		                  stretch_limit,
		                  elbow_hints);
	}
	inline bool solve(const rig& r,
	                  std::span<const bone> source,
	                  const std::array<anchor, 2>& targets,
	                  const std::array<vec, 2>& shoulders,
	                  const std::array<vec, 3>& body_axis,
	                  int holding_hand,
	                  std::span<bone> output,
	                  std::array<bool, 2>& limited,
	                  const vec* authored_rear_offset = nullptr,
	                  const vec* controlling_elbow = nullptr) noexcept
	{
		return solve_pose(r,
		                  source,
		                  targets,
		                  shoulders,
		                  body_axis,
		                  holding_hand,
		                  output,
		                  limited,
		                  authored_rear_offset,
		                  true,
		                  controlling_elbow);
	}
} // namespace vr::gameplay::hands
