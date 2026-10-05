#pragma once
#include "physical_reload_profile.hpp"

namespace game { struct XModel; }
namespace vr::gameplay::weapons::physical_reload::partition_assets
{
	struct asset
	{
		std::array<game::XModel*,2> models{}; // Source group remainder, independently posed piece.
		std::array<hands::anchor,2> in_part{};
		explicit operator bool() const noexcept{return models[0] && models[1];}
	};
	asset get(const reload_profile*) noexcept;
	const char* status(const reload_profile*) noexcept;
	void refresh();
	void clear() noexcept;
	void retire_after_drain() noexcept;
}
