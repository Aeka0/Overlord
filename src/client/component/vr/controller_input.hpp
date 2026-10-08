#pragma once

#include "head_pose_bridge.hpp"
#include "settings.hpp"
#include "input_observation.hpp"
#include <array>
#include <chrono>
#include <cstdint>

namespace vr::controller_input
{
	using clock = std::chrono::steady_clock;
	struct digital_action
	{
		bool active{};
		bool down{};
		std::uint64_t presses{};
		std::uint64_t generation{};
		std::uint64_t releases{};
	};

	// Runtime-only history: coalesce short taps between command reads without an
	// unbounded event queue. Generation prevents replay across lost input/focus.
	class digital_sampler
	{
	public:
		digital_action sample(bool active, bool down, clock::time_point now) noexcept
		{
			if (active && (!value_.active || now < last_time_ ||
				now - last_time_ > std::chrono::milliseconds(150)))
			{
				++value_.generation;
				value_.down = false;
			}
			if (active && down && !value_.down) ++value_.presses;
			if (active && !down && value_.down) ++value_.releases;
			value_.active = active;
			value_.down = active && down;
			last_time_ = now;
			return value_;
		}

	private:
		digital_action value_{};
		clock::time_point last_time_{};
	};
	struct hand_pose
	{
		bool valid{};
		head_pose_bridge::tracking_pose tracking{};
	};

	// Published once at the existing Present-post tracking boundary. Consumers
	// never poll the runtime or retain references into a mutable runtime buffer.
	struct frame
	{
		std::uint64_t sequence{};
		std::uint64_t reference_generation{};
		clock::time_point sampled_at{};
		bool focused{};
		bool move_active{};
		bool turn_active{};
		digital_action sprint{};
		digital_action jump{};
		digital_action menu_toggle{}; // Explicit rebindable application menu; never the system/dashboard button.
		digital_action menu_recenter{}; // Tap for pause/back, hold for recenter; dedicated binding.
		std::array<digital_action, 2> trigger{}; // Physical left/right inputs, never weapon ownership.
		std::array<digital_action, 2> trigger_touch{}; // Optional capacitive contact, independent of trigger click.
		std::array<digital_action, 2> squeeze{}; // Side buttons; gameplay assigns grip roles.
		std::array<digital_action, 2> primary{}; // Rebindable per-hand primary; Touch X is reserved for menu_recenter.
		std::array<digital_action, 2> secondary{}; // B / Y: rear-hand magazine/slide release.
		std::array<float, 2> move{};
		std::array<float, 2> turn{};
		std::array<hand_pose, 2> grip{};
		std::array<hand_pose, 2> aim{};
		// Preserve the raw pair for diagnostics and its rigid grip-to-aim basis.
		// Wrist alignment uses this pair; the physical lever uses filtered grip.
		std::array<hand_pose, 2> runtime_aim{}; // Unfiltered SDK pointing pose.
		std::array<hand_pose, 2> runtime_grip{}; // Unfiltered adapter grip in its reported calibration basis.
		std::array<float,3> orientation_degrees{}; // pitch, yaw, roll
		bool orientation_settling{};
		std::array<float,3> wrist_pivot_meters{settings::wrist_inward.default_value,
			settings::wrist_back.default_value,settings::wrist_up.default_value};
		std::uint64_t continuity_generation{}; // Producer continuity, independent of game simulation cadence.
		input_observation source{}; // Diagnostic provenance only; never grants gameplay admission.
	};

	class consumer_continuity
	{
		std::uint64_t generation_{};clock::time_point at_{};bool seen_{};
	public:
		bool update(const frame& input,clock::time_point now) noexcept
		{
			const bool changed=seen_ && (now<at_ ||
				(input.continuity_generation ? input.continuity_generation!=generation_ : now-at_>std::chrono::milliseconds(150)));
			generation_=input.continuity_generation;at_=now;seen_=true;return changed;
		}
	};
	inline bool producer_discontinuity(const frame& before,const frame& after) noexcept
	{
		if(after.sequence<before.sequence || after.reference_generation!=before.reference_generation ||
			after.focused!=before.focused || after.orientation_settling!=before.orientation_settling ||
			after.sampled_at<before.sampled_at || (before.sequence && after.sampled_at-before.sampled_at>std::chrono::milliseconds(150)))return true;
		for(unsigned h=0;h<2;++h)
			if(after.grip[h].valid!=before.grip[h].valid || after.aim[h].valid!=before.aim[h].valid)return true;
		return false;
	}

	void publish(const frame& value) noexcept;
	void invalidate(input_reason reason = input_reason::runtime_reset,
		input_backend backend = input_backend::unknown, std::int64_t code = 0,
		std::uint64_t initialization = 0) noexcept;
	void set_gameplay_active(bool active) noexcept;
	[[nodiscard]] frame latest() noexcept;
}
