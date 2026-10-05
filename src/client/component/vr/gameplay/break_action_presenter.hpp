#pragma once
#include "break_action_runtime.hpp"
#include "part_presentation.hpp"
#include "component/vr/gameplay/hands/pose_math.hpp"

namespace vr::gameplay::weapons::break_action
{
	struct part_rig
	{bool valid{};int barrel{-1},lock{-1},casing{-1};std::array<int,2> rounds{-1,-1};std::array<int,8> followers{};};
	inline part_rig bind_parts(const hands::rig& r,std::span<const hands::bone_definition> bones,const break_action_profile& p)noexcept
	{
		part_rig out;if(r.count<=0 || r.count>256 || bones.size()!=size_t(r.count) || r.gun<0 || r.gun>=r.count ||
			!p.ammunition.capacity || p.ammunition.capacity>2 || p.followers.size()>out.followers.size())return out;
		const auto find=[&](std::string_view name){int found=-1;for(int i=0;i<r.count;++i)if(r.weapon_bones[i] && bones[i].name==name){if(found>=0)return -1;found=i;}return found;};
		out.barrel=find(p.barrel_bone);out.lock=find(p.lock_bone);out.casing=find(p.case_bone);
		if(out.barrel<0 || out.lock<0 || out.casing<0 || out.barrel==out.lock || r.parent[out.barrel]!=r.gun || r.parent[out.lock]!=r.gun)return out;
		for(unsigned n=0;n<p.ammunition.capacity;++n)
		{
			out.rounds[n]=find(p.chamber_bones[n]);const int b=out.rounds[n];
			if(b<0 || b==out.barrel || b==out.lock || (r.parent[b]!=r.gun && r.parent[b]!=out.barrel) || (n && out.rounds[0]==b))return out;
		}
		if(r.parent[out.casing]!=r.gun && r.parent[out.casing]!=out.barrel)return out;
		for(size_t n=0;n<p.followers.size();++n)
		{out.followers[n]=find(p.followers[n].name);if(out.followers[n]<0 || r.parent[out.followers[n]]!=r.gun)return out;}
		out.valid=valid(p.interaction);return out;
	}
	inline part_mask pose_parts(const hands::rig& r,const part_rig& parts,const break_action_profile& p,
		const presentation& v,float hinge,std::span<hands::bone> solved) noexcept
	{
		using namespace hands;using namespace hands::pose_math;
		const auto gun=as_anchor(solved[r.gun]),barrel=compose(gun,barrel_pose(p,hinge));
		move_part(r,parts.barrel,barrel,solved);
		move_part(r,parts.lock,compose(gun,hinge_pose(p.lock_closed,p.lock_open,v.active && v.ammo.phase==action::opening?1.f:0.f)),solved);
		for(size_t n=0;n<p.followers.size();++n)move_part(r,parts.followers[n],compose(gun,hinge_pose(p.followers[n].closed,p.followers[n].open,hinge)),solved);
		part_mask hidden{};const auto hide=[&](int root){for(int b=0;b<r.count;++b)if(descendant(b,root,r))hidden[b/32]|=0x80000000u>>(b%32);};
		for(unsigned n=0;n<p.ammunition.capacity;++n)
		{
			move_part(r,parts.rounds[n],compose(barrel,p.chamber_in_barrel[n]),solved);
			if(v.active && !((v.ammo.live|(parts.casing==parts.rounds[0]?v.ammo.spent:0u))&(1u<<n)))hide(parts.rounds[n]);
		}
		if(parts.casing!=parts.rounds[0])
		{move_part(r,parts.casing,compose(compose(barrel,p.chamber_in_barrel[0]),p.case_in_shell),solved);if(!v.active || !v.ammo.spent)hide(parts.casing);}
		return hidden;
	}
	part_presentation::result present(void* object,std::uint32_t epoch,const void* matrices,const part_rig&,
		const hands::rig&,const hands::pose_library&,const weapons::profile&,const controller_input::frame&,const hold&,
		std::uint64_t assembly,bool gameplay,bool manipulation,const std::array<hands::anchor,2>& targets,
		const std::array<hands::vec,2>& shoulders,const std::array<hands::vec,3>& body_axis,hands::vec head,hands::vec offset,float units,
		std::span<hands::bone> solved,clock::time_point now,hands::part_hand_frame* hand_motion=nullptr)noexcept;
}
