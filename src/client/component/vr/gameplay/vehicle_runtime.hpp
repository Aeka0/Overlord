#pragma once
#include "vehicle_policy.hpp"
#include "vehicle_fire_intent.hpp"
#include "vehicle_steering.hpp"
#include "hand_pose_library.hpp"
#include "physical_reload_profile.hpp"
#include "hand_interaction/frame.hpp"

namespace vr::gameplay::hands {struct interaction_rig;}
namespace vr::gameplay::vehicles
{
	struct context {kind type{};int entity{-1};std::uint64_t epoch{};explicit operator bool()const noexcept{return type!=kind::none && entity>0 && epoch;}};
	// Native geometry relative to the transported tracking reference. Rebase it
	// at consumption so fast vehicle translation cannot become hand movement.
	struct handle_geometry {context driving{};anchor root{},handle{};controller_input::clock::time_point at{};std::uint64_t reference{};};
	struct snapshot
	{
		context driving{};weapons::hold owner{};
		bool inserted{true},magazine_grabbed{},quick_loading{},fire{};
		vr::hand magazine_hand{vr::hand::none};
		int rounds{},held_rounds{};
		anchor gun{},magazine{},muzzle{},brass{};
		vec grab_start{},grab_last{};float pull{};
		std::array<anchor,2> controls{};std::array<quat,2> basis{},mirror{};
		std::uint64_t reference{},continuity{},sequence{},revision{};float units{};
		controller_input::clock::time_point at{};
		bool geometry_ready{},handle_pose_ready{};
		fire_press press{};
		unsigned handles{};float steering{};
		float throttle{};unsigned steering_grip_down{},steering_trigger_down{},steering_tracked{};
		std::array<vec,2> steering_hands{};
		vec steering_axis{},steering_pivot{};
		float steering_raw_angle{},steering_angle{};bool steering_sample_valid{};
	};
	context current() noexcept;
	bool active() noexcept;
	bool presentation_allowed() noexcept;
	bool assets_ready(kind) noexcept;
	bool model_marker(kind,std::string_view,hands::anchor&) noexcept;
	void publish_render_hands(const controller_input::frame&,const std::array<hands::anchor,2>&,hands::vec view_offset) noexcept;
	void queue_effect(const snapshot&,bool brass) noexcept;
	std::string render_status();
	snapshot latest() noexcept;
	const weapons::profile& profile(kind) noexcept;
	// Called by the existing server hand coordinator, after ordinary escrow settlement.
	bool update_interactions() noexcept;
	void reconcile_lifecycle() noexcept;
	void reset() noexcept;
	void publish_geometry(kind,const std::array<anchor,2>&,const std::array<quat,2>&,const std::array<quat,2>&,bool handle_pose_ready) noexcept;
	void present(const hands::interaction_rig&,const hands::rig&,const controller_input::frame&,
		const std::array<anchor,2>&,const std::array<vec,2>&,const std::array<vec,3>&,float,std::span<hands::bone>) noexcept;
	void mask_native(const void* object,const void* matrices) noexcept;
	void publish_handles(const handle_geometry&) noexcept;
	handle_geometry rendered_handles() noexcept;
	void constrain_hands(std::array<anchor,2>&,const std::array<quat,2>&,vec view_offset,std::uint64_t reference) noexcept;
	driver_input controls(const controller_input::frame&,controller_input::clock::time_point) noexcept;
	hands::pose_library bind_handle_hands(const hands::rig&,std::span<const hands::bone_definition>) noexcept;
	std::array<hands::pose_library,2> bind_hands(const hands::rig&,std::span<const hands::bone_definition>) noexcept;
	anchor gun_pose(const snapshot&,const std::array<anchor,2>&) noexcept;
	anchor magazine_pose(const snapshot&,const std::array<anchor,2>&) noexcept;

	namespace native
	{
		bool ready(kind) noexcept;
		std::string status();
		int rounds(context) noexcept;
		bool set_rounds(context,int expected,int desired) noexcept;
		void update_aim(const snapshot&) noexcept;
		void feedback(const snapshot&,weapons::mechanics::effect,bool quick=false) noexcept;
	}
}
