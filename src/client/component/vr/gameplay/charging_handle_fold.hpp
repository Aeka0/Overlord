#pragma once
#include "hands/pose_library.hpp"
#include "part_mesh_partition.hpp"
#include <optional>

namespace vr::gameplay::weapons
{
	// Parent-local orientation while grasped. Folding is presentation only;
	// the existing axial gesture still owns the complete mechanical stroke.
	struct charging_handle_fold
	{
		hands::quat deployed;
		std::string_view end_bone{}; // Empty folds the slide root unless a mesh partition is specified.
		hands::anchor pivot{}; // End bind/pivot relative to the translating slide.
		std::optional<hands::quat> right_deployed{};
		const part_mesh_partition* mesh{};
	};
	inline bool valid_fold(const charging_handle_fold& p) noexcept
	{
		const auto unit=[](hands::quat q){float s{};for(float x:q){if(!std::isfinite(x))return false;s+=x*x;}return std::abs(s-1)<.001f;};
		if(!unit(p.deployed) || !unit(p.pivot.rotation) || (p.right_deployed && !unit(*p.right_deployed)) || (p.mesh && !p.end_bone.empty()))return false;
		for(float x:p.pivot.position)if(!std::isfinite(x) || std::abs(x)>1000)return false;
		if(p.mesh && !valid_partition(*p.mesh))return false;
		return true;
	}
	inline hands::quat folded_handle_rotation(hands::quat rest,const charging_handle_fold* fold,float amount,int hand=0) noexcept
	{
		if (!fold || !valid_fold(*fold) || !std::isfinite(amount) || hand<0 || hand>1) return rest;
		return hands::blend_quat(rest,hand==1 && fold->right_deployed?*fold->right_deployed:fold->deployed,amount);
	}
	inline hands::anchor folded_handle_pose(hands::anchor slide,const charging_handle_fold& fold,float amount,int hand) noexcept
	{
		using namespace hands;
		if(!valid_fold(fold) || !std::isfinite(amount) || hand<0 || hand>1)return slide;
		if(fold.end_bone.empty() && !fold.mesh){slide.rotation=folded_handle_rotation(slide.rotation,&fold,amount,hand);return slide;}
		auto local=fold.pivot;local.rotation=folded_handle_rotation(local.rotation,&fold,amount,hand);
		return {add(slide.position,rotate(slide.rotation,local.position)),normalize(multiply(slide.rotation,local.rotation))};
	}
}
