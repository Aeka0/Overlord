#pragma once
#include <utils/startup_callbacks.hpp>
#include <array>
#include <iostream>
#include <utility>

namespace startup_callbacks_tests
{
	inline std::array<unsigned,64> calls{};
	inline std::array<unsigned,64> order{};
	inline unsigned cursor{};
	template<std::size_t Index> void observer(){++calls[Index];order[cursor++]=unsigned(Index);}
	template<std::size_t... Index> constexpr auto observers(std::index_sequence<Index...>)
	{return std::array<void(*)(),sizeof...(Index)>{observer<Index>...};}
	inline int transform(int value){return value+7;}
	inline int run()
	{
		int failures{};const auto check=[&](bool ok,const char* why){if(!ok){++failures;std::cerr<<"FAIL: startup callbacks: "<<why<<'\n';}};
		utils::startup_callbacks<void(*)()> list;
		check(list.empty() && list.size()==0 && list.begin()==list.end(),"unregistered callbacks form an empty read-only range");
		constexpr auto entries=observers(std::make_index_sequence<64>{});
		for(std::size_t n=0;n<9;++n)list.add(entries[n]);
		check(list.size()==9,"ninth zone-unload observer no longer aborts component startup");
		for(std::size_t n=9;n<entries.size();++n)list.add(entries[n]);
		for(auto entry:entries)list.add(entry);
		check(list.size()==64,"growth and repeated registration never duplicate cleanup owners");
		bool rejected=false;try{list.add(nullptr);}catch(const std::invalid_argument&){rejected=true;}
		check(rejected && list.size()==64,"null callback rejected without corrupting registration");
		calls={};const auto& published=list;
		for(unsigned pass=1;pass<=2;++pass)
		{
			cursor=0;for(const auto callback:published)callback();
			check(cursor==64,"each unload boundary calls every registered owner");
			for(unsigned n=0;n<64;++n)check(calls[n]==pass && order[n]==n,"dispatch preserves registration order after storage growth");
		}
		utils::startup_callbacks<int(*)(int)> placements;placements.add(transform);placements.add(transform);
		check(placements.size()==1 && (*placements.begin())(5)==12,"same registry supports typed placement callbacks and return values");
		return failures;
	}
}
