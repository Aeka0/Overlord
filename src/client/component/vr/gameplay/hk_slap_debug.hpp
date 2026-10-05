#pragma once
#include "reload_debug.hpp"

namespace vr::gameplay::weapons::physical_reload::hk_slap_debug
{
	struct sample : debug::sample_identity
	{
		hands::anchor contact_model{}; // Raised tab centre; axes remain gun-local.
		std::array<hands::vec,hands::hand_contact_count> raw{}, visual{};
		bool valid{}, visual_valid{};
		slap_trace trace{};
	};
	bool enabled() noexcept;
	void publish(const sample& value) noexcept;
}
