#pragma once
#include <algorithm>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace utils
{
	// Register on the component initialization owner before native consumers run.
	// Storage grows only at startup; dispatch is allocation-free, read-only and
	// keeps registration order. Concurrent/late registration is not supported.
	template<class Callback>
	class startup_callbacks
	{
		static_assert(std::is_pointer_v<Callback> && std::is_function_v<std::remove_pointer_t<Callback>>);
		std::vector<Callback> values_;
	public:
		void add(Callback callback)
		{
			if(!callback)throw std::invalid_argument("empty startup callback");
			if(std::find(values_.begin(),values_.end(),callback)==values_.end())values_.push_back(callback);
		}
		bool empty()const noexcept{return values_.empty();}
		std::size_t size()const noexcept{return values_.size();}
		auto begin()const noexcept{return values_.cbegin();}
		auto end()const noexcept{return values_.cend();}
	};
}
