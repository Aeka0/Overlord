#pragma once
#include "weapon_carry.hpp"
#include "hand_pose_solver.hpp"
#include "../controller_input.hpp"

namespace vr::gameplay::weapons::carry
{
	// Sidecar to the native skeleton, not a second tracking or animation clock.
	// Callers serialize access. Native record preparation supplies the exact key.
	struct render_part
	{
		std::uintptr_t model{};
		std::size_t slot{};
		identity id{};
		hold owner{};
		hands::anchor relative{};
	};
	struct render_sample
	{
		std::uintptr_t object{}, matrices{};
		std::uint32_t epoch{};
		std::uint64_t reference{};
		controller_input::clock::time_point at{};
		std::array<render_part,32> parts{};
		std::size_t count{};
	};
	class render_cache
	{
	public:
		void publish(const render_sample& sample) noexcept
		{ if (sample.object && sample.matrices && sample.count<=sample.parts.size()) samples_[cursor_++ % samples_.size()]=sample; }
		bool find(std::uintptr_t object,std::uintptr_t matrices,std::uint32_t epoch,
			std::size_t slot,std::uintptr_t model,render_part& part,render_sample& metadata) const noexcept
		{
			for (std::size_t n=0;n<std::min(cursor_,samples_.size());++n)
			{
				const auto& s=samples_[(cursor_-1-n)%samples_.size()];
				if (s.object!=object || s.matrices!=matrices || s.epoch!=epoch) continue;
				for (std::size_t i=0;i<s.count;++i) if (s.parts[i].slot==slot && s.parts[i].model==model)
				{ part=s.parts[i];metadata.object=s.object;metadata.matrices=s.matrices;metadata.epoch=s.epoch;
					metadata.reference=s.reference;metadata.at=s.at;return true; }
				return false; // A matching epoch without this part is authoritative.
			}
			return false;
		}
	private:
		std::array<render_sample,128> samples_{};
		std::size_t cursor_{};
	};
}
