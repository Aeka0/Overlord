#pragma once
#include "launcher_runtime.hpp"
#include "part_presentation.hpp"
#include "component/vr/gameplay/hands/pose_math.hpp"
namespace vr::gameplay::weapons::launcher
{
	struct part_rig {bool valid{};int rocket{-1};};
	inline part_rig bind_parts(const hands::rig& r,std::span<const hands::bone_definition> bones,const launcher_profile& p)noexcept
	{
		part_rig out;if(r.gun<0 || bones.size()!=size_t(r.count))return out;
		if(!p.manual_loading()){out.valid=true;return out;}
		for(int i=0;i<r.count;++i)if(r.parent[i]==r.gun && bones[i].name=="tag_clip")
			{if(out.rocket>=0)return {};out.rocket=i;}
		out.valid=out.rocket>=0;return out;
	}
	part_presentation::result present(void*,std::uint32_t,const void*,const part_rig&,const hands::rig&,
		const hands::pose_library&,const profile&,const controller_input::frame&,const hold&,std::uint64_t,bool,bool,
		const std::array<hands::anchor,2>&,hands::vec,float,std::span<hands::bone>,hands::part_hand_frame* hand_motion=nullptr)noexcept;
}
