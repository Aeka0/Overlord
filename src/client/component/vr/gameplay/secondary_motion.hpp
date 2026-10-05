#pragma once
#include "component/vr/gameplay/hand_pose_math.hpp"
#include <chrono>

namespace vr::gameplay::weapons
{
	struct secondary_motion_link
	{
		part_pose rest;
		hands::vec lower,upper; // Angular limits in radians, parent-local axes.
	};
	struct secondary_motion_profile
	{
		std::span<const secondary_motion_link> links;
		hands::vec tip; // Last link's rest direction, native units.
		float stiffness{90},damping{11},force_gain{4};
	};
	namespace secondary_motion
	{
		using namespace hands;
		using namespace hands::pose_math;
		struct binding { std::array<int,4> bones{}; size_t count{}; bool valid{}; };
		inline bool finite(vec v) noexcept
		{ return std::all_of(v.begin(),v.end(),[](float x){return std::isfinite(x);}); }
		inline bool valid(const secondary_motion_profile& p) noexcept
		{
			if (p.links.empty() || p.links.size()>4 || !finite(p.tip) || length(p.tip)<.001f || length(p.tip)>10) return false;
			for (float x:{p.stiffness,p.damping,p.force_gain}) if (!std::isfinite(x) || x<=0 || x>200) return false;
			for (const auto& link:p.links)
			{
				if (link.rest.name.empty() || !finite(link.rest.local.position) || length(link.rest.local.position)>10) return false;
				float norm{}; for (float x:link.rest.local.rotation) { if (!std::isfinite(x)) return false; norm+=x*x; }
				if (std::abs(norm-1)>.001f || !finite(link.lower) || !finite(link.upper)) return false;
				for (int a=0;a<3;++a) if (link.lower[a]>0 || link.upper[a]<0 || link.lower[a]<-1 || link.upper[a]>1) return false;
			}
			return true;
		}
		inline binding bind(const secondary_motion_profile& p,const rig& r,std::span<const bone_definition> bones) noexcept
		{
			binding out;
			if (!valid(p) || r.count<=0 || r.count>256 || bones.size()!=size_t(r.count) || r.gun<0 || r.gun>=r.count) return out;
			for (int i=0;i<r.count;++i) if (r.parent[i]<-1 || r.parent[i]>=i) return out;
			out.bones.fill(-1); out.count=p.links.size();
			for (size_t n=0;n<out.count;++n)
			{
				for (int i=0;i<r.count;++i) if (r.weapon_bones[i] && bones[i].name==p.links[n].rest.name)
				{ if (out.bones[n]>=0) return {}; out.bones[n]=i; }
				if (out.bones[n]<=r.gun || !descendant(out.bones[n],r.gun,r) ||
					(n && r.parent[out.bones[n]]!=out.bones[n-1])) return {};
			}
			out.valid=true; return out;
		}
		class chain
		{
		public:
			using clock=std::chrono::steady_clock;
			void reset() noexcept { *this={}; }
			const std::array<vec,4>& angles() const noexcept { return angles_; }
			bool update(const secondary_motion_profile& p,const binding& b,const rig& r,std::span<bone> pose,
				vec view_offset,float units,std::uint64_t owner,std::uint64_t reference,std::uint64_t sequence,
				clock::time_point at,bool active) noexcept
			{
				if (!active || !b.valid || b.count!=p.links.size() || !valid(p) || pose.size()!=size_t(r.count) ||
					!finite(view_offset) || !std::isfinite(units) || units<=0 || !owner || !reference || !sequence)
				{ reset(); return false; }
				for (size_t n=0;n<b.count;++n)
				{
					if (b.bones[n]<0 || b.bones[n]>=r.count || r.parent[b.bones[n]]<0 || r.parent[b.bones[n]]>=b.bones[n]) { reset(); return false; }
					for (int i:{b.bones[n],r.parent[b.bones[n]]})
					{
						float norm{}; for (float x:pose[i].rotation) { if (!std::isfinite(x)) { reset(); return false; } norm+=x*x; }
						if (!finite(pose[i].position) || norm<.5f || norm>1.5f) { reset(); return false; }
					}
				}
				const auto root=scale(add(compose(as_anchor(pose[r.parent[b.bones[0]]]),p.links[0].rest.local).position,view_offset),1/units);
				if (!finite(root)) { reset(); return false; }
				float dt=std::chrono::duration<float>(at-at_).count();
				const bool changed=definition_!=&p || owner_!=owner || reference_!=reference || indices_!=b.bones ||
					sequence<sequence_ || at<at_ || dt>.05f || (sequence_ && length(sub(root,previous_root_))>.25f);
				if (changed)
				{
					reset(); definition_=&p; owner_=owner; reference_=reference; indices_=b.bones;
					previous_root_=root; dt=0;
				}
				vec acceleration{};
				const bool advance=sequence!=sequence_ && dt>=.0001f;
				if (advance)
				{
					const auto velocity=scale(sub(root,previous_root_),1/dt);
					if (velocity_valid_) acceleration=scale(sub(velocity,previous_velocity_),1/dt);
					const float magnitude=length(acceleration); if (magnitude>35) acceleration=scale(acceleration,35/magnitude);
					previous_velocity_=velocity; velocity_valid_=true;
				}
				const auto force=sub(vec{0,0,-9.81f},acceleration);
				for (size_t n=0;n<b.count;++n)
				{
					const auto& link=p.links[n]; const auto parent=as_anchor(pose[r.parent[b.bones[n]]]);
					const auto direction=rotate(link.rest.local.rotation,unit(n+1<b.count ? p.links[n+1].rest.local.position : p.tip));
					const auto torque=scale(cross(direction,rotate(conjugate(parent.rotation),force)),p.force_gain);
					if (advance)
					{
						const int steps=std::clamp(int(std::ceil(dt*240)),1,12); const float h=dt/steps,drag=std::exp(-p.damping*h);
						for (int step=0;step<steps;++step) for (int a=0;a<3;++a)
						{
							velocity_[n][a]=(velocity_[n][a]+(torque[a]-p.stiffness*angles_[n][a])*h)*drag;
							const float angle=angles_[n][a]+velocity_[n][a]*h;
							angles_[n][a]=std::clamp(angle,link.lower[a],link.upper[a]);
							if (angle!=angles_[n][a]) velocity_[n][a]=0;
						}
					}
					const float angle=length(angles_[n]); const auto axis=unit(angles_[n]); const float sine=std::sin(angle*.5f);
					const quat swing{axis[0]*sine,axis[1]*sine,axis[2]*sine,std::cos(angle*.5f)};
					auto local=link.rest.local; local.rotation=normalize(multiply(swing,local.rotation));
					move_part(r,b.bones[n],compose(parent,local),pose);
				}
				if (sequence!=sequence_) { sequence_=sequence; at_=at; previous_root_=root; }
				return true;
			}
		private:
			const secondary_motion_profile* definition_{};
			std::array<int,4> indices_{};
			std::array<vec,4> angles_{},velocity_{};
			vec previous_root_{},previous_velocity_{};
			bool velocity_valid_{};
			std::uint64_t owner_{},reference_{},sequence_{};
			clock::time_point at_{};
		};
	}
}
