#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace vr::game_view
{
	struct contribution
	{
		std::uint64_t generation{};
		float yaw{};
		float heading{},reference_offset{};
	};

	// Owned by head_pose_bridge's mutex. The client angles are real game input;
	// this tracks only the HMD part, never a replacement copy of native yaw.
	class state
	{
	public:
		void rebase(std::uint64_t generation,float head_yaw,float head_heading) noexcept
		{
			if(current_.generation && current_.generation!=generation)
				reference_offset_=std::remainder(reference_offset_+current_.heading-head_heading,360.f);
			current_={generation,head_yaw,head_heading,reference_offset_};
		}
		void rebase(std::uint64_t generation,float head_yaw) noexcept {rebase(generation,head_yaw,head_yaw);}
		bool apply(float& pitch, float& yaw, const float delta_pitch,
			const float head_pitch, const float head_yaw, const std::uint64_t generation,float head_heading) noexcept
		{
			if (!generation || !std::isfinite(pitch) || !std::isfinite(yaw) ||
				!std::isfinite(delta_pitch) || !std::isfinite(head_pitch) || !std::isfinite(head_yaw) || !std::isfinite(head_heading)) return false;
			// A recovered reference may select the other equivalent Euler branch.
			// Preserve physical heading while retaining native pitch/yaw pairing.
			const auto previous = current_.generation==generation?current_.yaw:
				current_.generation?current_.yaw-current_.heading+head_heading:0.f;
			const auto next_yaw = std::remainder(std::remainder(yaw, 360.0f) +
				std::remainder(head_yaw - previous, 360.0f), 360.0f);
			// H2 positive pitch looks down. delta_angles belongs to the engine
			// (spawn/script corrections), so remove it before native command packing.
			const auto next_pitch = std::remainder(-head_pitch - std::remainder(delta_pitch, 360.0f), 360.0f);
			if (!std::isfinite(next_yaw) || !std::isfinite(next_pitch)) return false;
			pitch = next_pitch;
			yaw = next_yaw;
			rebase(generation,head_yaw,head_heading);
			return true;
		}
		bool apply(float& pitch,float& yaw,float delta_pitch,float head_pitch,float head_yaw,std::uint64_t generation) noexcept
		{return apply(pitch,yaw,delta_pitch,head_pitch,head_yaw,generation,head_yaw);}

		void record(const int command_time, const int packed_pitch = 0, const bool tracked = false) noexcept
		{
			// New map/checkpoint timelines cannot resolve against a previous map.
			if (count_ && command_time < latest_time_) { count_ = 0; next_ = 0; rendered_valid_ = false; }
			latest_time_ = command_time;
			history_[next_] = {command_time, current_, packed_pitch, tracked};
			next_ = (next_ + 1) % history_.size();
			if (count_ < history_.size()) ++count_;
		}

		bool resolve_pitch(const int command_time, const int packed_pitch,
			const std::uint64_t generation, const float delta_pitch, float& pitch) const noexcept
		{
			if (!generation || !std::isfinite(delta_pitch)) return false;
			for (std::size_t i = 0; i < count_; ++i)
			{
				const auto& item = history_[(next_ + history_.size() - 1 - i) % history_.size()];
				if (item.time != command_time) continue;
				if (!item.tracked || item.head.generation != generation || item.packed_pitch != packed_pitch)
					return false;
				// H2 FinishMove stores a signed 24-bit turn in a 32-bit field.
				const auto value = static_cast<float>(packed_pitch) * (360.0f / 16777216.0f) + delta_pitch;
				if (!std::isfinite(value)) return false;
				pitch = value - std::floor(value / 360.0f + 0.5f) * 360.0f;
				return true;
			}
			return false;
		}

		bool resolve(const int command_time, const std::uint64_t generation, float& yaw) noexcept
		{
			if (!current_.generation) { yaw = 0; return true; }
			// Prefer the newest command if native time did not advance. Prediction
			// consumes exact command time, not the latest render/input publication.
			for (std::size_t i = 0; i < count_; ++i)
			{
				const auto& entry = history_[(next_ + history_.size() - 1 - i) % history_.size()];
				if (entry.time != command_time) continue;
				rendered_ = entry;
				rendered_valid_ = true;
				// Recenter makes the current native facing the new tracking origin.
				yaw = contribution_in_reference(entry.head,generation);
				return true;
			}
			// A paused scene may outlive the input ring. Retain only its exact
			// already-resolved command, not an arbitrary nearest-time substitute.
			if (rendered_valid_ && rendered_.time == command_time)
			{
				yaw = contribution_in_reference(rendered_.head,generation);
				return true;
			}
			return false;
		}

	private:
		float contribution_in_reference(const contribution& head,std::uint64_t generation)const noexcept
		{
			if(head.generation==generation)return head.yaw;
			if(current_.generation!=generation)return 0;
			return std::remainder(head.yaw-(reference_offset_-head.reference_offset),360.f);
		}
		struct entry { int time{}; contribution head{}; int packed_pitch{}; bool tracked{}; };
		contribution current_{};
		float reference_offset_{};
		std::array<entry, 64> history_{}; // Same bound as H2's usercmd ring.
		std::size_t next_{};
		std::size_t count_{};
		int latest_time_{};
		entry rendered_{};
		bool rendered_valid_{};
	};
}
