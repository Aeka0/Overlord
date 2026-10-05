#pragma once
#include "component/vr/digital_button_gate.hpp"
#include "../pose_filter.hpp"

namespace vr::gameplay::fixed_sniper
{
	inline constexpr settings::number_setting hand_travel{"vr_fixedSniperHandTravel",.20f,.05f,1.f,.01f};
	inline constexpr settings::number_setting hand_noise{"vr_fixedSniperHandDeadzone",.00035f,0.f,.003f,.0001f};
	struct hand_context
	{
		std::array<pose_filter::vec,2> positions{}; // Raw grip origins, meters in the tracking reference.
		pose_filter::matrix basis{pose_filter::identity}; // Mount's initial head forward/left/up.
		unsigned tracked{};
		float tan_half_y{},travel{hand_travel.default_value},noise{hand_noise.default_value};
	};
	struct hand_delta {float pitch{},yaw{};unsigned active{};};
	class hand_aim
	{
		struct clutch
		{
			controller_input::digital_press_gate press;
			std::uint64_t releases{};
			pose_filter::vec origin{},previous{};
			std::array<float,2> cursor{};
			bool dragging{};
		};
		std::array<clutch,2> hands_{};
		std::uint64_t epoch_{},reference_{},sequence_{};
		controller_input::clock::time_point at_{};
		pose_filter::matrix basis_{pose_filter::identity};
		float travel_{},noise_{};
		static float dot(const pose_filter::vec& a,const pose_filter::vec& b) noexcept
		{return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
	public:
		void reset() noexcept {*this={};}
		hand_delta consume(const controller_input::frame& input,std::uint64_t epoch,bool gameplay,
			const hand_context& context,controller_input::clock::time_point now) noexcept
		{
			if(!gameplay || !epoch || !input.focused || input.orientation_settling || !input.sequence || !input.reference_generation ||
				now<input.sampled_at || now-input.sampled_at>std::chrono::milliseconds(150) ||
				!pose_filter::valid({{},context.basis}) || !std::isfinite(context.tan_half_y) || context.tan_half_y<=0 || context.tan_half_y>2 ||
				!std::isfinite(context.travel) || context.travel<hand_travel.min || context.travel>hand_travel.max ||
				!std::isfinite(context.noise) || context.noise<hand_noise.min || context.noise>hand_noise.max)
			{reset();return {};}
			if(epoch_!=epoch || reference_!=input.reference_generation || input.sequence<sequence_ || input.sampled_at<at_ ||
				input.sampled_at-at_>std::chrono::milliseconds(150) || travel_!=context.travel || noise_!=context.noise)
			{
				reset();epoch_=epoch;reference_=input.reference_generation;basis_=context.basis;
				travel_=context.travel;noise_=context.noise;
			}
			hand_delta result;
			const bool new_pose=input.sequence!=sequence_;
			sequence_=input.sequence;at_=input.sampled_at;
			std::array<float,2> delta{};unsigned contributors{};
			for(unsigned h=0;h<2;++h)
			{
				auto& hand=hands_[h];const auto& button=input.squeeze[h];const auto& position=context.positions[h];
				if(!(context.tracked&(1u<<h)) || !pose_filter::valid({position,pose_filter::identity}) || !button.active)
				{hand={};continue;}
				if(button.generation!=hand.press.generation || button.presses<hand.press.presses || button.releases<hand.releases)hand={};
				const bool released=button.releases!=hand.releases;hand.releases=button.releases;
				const bool pressed=hand.press.consume(button);
				if(!button.down || released)hand.dragging=false;
				if(pressed && button.down)
				{
					hand.origin=hand.previous=position;hand.cursor={};hand.dragging=true;
					result.active|=1u<<h;continue; // Re-gripping never moves the current aim.
				}
				if(!hand.dragging)continue;
				if(new_pose && pose_filter::length(pose_filter::sub(position,hand.previous))>.25f)
				{hand={};continue;} // Tracking teleport: require release, not a large aim correction.
				result.active|=1u<<h;++contributors;
				if(!new_pose)continue;
				hand.previous=position;
				const auto relative=pose_filter::sub(position,hand.origin);
				const std::array<float,2> target{-dot(relative,basis_[1]),dot(relative,basis_[2])};
				std::array<float,2> step{target[0]-hand.cursor[0],target[1]-hand.cursor[1]};
				const float length=std::hypot(step[0],step[1]);
				if(length<=context.noise)continue;
				// Spatial hysteresis removes sub-millimeter tremor without a time
				// filter or a delayed tail after stopping/releasing the hand.
				const float gain=1-context.noise/length;
				for(unsigned i=0;i<2;++i){step[i]*=gain;hand.cursor[i]+=step[i];delta[i]+=step[i];}
			}
			if(contributors)
			{
				// One configured travel spans one vertical image FOV. Recompute gain
				// per displacement, so zooming with stationary hands cannot pan aim.
				const float gain=2*std::atan(context.tan_half_y)*57.295779513f/context.travel/contributors;
				result.yaw=-delta[0]*gain;result.pitch=-delta[1]*gain;
			}
			return result;
		}
	};
}
