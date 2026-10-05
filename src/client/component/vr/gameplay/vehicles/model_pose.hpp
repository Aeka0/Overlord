#pragma once
#include "component/vr/gameplay/hands/pose_math.hpp"

namespace vr::gameplay::vehicles
{
	// rigid_part keeps SOURCE-MODEL bind-space vertices. Authored equip poses
	// are parent-local; resolve the hierarchy before forming each rigid delta.
	inline bool model_rest_pose(std::span<const hands::bone_definition> bones,
		std::span<const hands::part_pose> rest,std::span<hands::anchor> output) noexcept
	{
		using namespace hands;using namespace hands::pose_math;
		if(bones.empty() || bones.size()>64 || output.size()<bones.size() || bones[0].parent!=-1 || rest.size()>64)return false;
		std::array<anchor,64> model{};std::array<bool,64> found{};
		for(size_t i=0;i<bones.size();++i)
		{
			const auto& b=bones[i];float norm{};
			for(float x:b.bind.position)if(!std::isfinite(x) || std::abs(x)>10000)return false;
			for(float x:b.bind.rotation){if(!std::isfinite(x))return false;norm+=x*x;}
			if(std::abs(norm-1)>.01f || (i && (b.parent<0 || size_t(b.parent)>=i)))return false;
			auto local=i?compose(inverse(as_anchor(bones[b.parent].bind)),as_anchor(b.bind)):as_anchor(b.bind);
			for(size_t n=0;n<rest.size();++n)if(rest[n].name==b.name)
			{
				if(found[n] || !i)return false;found[n]=true;local=rest[n].local;norm=0;
				for(float x:local.position)if(!std::isfinite(x) || std::abs(x)>10000)return false;
				for(float x:local.rotation){if(!std::isfinite(x))return false;norm+=x*x;}
				if(std::abs(norm-1)>.01f)return false;
			}
			model[i]=i?compose(model[b.parent],local):local;
		}
		for(size_t n=0;n<rest.size();++n)if(!found[n])return false;
		const auto gun=inverse(model[0]);
		for(size_t i=0;i<bones.size();++i)output[i]=compose(gun,model[i]);
		return true;
	}
	using hands::pose_math::rigid_delta;
}
