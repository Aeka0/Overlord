#pragma once
#include "launcher_feed.hpp"
#include "native_ammunition.hpp"
#include "hand_interaction/frame.hpp"
#include "launcher_support.hpp"
#include "weapon_interaction.hpp"

namespace vr::gameplay::weapons::launcher
{
	struct scene_frame
	{
		hold owner{};const launcher_profile* definition{};std::uint64_t assembly{};
		hand_interaction::hand_binding binding{};
	};
	struct presentation
	{
		bool active{},fault{},spent{},fire_armed{};int loaded{};hold owner{};
		const launcher_profile* definition{};rocket_hold rocket{};
		std::uint64_t reference{},sequence{};controller_input::clock::time_point at{};
	};
	inline bool owns_feed(const hold& owner,const scene_frame& bound)noexcept
	{
		// Binding has assembly lifetime. A support/rear revision invalidates a
		// rendered pose, but must not temporarily re-enable native auto reload.
		return owner.id() && owner.id()==bound.owner.id() && valid_hand(owner.holding_hand()) &&
			bound.definition && bound.assembly && bound.binding.valid;
	}
	void support_feedback(std::span<const carry::instance> before,std::span<const carry::instance> after,
		const controller_input::frame&,const std::array<hands::anchor,2>&,unsigned valid_hands,unsigned resumed);
	presentation current(weapon_identity)noexcept;
	void publish_scene(const scene_frame&)noexcept;
	void collect_interactions(const hand_interaction::frame&)noexcept;
	void update_interactions();
	void update_lifecycle(bool);
	void report_interactions()noexcept;
	bool prepare_transfer(weapon_identity)noexcept;
	bool restore_transfer(const presentation&)noexcept;
	void set_boundary_ready(unsigned)noexcept;
	bool ready()noexcept;
	bool blocks_reload(const void*,int)noexcept;
	bool allow_fire(const void*,int,int)noexcept;
	void consumed(const void*,int,std::uint32_t,bool,int,int,const native_ammunition::snapshot&,const native_ammunition::snapshot&,bool sustained=false)noexcept;
	bool aim_supported(const hold&)noexcept;
	muzzle_frame ads_muzzle(const hold&,muzzle_frame)noexcept;
	bool physical_fire_mode(const void*,std::uint32_t,bool)noexcept;
	bool allows_trigger(const hold&,std::uint64_t,controller_input::clock::time_point)noexcept;
	bool retain_empty(const void*,std::uint32_t)noexcept;
	std::array<std::uint64_t,3> native_statistics()noexcept; // reticle, retained selection, retained ownership
}
