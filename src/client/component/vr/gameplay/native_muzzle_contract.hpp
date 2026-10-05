#pragma once
#include "hand_pose_solver.hpp"
#include <string_view>

namespace vr::gameplay::hands
{
	// Reviewed exceptions to the generic root +X convention. These validate
	// asset bind data; they never straighten or replace the native firing axis.
	struct native_muzzle_contract
	{
		std::string_view receiver;
		int bones;
		anchor muzzle;
		bool matches(int count,int parent,int gun,vec offset,quat rotation) const noexcept
		{
			if(count!=bones || parent!=gun || length(sub(offset,muzzle.position))>.02f)return false;
			const auto delta=normalize(multiply(conjugate(muzzle.rotation),rotation));
			return std::abs(delta[3])>.9999996f; // 0.1 degree, including roll and pitch sign.
		}
	};
	inline constexpr native_muzzle_contract reviewed_muzzles[]{
		// H2 receiver baseMat: position in native inches; 10.30-degree upward axis.
		{"h2_viewmodel_javelin_base",6,{{12.70210837f,0,2.93677304f},{0,-.08975494f,0,.99597156f}}},
	};
	inline const native_muzzle_contract* reviewed_muzzle(std::string_view receiver) noexcept
	{
		for(const auto& value:reviewed_muzzles)if(value.receiver==receiver)return &value;
		return nullptr;
	}
}
