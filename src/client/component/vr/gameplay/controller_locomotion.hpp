#pragma once

#include "../controller_input.hpp"
#include <algorithm>
#include <cmath>

namespace vr::controller_input
{
	enum class turn_mode { smooth, snap };

	struct turn_settings
	{
		turn_mode mode{turn_mode::smooth};
		float deadzone{0.2f};
		float degrees_per_second{90.0f};
		float snap_degrees{30.0f};
	};

	struct movement
	{
		bool active{};
		float forward{};
		float right{};
		float yaw_delta{};
		bool discontinuous_turn{};
	};

	// Game-command-thread state, deliberately independent of H2 memory layouts.
	class locomotion
	{
	public:
		movement consume(const frame& input, const bool gameplay,
			const float deadzone, const turn_settings& turning, const clock::time_point now) noexcept
		{
			movement result{};
			const auto finite_axis = [](const auto& axis)
			{
				return std::isfinite(axis[0]) && std::isfinite(axis[1]) &&
					std::abs(axis[0]) <= 1.01f && std::abs(axis[1]) <= 1.01f;
			};
			if (!gameplay || !input.focused || !input.move_active || !input.turn_active ||
				input.sequence == 0 || input.sampled_at > now ||
				now - input.sampled_at > std::chrono::milliseconds(150) ||
				!finite_axis(input.move) || !finite_axis(input.turn) ||
				!std::isfinite(deadzone) || deadzone < 0.05f || deadzone > 0.5f ||
				!std::isfinite(turning.snap_degrees) || turning.snap_degrees < 5.0f || turning.snap_degrees > 90.0f ||
				!std::isfinite(turning.deadzone) || turning.deadzone < 0.05f || turning.deadzone > 0.5f ||
				!std::isfinite(turning.degrees_per_second) || turning.degrees_per_second < 15.0f ||
				turning.degrees_per_second > 360.0f ||
				(turning.mode != turn_mode::smooth && turning.mode != turn_mode::snap))
			{
				reset();
				return result;
			}
			if (generation_ != input.reference_generation || input.sequence < sequence_ ||
				mode_ != turning.mode || (armed_ && (now < last_consumed_ ||
					now - last_consumed_ > std::chrono::milliseconds(150))))
			{
				reset();
				generation_ = input.reference_generation;
			}
			mode_ = turning.mode;
			const auto elapsed = armed_ ? std::clamp(std::chrono::duration<float>(now - last_consumed_).count(),
				0.0f, 0.05f) : 0.0f;
			last_consumed_ = now;
			sequence_ = input.sequence;
			const auto magnitude = std::hypot(input.move[0], input.move[1]);
			if (!armed_)
			{
				armed_ = magnitude <= deadzone && std::hypot(input.turn[0], input.turn[1]) <= turning.deadzone;
				return result;
			}
			result.active = true;
			if (magnitude > deadzone)
			{
				const auto scale = (std::min(magnitude, 1.0f) - deadzone) / (1.0f - deadzone) / magnitude;
				const auto x = input.move[0] * scale;
				const auto y = input.move[1] * scale;
				// HMD orientation is now native view input. Rotating axes here
				// would apply head yaw twice and disagree with keyboard movement.
				result.forward = y;
				result.right = x;
			}
			if (turning.mode == turn_mode::smooth)
			{
				const auto deflection = std::min(std::abs(input.turn[0]), 1.0f);
				if (deflection > turning.deadzone)
				{
					const auto speed = (deflection - turning.deadzone) / (1.0f - turning.deadzone);
					result.yaw_delta = -std::copysign(speed, input.turn[0]) * turning.degrees_per_second * elapsed;
				}
				return result;
			}
			if (std::abs(input.turn[0]) < 0.25f) turn_latched_ = false;
			if (!turn_latched_ && std::abs(input.turn[0]) >= 0.7f &&
				std::abs(input.turn[0]) > std::abs(input.turn[1]))
			{
				result.yaw_delta = input.turn[0] > 0 ? -turning.snap_degrees : turning.snap_degrees;
				result.discontinuous_turn = true;
				turn_latched_ = true;
			}
			return result;
		}

		void reset() noexcept
		{
			armed_ = false;
			turn_latched_ = false;
			sequence_ = 0;
		}

	private:
		bool armed_{};
		bool turn_latched_{};
		std::uint64_t sequence_{};
		std::uint64_t generation_{};
		turn_mode mode_{turn_mode::smooth};
		clock::time_point last_consumed_{};
	};

	// Driver axes have no view-turn channel. Add the physical sticks before the
	// shared radial deadzone/normalization; neither hand owns steering/throttle.
	class vehicle_locomotion
	{
		bool armed_{};
		unsigned available_{};
		std::uint64_t reference_{},continuity_{},sequence_{};
		clock::time_point last_{};
	public:
		void reset() noexcept {*this={};}
		movement consume(const frame& input,bool gameplay,float deadzone,clock::time_point now,bool entering=false) noexcept
		{
			const auto valid=[](const auto& axis){return std::isfinite(axis[0]) && std::isfinite(axis[1]) && std::abs(axis[0])<=1.01f && std::abs(axis[1])<=1.01f;};
			const unsigned available=unsigned(input.move_active)|(unsigned(input.turn_active)<<1);
			if(!gameplay || !input.focused || input.orientation_settling || !available || !input.sequence ||
				now<input.sampled_at || now-input.sampled_at>std::chrono::milliseconds(150) ||
				(input.move_active && !valid(input.move)) || (input.turn_active && !valid(input.turn)) ||
				!std::isfinite(deadzone) || deadzone<.05f || deadzone>.5f)
			{reset();return {};}
			if(available!=available_ || input.reference_generation!=reference_ || input.continuity_generation!=continuity_ ||
				input.sequence<sequence_ || (sequence_ && (now<last_ || now-last_>std::chrono::milliseconds(150))))reset();
			available_=available;reference_=input.reference_generation;continuity_=input.continuity_generation;sequence_=input.sequence;last_=now;
			if(!armed_)
			{
				// Opposite held sticks may cancel numerically, but are not neutral.
				// Resume/reconnection must not accelerate when only one is released.
				if(!entering && ((input.move_active && std::hypot(input.move[0],input.move[1])>deadzone) ||
					(input.turn_active && std::hypot(input.turn[0],input.turn[1])>deadzone)))return {};
				armed_=true;
				if(!entering)return {};
			}
			auto combined=input;combined.move={};combined.turn={};combined.move_active=combined.turn_active=true;
			for(unsigned axis=0;axis<2;++axis)combined.move[axis]=(input.move_active?input.move[axis]:0.f)+(input.turn_active?input.turn[axis]:0.f);
			const float magnitude=std::hypot(combined.move[0],combined.move[1]);
			if(magnitude>1)for(auto& axis:combined.move)axis/=magnitude;
			movement result;result.active=true;
			const float bounded=std::min(magnitude,1.f);
			if(bounded>deadzone)
			{const float scale=(bounded-deadzone)/(1.f-deadzone)/bounded;result.right=combined.move[0]*scale;result.forward=combined.move[1]*scale;}
			return result;
		}
	};
}
