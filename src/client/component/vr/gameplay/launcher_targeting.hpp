#pragma once
#include "weapon_interaction.hpp"
#include "launcher_profile.hpp"
namespace vr::gameplay::weapons::launcher
{
	inline muzzle_frame sighting_frame(const launcher_profile& p,muzzle_frame muzzle)noexcept
	{
		muzzle.position=hands::add(muzzle.position,hands::scale(muzzle.axis[1],p.sight_lateral_meters*muzzle.units_per_meter));
		return muzzle;
	}
	// Preserve native FOV/reticle thresholds by expressing the controller's
	// target vector in the basis expected by the original projection leaf.
	inline hands::vec reticle_delta(hands::vec eye_delta,hands::vec native_eye,const muzzle_frame& muzzle,
		const std::array<hands::vec,3>& native_axis)noexcept
	{
		using namespace hands;const auto target=sub(add(native_eye,eye_delta),muzzle.position);vec out{};
		for(size_t n=0;n<3;++n)out=add(out,scale(native_axis[n],dot(target,muzzle.axis[n])));
		return out;
	}
}
