#pragma once
#include "cylinder_profile.hpp"
#include "native_ammunition.hpp"
#include "hand_interaction/frame.hpp"
#include "quick_reload.hpp"

namespace vr::gameplay::weapons::cylinder
{
	struct scene_frame
	{
		controller_input::frame input{}; hold owner{}; geometry contact{};
		std::uint64_t assembly{}; bool gameplay{};
		hands::anchor cylinder_world{}, loader_world{}; float units{};
		const cylinder_profile* definition{};
		bool manipulation{}; // Free-hand access is separate from scene/instance admission.
		hand_interaction::hand_binding binding{};hands::anchor cylinder_in_swing{};
	};
	struct presentation
	{
		bool active{}, fault{}, fire_armed{};
		quick_reload::dwell quick_load{};
		state ammo{}; hold owner{};
		const cylinder_profile* definition{};
		clock::time_point action_at{}, sampled_at{}, shot_at{};
		std::uint64_t reference{}, input_sequence{}, event_sequence{};
		struct event
		{
			std::uint64_t sequence{}; effect kind{}; clock::time_point at{};
			hands::anchor world{}; float units{}; int live{}, spent{};
			bool recoverable{};
		};
		std::array<event,8> events{};
		void resume_transfer() noexcept
		{events={};reference=0;input_sequence=0;fire_armed=false;quick_load.reset();}
	};
	presentation current(weapon_identity id) noexcept;
	void collect_interactions(const hand_interaction::frame&)noexcept;
	void update_interactions();
	// Identity/reconciliation/settlement only; never acquires from cached input.
	void update_lifecycle(bool suspended);
	void report_interactions()noexcept;
	bool fill_recovered(weapon_identity,hand,int rounds,const cylinder_profile& source,const hands::anchor& loader_world)noexcept;
	bool prepare_transfer(weapon_identity id,presentation& saved) noexcept;
	bool restore_transfer(const presentation& saved) noexcept;
	void publish_scene(const scene_frame&) noexcept;
	void set_boundary_ready(unsigned bit) noexcept;
	bool blocks_reload(const void* ps, int side) noexcept;
	bool allow_fire(const void* ps, int command, int side) noexcept;
	bool allow_owned_shot(const hold&,const native_ammunition::snapshot&) noexcept;
	void consumed(const void* ps, int command, std::uint32_t weapon, bool alternate, int amount, int side,
		const native_ammunition::snapshot& before, const native_ammunition::snapshot& after,bool sustained=false) noexcept;
}
