#pragma once
#include "physical_reload_profile.hpp"

namespace game { struct XModel; }
namespace vr::gameplay::weapons::physical_reload::magazine_assets
{
	struct asset { game::XModel* model{}; hands::anchor in_magazine{}; };
	asset get(const reload_profile* definition,int rounds) noexcept;
	asset cartridge(const reload_profile* definition) noexcept;
	const char* status(const reload_profile* definition) noexcept;
	void refresh(); // Main asset owner only; never called under a gameplay/render lock.
	void clear() noexcept;
	void retire_after_drain() noexcept; // Native asset-unload barrier, after queued rendering drains.
}
