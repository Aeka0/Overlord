#pragma once
#include "reload_item.hpp"
#include "hand_interaction/frame.hpp"
#include <optional>

namespace vr::gameplay::hands {struct interaction_rig;}
namespace vr::gameplay::weapons {struct reload_profile;struct cylinder_profile;}
namespace vr::gameplay::reload_items
{
	bool discard_penalty()noexcept;
	bool can_release(const weapons::reload_profile*)noexcept;
	bool can_release(const weapons::cylinder_profile*)noexcept;
	key reserve(weapons::weapon_identity,const weapons::reload_profile*,int rounds,const anchor& world,
		float units,std::uint64_t reference,clock::time_point at,bool ejected,const std::optional<motion::release_impulse>& impulse={})noexcept;
	key reserve(weapons::weapon_identity,const weapons::cylinder_profile*,int rounds,const anchor& world,
		float units,std::uint64_t reference,clock::time_point at)noexcept;
	void complete_release(key,bool committed)noexcept;
	void collect_interactions(const hand_interaction::frame&)noexcept;
	void update_interactions()noexcept;
	void report_interactions()noexcept;
	void lifecycle(bool suspended)noexcept;
	bool settle_inventory()noexcept;
	std::array<std::uint64_t,2> present(const hands::interaction_rig&,const hands::rig&,const controller_input::frame&,
		const std::array<anchor,2>& targets,std::span<hands::bone>,unsigned occupied,unsigned visible)noexcept;
}
