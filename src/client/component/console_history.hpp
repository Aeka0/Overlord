#pragma once
#include <cstdint>
#include <deque>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>

namespace console::detail
{
	// Independent of terminal draining: selection/full pipes cannot erase the
	// error that preceded a support request. No disk I/O on console producers.
	class message_history
	{
		struct entry {int type{};std::uint64_t first{},last{},repetitions{1};std::string text;};
		std::mutex mutex_;
		std::deque<entry> entries_;
		entry first_error_, last_error_;
		std::size_t bytes_{};
		std::uint64_t discarded_{}, errors_{};
	public:
		static constexpr std::size_t byte_limit=256*1024, entry_limit=512, text_limit=8192;
		void push(int type,std::string_view text,std::uint64_t tick)
		{
			entry value{type,tick,tick,1,std::string(text.substr(0,text_limit))};
			if(text.size()>text_limit)value.text+=" [truncated]";
			const std::lock_guard lock(mutex_);
			if(type==1){if(!errors_)first_error_=value;last_error_=value;++errors_;}
			if(!entries_.empty()&&entries_.back().type==type&&entries_.back().text==value.text)
			{++entries_.back().repetitions;entries_.back().last=tick;return;}
			while(!entries_.empty()&&(entries_.size()>=entry_limit||bytes_+value.text.size()>byte_limit))
			{bytes_-=entries_.front().text.size();entries_.pop_front();++discarded_;}
			bytes_+=value.text.size();entries_.push_back(std::move(value));
		}
		std::string format(bool errors_only=false)
		{
			std::deque<entry> entries;entry first,last;std::uint64_t discarded{},errors{};
			{const std::lock_guard lock(mutex_);if(!errors_only)entries=entries_;first=first_error_;last=last_error_;discarded=discarded_;errors=errors_;}
			std::ostringstream out;
			out<<"console_history: scope=process retained="<<entries.size()<<" discarded="<<discarded<<" errors="<<errors<<'\n';
			const auto append=[&](const char* label,const entry& e)
			{out<<label<<" tick="<<e.first<<" last_tick="<<e.last<<" type="<<e.type<<" repeats="<<e.repetitions<<'\n'<<e.text<<'\n';};
			if(errors){append("first_console_error",first);append("last_console_error",last);}
			for(const auto& e:entries)append("message",e);
			return out.str();
		}
	};
}
