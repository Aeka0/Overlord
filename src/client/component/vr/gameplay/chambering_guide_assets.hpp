#pragma once
#include "weapon_profile.hpp"
#include "../opaque_mesh_renderer.hpp"
#include <string>

namespace game { struct XModel; }
namespace vr::gameplay::weapons::chambering_guide
{
	struct part {std::shared_ptr<const opaque_mesh::mesh> geometry;hands::anchor in_part{};};
	struct assets
	{
		std::array<part,4> bones{}; // Action, folding tip, receiver catch, authored action detail.
		std::array<part,2> partition{}; // Only independently operated handle pieces.
	};
	bool enabled() noexcept;
	assets request(const profile&) noexcept;
	void initialize();
	void refresh();
	void clear() noexcept;
	void retire_after_drain() noexcept;
	std::string status();
}
