#pragma once
#include "viewmodel_policy.hpp"
#include "hands/rig_builder.hpp"

namespace vr::gameplay::weapons
{
	// Pure bounded assembly validation; resolve once with the rig, never search
	// names on render workers. Unknown assemblies keep their original behavior.
	struct attachment_binding { int parent{-1}, muzzle{-1}; bool valid{}; };
	// Shared role/cardinality vocabulary for reviewed rifle assemblies. Roles
	// describe geometry only; a launcher or shotgun role never enables its feed.
	enum class attachment_role { foregrip, launcher, cover, silencer, optic, sensor, laser, shotgun, bipod, count };
	struct assembly_attachment { attachment_contract contract; attachment_role role; int bones; };
	struct attachment_set_binding
	{
		std::array<unsigned,static_cast<size_t>(attachment_role::count)> counts{};
		int muzzle{-1}; bool valid{};
	};
	inline attachment_binding bind_attachment(const attachment_contract& policy,
		const hands::model_definition& model, const hands::model_definition& receiver,
		const hands::rig& rig, std::span<const hands::bone_definition> bones) noexcept
	{
		if (rig.count <= 0 || rig.count > 256 || bones.size() != static_cast<size_t>(rig.count) ||
			model.begin < 0 || model.count <= 0 || model.begin >= rig.count || model.count > rig.count-model.begin ||
			receiver.begin < 0 || receiver.count <= 0 || receiver.begin >= rig.count || receiver.count > rig.count-receiver.begin ||
			model.name != policy.model || bones[model.begin].name != policy.root) return {};
		if (model.begin < receiver.begin+receiver.count && receiver.begin < model.begin+model.count) return {};
		for (int i=0;i<rig.count;++i)
			if (rig.parent[i] < -1 || rig.parent[i] >= i) return {};
		int parent=-1;
		for (int i=receiver.begin;i<receiver.begin+receiver.count;++i)
			if (bones[i].name == policy.receiver_parent)
			{
				if (parent >= 0) return {};
				parent=i;
			}
		if (parent < 0 || parent == rig.gun || !rig.weapon_bones[parent] ||
			rig.parent[model.begin] != parent) return {};
		int muzzle=-1;
		for (int i=model.begin;i<model.begin+model.count;++i)
		{
			if (!hands::descendant(i,model.begin,rig)) return {};
			if (!policy.muzzle.empty() && bones[i].name==policy.muzzle)
			{
				if (muzzle >= 0 || i==model.begin) return {};
				muzzle=i;
			}
		}
		if (!policy.muzzle.empty())
		{
			if (muzzle < 0) return {};
			const auto& root=bones[model.begin].bind;
			const auto& tip=bones[muzzle].bind;
			for (const auto* b : {&root,&tip})
			{
				float norm{};
				for (float x:b->position) if (!std::isfinite(x) || std::abs(x)>1e5f) return {};
				for (float x:b->rotation) { if (!std::isfinite(x)) return {}; norm+=x*x; }
				if (norm < .5f || norm > 1.5f) return {};
			}
			const auto inverse=hands::conjugate(hands::normalize(root.rotation));
			const auto delta=hands::rotate(inverse,hands::sub(tip.position,root.position));
			const auto forward=hands::rotate(hands::multiply(inverse,hands::normalize(tip.rotation)),{1,0,0});
			if (delta[0]<=0 || forward[0]<.98f) return {};
		}
		return {parent,muzzle,true};
	}
	inline bool bind_hidden_attachment(const attachment_contract& policy,
		const hands::model_definition& model, const hands::model_definition& receiver,
		const hands::rig& rig, std::span<const hands::bone_definition> bones, part_mask& hidden) noexcept
	{
		const auto binding=bind_attachment(policy,model,receiver,rig,bones);
		if (!binding.valid || binding.muzzle>=0) return false; // A hidden prop cannot supply a firing origin.
		// Include the receiver alias as well as model-local bones: both duplicate
		// roots and independent rigid draw groups can reference the attachment.
		hidden[binding.parent/32] |= 0x80000000u >> (binding.parent%32);
		for (int i=model.begin;i<model.begin+model.count;++i)
			hidden[i/32] |= 0x80000000u >> (i%32);
		return true;
	}
	inline attachment_set_binding bind_attachment_set(std::span<const assembly_attachment> policies,
		std::span<const hands::model_definition> models,const hands::model_definition& receiver,
		const hands::rig& rig,std::span<const hands::bone_definition> bones) noexcept
	{
		attachment_set_binding out;
		if (rig.count<=0 || rig.count>256 || rig.gun<0 || rig.gun>=rig.count ||
			models.size()>32 || policies.size()>64 || bones.size()!=static_cast<size_t>(rig.count) ||
			receiver.begin<0 || receiver.count<=0 || receiver.begin>rig.gun ||
			receiver.count>rig.count-receiver.begin || rig.gun>=receiver.begin+receiver.count) return {};
		for (const auto& model:models) if (&model!=&receiver)
		{
			if (model.name==receiver.name || model.begin<0 || model.count<=0 || model.begin>rig.count || model.count>rig.count-model.begin) return {};
			const assembly_attachment* policy{};
			for (const auto& item:policies) if (item.contract.model==model.name) { policy=&item; break; }
			if (!policy)
			{
				for (int i=model.begin;i<model.begin+model.count;++i) if (rig.weapon_bones[i]) return {};
				continue;
			}
			const auto binding=bind_attachment(policy->contract,model,receiver,rig,bones);
			const auto role=static_cast<size_t>(policy->role);
			if (role>=out.counts.size() || model.count!=policy->bones || !binding.valid || ++out.counts[role]>1) return {};
			if (binding.muzzle>=0) { if (out.muzzle>=0) return {}; out.muzzle=binding.muzzle; }
		}
		out.valid=true; return out;
	}
}
