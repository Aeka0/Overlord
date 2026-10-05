#pragma once

#include <algorithm>
#include <span>

namespace vr::native_menu::stack
{
	// Only native popup entries retain their parent presentation. Ordinary page
	// navigation keeps history for Back, but must not display that closed history.
	enum class role {child,root};
	inline role classify(bool popup) noexcept
	{
		return popup?role::child:role::root;
	}
	inline std::size_t first_visible(std::span<const role> entries,std::size_t capacity) noexcept
	{
		std::size_t first{};
		for(std::size_t i=0;i<entries.size();++i)if(entries[i]==role::root)first=i;
		return std::max(first,entries.size()>capacity?entries.size()-capacity:0);
	}
}
