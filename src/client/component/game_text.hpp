#pragma once
#include "game_text_catalog.hpp"
#include <initializer_list>
#include <string>
#include <vector>
#include <optional>

// Shared MOD-owned in-game text entry point. No launcher preferences, disk IO,
// script VM calls or mutable translation cache in the formatter.
// VR action instructions use vr/hud_prompts.hpp above this layer; HUD consumers
// must not assemble their own control labels or native-key replacements.
namespace game_text
{
	locale current() noexcept;
	inline std::string_view utf8(std::u8string_view value) noexcept
	{return {reinterpret_cast<const char*>(value.data()),value.size()};}
	constexpr bool has_translation(key id,locale language) noexcept
	{
		const auto message=static_cast<std::size_t>(id),selected=static_cast<std::size_t>(language);
		return message<key_count && selected<catalogs.size() && !catalogs[selected][message].empty();
	}
	inline std::optional<std::string_view> translation(key id,locale language) noexcept
	{
		if(!has_translation(id,language))return {};
		return utf8(catalogs[static_cast<std::size_t>(language)][static_cast<std::size_t>(id)]);
	}
	inline locale effective_locale(key id,locale language) noexcept
	{
		const auto message=static_cast<std::size_t>(id),selected=static_cast<std::size_t>(language);
		return message<key_count && selected<catalogs.size() && !catalogs[selected][message].empty() ? language : locale::english;
	}
	inline std::string_view text(key id,locale language) noexcept
	{
		const auto index=static_cast<std::size_t>(id);
		if(index>=key_count)return "[missing text]";
		const auto language_index=static_cast<std::size_t>(language);
		const auto translated=catalogs[language_index<catalogs.size() ? language_index : 0][index];
		return utf8(translated.empty() ? english[index] : translated);
	}
	struct argument {std::string_view name,value;bool emphasized{};};
	struct run {std::string value;bool emphasized{};};
	using runs=std::vector<run>;
	inline constexpr std::size_t max_text_bytes=4096,max_runs=32;
	inline runs interpolate(std::string_view pattern,std::initializer_list<argument> arguments)
	{
		if(pattern.size()>max_text_bytes || arguments.size()>8)return {};
		runs out;out.reserve(8);std::size_t total{};
		const auto append=[&](std::string_view value,bool emphasized) {
			if(value.empty())return true;
			if(value.size()>max_text_bytes-total)return false;
			total+=value.size();
			if(!out.empty() && out.back().emphasized==emphasized){out.back().value+=value;return true;}
			if(out.size()>=max_runs)return false;
			out.push_back({std::string(value),emphasized});return true;
		};
		std::size_t cursor{},literal{};bool emphasis{};
		while(cursor<pattern.size())
		{
			const auto rest=pattern.substr(cursor);
			const bool opening=rest.starts_with("<em>"),closing=rest.starts_with("</em>");
			if(opening || closing)
			{
				if(opening==emphasis || !append(pattern.substr(literal,cursor-literal),emphasis))return {};
				emphasis=opening;cursor+=opening?4:5;literal=cursor;continue;
			}
			if(pattern[cursor]=='<' || pattern[cursor]=='>' || pattern[cursor]=='}')return {};
			if(pattern[cursor]!='{'){++cursor;continue;}
			if(!append(pattern.substr(literal,cursor-literal),emphasis))return {};
			const auto close=pattern.find('}',cursor+1);if(close==std::string_view::npos)return {};
			const auto name=pattern.substr(cursor+1,close-cursor-1);const argument* found{};
			for(const auto& arg:arguments)if(arg.name==name){if(found)return {};found=&arg;}
			if(!found || !append(found->value,emphasis || found->emphasized))return {};
			cursor=literal=close+1;
		}
		if(emphasis || !append(pattern.substr(literal),false))return {};
		return out;
	}
	inline runs format_runs(key id,locale language,std::initializer_list<argument> arguments={})
	{
		auto out=interpolate(text(id,language),arguments);
		if(out.empty() && language!=locale::english)out=interpolate(text(id,locale::english),arguments);
		return out;
	}
	inline std::string format(key id,locale language,std::initializer_list<argument> arguments={})
	{
		std::string out;
		for(const auto& run:format_runs(id,language,arguments))out+=run.value;
		return out;
	}
	inline key grip_key(int hand,bool shared) noexcept
	{return shared ? key::button_grip_either : hand==0 ? key::button_grip_left : key::button_grip_right;}
}
