#pragma once
#include "melee_motion.hpp"
#include "native_carry.hpp"

namespace vr::gameplay::melee::native
{
	struct hit
	{
		weapons::native_carry::world_key target{};
		vec point{},direction{};
		unsigned part_name{},model_index{},hit_location{};
		float fraction{1};
		bool flesh{true};
		explicit operator bool() const noexcept {return target.entity>0;}
	};
	bool initialize();
	bool allowed() noexcept;
	hit trace(vec from,vec to,vec head,float units,tool);
	int damage(const hit&,std::uint32_t weapon,tool,std::uint32_t tactical_knife);
	struct feedback_counts {std::uint64_t hits{},blood{},shield_hits{},shield_blood{};};
	feedback_counts feedback_status() noexcept;
}
