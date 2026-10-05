#pragma once
#include <cstdint>
#include <string>
#include "weapon_holding.hpp"
#include "weapon_carry.hpp"

namespace vr::gameplay::hands::empty_native
{
	// An independently owned, hands-only native DObj. No inventory token or
	// weapon animation tree is manufactured for empty-hand presentation.
	bool owns(const void* object) noexcept;
	std::uint64_t resource_generation() noexcept;
	void accept(const void* object, const void* matrices, std::uint32_t epoch) noexcept;
	std::string status();
}

namespace vr::gameplay::hands::owned_native
{
	// Stable native objects belong to carried instances, not native selection.
	bool active() noexcept;
	bool owns(const void* object) noexcept;
	weapons::hold owner(const void* object) noexcept;
	weapons::weapon_identity identity(const void* object) noexcept;
	std::uint64_t resource_generation(const void* object) noexcept;
	void accept(const void* object,const void* matrices,std::uint32_t epoch) noexcept;
}
