#pragma once
#include "skeleton.hpp"
#include "pose_schema.hpp"
namespace vr::gameplay::hands::trigger_index {struct profile;}

namespace vr::gameplay::hands
{
	// Some native gloves (viewhands_marine_sniper) omit the two skin-webbing
	// leaves. Finger/palm articulation is unchanged; absent deformation leaves
	// must not reject the entire weapon and both arms. Present nodes still pass
	// the same topology, duplicate and bind-transform validation below.
	inline bool optional_hand_joint(std::string_view name) noexcept
	{return name=="j_webbing_le" || name=="j_webbing_ri";}
	inline quat blend_quat(quat a, quat b, float t) noexcept
	{
		float d{};
		for (int i = 0; i < 4; ++i)
			d += a[i] * b[i];
		if (d < 0)
			for (auto& x : b)
				x = -x;
		for (int i = 0; i < 4; ++i)
			a[i] += (b[i] - a[i]) * std::clamp(t, 0.0f, 1.0f);
		return normalize(a);
	}
	struct pose_library
	{
		const trigger_index::profile* safe_index{}; // Resolved once with this registered weapon's pose schema.
		std::array<anchor, 256> rest_local{};
		std::array<int, 256> finger{}, part{};
		std::array<int,256> opposite{};
		std::array<quat,256> mirror_basis{};
		bool valid{};
		bool fixed_parts{};
		std::array<quat,2> neutral_wrists{{{0,0,0,1},{0,0,0,1}}};
	};
	inline pose_library bind_poses(const rig& r, std::span<const bone_definition> bones,
								   const pose_schema& profile) noexcept
	{
		pose_library out{};
		out.finger.fill(-1);
		out.part.fill(-1);
		out.opposite.fill(-1);
		out.mirror_basis.fill({0,0,0,1});
		if (r.count <= 0 || r.count > 256 || bones.size() != static_cast<size_t>(r.count) ||
			profile.fingers.empty() || profile.fingers.size() > 64 || profile.equip_rest.size() > static_cast<size_t>(r.count))
			return out;
		std::array<bool, 64> finger_seen{};
		if(r.weapon_tag>=0 && r.weapon_tag<r.count)
			for(int h=0;h<2;++h)if(r.arms[h].wrist>=0 && r.arms[h].wrist<r.count)
				out.neutral_wrists[h]=normalize(multiply(conjugate(normalize(bones[r.weapon_tag].bind.rotation)),bones[r.arms[h].wrist].bind.rotation));
		// Articulated belts and their receiver details can exceed 32 rest poses.
		// Bound them by the same native bone capacity as the destination library.
		std::array<bool, 256> part_seen{};
		size_t found{};
		for (int i = 0; i < r.count; ++i)
		{
			// Reflect pose rotations, then convert into the destination bone's
			// anatomical axes. Left/right joint axes need not themselves be mirrors.
			const auto name=bones[i].name;
			// Match the side suffix, not the first characters of "_ring".
			const auto side_marker=[&](std::string_view token) {
				const auto at=name.rfind(token);
				return at!=std::string_view::npos && (at+3==name.size() || name[at+3]=='_') ? at : std::string_view::npos;
			};
			const auto side=side_marker("_le");const auto right=side_marker("_ri");
			const auto marker=side!=std::string_view::npos ? side : right;
			if (marker!=std::string_view::npos)
				for (int j=0;j<r.count;++j)
				{
					const auto other=bones[j].name;
					if (other.size()!=name.size() || other.substr(0,marker)!=name.substr(0,marker) ||
						other.substr(marker+3)!=name.substr(marker+3) || other.substr(marker,3)!=(side!=std::string_view::npos ? "_ri" : "_le")) continue;
					out.opposite[i]=j;
					const auto q=normalize(bones[j].bind.rotation);
					out.mirror_basis[i]=normalize(multiply(conjugate(quat{-q[0],q[1],-q[2],q[3]}),normalize(bones[i].bind.rotation)));
					break;
				}
			for (size_t j = 0; j < profile.fingers.size(); ++j)
				if (bones[i].name == profile.fingers[j].name)
				{
					if (finger_seen[j])
						return out;
					finger_seen[j] = true;
					if (r.parent[i] < 0 ||
						(!descendant(i, r.arms[0].wrist, r) && !descendant(i, r.arms[1].wrist, r)))
						return out;
					for (const auto index : {i, r.parent[i]})
					{
						float norm{};
						for (auto x : bones[index].bind.rotation)
						{
							if (!std::isfinite(x))
								return out;
							norm += x * x;
						}
						if (norm < 0.5f || norm > 1.5f)
							return out;
					}
					const auto inverse = conjugate(normalize(bones[r.parent[i]].bind.rotation));
					out.rest_local[i] = {
						rotate(inverse, sub(bones[i].bind.position, bones[r.parent[i]].bind.position)),
						normalize(multiply(inverse, normalize(bones[i].bind.rotation)))};
					for (auto x : out.rest_local[i].position)
						if (!std::isfinite(x))
							return out;
					for (auto x : out.rest_local[i].rotation)
						if (!std::isfinite(x))
							return out;
					out.finger[i] = static_cast<int>(j);
					++found;
				}
			for (size_t j = 0; j < profile.equip_rest.size(); ++j)
				if (bones[i].name == profile.equip_rest[j].name && r.weapon_bones[i])
				{
					if (part_seen[j])
						return out;
					part_seen[j] = true;
					out.part[i] = static_cast<int>(j);
					out.fixed_parts |= profile.equip_rest[j].mode == part_pose_mode::fixed_attachment;
				}
		}
		bool complete=found!=0;
		for(size_t i=0;i<profile.fingers.size();++i)complete=complete && (finger_seen[i] || optional_hand_joint(profile.fingers[i].name));
		for(size_t i=0;i<profile.equip_rest.size();++i)
			complete = complete && (part_seen[i] || profile.equip_rest[i].mode == part_pose_mode::fixed_attachment);
		out.valid = complete;
		return out;
	}
}
