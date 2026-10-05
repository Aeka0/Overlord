#pragma once
#include "tube_runtime.hpp"
#include "part_presentation.hpp"
#include "component/vr/gameplay/hand_pose_math.hpp"

namespace vr::gameplay::weapons::tube
{
	struct part_rig{bool valid{};int bolt{-1},lifter{-1},shell{-1},pump{-1},drum{-1};std::array<int,4> lever_parts{-1,-1,-1,-1};std::array<int,4> hidden_rounds{-1,-1,-1,-1};};
	inline part_rig bind_parts(const hands::rig& r,std::span<const hands::bone_definition> bones,const tube_profile* p=nullptr)noexcept
	{
		part_rig out;if(r.count<=0 || r.count>256 || bones.size()!=size_t(r.count) || r.gun<0 || r.gun>=r.count)return out;
		const auto find=[&](std::string_view name){int found=-1;for(int i=0;i<r.count;++i)if(r.weapon_bones[i] && bones[i].name==name){if(found>=0)return -1;found=i;}return found;};
		out.bolt=find(p ? p->bolt_bone : "j_bolt");out.lifter=find(p ? p->lifter_bone : "j_load");out.shell=find(p ? p->shell_bone : "tag_clip");
		out.valid=out.bolt>=0 && out.lifter>=0 && out.shell>=0 && r.parent[out.bolt]==r.gun && r.parent[out.lifter]==r.gun && r.parent[out.shell]==r.gun;
		if(p && pumped(p->ammunition)){out.pump=find(p->pump_bone);out.valid=out.valid && out.pump>=0 && r.parent[out.pump]==r.gun && out.pump!=out.bolt && out.pump!=out.lifter && out.pump!=out.shell;}
		if(p && rotary(p->ammunition))
		{
			out.drum=find(p->drum_bone);
			out.valid=out.lifter>=0 && out.shell>=0 && out.drum>=0 && out.shell!=out.lifter && out.drum!=out.shell && out.drum!=out.lifter &&
				r.parent[out.lifter]==r.gun && r.parent[out.shell]==r.gun && r.parent[out.drum]==r.gun &&
				p->bolt_bone.empty() && p->rack_grips.empty() && !manual(p->ammunition);
		}
		if(p && levered(p->ammunition))
		{
			out.valid=out.valid && p->lever && p->rack_grips.empty() && p->lever->parts.size()<=out.lever_parts.size() && p->lever->hidden_rounds.size()<=out.hidden_rounds.size() &&
				p->lever->open_fingers.size()==p->lever->closed_fingers.size() && p->lever->open_fingers.size()<=64 &&
				p->lever->spin_fingers.size()==p->lever->spin_wrists.size()*p->lever->open_fingers.size() && lever::valid(p->interaction.lever);
			if(out.valid)for(size_t n=0;n<p->lever->parts.size();++n)
			{
				const auto& part=p->lever->parts[n];out.lever_parts[n]=find(part.name);
				if(out.lever_parts[n]<0 || r.parent[out.lever_parts[n]]!=(part.parent.empty()?r.gun:find(part.parent)))out.valid=false;
			}
			if(out.valid)for(size_t n=0;n<p->lever->hidden_rounds.size();++n)
			{
				out.hidden_rounds[n]=find(p->lever->hidden_rounds[n]);
				if(out.hidden_rounds[n]<0 || out.hidden_rounds[n]==out.shell || r.parent[out.hidden_rounds[n]]!=r.gun)out.valid=false;
			}
		}
		return out;
	}
	part_presentation::result present(const void* object,std::uint32_t epoch,const void* matrices,
		const part_rig&,const hands::rig&,const hands::pose_library&,const weapons::profile&,
		const controller_input::frame&,const hold&,std::uint64_t assembly,bool gameplay,bool manipulation,
		const std::array<hands::anchor,2>& targets,const std::array<hands::vec,2>& shoulders,const std::array<hands::vec,3>& body_axis,
		hands::vec head,hands::vec offset,float units,std::span<hands::bone> solved,clock::time_point now,hands::part_hand_frame* hand_motion=nullptr)noexcept;
}
