#pragma once
#include "world_interaction_policy.hpp"

namespace vr::gameplay::interaction::debug {struct world_sample;}

namespace vr::gameplay::interaction::native
{
	bool initialize();
	bool ready() noexcept;
	target query(const ray&, target_key held_target = {}, debug::world_sample* diagnostic = nullptr);
	bool live(const target&) noexcept;
	// The command owner publishes the exact admitted hold. The native server
	// use pass validates that target again through its normal hint/use pipeline.
	void set_command_lease(const target&, bool held, std::uint64_t reference) noexcept;
	const char* status() noexcept;
}
