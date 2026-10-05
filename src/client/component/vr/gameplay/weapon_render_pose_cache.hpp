#pragma once
#include "weapon_render_pose.hpp"
#include "weapon_render_owner.hpp"
#include <cstring>

namespace vr::gameplay::weapon_render_pose
{
	// CPU-only, bounded sidecar. Caller serializes copies, never native calls.
	// A record address alone is not a frame ID. begin() precedes native job
	// dispatch, submit() runs within that record's native worker lifetime, and
	// get() requires the complete immutable stereo publication identity.
	class pose_cache
	{
		struct skin_entry { std::uintptr_t entity{}, surface{}; solved_pose pose{}; };
		enum class state { pending, ready, conflict };
		struct scene_entry
		{
			engine_stereo_view::slot_pair views{};
			std::uintptr_t record{};
			weapons::hold owner{};
			std::uint64_t reference{};
			std::uint64_t registration{};
			snapshot pose{};
			state phase{};
			snapshot prepared_pose{};
			state prepared_phase{};
		};
		std::array<solved_pose, 128> solved_{};
		std::array<skin_entry, 128> skins_{};
		std::array<scene_entry, 64> scenes_{};
		std::size_t solve_cursor_{}, skin_cursor_{};
		std::uint64_t registration_{};
		static bool identity(const engine_stereo_view::slot_pair& a,
			const engine_stereo_view::slot_pair& b) noexcept
		{
			for (unsigned eye = 0; eye < 2; ++eye)
				if (!a.eyes[eye].pair_id || a.eyes[eye].pair_id != b.eyes[eye].pair_id ||
					a.eyes[eye].publication != b.eyes[eye].publication ||
					a.eyes[eye].output_eye != eye || b.eyes[eye].output_eye != eye) return false;
			return a.natural_camera == b.natural_camera;
		}
		std::size_t record_index(std::uintptr_t record, const std::array<float,12>& camera, weapons::weapon_identity weapon={}) const noexcept
		{
			auto selected = scenes_.size();
			for (std::size_t i=0;i<scenes_.size();++i)
				if (record && scenes_[i].record == record && scenes_[i].views.natural_camera == camera &&
					(!weapon.weapon || scenes_[i].owner.id()==weapon) &&
					(selected == scenes_.size() || scenes_[i].registration > scenes_[selected].registration)) selected = i;
			if (weapon.weapon && selected!=scenes_.size())
			{
				const auto latest=record_index(record,camera);
				if (latest==scenes_.size() || !identity(scenes_[selected].views,scenes_[latest].views)) return scenes_.size();
			}
			return selected;
		}
	public:
		// Frontend consumers run inside this record's native preparation lifetime.
		// Viewmodels use a separate transient entity range. Their scoped skin
		// preparation precedes rigid packing; HUD submission follows later. Keep
		// these readiness states separate, with the same scene identity checks.
		bool for_record(std::uintptr_t record, const std::array<float,12>& camera, snapshot& output, weapons::weapon_identity weapon={}) const noexcept
		{
			const auto index = record_index(record,camera,weapon);
			if (index == scenes_.size()) return false;
			const auto& scene = scenes_[index];
			if (scene.phase == state::conflict || scene.prepared_phase == state::conflict) return false;
			if (scene.phase == state::ready) output = scene.pose;
			else if (scene.prepared_phase == state::ready) output = scene.prepared_pose;
			else return false;
			return true;
		}
		bool publish(const solved_pose& pose) noexcept
		{
			weapons::muzzle_frame laser;
			if(pose.muzzle.laser.valid && (pose.laser_bone>=256 || pose.laser.position!=pose.muzzle.laser.position ||
				!weapons::laser_pose(pose.muzzle,laser)))return false;
			if (!pose.object || !pose.matrices || pose.muzzle_bone >= 256 ||
				!weapons::pose_ready(pose.muzzle, pose.muzzle.owner, pose.muzzle.reference_generation, pose.committed_at) ||
				!weapons::valid_model_anchor(pose.muzzle.model) || pose.bone.position != pose.muzzle.model.position ||
				!std::isfinite(pose.muzzle.units_per_meter) || pose.muzzle.units_per_meter <= 0 ||
				!weapons::valid_model_anchor(pose.control_grip)) return false;
			solved_[solve_cursor_++ % solved_.size()] = pose;
			return true;
		}
		bool find(std::uintptr_t object, std::uintptr_t matrices, std::uint32_t epoch, solved_pose& output) const noexcept
		{
			for (std::size_t n = 0; n < solved_.size(); ++n)
			{
				const auto& pose = solved_[(solve_cursor_ - 1 - n) % solved_.size()];
				if (pose.object == object && object && pose.matrices == matrices && pose.epoch == epoch)
				{ output = pose; return true; }
			}
			return false;
		}
		// Clear a previous surface association even if this skin attempt cannot
		// match a VR solve (tracking loss, switch, or native animation fallback).
		void invalidate_skin(std::uintptr_t entity) noexcept
		{
			for (auto& entry : skins_) if (entry.entity == entity) entry = {};
		}
		bool skin(std::uintptr_t entity, std::uintptr_t surface, const solved_pose& pose,
			const hands::bone& consumed,const hands::bone* consumed_laser=nullptr) noexcept
		{
			invalidate_skin(entity);
			if (!entity || !surface || !pose.object ||
				std::memcmp(&pose.bone, &consumed, sizeof(consumed)) != 0 ||
				(pose.muzzle.laser.valid && (!consumed_laser || std::memcmp(&pose.laser,consumed_laser,sizeof(pose.laser))!=0))) return false;
			skins_[skin_cursor_++ % skins_.size()] = {entity, surface, pose};
			return true;
		}
		bool begin(const engine_stereo_view::slot_pair& views, std::uintptr_t record,
			const weapons::hold& owner, std::uint64_t reference) noexcept
		{
			if (!record || !identity(views, views) || views.eyes[0].pair_id != views.eyes[1].pair_id ||
				views.eyes[0].publication != views.eyes[1].publication) return false;
			const auto hand=owner.holding_hand()==vr::hand::right ? 1u : 0u;
			const auto slot=(views.eyes[0].pair_id % (scenes_.size()/2))*2+hand;
			scenes_[slot] = {views, record, owner, reference, ++registration_};
			return true;
		}
	private:
		bool bind(std::uintptr_t record, const std::array<float, 12>& camera,
			std::uintptr_t entity, std::uintptr_t surface, std::uintptr_t object, bool preparing) noexcept
		{
			const skin_entry* skin = nullptr;
			for (const auto& entry : skins_)
				if (entry.entity == entity && entity && entry.surface == surface && surface && entry.pose.object == object)
				{ skin = &entry; break; }
			if (!skin) return false;
			const auto index = record_index(record,camera,skin->pose.muzzle.owner.id());
			auto* selected = index == scenes_.size() ? nullptr : &scenes_[index];
			if (selected)
			{
				auto& scene = *selected;
				auto& phase = preparing ? scene.prepared_phase : scene.phase;
				auto& bound = preparing ? scene.prepared_pose : scene.pose;
				const auto& pose = skin->pose;
				if (!weapons::same_render_carrier(scene.owner,pose.muzzle.owner) ||
					scene.reference != pose.muzzle.reference_generation) return false;
				const snapshot value{pose.muzzle, pose.committed_at, pose.object, pose.matrices, surface, pose.epoch,pose.control_grip};
				if (phase == state::conflict) return false;
				if (phase == state::ready)
				{
					if (bound.object == value.object && bound.matrices == value.matrices &&
						bound.surface == value.surface && bound.epoch == value.epoch &&
						bound.muzzle.model.position == value.muzzle.model.position &&
						bound.muzzle.axis == value.muzzle.axis &&
						bound.muzzle.ads_translation == value.muzzle.ads_translation &&
						bound.muzzle.laser.valid == value.muzzle.laser.valid &&
						bound.muzzle.laser.position == value.muzzle.laser.position &&
						bound.muzzle.laser.solve_origin == value.muzzle.laser.solve_origin &&
						bound.muzzle.laser_axis == value.muzzle.laser_axis &&
						weapons::optics::same_view(bound.muzzle.optic,value.muzzle.optic) &&
						bound.control_grip.position == value.control_grip.position) return true;
					phase = state::conflict; return false;
				}
				bound = value; phase = state::ready; return true;
			}
			return false;
		}
	public:
		bool prepare(std::uintptr_t record, const std::array<float,12>& camera,
			std::uintptr_t entity, std::uintptr_t surface, std::uintptr_t object) noexcept
		{ return bind(record,camera,entity,surface,object,true); }
		bool submit(std::uintptr_t record, const std::array<float,12>& camera,
			std::uintptr_t entity, std::uintptr_t surface, std::uintptr_t object) noexcept
		{ return bind(record,camera,entity,surface,object,false); }
		bool get(const engine_stereo_view::slot_pair& views, snapshot& output, weapons::weapon_identity weapon={}) const noexcept
		{
			const scene_entry* selected=nullptr;
			for (const auto& candidate:scenes_) if (identity(candidate.views,views) && (!weapon.weapon || candidate.owner.id()==weapon) &&
				(!selected || candidate.registration>selected->registration)) selected=&candidate;
			if (!selected) return false;
			const auto& scene=*selected;
			if (!scene.record || scene.phase != state::ready || !identity(scene.views, views)) return false;
			output = scene.pose; return true;
		}
	};
}
