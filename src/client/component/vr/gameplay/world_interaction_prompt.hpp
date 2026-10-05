#pragma once
#include <string>
#include <string_view>

namespace vr::gameplay::interaction::prompt_text
{
	struct weapon_name {std::string name, attachments;};
	inline void plain(std::string& text)
	{
		for (auto& c:text) if (static_cast<unsigned char>(c)<32 || c=='^') c=' ';
		const auto first=text.find_first_not_of(' ');
		if (first==std::string::npos) {text.clear();return;}
		text=text.substr(first,text.find_last_not_of(' ')-first+1);
	}
	// Use localization ancestry, not a first-space split: base weapon names
	// themselves can contain spaces. Unknown/non-prefix translations stay intact.
	template<class Lookup> weapon_name split(std::string_view key,std::string name,Lookup lookup)
	{
		weapon_name out;
		const auto newline=name.find_first_of("\r\n");
		if (newline!=std::string::npos)
		{
			out={name.substr(0,newline),name.substr(newline+1)};
			plain(out.name);plain(out.attachments);return out;
		}
		plain(name);out.name=name;
		const auto consider=[&](std::string base) {
			plain(base);
			if (!base.empty() && base.size()<out.name.size() && name.size()>base.size() &&
				name.compare(0,base.size(),base)==0 && name[base.size()]==' ')
			{
				out={base,name.substr(base.size()+1)};plain(out.attachments);
			}
		};
		if (key.size()>256 || key.substr(0,7)!="WEAPON_") return out;
		for (auto end=key.find('_',7);end!=std::string_view::npos;end=key.find('_',end+1))
			consider(lookup(key.substr(0,end)));
		// These native display keys omit the base-key hierarchy.
		if (key=="WEAPON_M4M203" || key.starts_with("WEAPON_M4M203_")) consider(lookup("WEAPON_M4_CARBINE"));
		if (key.starts_with("WEAPON_M14EBR_")) consider("M14 EBR");
		return out;
	}
}
