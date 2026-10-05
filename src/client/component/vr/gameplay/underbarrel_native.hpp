#pragma once
#include "underbarrel_feed.hpp"
#include "shot_geometry.hpp"
#include "native_ammunition.hpp"
namespace vr::gameplay::weapons::underbarrel::native
{
	struct binding {identity id{};std::uint64_t clip{},reserve{},timeline{};bool operator==(const binding&)const=default;explicit operator bool()const noexcept{return bool(id)&&clip&&reserve;}};
	struct observation {bool valid{};binding module{};ammunition::projection ammo{};};
	bool initialize();
	binding resolve(weapon_identity)noexcept;
	observation observe(const binding&)noexcept;
	bool commit(const observation&,ammunition::projection)noexcept;
	bool exchange_reserves(const observation&,const native_ammunition::snapshot& primary,int primary_reserve,int secondary_reserve)noexcept;
	int interval(const binding&)noexcept;
	// Debit and mechanical settlement precede native damage/script callbacks.
	// No selected weapon/mode or host holding-hand mutation is permitted.
	using settle_fn=void(*)(void*)noexcept;
	bool fire(const observation&,ammunition::projection after,const shot_geometry&,int command,settle_fn,void*)noexcept;
}
