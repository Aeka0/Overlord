#pragma once
#include <algorithm>
#include <string_view>

namespace vr::gameplay::weapons
{
	inline bool native_weapon_family(std::string_view name,std::string_view family) noexcept
	{
		if (family.empty() || name.empty() || name.size()>=64 || !name.starts_with(family) ||
			(name.size()!=family.size() && (name.size()<=family.size()+1 || name[family.size()]!='_'))) return false;
		return std::all_of(name.begin(),name.end(),[](char c) {
			return (c>='a' && c<='z') || (c>='0' && c<='9') || c=='_';
		});
	}
}
