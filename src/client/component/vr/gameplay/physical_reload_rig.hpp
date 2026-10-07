#pragma once
#include "hands/rig_builder.hpp"
#include "physical_reload_profile.hpp"
#include "weapon_ejection.hpp"

namespace vr::gameplay::weapons::physical_reload
{
	struct part_rig
	{
		int magazine{-1}, slide{-1}, bullets{-1};
		bool valid{};
		part_mask bullet_mask{};
		hands::anchor bullets_in_magazine{};
		int bolt{-1};
		hands::contact_rig slap_hand{};
		hands::contact_rig right_slap_hand{};
		part_mask animation_only_magazines{};
		int ejection{-1};
		ejection_port port{};
		std::array<int, 12> magazine_variants{};
		size_t magazine_variant_count{};
		int cover{-1};
		std::array<int, 24> belt_links{};
		size_t belt_link_count{};
		int bridge{-1};
		std::array<int, 8> belt_details{};
		int fold_end{-1};       // Optional direct child; never rotates its translating carrier.
		int partition_root{-1}; // Only this rigid group is replaced; never its descendants.
		int guide_catch{-1};    // Cosmetic only; missing geometry cannot disable mechanics.
		int guide_detail{-1};
	};
	inline part_rig bind_parts(const hands::rig& r,
	                           std::span<const hands::bone_definition> bones,
	                           const reload_profile& definition) noexcept
	{
		part_rig out;
		if (r.count <= 0 || r.count > 256 || r.gun < 0 || r.gun >= r.count ||
		    bones.size() != static_cast<size_t>(r.count) || !valid_partition_profile(definition))
			return out;
		for (int i = 0; i < r.count; ++i)
			if (r.weapon_bones[i])
			{
				if (bones[i].name == definition.magazine_bone)
				{
					if (out.magazine >= 0)
						return {};
					out.magazine = i;
				}
				if (bones[i].name == definition.slide_bone)
				{
					if (out.slide >= 0)
						return {};
					out.slide = i;
				}
				if (bones[i].name == definition.bullets_bone)
				{
					if (out.bullets >= 0)
						return {};
					out.bullets = i;
				}
			}
		if (definition.bullet_parents.size() > 8)
			return {};
		const auto find = [&](std::string_view name)
		{
			int found = -1;
			for (int i = 0; i < r.count; ++i)
				if (r.weapon_bones[i] && bones[i].name == name)
				{
					if (found >= 0)
						return -1;
					found = i;
				}
			return found;
		};
		for (size_t i = 0; i < definition.bullet_parents.size(); ++i)
		{
			const auto& contract = definition.bullet_parents[i];
			const int bone = find(contract.bone), parent = find(contract.parent);
			if (definition.receiver_parented_bullets || bone < 0 || parent < 0 || bone == out.magazine ||
			    parent >= bone || r.parent[bone] != parent || !hands::descendant(bone, out.magazine, r))
				return {};
			for (size_t j = 0; j < i; ++j)
				if (definition.bullet_parents[j].bone == contract.bone)
					return {};
		}
		const auto expected_parent = [&](std::string_view name, int fallback)
		{
			for (const auto& contract : definition.bullet_parents)
				if (contract.bone == name)
					return find(contract.parent);
			return fallback;
		};
		if (definition.interaction.receiver_release &&
		    !definition.interaction.receiver_release->visual_bone.empty())
			out.guide_catch = find(definition.interaction.receiver_release->visual_bone);
		if (!definition.action_detail_bone.empty())
		{
			const auto detail = find(definition.action_detail_bone);
			if (detail >= 0 && detail != out.slide && hands::descendant(detail, out.slide, r))
				out.guide_detail = detail;
		}
		if (definition.bolt_partition)
		{
			out.partition_root = find(definition.bolt_partition->motion.bone);
			if (out.partition_root < 0 ||
			    (definition.bolt_partition->motion.bone == "j_gun" ? out.partition_root != r.gun
			                                                       : out.partition_root != out.slide))
				return {};
		}
		if (definition.handle_child_of_bolt &&
		    (!definition.bolt || definition.concealed_bolt || definition.handle_fold ||
		     definition.handle_catch || definition.interaction.manual_bolt))
			return {};
		const int slide_parent = definition.handle_child_of_bolt ? find(definition.bolt->bone) : r.gun;
		out.valid = out.magazine >= 0 && out.slide >= 0 && out.bullets >= 0 && slide_parent >= 0 &&
		            out.magazine != out.slide && out.magazine != out.bullets && out.slide != out.bullets &&
		            r.parent[out.magazine] == r.gun && r.parent[out.slide] == slide_parent &&
		            r.parent[out.bullets] ==
		                expected_parent(definition.bullets_bone,
		                                definition.receiver_parented_bullets ? r.gun : out.magazine);
		if (!out.valid || definition.additional_bullet_bones.size() > 8 ||
		    (definition.interaction.manual_magazine && !definition.magazine_contacts))
			return {};
		if (definition.interaction.manual_magazine && definition.interaction.manual_magazine->spare_strike)
		{
			const auto regions = definition.magazine_contacts->strike_regions;
			if (regions.empty() || regions.size() > physical_reload::max_contact_boxes)
				return {};
			for (const auto& region : regions)
				if (!valid(region))
					return {};
		}
		if (definition.chamber_round)
		{
			if (!definition.rigid_magazine_source || !definition.additional_bullet_bones.empty() ||
			    definition.feeding_path || definition.interaction.manual_bolt)
				return {};
			if (!finite_part_vec(definition.chamber_round->position) ||
			    !finite_part_quat(definition.chamber_round->rotation))
				return {};
		}
		if (definition.interaction.manual_bolt)
		{
			out.ejection = find("tag_brass");
			out.port = bind_ejection_port(r, bones);
			if (!physical_reload::valid(definition.interaction) ||
			    definition.ammunition.feed != mechanics::feed_type::manual_bolt || out.ejection < 0 ||
			    !out.port.valid || definition.bolt || definition.handle_catch || definition.handle_fold ||
			    !definition.feeding_path)
				return {};
			for (const auto& pose : *definition.feeding_path)
			{
				for (float x : pose.position)
					if (!std::isfinite(x) || std::abs(x) > 10000)
						return {};
				float norm{};
				for (float x : pose.rotation)
				{
					if (!std::isfinite(x))
						return {};
					norm += x * x;
				}
				if (std::abs(norm - 1) > .001f)
					return {};
			}
		}
		if (const auto* fold = definition.handle_fold)
		{
			if (!valid_fold(*fold) || definition.interaction.locked_travel != 0 ||
			    (definition.interaction.motion != action_motion::charging_handle &&
			     !(definition.interaction.motion == action_motion::reciprocating_slide &&
			       !fold->end_bone.empty())))
				return {};
			if (!fold->end_bone.empty())
			{
				out.fold_end = find(fold->end_bone);
				if (out.fold_end < 0 || r.parent[out.fold_end] != out.slide || out.fold_end == out.magazine ||
				    out.fold_end == out.bullets)
					return {};
				const auto local = compose_reload(
				    inverse_reload({bones[out.slide].bind.position, bones[out.slide].bind.rotation}),
				    {bones[out.fold_end].bind.position, bones[out.fold_end].bind.rotation});
				if (hands::length(hands::sub(local.position, fold->pivot.position)) > .01f ||
				    std::abs(hands::multiply(hands::conjugate(local.rotation), fold->pivot.rotation)[3]) <
				        .999f)
					return {};
			}
		}
		// Counted detachable magazines may coexist with permanent body parts,
		// receiver-parented ammunition and an independently owned feed/chamber.
		// Native mutually exclusive contents and articulated belts keep their own policy.
		if (definition.magazine_fill() &&
		    (definition.interaction.belt || !definition.magazine_round_variants.empty()))
			return {};
		if (definition.ammunition.manual_catch != bool(definition.handle_catch) ||
		    bool(definition.interaction.manual_catch) != bool(definition.handle_catch) ||
		    (definition.handle_catch && (!valid_catch(*definition.handle_catch) || definition.handle_fold ||
		                                 !valid(*definition.interaction.manual_catch))))
			return {};
		// Open-bolt admission needs its independent internal-bolt presentation;
		// a hand-operated handle at rest is not evidence of an open chamber.
		if (definition.ammunition.feed == mechanics::feed_type::open_bolt &&
		    ((!definition.concealed_bolt && (!definition.bolt || definition.bolt->locked_m <= 0)) ||
		     definition.interaction.motion != action_motion::charging_handle))
			return {};
		if (definition.concealed_bolt &&
		    (definition.bolt || definition.ammunition.feed != mechanics::feed_type::open_bolt))
			return {};
		if (definition.ammunition.belt_fed != bool(definition.interaction.belt))
			return {};
		if (definition.ammunition.belt_bridge !=
		    bool(definition.interaction.belt && definition.interaction.belt->bridge))
			return {};
		if (const auto* p = definition.interaction.belt)
		{
			out.cover = find(p->cover_bone);
			if (out.cover < 0 || r.parent[out.cover] != r.gun || out.cover == out.slide ||
			    out.cover == out.magazine || p->links.size() < 2 || p->links.size() > out.belt_links.size() ||
			    !std::isfinite(p->cover_angle) || p->cover_angle <= 0 || p->cover_angle > 3.141593f ||
			    !finite_part_vec(p->cover_axis) ||
			    std::abs(hands::dot(p->cover_axis, p->cover_axis) - 1) > .001f)
				return {};
			int parent = out.magazine;
			for (const auto& link : p->links)
			{
				const int b = find(link.bone);
				if (b < 0 || r.parent[b] != parent || (out.belt_link_count == 0 && b != out.bullets))
					return {};
				for (const auto& pose : {link.loose, link.seated})
					if (!finite_part_vec(pose.position) || !finite_part_quat(pose.rotation))
						return {};
				out.belt_links[out.belt_link_count++] = b;
				parent = b;
			}
			if (p->bridge)
			{
				const auto& b = *p->bridge;
				out.bridge = find(b.bone);
				if (out.bridge < 0 || r.parent[out.bridge] != r.gun || out.bridge == out.cover ||
				    out.bridge == out.slide || out.bridge == out.magazine || !std::isfinite(b.angle) ||
				    b.angle <= 0 || b.angle > 3.141593f || !finite_part_vec(b.axis) ||
				    std::abs(hands::dot(b.axis, b.axis) - 1) > .001f || !finite_part_vec(b.rest.position) ||
				    !finite_part_quat(b.rest.rotation))
					return {};
			}
			if (p->details.size() > out.belt_details.size())
				return {};
			for (size_t i = 0; i < p->details.size(); ++i)
			{
				const auto& d = p->details[i];
				const int b = find(d.bone);
				if (b < 0 || r.parent[b] != r.gun || b == out.bridge || b == out.cover || b == out.magazine ||
				    b == out.slide || (definition.bolt && d.bone == definition.bolt->bone) ||
				    (d.bridge && !p->bridge))
					return {};
				for (size_t n = 0; n < i; ++n)
					if (out.belt_details[n] == b)
						return {};
				for (auto pose : {d.rest, d.open})
					if (!finite_part_vec(pose.position) || !finite_part_quat(pose.rotation))
						return {};
				out.belt_details[i] = b;
			}
		}
		if (definition.receiver_parented_bullets)
		{
			if (!definition.additional_bullet_bones.empty())
				return {};
			for (const auto index : {out.magazine, out.bullets})
			{
				const auto& bind = bones[index].bind;
				float norm{};
				for (float x : bind.position)
					if (!std::isfinite(x) || std::abs(x) > 10000)
						return {};
				for (float x : bind.rotation)
				{
					if (!std::isfinite(x))
						return {};
					norm += x * x;
				}
				if (std::abs(norm - 1) > .01f)
					return {};
			}
			const auto& mag = bones[out.magazine].bind;
			const auto& round = bones[out.bullets].bind;
			out.bullets_in_magazine = compose_reload(inverse_reload({mag.position, mag.rotation}),
			                                         {round.position, round.rotation});
		}
		const auto add_bullet = [&](int root)
		{
			for (int i = 0; i < r.count; ++i)
				if (hands::descendant(i, root, r))
					out.bullet_mask[i / 32] |= 0x80000000u >> (i % 32);
		};
		add_bullet(out.bullets);
		for (size_t n = 0; n < definition.additional_bullet_bones.size(); ++n)
		{
			const auto name = definition.additional_bullet_bones[n];
			for (size_t prior = 0; prior < n; ++prior)
				if (definition.additional_bullet_bones[prior] == name)
					return {};
			int found = -1;
			for (int i = 0; i < r.count; ++i)
				if (r.weapon_bones[i] && bones[i].name == name)
				{
					if (found >= 0)
						return {};
					found = i;
				}
			if (found < 0 || found == out.bullets || r.parent[found] != expected_parent(name, out.magazine) ||
			    (definition.bullet_parents.empty() &&
			     (out.bullet_mask[found / 32] & (0x80000000u >> (found % 32)))))
				return {};
			add_bullet(found);
		}
		if (definition.bolt)
		{
			if (definition.interaction.motion != action_motion::charging_handle ||
			    !valid_bolt(*definition.bolt, definition.interaction.slide_stroke))
				return {};
			for (int i = 0; i < r.count; ++i)
				if (r.weapon_bones[i] && bones[i].name == definition.bolt->bone)
				{
					if (out.bolt >= 0)
						return {};
					out.bolt = i;
				}
			if (out.bolt < 0 || out.bolt == out.magazine || out.bolt == out.slide ||
			    r.parent[out.bolt] != r.gun ||
			    (out.bullet_mask[out.bolt / 32] & (0x80000000u >> (out.bolt % 32))))
				return {};
		}
		if (!definition.magazine_body_bones.empty())
		{
			if (!definition.rigid_magazine_source || !definition.magazine_mesh())
				return {};
			for (auto name : definition.magazine_body_bones)
			{
				const int bone = find(name);
				if (bone < 0 || r.parent[bone] != out.magazine || bone == out.slide || bone == out.bolt ||
				    (out.bullet_mask[bone / 32] & (0x80000000u >> (bone % 32))))
					return {};
			}
		}
		if (!definition.magazine_round_variants.empty())
		{
			if (!definition.rigid_magazine_source || !definition.magazine_mesh())
				return {};
			for (auto name : definition.magazine_round_variants)
			{
				const int bone = find(name);
				if (bone < 0 || r.parent[bone] != out.magazine ||
				    (out.bullet_mask[bone / 32] & (0x80000000u >> (bone % 32))))
					return {};
				out.magazine_variants[out.magazine_variant_count++] = bone;
			}
		}
		if (definition.animation_only_magazines.size() > 4)
			return {};
		for (auto name : definition.animation_only_magazines)
		{
			int root = -1;
			for (int i = 0; i < r.count; ++i)
				if (r.weapon_bones[i] && bones[i].name == name)
				{
					if (root >= 0)
						return {};
					root = i;
				}
			if (name.empty() || root < 0 || r.parent[root] != r.gun)
				return {};
			for (int i = 0; i < r.count; ++i)
				if (hands::descendant(i, root, r))
				{
					const auto bit = 0x80000000u >> (i % 32);
					if (!r.weapon_bones[i] || i == out.magazine || i == out.slide || i == out.bolt ||
					    ((out.bullet_mask[i / 32] | out.animation_only_magazines[i / 32]) & bit))
						return {};
					out.animation_only_magazines[i / 32] |= bit;
				}
		}
		if (definition.handle_catch || definition.interaction.receiver_release ||
		    (definition.interaction.belt && definition.interaction.belt->push))
		{
			out.slap_hand = hands::bind_contacts(r, bones, 0);
			out.right_slap_hand = hands::bind_contacts(r, bones, 1);
			if (!out.slap_hand.valid || !out.right_slap_hand.valid)
				return {};
		}
		return out;
	}
	inline part_mask magazine_visibility(const hands::rig& r,
	                                     const part_rig& parts,
	                                     const reload_profile& definition,
	                                     const mechanics::state& ammo) noexcept
	{
		auto hidden = parts.animation_only_magazines;
		for (int i = 0; i < r.count; ++i)
		{
			const bool bullet = (parts.bullet_mask[i / 32] & (0x80000000u >> (i % 32))) != 0;
			if (hide_reload_geometry(definition, ammo, hands::descendant(i, parts.magazine, r), bullet))
				hidden[i / 32] |= 0x80000000u >> (i % 32);
		}
		for (size_t i = 0; i < parts.magazine_variant_count; ++i)
			if (i != definition.magazine_subset(ammo.magazine_rounds))
				for (int b = 0; b < r.count; ++b)
					if (hands::descendant(b, parts.magazine_variants[i], r))
						hidden[b / 32] |= 0x80000000u >> (b % 32);
		return hidden;
	}
}
