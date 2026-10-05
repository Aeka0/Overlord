#pragma once
#include "optic_catalog.hpp"
#include "optic_view.hpp"
#include "ads_comfort.hpp"
#include "weapon_holding.hpp"
#include "hand_rig_builder.hpp"
#include "../controller_input.hpp"
#include <span>
namespace game {struct XModel; struct XSurface;}

namespace vr::gameplay::weapons::optics
{
	struct binding
	{
		const definition* lens{};
		int root{-1};
		std::array<const game::XSurface*,8> hidden{};
		std::uintptr_t reticle_image{};
	};
	// Bind asset identity once per admitted assembly; GPU readiness is sampled by
	// present()/the final eye consumer. Never mutate native assets.
	binding bind(std::span<game::XModel* const> models, std::span<const hands::model_definition> definitions) noexcept;
	ads_comfort::binding bind_comfort(std::span<game::XModel* const> models,
		std::span<const hands::model_definition> definitions) noexcept;
	// Presentation permission survives support-only changes to allow a gradual
	// return; active always refers to the exact current two-hand grip revision.
	struct ads_control {bool allowed{},active{};};
	void request(const hold&, ads_control, const controller_input::frame&) noexcept;
	ads_control control_for(const hold&, const controller_input::frame&) noexcept;
	view present(const binding&, const hold&, const controller_input::frame&, std::span<const hands::bone>,
		const hands::vec& view_offset, float units, bool ads_requested) noexcept;
}
