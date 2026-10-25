#pragma once
#include "weapon_identity.hpp"
#include <string>

namespace vr::gameplay::weapons::tracers
{
	namespace detail
	{
		struct binding { weapon_identity id{}, lease{}; };
		inline thread_local binding owner{};
	}
	// Definition tokens cannot distinguish two carried copies of the same gun.
	// Supply the committed owner across the synchronous native bullet call.
	class owner_scope
	{
		detail::binding previous_{};
	public:
		explicit owner_scope(weapon_identity id, weapon_identity lease = {}) noexcept : previous_(detail::owner)
		{ detail::owner = {id, lease.weapon ? lease : id}; }
		~owner_scope() { detail::owner = previous_; }
		owner_scope(const owner_scope&) = delete;
		owner_scope& operator=(const owner_scope&) = delete;
	};
	std::string status();
}
