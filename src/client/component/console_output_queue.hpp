#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <limits>
#include <mutex>
#include <string>
#include <utility>

namespace console::detail
{
	struct output_message {int type{};std::string text;std::uint64_t repetitions{1};};
	struct output_batch {std::deque<output_message> messages;std::uint64_t dropped{};};
	// Producers never hold this lock during terminal I/O or game-console drawing.
	// A stopped/selected terminal cannot grow an unbounded log or stall rendering.
	class output_queue
	{
		std::mutex mutex_;
		output_batch pending_;
		std::size_t bytes_{};
	public:
		static constexpr std::size_t byte_limit=256*1024,message_limit=256;
		bool push(int type,std::string text)
		{
			const std::lock_guard lock(mutex_);
			if(!pending_.messages.empty())
			{
				auto& last=pending_.messages.back();
				if(last.type==type && last.text==text && last.repetitions<std::numeric_limits<std::uint64_t>::max())
				{++last.repetitions;return true;}
			}
			if(text.size()>byte_limit-bytes_ || pending_.messages.size()==message_limit)
			{if(pending_.dropped<std::numeric_limits<std::uint64_t>::max())++pending_.dropped;return false;}
			bytes_+=text.size();pending_.messages.push_back({type,std::move(text),1});return true;
		}
		output_batch take()
		{
			const std::lock_guard lock(mutex_);output_batch batch;batch.messages.swap(pending_.messages);
			batch.dropped=std::exchange(pending_.dropped,0);bytes_=0;return batch;
		}
	};
}
