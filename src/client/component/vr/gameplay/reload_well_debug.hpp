#pragma once
#include "reload_debug.hpp"

namespace vr::gameplay::weapons::physical_reload::well_debug
{
	struct sample : debug::sample_identity
	{
		hands::anchor well_model{};
		hands::vec raw_tip{}, visual_tip{}, examined_tip{}; // meters in well space
		float alignment{};
		bool held{}, occupied{}, examined{}, well_contact{}, requires_withdrawal{};
		const char* decision{"not examined"}; // immutable controller-owned literals only
	};
	bool enabled() noexcept;
	void publish(const sample& value) noexcept;
}
