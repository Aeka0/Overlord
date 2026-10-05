#pragma once

#include "hand_skeleton.hpp"
#include "native_muzzle_contract.hpp"
#include "special_melee.hpp"
#include <string_view>

namespace vr::gameplay::hands
{
	struct rig_resolution
	{
		rig layout{};
		int hands_model{-1}, weapon_model{-1};
		std::string_view contract;
		int muzzle_bone{-1};
		float muzzle_forward_dot{};
		const char* rejection{"unresolved rig"};
	};

	enum class rig_kind { firearm, hands_only, shield, melee, scripted_body };
	inline rig_resolution resolve_rig(std::span<const model_definition> models,
									  std::span<const bone_definition> bones, rig_kind kind = rig_kind::firearm) noexcept
	{
		rig_resolution result{};
		const bool arms_only=kind==rig_kind::hands_only || kind==rig_kind::scripted_body;
		auto& r = result.layout;
		const auto reject = [&](const char* message) {
			result.rejection = message;
			return result;
		};
		if (models.empty() || models.size() > 32 || bones.empty() || bones.size() > 255)
			return reject("model/bone budget rejected");
		r.count = static_cast<int>(bones.size());
		r.parent.fill(-1);
		int next{};
		for (size_t m = 0; m < models.size(); ++m)
		{
			const auto& model = models[m];
			if (model.begin != next || model.count <= 0 || model.count > r.count - next)
				return reject("model ranges rejected");
			next += model.count;
		}
		if (next != r.count)
			return reject("unowned bones rejected");
		for (int i = 0; i < r.count; ++i)
		{
			if (bones[i].parent < -1 || bones[i].parent >= i)
				return reject("non-topological parent rejected");
			r.parent[i] = bones[i].parent;
		}
		const auto find_unique = [&](int m, std::string_view name) {
			int found = -1;
			for (int i = models[m].begin; i < models[m].begin + models[m].count; ++i)
				if (bones[i].name == name)
				{
					if (found >= 0)
						return -1;
					found = i;
				}
			return found;
		};
		// Model names vary across H1/H2 assets, skins and hand sets. Select roles
		// by unique semantic nodes within this already-proven first-person DObj.
		constexpr std::array<std::string_view, 7> hand_nodes{"j_shoulder_le", "j_shoulder_ri", "j_elbow_le",
															 "j_elbow_ri",	  "j_wrist_le",	   "j_wrist_ri",
															 "tag_weapon"};
		for (size_t m = 0; m < models.size(); ++m)
		{
			if (std::all_of(hand_nodes.begin(), hand_nodes.end(),
							[&](auto name) { return find_unique(static_cast<int>(m), name) >= 0; }))
			{
				if (result.hands_model >= 0)
					return reject("multiple semantic hand rigs rejected");
				result.hands_model = static_cast<int>(m);
			}
			for (int i = models[m].begin; kind!=rig_kind::scripted_body && i < models[m].begin + models[m].count; ++i)
				if (bones[i].name == (kind==rig_kind::melee ? weapons::special_melee::root(models[m].name) : "j_gun"))
				{
					if (result.weapon_model >= 0)
						return reject("multiple gun roots rejected");
					result.weapon_model = static_cast<int>(m);
				}
		}
		if (result.hands_model < 0 || (!arms_only &&
			(result.weapon_model < 0 || result.hands_model == result.weapon_model)))
			return reject("separate semantic hand/receiver models required");
		r.arms = {
			arm{find_unique(result.hands_model, "j_shoulder_le"), find_unique(result.hands_model, "j_elbow_le"),
				find_unique(result.hands_model, "j_wrist_le")},
			arm{find_unique(result.hands_model, "j_shoulder_ri"), find_unique(result.hands_model, "j_elbow_ri"),
				find_unique(result.hands_model, "j_wrist_ri")}};
		r.weapon_tag = find_unique(result.hands_model, "tag_weapon");
		// H2's native single-held animation defines its rear-grip relation at
		// the right wrist. This asset convention is not VR hand ownership.
		r.rear_grip_wrist = r.arms[1].wrist;
		r.gun = result.weapon_model >= 0 ? find_unique(result.weapon_model,
			kind==rig_kind::melee ? weapons::special_melee::root(models[result.weapon_model].name) : "j_gun") : -1;
		if (r.weapon_tag < 0 || (!arms_only && r.gun < 0))
			return reject("weapon anchor missing or ambiguous");
		for (const auto& a : r.arms)
			if (a.shoulder < 0 || a.elbow <= a.shoulder || a.wrist <= a.elbow ||
				!descendant(a.elbow, a.shoulder, r) || !descendant(a.wrist, a.elbow, r))
				return reject("arm chain missing or ambiguous");
		if (descendant(r.arms[0].shoulder, r.arms[1].shoulder, r) ||
			descendant(r.arms[1].shoulder, r.arms[0].shoulder, r) ||
			descendant(r.weapon_tag, r.arms[0].shoulder, r) || descendant(r.weapon_tag, r.arms[1].shoulder, r))
			return reject("overlapping arm/weapon ownership rejected");
		if (kind == rig_kind::hands_only)
		{
			if (models.size() != 1 || result.weapon_model >= 0)
				return reject("hands-only presentation must contain only its hand model");
			r.rear_grip_wrist = -1;
			result.rejection = nullptr;
			result.contract = "independent-arms";
			return result;
		}
		if(kind==rig_kind::scripted_body)
		{
			// The scene admits the body model. Other models must belong to a
			// wrist (picks/knife), never a detached gun or a second full body.
			for(size_t m=0;m<models.size();++m)if(int(m)!=result.hands_model)
				for(int i=models[m].begin;i<models[m].begin+models[m].count;++i)
					if(!descendant(i,r.arms[0].wrist,r) && !descendant(i,r.arms[1].wrist,r))
						return reject("scripted prop is not attached to a wrist");
			r.rear_grip_wrist=-1;result.rejection=nullptr;result.contract="native-body-arms";return result;
		}
		if (!descendant(r.gun, r.weapon_tag, r))
			return reject("weapon is not attached to hand-rig weapon tag");
		if(kind==rig_kind::melee)
		{
			const auto& model=models[result.weapon_model];
			if(models.size()!=2 || model.count!=weapons::special_melee::bone_count(model.name) || r.gun!=model.begin)
				return reject("unreviewed melee assembly");
			for(int i=0;i<model.count;++i)
			{
				const auto name=i==0 ? weapons::special_melee::root(model.name) : i==model.count-1 ? "tag_knife_fx" : "tag_clip";
				if(bones[model.begin+i].name!=name || (i && r.parent[model.begin+i]!=r.gun))return reject("melee skeleton changed");
				const auto& bind=bones[model.begin+i].bind;float norm{};
				for(float x:bind.rotation){if(!std::isfinite(x))return reject("invalid melee bind");norm+=x*x;}
				if(norm<.5f || norm>1.5f || !std::all_of(bind.position.begin(),bind.position.end(),[](float x){return std::isfinite(x) && std::abs(x)<1000;}))return reject("invalid melee bind");
				r.weapon_bones[model.begin+i]=true;
			}
			result.rejection=nullptr;result.contract="single-root-melee-blade";return result;
		}
		result.muzzle_bone = find_unique(result.weapon_model, "tag_flash");
		r.muzzle = result.muzzle_bone;
		if (result.muzzle_bone < 0 || !descendant(result.muzzle_bone, r.gun, r))
			return reject("single receiver muzzle required");
		// Native tag lookup starts with the receiver; attachment models may
		// repeat its socket name. Missing/ambiguous laser tags do not reject guns.
		r.laser=find_unique(result.weapon_model,"tag_laser");
		if(r.laser>=0 && !descendant(r.laser,r.gun,r))r.laser=-1;
		const auto& gun_bind = bones[r.gun].bind;
		const auto& muzzle_bind = bones[result.muzzle_bone].bind;
		for (const auto* bind : {&gun_bind, &muzzle_bind})
		{
			float norm{};
			for (auto x : bind->rotation)
			{
				if (!std::isfinite(x))
					return reject("invalid muzzle bind rotation");
				norm += x * x;
			}
			if (norm < 0.5f || norm > 1.5f)
				return reject("muzzle bind basis unavailable");
			for (auto x : bind->position)
				if (!std::isfinite(x))
					return reject("invalid muzzle bind position");
		}
		const auto inverse = conjugate(normalize(gun_bind.rotation));
		const auto muzzle_rotation=normalize(multiply(inverse,normalize(muzzle_bind.rotation)));
		const auto forward = rotate(muzzle_rotation, {1, 0, 0});
		const auto offset = rotate(inverse, sub(muzzle_bind.position, gun_bind.position));
		result.muzzle_forward_dot = forward[0];
		// Root +X is the accepted M9/M4 aiming convention. Bind-space checking
		// avoids reload/recoil animation changing eligibility frame by frame.
		// Five degrees is a conservative admission tolerance, not auto-calibration.
		if (kind==rig_kind::shield && (models[result.weapon_model].name!="h2_viewmodel_riot_shield_mp" || models[result.weapon_model].count!=17))
			return reject("unreviewed shield receiver");
		const auto* reviewed=kind==rig_kind::firearm ? reviewed_muzzle(models[result.weapon_model].name) : nullptr;
		if(reviewed && !reviewed->matches(models[result.weapon_model].count,r.parent[r.muzzle],r.gun,offset,muzzle_rotation))
			return reject("reviewed receiver muzzle bind contract rejected");
		if (kind!=rig_kind::shield && !reviewed && (forward[0] < 0.9961947f || !std::isfinite(offset[0]) || offset[0] <= 0.01f))
			return reject("nonstandard muzzle axis requires dedicated adapter");
		for (int i = 0; i < r.count; ++i)
		{
			if (bones[i].name == "j_gun" && i != r.gun)
				return reject("secondary gun root rejected");
			r.weapon_bones[i] = descendant(i, r.gun, r);
		}
		for (const auto& a : r.arms)
			if (r.weapon_bones[a.shoulder])
				return reject("hand rig parented to weapon rejected");
		// Every extra model must have proven ownership. Do not assume that all
		// models following the gun are weapon parts: sleeves/camera props may follow.
		for (size_t m = 0; m < models.size(); ++m)
		{
			if (static_cast<int>(m) == result.hands_model)
				continue;
			const auto& model = models[m];
			for (int i = model.begin; i < model.begin + model.count; ++i)
			{
				if (r.weapon_bones[i])
					continue;
				if (static_cast<int>(m) == result.weapon_model)
					return reject("weapon contains detached root");
				int root = i;
				while (r.parent[root] >= 0)
					root = r.parent[root];
				const auto& hand_model = models[result.hands_model];
				if (root < hand_model.begin || root >= hand_model.begin + hand_model.count)
					return reject("attachment ownership unavailable");
			}
		}
		result.rejection = nullptr;
		result.contract = kind==rig_kind::shield ? "single-root-shield-frame" : reviewed ? "single-root-reviewed-muzzle" : "single-root-forward-muzzle";
		return result;
	}
}
