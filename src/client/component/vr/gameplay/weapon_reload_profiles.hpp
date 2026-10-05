#pragma once
#include "weapon_registration.hpp"
#include "physical_reload_profile.hpp"

namespace vr::gameplay::weapons
{
	extern const profile_capabilities<reload_profile> reload_profiles;
	inline const reload_profile* native_reload_profile(std::string_view name, int capacity,
		const reload_profile* scene=nullptr) noexcept
	{
		// The complete scene selects an immutable render recipe within a native
		// family. Only registered descriptors can participate; exact instance
		// name, base capacity and native mode checks remain in the adapter.
		if (scene)
		{
			for (const auto* p:reload_profiles) if (p==scene) return p->matches_native(name,capacity) ? p : nullptr;
			return nullptr;
		}
		for (const auto* p : reload_profiles) if (p->matches_native(name,capacity)) return p;
		return nullptr;
	}
	inline size_t reload_profile_index(const reload_profile* p) noexcept
	{
		for (size_t i=0;i<reload_profiles.size();++i) if (reload_profiles[i] == p) return i;
		return reload_profiles.size();
	}
	inline bool native_reload_shape_supported(std::string_view name,int capacity,bool segmented,int reload_add) noexcept
	{
		// Zero is the existing native default. AA-12 explicitly adds a complete
		// eight-shell magazine. Admit full replacements only for registered feeds;
		// segmented/partial additions require their own mechanical transaction.
		return !segmented && (reload_add==0 ||
			(reload_add>0 && reload_add==capacity && native_reload_profile(name,capacity)));
	}
}
