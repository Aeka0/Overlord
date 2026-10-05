#pragma once
#include "underbarrel_feed.hpp"
#include "underbarrel_grip.hpp"
#include "weapons/attachments/underbarrel_poses.hpp"
#include "component/vr/gameplay/hands/pose_math.hpp"
#include "component/vr/gameplay/hands/pose_mirror.hpp"

namespace vr::gameplay::weapons::underbarrel
{
	struct part_rig
	{
		kind type{};int root{-1},motion{-1},round{-1};hands::anchor mount{},motion_rest{},round_rest{},muzzle{};
		std::array<hands::vec,2> palm{};bool palms_valid{};
		hands::anchor firing_pose{authored::fire_grip};float firing_forward_m{};
		std::span<const joint_pose> firing_fingers{authored::fire_fingers};
		std::array<int,2> round_parts{-1,-1};std::array<hands::anchor,2> round_parts_local{};
		int bolt{-1};hands::anchor bolt_rest{}; // Shotgun bolt is a sibling of the pump, not its child.
		explicit operator bool()const noexcept{return type!=kind::none && root>=0 && round>=0;}
	};
	inline constexpr std::array<std::string_view,7> m203_bones{"tag_m203","j_grenade_m203","j_m203_button","j_slider_m203","j_trigger_m203","j_grenade_main","j_grenade_shell"};
	inline constexpr std::array<std::string_view,9> gp25_bones{"tag_gp25","j_gp25_button","j_gp25_trigger","j_grenade_gp25","j_sight_main","j_grenade_main","j_grenade_shell","j_sight_adjust1","j_sight_adjust2"};
	inline constexpr std::array<std::string_view,5> shotgun_bones{"tag_shotgun","j_ammo_shotgun","j_plate_shotgun","j_pump_shotgun","j_reload_shotgun"};
	inline constexpr std::array<int,7> m203_parents{-1,0,0,0,0,1,1};
	inline constexpr std::array<int,9> gp25_parents{-1,0,0,0,0,3,3,4,4};
	inline hands::anchor firing_anchor(const part_rig& p,const hands::rig& r,const hands::pose_library& library,
		hands::anchor ordinary,int hand)noexcept
	{
		if(p.type==kind::gp25)return ordinary;
		auto result=hands::pose_math::compose(p.mount,p.firing_pose);
		return hand==1?hands::pose_mirror::wrist(result,library.mirror_basis[r.arms[1].wrist]):result;
	}
	inline part_rig bind(std::span<const hands::model_definition> models,const hands::rig& r,std::span<const hands::bone_definition> bones)noexcept
	{
		using namespace hands::pose_math;part_rig out;
		if(r.gun<0 || r.gun>=r.count || r.count>256 || bones.size()!=size_t(r.count))return out;
		for(const auto& m:models)
		{
			const auto k=m.name=="attach_h2_m203_vm" || m.name=="attach_h2_m203_vm_digital" ? kind::m203 :
				m.name=="attach_h2_gp25_vm" ? kind::gp25 : m.name=="attach_h2_shotgun_vm" ? kind::shotgun : kind::none;
			if(k==kind::none)continue;if(out.type!=kind::none || m.begin<=r.gun || m.begin+m.count>r.count)return {};
			const auto tag=k==kind::m203 ? "tag_m203" : k==kind::gp25 ? "tag_gp25" : "tag_shotgun";
			const int parent=r.parent[m.begin];
			if(parent<0 || parent>=m.begin || bones[parent].name!=tag || bones[m.begin].name!=tag ||
				m.count!=(k==kind::m203?7:k==kind::gp25?9:5))return {};
			out.type=k;out.root=m.begin;out.mount=compose(inverse(as_anchor(bones[r.gun].bind)),as_anchor(bones[parent].bind));
			const auto attachment=inverse(as_anchor(bones[m.begin].bind));
			for(int i=m.begin;i<m.begin+m.count;++i)
			{
				if(!hands::descendant(i,m.begin,r))return {};
				const auto n=bones[i].name;
				const int index=i-m.begin;
				const auto expected=k==kind::m203?m203_bones[index]:k==kind::gp25?gp25_bones[index]:shotgun_bones[index];
				const int expected_parent=k==kind::m203?m203_parents[index]:k==kind::gp25?gp25_parents[index]:index?0:-1;
				if(n!=expected || (index && r.parent[i]!=m.begin+expected_parent))return {};
				for(float f:bones[i].bind.position)if(!std::isfinite(f)||std::abs(f)>10000)return {};
				if(n=="j_slider_m203" || n=="j_pump_shotgun")
				{out.motion=i;out.motion_rest=compose(out.mount,compose(attachment,as_anchor(bones[i].bind)));}
				if(k==kind::shotgun && n=="j_plate_shotgun")
				{out.bolt=i;out.bolt_rest=compose(out.mount,compose(attachment,as_anchor(bones[i].bind)));}
				if(n=="j_grenade_m203" || n=="j_grenade_gp25" || n=="j_ammo_shotgun")
				{out.round=i;out.round_rest=compose(out.mount,compose(attachment,as_anchor(bones[i].bind)));}
			}
			out.muzzle=compose(out.mount,k==kind::m203?authored::m203_muzzle:k==kind::gp25?authored::gp25_muzzle:authored::shotgun_muzzle);
			if(out.round<0 || (k!=kind::gp25 && out.motion<0) || (k==kind::shotgun && out.bolt<0))return {};
			if(k!=kind::shotgun)for(int n=0;n<2;++n)
			{
				const int child=m.begin+5+n;out.round_parts[n]=child;
				out.round_parts_local[n]=compose(inverse(as_anchor(bones[out.round].bind)),as_anchor(bones[child].bind));
			}
		}
		if(out.type==kind::m203)for(const auto& model:models)if(model.begin==r.gun)
		{
			if(model.name=="h2_viewmodel_scar_h_base"){out.firing_pose=authored::scar_fire_grip;out.firing_fingers=authored::scar_fire_fingers;}
			if(model.name=="h2_viewmodel_m4_base")out.firing_forward_m=.05f;
		}
		out.palms_valid=bool(out);
		for(int h=0;h<2 && out.palms_valid;++h)
		{
			const int wrist=r.arms[h].wrist;int index=-1,pinky=-1;
			for(int i=0;i<r.count;++i)
			{
				if(bones[i].name==(h?"j_index_ri_0":"j_index_le_0"))index=i;
				if(bones[i].name==(h?"j_pinky_ri_0":"j_pinky_le_0"))pinky=i;
			}
			if(wrist<0 || wrist>=r.count || index<0 || pinky<0){out.palms_valid=false;break;}
			const auto a=hands::sub(bones[index].bind.position,bones[wrist].bind.position),b=hands::sub(bones[pinky].bind.position,bones[wrist].bind.position);
			const auto normal=h?hands::cross(a,b):hands::cross(b,a);const float length=hands::length(normal);
			if(!std::isfinite(length)||length<.0001f){out.palms_valid=false;break;}
			out.palm[h]=hands::rotate(hands::conjugate(hands::normalize(bones[wrist].bind.rotation)),hands::scale(normal,1/length));
		}
		return out;
	}
	inline bool pose_action(const part_rig& parts,const hands::rig& rig,hands::anchor gun,
		float travel,float units,std::span<hands::bone> solved) noexcept
	{
		using namespace hands::pose_math;
		if(!parts || rig.count<=0 || solved.size()<size_t(rig.count) || !std::isfinite(travel) ||
			!std::isfinite(units) || units<=0)return false;
		if(parts.type==kind::gp25)return true;
		if(parts.motion<0 || parts.motion>=rig.count ||
			(parts.type==kind::shotgun && (parts.bolt<0 || parts.bolt>=rig.count || parts.bolt==parts.motion)))return false;
		const bool shotgun=parts.type==kind::shotgun;
		const float stroke=shotgun?authored::shotgun_stroke:authored::m203_stroke;
		travel=std::clamp(travel,0.f,stroke);
		const auto axis=rotate(parts.mount.rotation,shotgun?authored::shotgun_axis:authored::m203_axis);
		auto target=parts.motion_rest;target.position=add(target.position,scale(axis,travel*units));
		move_part(rig,parts.motion,compose(gun,target),solved);
		if(shotgun)
		{
			target=parts.bolt_rest;
			target.position=add(target.position,scale(axis,authored::shotgun_bolt_stroke*(travel/stroke)*units));
			move_part(rig,parts.bolt,compose(gun,target),solved);
		}
		return true;
	}
}
