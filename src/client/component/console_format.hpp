#pragma once
#include <array>
#include <cstdarg>
#include <cstdio>
#include <string>

namespace console::detail
{
	inline constexpr std::size_t maximum_message_size=1024*1024;
	inline std::string format_message(const char* format,va_list arguments)
	{
		if (!format) return {};
		std::array<char,4096> local{};
		va_list copy;va_copy(copy,arguments);
		const int count=std::vsnprintf(local.data(),local.size(),format,copy);
		va_end(copy);
		if (count<0) return {};
		if (static_cast<std::size_t>(count)<local.size()) return {local.data(),static_cast<std::size_t>(count)};
		// Status reports may exceed 4 KiB. C99 vsnprintf reports the required
		// size without invoking the secure CRT's invalid-parameter assertion.
		// Keep pathological width/precision requests from allocating gigabytes.
		if (static_cast<std::size_t>(count)>maximum_message_size)
			return "Console message exceeds 1 MiB; write detailed diagnostics to a file.\n";
		std::string result(static_cast<std::size_t>(count),'\0');
		va_copy(copy,arguments);
		const int written=std::vsnprintf(result.data(),result.size()+1,format,copy);
		va_end(copy);
		if (written<0 || written>count) return {};
		result.resize(static_cast<std::size_t>(written));
		return result;
	}
}
