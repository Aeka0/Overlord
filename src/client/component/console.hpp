#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace console
{
	enum console_type
	{
		con_type_error = 1,
		con_type_debug = 2,
		con_type_warning = 3,
		con_type_info = 7
	};

	void print(int type, const char* fmt, ...);

	inline constexpr std::size_t max_text_chunk_size = 3072;

	// Split already-formatted control-plane output into bounded console writes,
	// preserving complete lines
	// whenever an individual line fits in the bounded chunk.
	[[nodiscard]] inline std::vector<std::string_view> split_text_chunks(
		const std::string_view text, const std::size_t maximum_chunk_size = max_text_chunk_size)
	{
		std::vector<std::string_view> chunks;
		if (maximum_chunk_size == 0) return chunks;
		std::size_t offset{};
		while (offset < text.size())
		{
			const auto remaining = text.size() - offset;
			auto length = remaining < maximum_chunk_size ? remaining : maximum_chunk_size;
			if (length < remaining)
			{
				const auto newline = text.rfind('\n', offset + length - 1);
				if (newline != std::string_view::npos && newline >= offset)
				{
					length = newline - offset + 1;
				}
			}
			chunks.emplace_back(text.substr(offset, length));
			offset += length;
		}
		return chunks;
	}

	void print_text(int type, std::string_view text);

	template <typename... Args>
	void error(const char* fmt, Args&&... args)
	{
		print(con_type_error, fmt, std::forward<Args>(args)...);
	}

	template <typename... Args>
	void debug(const char* fmt, Args&&... args)
	{
#ifdef DEBUG
		print(con_type_debug, fmt, std::forward<Args>(args)...);
#endif
	}

	template <typename... Args>
	void warn(const char* fmt, Args&&... args)
	{
		print(con_type_warning, fmt, std::forward<Args>(args)...);
	}

	template <typename... Args>
	void info(const char* fmt, Args&&... args)
	{
		print(con_type_info, fmt, std::forward<Args>(args)...);
	}
}
