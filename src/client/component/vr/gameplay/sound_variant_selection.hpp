#pragma once
#include <span>
#include <string_view>

namespace vr::gameplay::weapons
{
	inline std::string_view sound_file_stem(std::string_view path) noexcept
	{
		const auto slash=path.find_last_of("/\\");
		if(slash!=std::string_view::npos)path.remove_prefix(slash+1);
		const auto dot=path.find_last_of('.');
		return path.substr(0,dot);
	}
	// An alias may randomize between several files. Admit it only if every
	// variant belongs to the requested set; never guess an alias from a filename.
	inline unsigned sound_variant_mask(std::span<const std::string_view> requested,
		std::span<const std::string_view> files) noexcept
	{
		if(requested.empty() || requested.size()>32 || files.empty())return 0;
		unsigned result{};
		for(const auto file:files)
		{
			unsigned bit{};
			for(unsigned i=0;i<requested.size();++i)if(sound_file_stem(file)==requested[i])bit|=1u<<i;
			if(!bit)return 0;
			result|=bit;
		}
		return result;
	}
}
