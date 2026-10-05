#pragma once
#include "cylinder_runtime.hpp"
#include "part_presentation.hpp"
#include "component/vr/gameplay/hand_pose_math.hpp"
#include "reload_item_visual.hpp"

namespace vr::gameplay::weapons::cylinder
{
	const char* presentation_status() noexcept;
	reload_items::visual loader_visual(const cylinder_profile*,int rounds)noexcept;
	struct part_rig
	{
		bool valid{}; int swing{-1}, barrel{-1}, ammo{-1}, loader{-1};
		std::array<int,6> cases{},tips{};
		hands::anchor cylinder_in_swing{};
		std::array<hands::anchor,6> case_in_ammo{},tip_in_case{};
	};
	inline part_rig bind_parts(const hands::rig& r,std::span<const hands::bone_definition> bones) noexcept
	{
		part_rig out;
		if (r.count<=0 || r.count>256 || bones.size()!=size_t(r.count) || r.gun<0 || r.gun>=r.count) return out;
		const auto find=[&](std::string_view name) {
			int found=-1;
			for (int i=0;i<r.count;++i) if (r.weapon_bones[i] && bones[i].name==name)
			{ if (found>=0) return -1; found=i; }
			return found;
		};
		out.swing=find("j_cylinder_rot"); out.barrel=find("j_cylinder"); out.ammo=find("j_cylinder_ammo"); out.loader=find("j_speed_loader");
		if (out.swing<0 || out.barrel<0 || out.ammo<0 || out.loader<0 || r.parent[out.swing]!=r.gun ||
			r.parent[out.barrel]!=out.swing || r.parent[out.ammo]!=r.gun || r.parent[out.loader]!=r.gun) return {};
		const auto relative=[&](int parent,int child) { return hands::pose_math::compose(
			hands::pose_math::inverse(hands::pose_math::as_anchor(bones[parent].bind)),hands::pose_math::as_anchor(bones[child].bind)); };
		out.cylinder_in_swing=relative(out.swing,out.barrel);
		constexpr std::array<std::string_view,6> cases{"j_bullet01","j_bullet02","j_bullet03","j_bullet04","j_bullet05","j_bullet06"};
		constexpr std::array<std::string_view,6> tips{"j_bullet_tip01","j_bullet_tip02","j_bullet_tip03","j_bullet_tip04","j_bullet_tip05","j_bullet_tip06"};
		for (size_t n=0;n<6;++n)
		{
			const int b=find(cases[n]),t=find(tips[n]);
			if (b<0 || t<0 || r.parent[b]!=out.ammo || r.parent[t]!=b) return {};
			out.cases[n]=b; out.tips[n]=t;
			out.case_in_ammo[n]=relative(out.ammo,b);
			out.tip_in_case[n]=relative(b,t);
		}
		out.valid=true; return out;
	}
	inline void pose_parts(const hands::rig& r,const part_rig& parts,const cylinder_profile& p,
		float opening,std::span<hands::bone> solved) noexcept
	{
		using namespace hands::pose_math;
		// Mechanical admission can lag one scene or be unavailable. A closed
		// receiver still needs assembled parts rather than animation bind offsets.
		auto swing=p.swing_closed;
		swing.rotation=hands::blend_quat(swing.rotation,p.swing_open.rotation,std::clamp(opening,0.f,1.f));
		move_part(r,parts.swing,compose(as_anchor(solved[r.gun]),swing),solved);
		move_part(r,parts.barrel,compose(as_anchor(solved[parts.swing]),parts.cylinder_in_swing),solved);
		move_part(r,parts.ammo,compose(as_anchor(solved[parts.barrel]),p.ammo_in_cylinder),solved);
		for (size_t i=0;i<6;++i)
		{
			move_part(r,parts.cases[i],compose(as_anchor(solved[parts.ammo]),parts.case_in_ammo[i]),solved);
			move_part(r,parts.tips[i],compose(as_anchor(solved[parts.cases[i]]),parts.tip_in_case[i]),solved);
		}
	}
	inline part_mask hidden_parts(const part_rig& parts,const presentation& v) noexcept
	{
		part_mask hidden{};
		const auto hide=[&](int bone) { hidden[bone/32]|=0x80000000u>>(bone%32); };
		// The native reload's cosmetic loader is never a separately owned item.
		hide(parts.loader);
		for (int i=0;i<6;++i)
		{
			if (!v.active || i>=v.ammo.live+v.ammo.spent) hide(parts.cases[i]);
			if (!v.active || i>=v.ammo.live) hide(parts.tips[i]);
		}
		return hidden;
	}
	part_presentation::result present(void* object,std::uint32_t epoch,const void* matrices,const part_rig& parts,
		const hands::rig& rig,const hands::pose_library& library,const weapons::profile& grip,
		const controller_input::frame& input,const hold& owner,std::uint64_t assembly,bool gameplay, bool manipulation,
		const std::array<hands::anchor,2>& targets,const std::array<hands::vec,2>& shoulders,
		const std::array<hands::vec,3>& body_axis,hands::vec head,hands::vec offset,float units,
		std::span<hands::bone> solved,clock::time_point now,hands::part_hand_frame* hand_motion=nullptr) noexcept;
}
