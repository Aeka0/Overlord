#pragma once
#include "underbarrel_feed.hpp"
#include <string_view>
namespace vr::gameplay::weapons::underbarrel
{
	inline bool module_name(std::string_view name) noexcept
	{
		if(name.empty() || name.size()>127 || name.front()=='_' || name.back()=='_' || name.find("__")!=std::string_view::npos)return false;
		for(char c:name)if(!((c>='a'&&c<='z') || (c>='0'&&c<='9') || c=='_'))return false;
		return true;
	}
	inline bool module_family(std::string_view name,std::string_view family) noexcept
	{return name==family || (name.size()>family.size() && name.starts_with(family) && name[family.size()]=='_');}
	inline bool shotgun_pair(std::string_view host,std::string_view child) noexcept
	{
		// Optic/skin variants have their own linked "<host>_attach" definition.
		// Require a complete shotgun name component and the exact paired host;
		// substring matches must not admit another weapon's secondary feed.
		if(host.size()>120 || child.size()!=host.size()+7 || !child.starts_with(host) || child.substr(host.size())!="_attach")return false;
		if(!module_name(host))return false;
		for(auto family:{std::string_view{"ak47"},std::string_view{"scar_h"},std::string_view{"fal"}})
		{
			if(!host.starts_with(family) || host.size()<=family.size()+1 || host[family.size()]!='_')continue;
			auto suffix=host.substr(family.size()+1);bool shotgun=false;
			while(!suffix.empty())
			{
				const auto end=suffix.find('_');const auto part=suffix.substr(0,end);
				if(part.empty())return false;if(part=="shotgun")shotgun=true;
				if(end==std::string_view::npos)return shotgun;
				suffix.remove_prefix(end+1);if(suffix.empty())return false;
			}
		}
		return false;
	}
	inline kind classify(std::string_view host,std::string_view child,int type,int weapon_class,int count)noexcept
	{
		if(!module_name(host) || !module_name(child))return kind::none;
		kind k=kind::none;
		// Host and linked child must belong to the same reviewed attachment
		// family. Native aliases include m4m203[_optic], not just m4_grenadier.
		if(((module_family(host,"m4") || module_family(host,"m4m203")) && module_family(child,"m203_m4")) ||
			(module_family(host,"m16") && module_family(child,"m203_m16")) ||
			(module_family(host,"scar_h") && module_family(child,"scar_h_m203")) ||
			(module_family(host,"masada") && module_family(child,"gl_masada")))k=kind::m203;
		else if(module_family(host,"ak47") && module_family(child,"gl_ak47"))k=kind::gp25;
		else if(shotgun_pair(host,child))k=kind::shotgun;
		return k!=kind::none && count==capacity(k) && type==(k==kind::shotgun?1:3) && weapon_class==(k==kind::shotgun?4:6) ? k : kind::none;
	}
}
