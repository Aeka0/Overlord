#pragma once
#include "physical_reload_profile.hpp"
#include "cylinder_profile.hpp"
namespace vr::gameplay::reload_items
{
	inline bool compatible(const weapons::reload_profile& a,const weapons::reload_profile& b)noexcept
	{
		if(a.native_name.empty() || b.native_name.empty() || a.ammunition.magazine_capacity<=0 || a.ammunition.magazine_capacity>1000 ||
			a.ammunition.magazine_capacity!=b.ammunition.magazine_capacity || a.ammunition.feed!=b.ammunition.feed ||
			a.ammunition.belt_fed!=b.ammunition.belt_fed)return false;
		const bool shared_asset=a.magazine_model && b.magazine_model && std::string_view(a.magazine_model)==b.magazine_model;
		return (shared_asset || a.native_name==b.native_name) && a.magazine_top==b.magazine_top &&
			a.magazine_rest.position==b.magazine_rest.position && a.magazine_rest.rotation==b.magazine_rest.rotation;
	}
	inline bool compatible(const weapons::cylinder_profile& a,const weapons::cylinder_profile& b)noexcept
	{return !a.native_name.empty() && a.native_name==b.native_name && a.ammunition.capacity>0 && a.ammunition.capacity<=64 &&
		a.ammunition.capacity==b.ammunition.capacity && a.loader_tip==b.loader_tip;}
}
