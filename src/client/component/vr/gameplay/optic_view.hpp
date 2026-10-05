#pragma once
#include "weapon_model_anchor.hpp"
#include "hand_pose_solver.hpp"
#include <array>
#include <cstdint>

namespace vr::gameplay::weapons::optics
{
	// Asset identity can be bound before its GPU view exists. Availability is a
	// presentation-time condition so a cached rig survives image streaming and
	// device recreation without making a transparent lens while the view is absent.
	struct reticle_image_state
	{
		std::uintptr_t image{},shader_view{};
		unsigned width{},height{};
		bool texture_2d{};
		std::uintptr_t identity() const noexcept
		{return image && texture_2d && width>=4 && height>=4 ? image : 0;}
		bool ready() const noexcept {return identity() && shader_view;}
	};
	struct view
	{
		model_anchor center{};
		std::array<hands::vec,3> axis{};
		float radius{}, magnification{};
		std::uintptr_t reticle_image{}; // Native scene-owned asset; read on its render owner.
		bool active{};
		float pupil_radius{1.f};
		float scene_clearance{}; // Model units, zero retains ordinary optical clipping.
		bool thermal{}; // Admitted optic capability, retained even before ADS activates its image.
	};
	inline bool same_view(const view& a,const view& b) noexcept
	{
		return a.active==b.active && a.center.valid==b.center.valid && a.center.position==b.center.position &&
			a.center.solve_origin==b.center.solve_origin && a.axis==b.axis && a.radius==b.radius &&
			a.magnification==b.magnification && a.reticle_image==b.reticle_image && a.pupil_radius==b.pupil_radius && a.scene_clearance==b.scene_clearance && a.thermal==b.thermal;
	}
}
