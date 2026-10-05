#pragma once
#include "hinged_attachment.hpp"
#include "weapons/attachments/heartbeat_poses.hpp"

namespace vr::gameplay::weapons::heartbeat
{
	using namespace hands;
	inline constexpr std::array<std::string_view,9> bone_names{"tag_heartbeat","j_motion_tracker_roty","j_motion_tracker_rotz","tag_motion_tracker","j_wire_base","tag_screen_bl","tag_screen_br","tag_screen_tl","tag_screen_tr"};
	inline constexpr std::array<int,9> parents{-1,0,1,2,3,3,3,3,3};
	struct housing_bounds{vec low{},high{};};
	struct part_rig{bool valid{};std::array<int,9> bones{};std::array<anchor,9> local{};housing_bounds housing{};};
	inline part_rig bind(std::span<const model_definition> models,const rig& r,std::span<const bone_definition> bones,housing_bounds bounds)noexcept
	{
		part_rig out;if(r.gun<0 || r.gun>=r.count || bones.size()!=size_t(r.count))return out;
		for(int i=0;i<3;++i)if(!std::isfinite(bounds.low[i]) || !std::isfinite(bounds.high[i]) ||
			bounds.high[i]<=bounds.low[i])return out;
		out.housing=bounds;
		const model_definition* sensor{};
		for(const auto& m:models)if(m.name=="attach_h2_heartbeat_vm" || m.name=="attach_h2_heartbeat_vm_arctic")
		{if(sensor)return {};sensor=&m;}
		if(!sensor || sensor->count!=9 || sensor->begin<=r.gun || sensor->begin+9>r.count)return out;
		const int root_parent=r.parent[sensor->begin];
		if(root_parent<=r.gun || root_parent>=sensor->begin || bones[root_parent].name!="tag_heartbeat")return {};
		for(int i=0;i<9;++i)
		{
			const int b=sensor->begin+i,p=i ? sensor->begin+parents[i] : r.gun;
			if(bones[b].name!=bone_names[i] || (i && r.parent[b]!=p))return {};
			out.bones[i]=b;out.local[i]=hands::pose_math::compose(hands::pose_math::inverse(hands::pose_math::as_anchor(bones[p].bind)),hands::pose_math::as_anchor(bones[b].bind));
			// XModel bind arrays have separate origins. A duplicate root aliases
			// the receiver marker; the attachment's own root bind is not gun-local.
			if(!i)out.local[i]=hands::pose_math::compose(hands::pose_math::inverse(hands::pose_math::as_anchor(bones[r.gun].bind)),hands::pose_math::as_anchor(bones[root_parent].bind));
			if(!hinged_attachment::finite(out.local[i]))return {};
		}
		out.valid=true;return out;
	}
	inline quat wire_rotation(float travel)noexcept
	{
		// Continuous cubic quaternion curve through the witnessed native wire
		// poses. Only the cable uses these knots; they never quantize the hinge.
		const float f=std::clamp(travel,0.f,16.f);const int i=std::min(int(f),15);const float t=f-float(i);
		const auto a=authored::rotations[i][2];
		const auto aligned=[&](int index){auto q=authored::rotations[std::clamp(index,0,16)][2];float d{};
			for(int j=0;j<4;++j)d+=a[j]*q[j];if(d<0)for(auto& v:q)v=-v;return q;};
		const auto previous=aligned(i-1),b=aligned(i+1),next=aligned(i+2);quat out{};
		for(int j=0;j<4;++j)out[j]=.5f*(2*a[j]+(-previous[j]+b[j])*t+
			(2*previous[j]-5*a[j]+4*b[j]-next[j])*t*t+(-previous[j]+3*a[j]-3*b[j]+next[j])*t*t*t);
		return normalize(out);
	}
	inline float wire_fold_travel(float amount)noexcept
	{
		const float angle=hinged_attachment::stroke*(1-std::clamp(amount,0.f,1.f));
		const auto native_angle=[](int i){const auto q=authored::rotations[i][1];return 2*std::atan2(q[2],q[3]);};
		for(int i=0;i<12;++i){const auto a=native_angle(i),b=native_angle(i+1);if(angle>=b)return float(i)+std::clamp((a-angle)/(a-b),0.f,1.f);}
		return 12; // The remaining native motion is the automatic screen tilt.
	}
	inline std::array<anchor,9> pose(const part_rig& r,float amount,float tilt=1)noexcept
	{
		amount=std::clamp(amount,0.f,1.f);tilt=std::clamp(tilt,0.f,1.f);auto out=r.local;
		const float y=std::atan2(authored::rotations.back()[0][1],authored::rotations.back()[0][3])*amount*tilt;
		const float z=hinged_attachment::stroke*(1-amount)*.5f;
		out[1].rotation={0,std::sin(y),0,std::cos(y)};out[2].rotation={0,0,std::sin(z),std::cos(z)};
		out[4].rotation=wire_rotation(wire_fold_travel(amount)+4*amount*tilt);
		for(int j=1;j<9;++j)out[j]=hands::pose_math::compose(out[parents[j]],out[j]);return out;
	}
	inline hinged_attachment::path manipulation_path(const part_rig& r,anchor wrist,float units,float amount,float tilt)noexcept
	{
		using namespace hands::pose_math;hinged_attachment::path out;
		auto y=r.local[1],z=r.local[2];y.rotation=z.rotation={0,0,0,1};
		out.hinge=compose(compose(r.local[0],y),z);out.wrist=compose(r.local[3],wrist);
		const auto mechanical=pose(r,amount,tilt);out.contact=compose(mechanical[3],wrist);
		// Native skin bounds belong to the moving Z bone, not the tag-only
		// housing marker or the screen quad. This includes the whole casing.
		out.housing=mechanical[2];out.housing.position=scale(out.housing.position,1/units);
		out.low=scale(r.housing.low,1/units);out.high=scale(r.housing.high,1/units);out.housing_valid=r.valid;
		out.hinge.position=scale(out.hinge.position,1/units);out.wrist.position=scale(out.wrist.position,1/units);
		out.contact.position=scale(out.contact.position,1/units);return out;
	}
}
