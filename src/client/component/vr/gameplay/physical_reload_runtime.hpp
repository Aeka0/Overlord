#pragma once
#include "physical_reload_gesture.hpp"
#include "native_ammunition.hpp"
#include "physical_reload_profile.hpp"
#include "hand_interaction/frame.hpp"
#include "hand_contact.hpp"
#include "quick_reload.hpp"

namespace vr::gameplay::weapons::physical_reload
{
	struct scene_frame
	{
		controller_input::frame input{};
		hold owner{};
		geometry contact{};
		std::uint64_t assembly{};
		bool gameplay{};
		hands::anchor attached_world{}, held_world{};
		float units_per_meter{};
		const reload_profile* definition{};
		bool manipulation{}; // Free-hand access is separate from scene/instance admission.
		hands::anchor ejection_world{};
		hand_interaction::hand_binding binding{};
		std::array<hands::vec,hands::hand_contact_count> contact_in_wrist{};
		std::optional<contact_box> palm_in_wrist;
	};
	// Shared by read-only candidate preview and real instance admission. The
	// preview is never published or committed; execution imports/resamples again
	// with the generation it actually admitted.
	inline std::optional<mechanics::state> import_native_feed(const reload_profile& definition,weapon_identity id,
		const native_ammunition::reload_snapshot& observed,int live_capacity,std::uint64_t generation) noexcept
	{
		const auto name_end=std::find(observed.native_name.begin(),observed.native_name.end(),'\0');
		const std::string_view name{observed.native_name.data(),std::size_t(name_end-observed.native_name.begin())};
		if (!id || !observed.valid || !observed.ammo.valid || observed.ammo.id()!=id ||
			!definition.matches_native(name,observed.base_capacity) || live_capacity!=definition.ammunition.magazine_capacity ||
			(definition.interaction.manual_bolt && !observed.bolt_action)) return std::nullopt;
		mechanics::state initial{id.weapon,generation,1,false,false,0,observed.ammo.reserve};
		if (!mechanics::from_native_automatic(definition.ammunition,observed.ammo.loaded,initial)) return std::nullopt;
		return initial;
	}
	struct presentation
	{
		struct event
		{
			std::uint64_t sequence{};
			mechanics::effect kind{};
			clock::time_point at{};
			hands::anchor world{};
			float units_per_meter{};
			hands::anchor attached_world{}; // parent pose at commit for weapon-local seating
			int magazine_rounds{}; // Visual drop snapshot, not an ammunition authority.
			bool recoverable{}; // Independent item owns the payload and its rendering.
		};
		bool active{}, fault{}, slide_held{}, magazine_seated{};
		quick_reload::dwell quick_load{};
		mechanics::state ammo{};
		hold owner{};
		float slide_travel{};
		std::uint64_t effect_sequence{};
		mechanics::effect effect{};
		clock::time_point effect_at{};
		std::array<event, 8> events{};
		clock::time_point shot_at{};
		slide_constraint slide_grip{}; // simulation-owned grab origin; render-only travel evaluation
		std::uint64_t reference_generation{}, input_sequence{};
		const reload_profile* definition{};
		// Last simulation examination, distinct from the current rendered hand.
		// Diagnostics only: never feed this back into gesture acquisition.
		geometry examined{};
		const char* decision{"not examined"};
		bool well_contact{}, requires_withdrawal{};
		bool magazine_grabbed{};
		hands::vec magazine_grab_start{};
		float handle_amount{};
		belt_feed::grasp belt_grip{};
		const char* belt_push_reason{"no simulation sample"};
		handle_catch_grip handle_grip{};
		slap_trace slap_diagnostics{};
		bool knife_magazine_grasp{},knife_slide_grasp{};
		std::uint8_t magazine_pose{};
		clock::time_point insert_after{};
		bool magazine_leased() const noexcept
		{return ammo.magazine_hand!=hand::none || magazine_seated || magazine_grabbed;}
		bool use_knife_magazine_grasp(bool knife_held) const noexcept
		{return definition && definition->knife_magazine_in_wrist && (magazine_leased() ? knife_magazine_grasp : knife_held);}
		bool use_knife_slide_grasp(bool knife_held) const noexcept
		{return definition && !definition->knife_slide_grips.empty() && (slide_held ? knife_slide_grasp : knife_held);}
		bool part_leased() const noexcept
		{return magazine_leased() || slide_held || belt_grip.part!=belt_feed::lease::none;}
		void resume_transfer() noexcept
		{
			quick_load.reset();
			// The ammo identity and event watermark belong to the weapon. Only
			// old render payloads and the departed hand's scene lease are cleared.
			events={};reference_generation=0;input_sequence=0;
			belt_grip={};
		}
	};
	// Short copied snapshots only; no native calls or ammo writes on render thread.
	presentation current(weapon_identity id) noexcept;
	inline bool takes_carry_support(const presentation& reload, const hold& owner) noexcept
	{
		return owner.can_fire() && valid_hand(owner.support) && reload.active && reload.definition &&
		       reload.definition->interaction.support_magazine_catch &&
		       reload.ammo.magazine_hand == owner.support;
	}
	// After granted mechanical commits, transfer supported-pistol ownership to
	// its settled magazine lease through the server-owned carry adapter.
	void reconcile_carry_support() noexcept;
	void collect_interactions(const hand_interaction::frame&) noexcept;
	void update_interactions();
	// Identity/reconciliation/settlement only; never acquires from cached input.
	void update_lifecycle(bool suspended);
	void report_interactions()noexcept;
	bool insert_recovered(weapon_identity,hand,int rounds,const reload_profile& source,std::uint8_t pose,const hands::anchor& magazine_world)noexcept;
	using supply_commit_fn=bool(*)(const native_ammunition::snapshot&,int reserve,void*)noexcept;
	bool exchange_supply(weapon_identity,hand,bool draw,supply_commit_fn,void*)noexcept;
	// Server transfer boundary. Cancel gestures while still owned; retain the
	// mechanical snapshot on the native world entity until its generation ends.
	bool prepare_transfer(weapon_identity id,presentation& saved) noexcept;
	bool restore_transfer(const presentation& saved) noexcept;
	// Hot grip admission only needs occupancy, not a full diagnostic/event copy.
	bool support_available(const controller_input::frame& input,const hold& owner) noexcept;
	bool part_leased(weapon_identity id) noexcept;
	void publish_scene(const scene_frame& value) noexcept;
	void set_boundary_ready(unsigned bit) noexcept; // fire/consume=1, reload=2, presenter=4
	bool blocks_reload(const void* ps, int side) noexcept;
	bool allow_fire(const void* ps, int command_time, int side) noexcept;
	bool allow_owned_shot(const hold&,const native_ammunition::snapshot&) noexcept;
	void consumed(const void* ps, int command_time, std::uint32_t weapon, bool alternate, int amount, int side,
		const native_ammunition::snapshot& before, const native_ammunition::snapshot& after,bool sustained=false) noexcept;
}
