#pragma once
#include "../controller_input.hpp"
#include <cmath>
#include <optional>

namespace vr::controller_input
{
	enum class posture : int { unknown=-1, stand, crouch, prone };
	inline posture native_posture(unsigned pm_flags) noexcept
	{return (pm_flags&1) ? posture::prone : (pm_flags&2) ? posture::crouch : posture::stand;}
	inline std::optional<posture> step_posture(posture current,int direction) noexcept
	{
		if(current==posture::unknown)return {};
		const int next=int(current)+(direction<0 ? 1:-1);
		return next>=0 && next<=2 ? std::optional{posture(next)}:std::nullopt;
	}
	inline int stance_binding(posture target,int requested) noexcept
	{
		if(requested<0 || requested>2 || int(target)==requested)return 0;
		if(target==posture::crouch)return 101; // gocrouch, absolute native input request
		if(target==posture::prone)return 100; // goprone
		if(target==posture::stand)return requested==1 ? 89 : requested==2 ? 99 : 0;
		return 0; // Native crouch/prone toggle returns its known current request to stand.
	}
	struct stance_command {std::optional<posture> target;bool suppress_jump{};};
	class stance_gesture
	{
		bool armed_{},suppress_jump_{};int direction_{};unsigned steps_{};
		std::uint64_t reference_{},sequence_{};
		clock::time_point last_{},issued_at_{};
		posture expected_{posture::unknown};
		void clear_stroke() noexcept {armed_=false;direction_=0;steps_=0;}
	public:
		void reset() noexcept {*this={};}
		stance_command consume(const frame& input,posture actual,bool gameplay,clock::time_point now,
			bool jump_pressed,bool jump_held) noexcept
		{
			if(!gameplay || !input.focused || !input.reference_generation || !input.sequence ||
				now<input.sampled_at || now-input.sampled_at>std::chrono::milliseconds(150) || actual==posture::unknown)
			{reset();return {};}
			if(reference_!=input.reference_generation || input.sequence<sequence_ || input.sampled_at<last_ ||
				input.sampled_at-last_>std::chrono::milliseconds(150))reset();
			reference_=input.reference_generation;
			if(!jump_held)suppress_jump_=false;
			if(jump_pressed && input.sequence!=sequence_ && actual!=posture::stand)
			{clear_stroke();suppress_jump_=true;sequence_=input.sequence;last_=input.sampled_at;return {posture::stand,true};}
			if(jump_held){clear_stroke();sequence_=input.sequence;last_=input.sampled_at;return {{},suppress_jump_};}
			if(input.sequence==sequence_)return {{},suppress_jump_};
			sequence_=input.sequence;last_=input.sampled_at;
			if(!input.turn_active || !std::isfinite(input.turn[0]) || !std::isfinite(input.turn[1]) ||
				std::abs(input.turn[0])>1.01f || std::abs(input.turn[1])>1.01f){clear_stroke();return {};}
			const float y=std::abs(input.turn[1]);
			// Only vertical deflection controls stance. Horizontal turning neither
			// prevents acquisition nor changes neutral rearming or hold continuation.
			if(y<=.30f){clear_stroke();armed_=true;return {};}
			if(!armed_)return {};
			const int direction=input.turn[1]<0 ? -1:1;
			const auto at=input.sampled_at;
			if(!direction_)
			{
				if(y<.90f)return {};
				direction_=direction;steps_=1;issued_at_=at;
				const auto target=step_posture(actual,direction_);expected_=target.value_or(actual);
				if(!target)steps_=2;
				return {target,false};
			}
			if(direction!=direction_ || y<.75f){clear_stroke();return {};}
			// Continue only after the native game actually accepted the first
			// step. A blocked stance cannot queue a later surprise transition.
			if(at-issued_at_>std::chrono::milliseconds(1500))steps_=2;
			if(steps_<2 && actual==expected_ && at-issued_at_>=std::chrono::milliseconds(450))
			{++steps_;return {step_posture(actual,direction_),false};}
			return {};
		}
	};
}
