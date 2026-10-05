#pragma once
#include "physical_reload_gesture.hpp"
#include "charging_handle_bolt.hpp"
#include "charging_handle_fold.hpp"
#include "charging_handle_catch.hpp"
#include "weapon_sound_reference.hpp"
#include "magazine_grasp_pose.hpp"
#include "native_weapon_family.hpp"
#include "magazine_fill.hpp"
#include "body_supply_layout.hpp"

namespace vr::gameplay::weapons
{
	struct partitioned_bolt
	{
		part_mesh_partition mesh;
		charging_handle_bolt motion; // Existing source bone supplies only the bind frame, not the final bolt pose.
	};
	struct magazine_latch_profile {hands::vec position,direction;};
	struct magazine_contact_profile
	{
		// Native model units. Grip contact is wrist-local; the box/latch are
		// gun-local. One or two boxes conservatively enclose the complete magazine
		// body in magazine-local space; cartridge geometry is excluded.
		hands::vec grip_contact, grab_low, grab_high, latch;
		std::span<const physical_reload::contact_box> strike_regions;
		const magazine_latch_profile* second_latch{}; // Optional distinct hardware, not a larger shared trigger area.
	};
	struct part_parent_contract { std::string_view bone,parent; };
	struct magazine_mesh_recipe
	{
		std::array<std::string_view,16> names{};
		std::size_t body_count{},count{},subsets{};
		bool exclusive_rounds{};
		explicit operator bool() const noexcept { return body_count && count && subsets; }
		std::size_t selected_count(std::size_t rounds) const noexcept
		{ return subsets ? exclusive_rounds ? body_count+1+int(rounds>0) : body_count+std::min(rounds,count-body_count) : 0; }
		std::size_t selected_index(std::size_t subset,std::size_t item)const noexcept
		{
			if(!exclusive_rounds || item<body_count)return item;
			return subset && item==body_count ? body_count : body_count+1+std::min(subset,subsets-1);
		}
	};
	// Immutable authored data. Runtime instances, scene snapshots and cosmetic
	// drops retain their own profile identity; no global current-weapon settings.
	struct reload_presentation_tuning
	{
		float slide_return_seconds{.075f}, magazine_exit_seconds{.16f}, magazine_exit_clearance_m{.01f};
	};
	struct reload_profile
	{
		std::string_view id, native_name;
		mechanics::rules ammunition;
		physical_reload::profile interaction;
		std::string_view magazine_bone, slide_bone, bullets_bone;
		const char* magazine_model;
		hands::anchor magazine_rest, slide_rest, rigid_in_magazine, magazine_in_wrist;
		hands::vec magazine_top;
		hands::anchor well; // gun-local; +Z follows THIS magazine's insertion rail
		hands::vec slide_grab_low, slide_grab_high;
		std::span<const joint_pose> magazine_fingers;
		std::span<const part_grip_pose> slide_grips;
		reload_presentation_tuning presentation{};
		body_supply_layout supply{};
		const char* (*sound_key)(mechanics::effect) noexcept{};
		// A family binding still requires the complete matching scene contract at
		// admission. Native instance names are pinned separately, never merged.
		bool (*native_family)(std::string_view) noexcept{};
		const char* skinned_receiver{}; // Optional prepared triangle-mask contract; not a source asset mutation.
		const magazine_contact_profile* magazine_contacts{};
		std::span<const std::string_view> additional_bullet_bones{};
		const char* rigid_magazine_source{}; // Exact receiver for immutable body/round subsets.
		bool receiver_parented_bullets{}; // Explicit single-round sibling; follows magazine through its captured bind offset.
		const charging_handle_bolt* bolt{};
		const charging_handle_fold* handle_fold{};
		// Explicit alias evidence may supply an event absent from the notetrack
		// map. An empty override keeps the ordinary notetrack path.
		sound_reference (*sound_override)(mechanics::effect) noexcept{};
		const charging_handle_catch* handle_catch{};
		// Native reload clips may carry a second cosmetic magazine. Hide these
		// disjoint receiver subtrees only while physical ownership is active.
		std::span<const std::string_view> animation_only_magazines{};
		// Optional exact hierarchy for round groups behind intermediate carrier
		// bones. Ordinary profiles retain the direct magazine-child contract.
		std::span<const part_parent_contract> bullet_parents{};
		// Manual feed: seated cartridge, feed-rail entry and chamber endpoint, gun-local.
		const std::array<hands::anchor,3>* feeding_path{};
		// A native magazine child may depict the chambered cartridge, not a
		// magazine round. Keep that mesh with the receiver and out of detached subsets.
		const hands::anchor* chamber_round{};
		// Permanent magazine components (e.g. follower/base plate), present even
		// at zero rounds. They are not ammunition and must never enter bullet masks.
		std::span<const std::string_view> magazine_body_bones{};
		// Mutually exclusive native magazine contents, including the empty
		// follower at index zero. These are alternatives, never additive groups.
		std::span<const std::string_view> magazine_round_variants{};
		unsigned rounds_per_variant{};
		bool concealed_bolt{}; // Audited receiver has no separately skinned internal bolt.
		const part_capture_halfspace* slide_capture{}; // Optional hard acquisition side; never mirrored with the hand.
		bool handle_child_of_bolt{}; // Audited handle subtree below its separately posed bolt.
		// Explicit pistol co-grasp capabilities; absent data keeps the part exclusive.
		const hands::anchor* knife_magazine_in_wrist{};
		std::span<const part_grip_pose> knife_slide_grips{};
		std::span<const magazine_grasp_pose> magazine_grasps{};
		magazine_grasp_policy magazine_selection{magazine_grasp_policy::wrist_facing};
		std::uint8_t magazine_default_pose{}; // Ambiguous/invalid facing never prevents an otherwise valid draw.
		bool authored_action_fire{}; // Audited static native fire clip needs the same cycle as tree-less models.
		const partitioned_bolt* bolt_partition{};
		std::span<const magazine_fill_recipe> magazine_fills{};
		magazine_tracking_frame magazine_tracking{magazine_tracking_frame::weapon_wrist};
		sound_reference rear_sound{}, close_sound{}, removal_sound{};
		std::string_view action_detail_bone{}; // Audited visible child of the operated action (latch/handle tip).
		const magazine_fill_recipe* magazine_fill() const noexcept
		{
			if(!rigid_magazine_source)return nullptr;
			for(const auto& p:magazine_fills)if(p.source && std::string_view(p.source)==rigid_magazine_source)return &p;
			return nullptr;
		}
		std::size_t magazine_subset_count() const noexcept
		{return magazine_fill() ? 4 : interaction.belt ? 1 : !magazine_round_variants.empty() ? magazine_round_variants.size() : chamber_round ? 1 : 2+additional_bullet_bones.size();}
		std::size_t magazine_subset(int rounds)const noexcept
		{
			if(magazine_fill())return magazine_fill_level(rounds);
			const auto value=std::size_t(std::max(0,rounds));
			return std::min(magazine_subset_count()-1,magazine_round_variants.empty() || !rounds_per_variant ? value : (value+rounds_per_variant-1)/rounds_per_variant);
		}
		magazine_mesh_recipe magazine_mesh() const noexcept
		{
			if (magazine_body_bones.size()>2 || additional_bullet_bones.size()>2) return {};
			if(!magazine_round_variants.empty() && (magazine_round_variants.size()<2 || magazine_round_variants.size()>12 || !rounds_per_variant ||
				chamber_round || !additional_bullet_bones.empty() || receiver_parented_bullets || interaction.manual_bolt))return {};
			magazine_mesh_recipe out;out.names[out.count++]=magazine_bone;
			for (auto name:magazine_body_bones) out.names[out.count++]=name;
			out.body_count=out.count;out.subsets=magazine_subset_count();
			if (!chamber_round && !interaction.belt)
			{
				out.names[out.count++]=bullets_bone;
				for (auto name:additional_bullet_bones) out.names[out.count++]=name;
			}
			out.exclusive_rounds=!magazine_round_variants.empty();
			for(auto name:magazine_round_variants)out.names[out.count++]=name;
			for (std::size_t i=0;i<out.count;++i)
			{
				if (out.names[i].empty()) return {};
				for (std::size_t j=0;j<i;++j) if (out.names[i]==out.names[j]) return {};
			}
			return out;
		}
		sound_reference interaction_sound(mechanics::effect event) const noexcept
		{
			using enum mechanics::effect;
			if(event==action_rear && rear_sound.name)return rear_sound;
			if(event==action_close && close_sound.name)return close_sound;
			// A dedicated take cue wins; otherwise retain the authored removal slice.
			if(event==magazine_take && sound_override)
			{ const auto sound=sound_override(event); if(sound.name)return sound; }
			if((event==magazine_out || event==magazine_take) && removal_sound.name)return removal_sound;
			const auto resolve=[&](mechanics::effect kind) {
				if (sound_override) { const auto sound=sound_override(kind); if (sound.name) return sound; }
				return sound_reference{sound_key ? sound_key(kind) : nullptr};
			};
			const auto sound=resolve(event);
			// Taking a magazine into the hand shares removal audio, while its
			// distinct event still prevents the presenter spawning a dropped copy.
			return !sound.name && event==mechanics::effect::magazine_take ? resolve(mechanics::effect::magazine_out) : sound;
		}
		bool matches_native(std::string_view name, int capacity) const noexcept
		{ return capacity == ammunition.magazine_capacity && (native_family ? native_family(name) : name == native_name); }
	};
	inline reload_profile with_split_sounds(reload_profile profile,const char* cycle,
		bool retain_close=false,bool split_removal=false) noexcept
	{
		if(cycle)
		{
			profile.rear_sound={cycle,sound_reference_kind::notetrack,sound_part::first};
			if(!retain_close)profile.close_sound={cycle,sound_reference_kind::notetrack,sound_part::second};
		}
		if(split_removal)
		{
			profile.removal_sound=profile.interaction_sound(mechanics::effect::magazine_out);
			profile.removal_sound.part=sound_part::first;
		}
		return profile;
	}
	inline const part_mesh_partition* partition_mesh(const reload_profile& p) noexcept
	{
		return p.bolt_partition ? &p.bolt_partition->mesh : p.handle_fold ? p.handle_fold->mesh : nullptr;
	}
	inline bool valid_partition_profile(const reload_profile& p) noexcept
	{
		if(p.bolt_partition)
		{
			const auto& b=*p.bolt_partition;
			return !p.bolt && !p.handle_fold && !p.handle_child_of_bolt && !p.concealed_bolt && !p.interaction.manual_bolt &&
				p.ammunition.feed==mechanics::feed_type::closed_bolt && p.interaction.motion==physical_reload::action_motion::charging_handle &&
				(b.motion.bone=="j_gun" || b.motion.bone==p.slide_bone) && valid_partition(b.mesh) &&
				p.rigid_magazine_source && std::string_view(b.mesh.source)==p.rigid_magazine_source &&
				valid_bolt(b.motion,p.interaction.slide_stroke) && b.motion.shot_stroke_m>0;
		}
		return !p.handle_fold || valid_fold(*p.handle_fold);
	}
	// Presentation only. Feed semantics select the open-bolt cycle; hierarchy
	// selects parenting, and must never decide whether a bolt can leave its sear.
	inline float displayed_internal_bolt(const reload_profile& p,float handle_m,float shot_handle_m,
		bool retained,float shot_age) noexcept
	{
		const auto* bolt=p.bolt_partition?&p.bolt_partition->motion:p.bolt;
		if(!bolt)return 0;
		if(p.ammunition.feed==mechanics::feed_type::open_bolt)
			return fired_bolt_travel(*bolt,handle_m,retained,shot_age);
		if(bolt->shot_stroke_m>0)
			return std::max(bolt_travel(*bolt,handle_m,retained),bolt->shot_stroke_m*action_shot_fraction(shot_age));
		return bolt_travel(*bolt,std::max(handle_m,shot_handle_m),retained);
	}
	inline hands::anchor partition_bolt_pose(const reload_profile& p,float handle_m,bool retained,float shot_age,float units) noexcept
	{
		if(!p.bolt_partition || !std::isfinite(units) || units<=0)return {};
		auto pose=p.bolt_partition->motion.rest;
		pose.position=hands::add(pose.position,hands::scale(p.interaction.slide_axis,
			displayed_internal_bolt(p,handle_m,0,retained,shot_age)*units));
		return pose;
	}
	inline hands::vec action_slap_centre(const reload_profile& p,mechanics::action_state state,float units)noexcept
	{
		if(p.interaction.receiver_release && state!=mechanics::action_state::latched_open)return p.interaction.receiver_release->centre;
		if(!p.handle_catch)return {};
		const auto raised=handle_pose(p.slide_rest,p.handle_catch,p.interaction.slide_axis,p.interaction.slide_stroke*units,1);
		return hands::add(raised.position,hands::rotate(raised.rotation,p.handle_catch->contact));
	}

