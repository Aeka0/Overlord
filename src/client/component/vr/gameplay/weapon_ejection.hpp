#pragma once
#include "hands/rig_builder.hpp"
#include "component/vr/gameplay/hands/pose_math.hpp"

namespace vr::gameplay::weapons
{
	struct ejection_port
	{
		bool valid{};
		hands::anchor local{}; // Actual held receiver coordinates; never a muzzle-aligned world model.
	};
	inline ejection_port bind_ejection_port(const hands::rig& rig,
		std::span<const hands::bone_definition> bones) noexcept
	{
		if (rig.count<=0 || rig.count>256 || bones.size()!=size_t(rig.count) || rig.gun<0 || rig.gun>=rig.count) return {};
		int marker=-1;
		for (int i=0;i<rig.count;++i) if (rig.weapon_bones[i] && bones[i].name=="tag_brass")
		{
			if (marker>=0 || rig.parent[i]!=rig.gun) return {};
			marker=i;
		}
		if (marker<0) return {};
		for (int index:{rig.gun,marker})
		{
			const auto& b=bones[index].bind;
			for (float x:b.position) if (!std::isfinite(x) || std::abs(x)>10000) return {};
			float norm{};for (float x:b.rotation) {if (!std::isfinite(x)) return {};norm+=x*x;}
			if (std::abs(norm-1)>.01f) return {};
		}
		return {true,hands::pose_math::compose(hands::pose_math::inverse(hands::pose_math::as_anchor(bones[rig.gun].bind)),
			hands::pose_math::as_anchor(bones[marker].bind))};
	}
}
