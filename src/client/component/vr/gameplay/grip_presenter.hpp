#pragma once
#include "component/vr/gameplay/weapon_pose_library.hpp"
#include "hands/pose_library.hpp"
#include "support_grip.hpp"

namespace vr::gameplay::weapons
{
	struct grip_result
	{
		bool valid{};
		hand support{hand::none};
		float distance_meters{}, blend{};
	};
	class grip_presenter
	{
	  public:
		grip_result update(const profile& profile, const hands::pose_library& library, const hands::rig& rig,
						   std::span<const hands::bone> native, std::array<hands::anchor, 2> targets,
						   const std::array<hands::vec, 2>& shoulders,
						   const std::array<hands::vec, 3>& body_axis, const controller_input::frame& input,
						   const hold& owner, std::uint64_t assembly, float units_per_meter, bool gameplay,
						   bool equip_animation, controller_input::clock::time_point now,
						   std::span<hands::bone> solved, std::array<bool, 2>& limited, bool offhand_available = true,
						   bool authoritative_support = false) noexcept
		{
			using namespace hands;
			offhand_available=offhand_available && profile.support_enabled;
			if (!owner.can_fire() || static_cast<int>(owner.rear) != profile.authored_rear ||
				!library.valid || !std::isfinite(units_per_meter) || units_per_meter <= 0 ||
				!std::isfinite(profile.blend_seconds) || profile.blend_seconds <= 0)
			{
				reset();
				return {};
			}
			if (owner.id() != weapon_ || owner.rear_revision != rear_revision_ || assembly != assembly_ ||
				input.reference_generation != reference_ || now < last_time_ ||
				now - last_time_ > std::chrono::milliseconds(150))
				reset();
			const float dt =
				last_time_ == controller_input::clock::time_point{}
					? 0.0f
					: std::clamp(std::chrono::duration<float>(now - last_time_).count(), 0.0f, 0.05f);
			weapon_ = owner.id();
			rear_revision_ = owner.rear_revision;
			assembly_ = assembly;
			reference_ = input.reference_generation;
			last_time_ = now;
			const int rear = static_cast<int>(owner.rear), other = 1 - rear;
			targets[rear].rotation=control_rotation(profile,rear,targets[rear].rotation);
			vec elbow_hint{};const vec* elbow=nullptr;
			if(profile.forearm)
			{
				const auto arm=rig.arms[rear];
				if(arm.shoulder<0 || arm.elbow<0 || arm.wrist<0 || native.size()<=std::size_t(std::max({arm.shoulder,arm.elbow,arm.wrist})))
				{reset();return {};}
				const auto mounted=mount_forearm(*profile.forearm,length(sub(native[arm.elbow].position,native[arm.shoulder].position)),
					length(sub(native[arm.wrist].position,native[arm.elbow].position)),targets[rear],shoulders[rear],body_axis,rear);
				if(!mounted.limb.valid){reset();return {};}
				targets[rear].rotation=mounted.rotation;elbow_hint=mounted.limb.elbow;elbow=&elbow_hint;
			}
			const auto& offset = profile.wrists[rear].position;
			if (!solve(rig, native, targets, shoulders, body_axis, rear, solved, limited, &offset,elbow))
			{
				reset();
				return {};
			}
			const auto gun = solved[rig.gun];
			const auto authored_span = sub(profile.wrists[other].position, offset);
			const auto two_hand = aimed_rotation(profile.aiming, targets[rear].rotation,
				targets[rear].position, targets[other].position, authored_span);
			// Acquire at the one-hand pose, retain at the solved two-hand pose.
			// Otherwise a correctly held rifle drops support when the rear wrist
			// rotates, because the old one-hand anchor moves away from the foregrip.
			const auto check_rotation = (lease_.engaged() || (authoritative_support && valid_hand(owner.support))) ? two_hand : gun.rotation;
			const auto anchor = add(solved[rig.arms[rear].wrist].position, rotate(check_rotation, authored_span));
			float distance = length(sub(targets[other].position, anchor)) / units_per_meter;
			if (profile.aiming == aim_rule::two_hand &&
				length(sub(targets[other].position, targets[rear].position)) / units_per_meter < .08f)
				distance = profile.release_meters + 1; // Coincident/crossing hands cannot define a stable rifle axis.
			const auto proposed = authoritative_support ? hand::none : lease_.consume(input, owner, assembly, gameplay && offhand_available, distance,
													profile.acquire_meters, profile.release_meters, now);
			const auto support=authoritative_support ? (valid_hand(owner.support) && gameplay && offhand_available ? owner.support : hand::none) : proposed;
			if (support != hand::none)
				support_delta_ = normalize(multiply(conjugate(targets[rear].rotation), two_hand));
			const float goal = support == hand::none ? 0.0f : 1.0f;
			blend_ += std::clamp(goal - blend_, -dt / profile.blend_seconds, dt / profile.blend_seconds);
			if (!gameplay || !offhand_available)
				blend_ = 0;
			if (blend_ > 0)
			{
				// Freeze the last relative support rotation on release. A free hand
				// moving to reload must not keep steering during the return blend.
				const auto retained_aim = multiply(targets[rear].rotation, support_delta_);
				targets[rear].rotation = blend_quat(targets[rear].rotation, retained_aim, blend_);
				const auto gun_position =
					sub(solved[rig.arms[rear].wrist].position, rotate(targets[rear].rotation, offset));
				const auto attached =
					add(gun_position, rotate(targets[rear].rotation, profile.wrists[other].position));
				targets[other].position =
					add(targets[other].position, scale(sub(attached, targets[other].position), blend_));
				targets[other].rotation = blend_quat(targets[other].rotation, targets[rear].rotation, blend_);
				if (!solve(rig, native, targets, shoulders, body_axis, rear, solved, limited, &offset,elbow))
				{
					reset();
					return {};
				}
			}
			std::array<float, 2> amounts{};
			amounts[rear] = 1;
			amounts[other] = blend_;
			if (!apply_poses(rig, library, profile, targets, amounts, equip_animation, solved))
			{
				reset();
				return {};
			}
			return {true, support, distance, blend_};
		}
		void reset() noexcept
		{
			lease_.reset();
			blend_ = 0;
			support_delta_ = {0, 0, 0, 1};
			last_time_ = {};
		}

	  private:
		support_grip lease_;
		float blend_{};
		hands::quat support_delta_{0, 0, 0, 1};
		weapon_identity weapon_{};
		std::uint64_t rear_revision_{}, assembly_{}, reference_{};
		controller_input::clock::time_point last_time_{};
	};
} // namespace vr::gameplay::weapons