	inline bool hide_reload_geometry(const reload_profile& p,const mechanics::state& ammo,
		bool magazine_descendant,bool bullet) noexcept
	{
		if (p.chamber_round && bullet) return !ammo.chamber_loaded;
		if (p.interaction.manual_bolt && ammo.bolt.feeding && bullet) return false;
		return (!ammo.magazine_inserted && magazine_descendant) || (ammo.magazine_rounds==0 && bullet);
	}
	inline hands::anchor compose_reload(hands::anchor a, hands::anchor b) noexcept
	{
		using namespace hands;
		return {add(a.position,rotate(a.rotation,b.position)),normalize(multiply(a.rotation,b.rotation))};
	}
	inline hands::anchor inverse_reload(hands::anchor a) noexcept
	{
		const auto q = hands::conjugate(hands::normalize(a.rotation));
		return {hands::rotate(q,hands::scale(a.position,-1)),q};
	}
	inline hands::vec magazine_tip_in_well(const reload_profile& p, hands::anchor gun,
		hands::anchor magazine, float units) noexcept
	{
		const auto local = compose_reload(inverse_reload(compose_reload(gun,p.well)),
			compose_reload(magazine,{p.magazine_top,{0,0,0,1}}));
		return hands::scale(local.position,1/units);
	}
	inline float magazine_alignment(const reload_profile& p, hands::anchor gun, hands::anchor magazine) noexcept
	{
		return hands::dot(hands::rotate(compose_reload(gun,p.magazine_rest).rotation,{0,0,1}),
			hands::rotate(magazine.rotation,{0,0,1}));
	}
	inline hands::vec magazine_exit_translation(const reload_profile& p, float units) noexcept
	{
		return physical_reload::well_exit_translation(p.magazine_rest,p.magazine_top,p.well.position,
			p.presentation.magazine_exit_clearance_m*units);
	}
	inline physical_reload::magazine_contact magazine_contacts(const reload_profile& p,
		hands::anchor gun,hands::anchor wrist,hands::anchor magazine,float units,
		const hands::vec* grip_override=nullptr) noexcept
	{
		physical_reload::magazine_contact out;
		if (!p.magazine_contacts || !std::isfinite(units) || units<=0) return out;
		const auto& c=*p.magazine_contacts;
		const auto inv=inverse_reload(gun);
		const auto contact=compose_reload(compose_reload(inv,wrist),{grip_override ? *grip_override : c.grip_contact,{0,0,0,1}}).position;
		out.grip_distance=physical_reload::box_distance(contact,c.grab_low,c.grab_high)/units;
		const auto local=compose_reload(inv,magazine);
		if(p.interaction.manual_magazine && p.interaction.manual_magazine->spare_strike)
		{
			if(c.strike_regions.empty() || c.strike_regions.size()>physical_reload::max_contact_boxes)return {};
			physical_reload::box_motion motion;motion.frame=local;motion.frame.position=hands::scale(hands::sub(local.position,c.latch),1/units);
			motion.region_count=c.strike_regions.size();
			for(size_t i=0;i<motion.region_count;++i)
			{
				motion.regions[i]=c.strike_regions[i];motion.regions[i].pose.position=hands::scale(motion.regions[i].pose.position,1/units);
				motion.regions[i].half=hands::scale(motion.regions[i].half,1/units);
			}
			out.strike=motion;
			if(c.second_latch)
			{
				motion.frame.position=hands::scale(hands::sub(local.position,c.second_latch->position),1/units);
				out.second_strike=physical_reload::magazine_contact::directed_strike{motion,c.second_latch->direction};
			}
		}
		out.valid=true;
		if (!physical_reload::valid(out)) return {};
		return out;
	}
}
