#pragma once
#include "game_text/catalog_types.hpp"
#include "game_text/template_contract.hpp"
#include <utility>
#include "game_text/locales/en.hpp"
#include "game_text/locales/zh-CN.hpp"
#include "game_text/locales/zh-TW.hpp"
#include "game_text/locales/fr.hpp"
#include "game_text/locales/de.hpp"
#include "game_text/locales/it.hpp"
#include "game_text/locales/es.hpp"
#include "game_text/locales/ru.hpp"
#include "game_text/locales/pl.hpp"
#include "game_text/locales/pt.hpp"
#include "game_text/locales/ja.hpp"
#include "game_text/locales/ar.hpp"
#include "game_text/locales/cs.hpp"
#include "game_text/locales/es-419.hpp"
#include "game_text/locales/ko.hpp"
#include "game_text/locales/tr.hpp"

namespace game_text
{
	// Single assembly/validation entry; translated literals live only in locales/.
	using locales::english;
	using locales::simplified_chinese;
	inline constexpr std::array<catalog,static_cast<std::size_t>(locale::count)> catalogs{
		locales::english, // en
		locales::simplified_chinese, // zh-CN
		locales::traditional_chinese, // zh-TW
		locales::french, // fr
		locales::german, // de
		locales::italian, // it
		locales::spanish, // es
		locales::russian, // ru
		locales::polish, // pl
		locales::portuguese, // pt
		locales::japanese, // ja
		locales::arabic, // ar
		locales::czech, // cs
		locales::spanish_latin_america, // es-419
		locales::korean, // ko
		locales::turkish, // tr
	};
	// Validate each catalog separately so adding translations does not consume
	// one compiler constexpr budget for every language's UTF-8 byte stream.
	template<std::size_t Language> inline constexpr bool valid_catalog=[] {
		for(std::size_t i=0;i<key_count;++i)
			if(!catalogs[Language][i].empty() && template_parameters(catalogs[Language][i])!=parameters(static_cast<key>(i)))return false;
		return true;
	}();
	template<std::size_t... Languages> constexpr bool valid_catalogs(std::index_sequence<Languages...>)
	{
		return (valid_catalog<Languages> && ...);
	}
	static_assert(valid_catalogs(std::make_index_sequence<catalogs.size()>{}),"Every template must match its language-independent contract");
	static_assert([] {
		for(std::size_t i=0;i<key_count;++i)
		{
			bool present{};
			for(const auto& language:catalogs)
				if(!language[i].empty()){present=true;break;}
			if(!present)return false;
		}
		return true;
	}(),"Every key needs a translation");
}
