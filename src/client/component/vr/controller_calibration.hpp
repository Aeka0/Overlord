#pragma once
#include "controller_input.hpp"
#include "settings.hpp"

namespace vr::controller_calibration
{
	using angles=std::array<float,3>;
	using hand_angles=std::array<angles,2>; // Physical left, right; independent of weapon ownership.
	using matrix=std::array<std::array<float,3>,3>;
	inline constexpr angles defaults{settings::hand_pitch.default_value,settings::hand_yaw.default_value,settings::hand_roll.default_value};
	inline constexpr angles default_position{settings::hand_inward.default_value,settings::hand_back.default_value,settings::hand_up.default_value};
	struct configuration
	{
		angles orientation=defaults,position=default_position;
		hand_angles hands{defaults,defaults};
		bool independent{};
		hand_angles effective_angles() const noexcept {return independent ? hands : hand_angles{orientation,orientation};}
	};
	using settings_provider=configuration(*)(controller_pose_pipeline::mode) noexcept;
	// Native settings are optional: standalone runtimes retain shared defaults
	// without importing game dvars or component registration.
	void set_settings_provider(settings_provider provider) noexcept;
	inline configuration defaults_for(controller_pose_pipeline::mode mode) noexcept
	{
		configuration result;
		if (mode == controller_pose_pipeline::mode::standard)
			for (unsigned i = 0; i < 3; ++i)
			{
				result.orientation[i] = settings::standard_hand_alignment[i + 3].default_value;
				result.position[i] = settings::standard_hand_alignment[i].default_value;
			}
		return result;
	}
	configuration read_settings(controller_pose_pipeline::mode mode) noexcept;
	inline bool valid(angles value) noexcept
	{
		for (unsigned i=0;i<3;++i)
			if (!std::isfinite(value[i]) || value[i]<settings::hand_angles[i].min || value[i]>settings::hand_angles[i].max) return false;
		return true;
	}
	inline matrix multiply(const matrix& a,const matrix& b) noexcept
	{
		matrix out{};
		for (unsigned r=0;r<3;++r) for (unsigned c=0;c<3;++c) for (unsigned k=0;k<3;++k) out[r][c]+=a[r][k]*b[k][c];
		return out;
	}
	inline matrix rotation(angles degrees) noexcept
	{
		constexpr float radians=.01745329251994329577f;
		const float p=degrees[0]*radians,y=degrees[1]*radians,r=-degrees[2]*radians;
		const float cp=std::cos(p),sp=std::sin(p),cy=std::cos(y),sy=std::sin(y),cr=std::cos(r),sr=std::sin(r);
		// Tracking axes: +X right, +Y up, -Z forward. Local yaw, pitch, roll;
		// positive values turn left, tilt up, and roll the right side down.
		const matrix pitch{{{1,0,0},{0,cp,-sp},{0,sp,cp}}};
		const matrix yaw{{{cy,0,sy},{0,1,0},{-sy,0,cy}}};
		const matrix roll{{{cr,-sr,0},{sr,cr,0},{0,0,1}}};
		return multiply(multiply(yaw,pitch),roll);
	}
	class calibration
	{
		hand_angles previous_{};
		angles previous_position_{};
		controller_input::clock::time_point changed_at_{};
		bool initialized_{},settling_{};
	public:
		controller_input::frame apply(const controller_input::frame& raw,angles degrees,angles position=default_position) noexcept
		{
			return apply_hands(raw,{degrees,degrees},position);
		}
		controller_input::frame apply_hands(const controller_input::frame& raw,hand_angles degrees,angles position=default_position) noexcept
		{
			auto out=raw;out.runtime_grip=raw.grip;out.runtime_aim=raw.aim;out.orientation_degrees=degrees;out.orientation_settling=false;
			out.position_offsets_meters=position;
			for (float value:position) if (!std::isfinite(value) || std::abs(value)>settings::max_hand_offset)
			{for (auto& pose:out.aim) pose.valid=false;return out;}
			for (unsigned h=0;h<2;++h) if (!valid(degrees[h]))
			{
				out.aim[h].valid=false;
				degrees[h]={}; // A malformed hand must not keep resetting the opposite hand's history.
			}
			if (!initialized_) {previous_=degrees;previous_position_=position;initialized_=true;}
			else if (previous_!=degrees || previous_position_!=position)
			{previous_=degrees;previous_position_=position;changed_at_=raw.sampled_at;settling_=true;}
			// Live calibration is not hand motion. Mark the discontinuity for
			// melee history without hiding the model or losing a holding hand.
			if (settling_)
			{
				if (raw.sampled_at<changed_at_) changed_at_=raw.sampled_at;
				settling_=raw.sampled_at-changed_at_<std::chrono::milliseconds(200);
				out.orientation_settling=settling_;
			}
			for (unsigned h=0;h<2;++h) if (auto& pose=out.aim[h];pose.valid && degrees[h]!=angles{})
			{
				// Zero preserves the exact runtime pose; each correction is aim-local.
				pose.tracking.orientation=multiply(pose.tracking.orientation,rotation(degrees[h]));
				for (const auto& row:pose.tracking.orientation) for (float x:row) if (!std::isfinite(x)) pose.valid=false;
			}
			return out;
		}
	};
}
