#pragma once
#include "hand_rig_builder.hpp"

namespace vr::gameplay::hands
{
	inline constexpr size_t hand_contact_count=21; // 15 joints, five tips, palm.
	struct contact_rig
	{
		int wrist{-1}; std::array<int,15> fingers{}; bool valid{};
	};
	inline contact_rig bind_contacts(const rig& r,std::span<const bone_definition> bones,int side) noexcept
	{
		contact_rig out;
		if (side<0 || side>1 || r.count<=0 || r.count>256 || bones.size()!=size_t(r.count)) return out;
		for (int i=0;i<r.count;++i) if (r.parent[i]<-1 || r.parent[i]>=i) return out;
		out.wrist=r.arms[side].wrist;
		if (out.wrist<0 || out.wrist>=r.count) return out;
		constexpr std::string_view names[]{"j_thumb_le_0","j_thumb_le_1","j_thumb_le_2",
			"j_index_le_0","j_index_le_1","j_index_le_2","j_mid_le_0","j_mid_le_1","j_mid_le_2",
			"j_ring_le_0","j_ring_le_1","j_ring_le_2","j_pinky_le_0","j_pinky_le_1","j_pinky_le_2"};
		constexpr std::string_view right_names[]{"j_thumb_ri_0","j_thumb_ri_1","j_thumb_ri_2",
			"j_index_ri_0","j_index_ri_1","j_index_ri_2","j_mid_ri_0","j_mid_ri_1","j_mid_ri_2",
			"j_ring_ri_0","j_ring_ri_1","j_ring_ri_2","j_pinky_ri_0","j_pinky_ri_1","j_pinky_ri_2"};
		out.fingers.fill(-1);
		for (int i=0;i<r.count;++i) for (size_t n=0;n<out.fingers.size();++n)
			if (bones[i].name==(side ? right_names[n] : names[n]))
			{
				if (out.fingers[n]>=0 || r.weapon_bones[i] || !descendant(i,out.wrist,r)) return {};
				out.fingers[n]=i;
			}
		out.valid=std::all_of(out.fingers.begin(),out.fingers.end(),[](int i){return i>=0;});
		return out;
	}
	// Borrow only the current glove's anatomical shape, never its IK placement.
	// Rebase it onto the raw wrist so a snapped hand cannot manufacture impacts.
	inline bool contact_points(const contact_rig& r,std::span<const bone> pose,anchor raw,
		std::array<vec,hand_contact_count>& out) noexcept
	{
		if (!r.valid || r.wrist<0 || size_t(r.wrist)>=pose.size()) return false;
		const auto& wrist=pose[r.wrist];
		for (const auto q:{wrist.rotation,raw.rotation})
		{
			float norm{}; for (float x:q) { if (!std::isfinite(x)) return false; norm+=x*x; }
			if (norm<.5f || norm>1.5f) return false;
		}
		const auto inverse=conjugate(normalize(wrist.rotation)); raw.rotation=normalize(raw.rotation);
		for (size_t n=0;n<r.fingers.size();++n)
		{
			const int i=r.fingers[n]; if (i<0 || size_t(i)>=pose.size()) return false;
			out[n]=rotate(inverse,sub(pose[i].position,wrist.position));
		}
		for (int f=0;f<5;++f) out[15+f]=add(out[3*f+2],scale(sub(out[3*f+2],out[3*f+1]),.75f));
		out[20]=scale(add(out[3],out[12]),.35f);
		for (auto& point:out)
		{
			point=add(raw.position,rotate(raw.rotation,point));
			for (float x:point) if (!std::isfinite(x)) return false;
		}
		return true;
	}
}
