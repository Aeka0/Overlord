#pragma once
#include "hand_rig_builder.hpp"
#include <chrono>
#include <cstdint>

namespace vr::gameplay::hands
{
	struct forearm_twist_binding
	{
		struct limb
		{
			int node{-1};
			quat wrist_in_elbow{}, twist_in_elbow{};
			std::array<bool,256> affected{};
			bool operator==(const limb&) const = default;
		};
		std::array<limb,2> arms{};
		bool operator==(const forearm_twist_binding&) const = default;
	};
	inline bool finite_twist_pose(const bone& b) noexcept
	{
		float n{};
		for(float x:b.position)if(!std::isfinite(x))return false;
		for(float x:b.rotation){if(!std::isfinite(x))return false;n+=x*x;}
		return n>.5f && n<1.5f;
	}
	// Optional deformation contract, independent of weapon/grip recipes. Native
	// wristtwist is an elbow child beside the wrist, not part of the wrist tree.
	inline forearm_twist_binding bind_forearm_twist(const rig& r,
		std::span<const bone_definition> bones,const model_definition& model) noexcept
	{
		forearm_twist_binding out;
		if(r.count<=0 || r.count>256 || bones.size()!=size_t(r.count) || model.begin<0 ||
			model.count<=0 || model.begin>r.count-model.count)return out;
		for(int i=0;i<r.count;++i)if(r.parent[i]<-1 || r.parent[i]>=i)return out;
		const auto owned=[&](int i){return i>=model.begin && i<model.begin+model.count && !r.weapon_bones[i];};
		for(int h=0;h<2;++h)
		{
			const auto a=r.arms[h];
			if(!owned(a.elbow) || !owned(a.wrist) || !descendant(a.wrist,a.elbow,r) ||
				!finite_twist_pose(bones[a.elbow].bind) || !finite_twist_pose(bones[a.wrist].bind))continue;
			int node=-1;bool duplicate=false;
			for(int i=model.begin;i<model.begin+model.count;++i)
				if(bones[i].name==(h==0 ? "j_wristtwist_le" : "j_wristtwist_ri"))
				{if(node>=0)duplicate=true;node=i;}
			if(duplicate || node<0 || !owned(node) || r.parent[node]!=a.elbow || bones[node].parent!=a.elbow ||
				node==a.wrist || !finite_twist_pose(bones[node].bind) ||
				length(sub(bones[a.wrist].bind.position,bones[a.elbow].bind.position))<.1f)continue;
			auto& limb=out.arms[h];bool valid=true;
			for(int i=0;i<r.count;++i)if(descendant(i,node,r))
			{
				if(!owned(i) || i==r.arms[0].shoulder || i==r.arms[1].shoulder ||
					i==r.arms[0].elbow || i==r.arms[1].elbow || i==r.arms[0].wrist || i==r.arms[1].wrist ||
					!finite_twist_pose(bones[i].bind))valid=false;
				limb.affected[i]=true;
			}
			if(!valid){limb={};continue;}
			const auto inverse=conjugate(normalize(bones[a.elbow].bind.rotation));
			limb.node=node;
			limb.wrist_in_elbow=normalize(multiply(inverse,normalize(bones[a.wrist].bind.rotation)));
			limb.twist_in_elbow=normalize(multiply(inverse,normalize(bones[node].bind.rotation)));
		}
		return out;
	}

	class forearm_twist
	{
	public:
		using clock=std::chrono::steady_clock;
		void reset() noexcept {*this={};}
		// Run once after all anatomical wrist overrides, before committing the
		// skeleton. No tracked input, wrist, elbow, fingers or weapon is changed.
		unsigned update(const forearm_twist_binding& binding,const rig& r,std::span<bone> pose,
			std::uint64_t reference,clock::time_point at,unsigned tracked=3) noexcept
		{
			if(!reference || r.count<=0 || r.count>256 || pose.size()<size_t(r.count)){reset();return 0;}
			if(reference!=reference_ || at<at_ || at-at_>std::chrono::milliseconds(150))valid_.fill(false);
			reference_=reference;at_=at;unsigned applied{};
			for(int h=0;h<2;++h)
			{
				const auto& limb=binding.arms[h];const auto a=r.arms[h];
				if(!(tracked&(1u<<h)) || limb.node<0 || limb.node>=r.count || a.elbow<0 || a.elbow>=r.count || a.wrist<0 || a.wrist>=r.count)
				{valid_[h]=false;continue;}
				bool finite=finite_twist_pose(pose[a.elbow]) && finite_twist_pose(pose[a.wrist]);
				for(int i=0;i<r.count;++i)if(limb.affected[i])finite &= finite_twist_pose(pose[i]);
				const auto reach=sub(pose[a.wrist].position,pose[a.elbow].position);
				if(!finite || length(reach)<.1f){valid_[h]=false;continue;}
				const auto axis=unit(reach);
				const auto parent=normalize(pose[a.elbow].rotation);
				const auto neutral=normalize(multiply(parent,limb.wrist_in_elbow));
				const auto residual=normalize(multiply(normalize(pose[a.wrist].rotation),conjugate(neutral)));
				const float axial=dot({residual[0],residual[1],residual[2]},axis);
				quat twist;
				// At a 180-degree bend across the forearm, axial orientation is
				// undefined. Keep the last valid rotation in the elbow's frame; a
				// fresh object/reference uses neutral. Full twist has no half-angle
				// branch jump when normal pronation crosses +/-180 degrees.
				if(axial*axial+residual[3]*residual[3]<.0025f)
					twist=valid_[h] ? normalize(multiply(multiply(parent,last_[h]),conjugate(parent))) : quat{0,0,0,1};
				else
				{
					twist=normalize({axis[0]*axial,axis[1]*axial,axis[2]*axial,residual[3]});
					last_[h]=normalize(multiply(multiply(conjugate(parent),twist),parent));valid_[h]=true;
				}
				const auto target=normalize(multiply(twist,multiply(parent,limb.twist_in_elbow)));
				const auto pivot=pose[limb.node].position;
				const auto delta=normalize(multiply(target,conjugate(normalize(pose[limb.node].rotation))));
				// Absolute target prevents accumulated twist on repeated evaluation.
				for(int i=0;i<r.count;++i)if(limb.affected[i])pose[i]=transformed(pose[i],pivot,pivot,delta);
				applied|=1u<<h;
			}
			return applied;
		}
	private:
		std::array<quat,2> last_{};
		std::array<bool,2> valid_{};
		std::uint64_t reference_{};
		clock::time_point at_{};
	};
}
