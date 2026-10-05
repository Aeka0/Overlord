#pragma once
#include <string_view>

namespace game_text
{
	inline constexpr unsigned invalid_parameters=4;
	// <em> annotates authored controls/locations, never an already-formatted
	// native argument. Only balanced, non-nested emphasis is accepted.
	constexpr unsigned template_parameters(std::u8string_view pattern) noexcept
	{
		if(pattern.size()>4096)return invalid_parameters;
		unsigned result{};bool emphasis{};
		for(std::size_t i=0;i<pattern.size();++i)
		{
			if(pattern.substr(i,4)==u8"<em>"){if(emphasis)return invalid_parameters;emphasis=true;i+=3;continue;}
			if(pattern.substr(i,5)==u8"</em>"){if(!emphasis)return invalid_parameters;emphasis=false;i+=4;continue;}
			if(pattern[i]==u8'<' || pattern[i]==u8'>' || pattern[i]==u8'}')return invalid_parameters;
			if(pattern[i]!=u8'{')continue;
			const auto end=pattern.find(u8'}',i+1);if(end==pattern.npos)return invalid_parameters;
			const auto name=pattern.substr(i+1,end-i-1);
			if(name==u8"button")result|=1;
			else if(name==u8"item")result|=2;
			else return invalid_parameters;
			i=end;
		}
		return emphasis?invalid_parameters:result;
	}
}
