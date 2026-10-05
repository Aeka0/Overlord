#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace vr::engine_stereo_dxbc_declarations
{
	enum class shader_profile : std::uint8_t
	{
		unknown,
		ps_4_0,
		ps_4_1,
		ps_5_0,
		unsupported,
	};

	template <std::size_t SrvSlots, std::size_t ConstantBufferSlots>
	struct declarations
	{
		shader_profile profile{shader_profile::unknown};
		std::array<bool, SrvSlots> shader_resources{};
		std::array<bool, ConstantBufferSlots> constant_buffers{};
		std::uint32_t resource_declarations{};
		std::uint32_t constant_buffer_declarations{};
		std::uint32_t out_of_range_declarations{};
		std::uint32_t malformed_declarations{};
		bool parsed{};
	};

	namespace detail
	{
		[[nodiscard]] constexpr bool ascii_digit(const char value) noexcept
		{
			return value >= '0' && value <= '9';
		}

		[[nodiscard]] constexpr char ascii_lower(const char value) noexcept
		{
			return value >= 'A' && value <= 'Z' ?
				static_cast<char>(value - 'A' + 'a') : value;
		}

		[[nodiscard]] constexpr bool ascii_space(const char value) noexcept
		{
			return value == ' ' || value == '\t' || value == '\r';
		}

		[[nodiscard]] constexpr std::string_view trim(
			std::string_view value) noexcept
		{
			while (!value.empty() && ascii_space(value.front())) value.remove_prefix(1);
			while (!value.empty() && ascii_space(value.back())) value.remove_suffix(1);
			return value;
		}

		[[nodiscard]] constexpr bool starts_with(const std::string_view value,
			const std::string_view prefix) noexcept
		{
			return value.size() >= prefix.size() &&
				value.substr(0, prefix.size()) == prefix;
		}

		[[nodiscard]] constexpr bool register_left_boundary(
			const std::string_view line, const std::size_t position) noexcept
		{
			if (position == 0) return true;
			const auto value = line[position - 1];
			return ascii_space(value) || value == ',' || value == '(' || value == ')';
		}

		[[nodiscard]] constexpr bool register_right_boundary(
			const std::string_view line, const std::size_t position) noexcept
		{
			if (position >= line.size()) return true;
			const auto value = line[position];
			return ascii_space(value) || value == ',' || value == '[' || value == ')';
		}

		[[nodiscard]] constexpr bool parse_register(const std::string_view line,
			const std::string_view prefix, std::uint32_t& output) noexcept
		{
			output = 0;
			for (std::size_t position{}; position + prefix.size() < line.size();
				++position)
			{
				bool prefix_matches = true;
				for (std::size_t prefix_index{}; prefix_index < prefix.size();
					++prefix_index)
				{
					if (ascii_lower(line[position + prefix_index]) !=
						ascii_lower(prefix[prefix_index]))
					{
						prefix_matches = false;
						break;
					}
				}
				if (!prefix_matches || !register_left_boundary(line, position)) continue;
				auto cursor = position + prefix.size();
				if (cursor >= line.size() || !ascii_digit(line[cursor])) continue;
				std::uint64_t value{};
				while (cursor < line.size() && ascii_digit(line[cursor]))
				{
					value = value * 10 + static_cast<std::uint64_t>(line[cursor] - '0');
					if (value > UINT32_MAX) return false;
					++cursor;
				}
				if (!register_right_boundary(line, cursor)) continue;
				output = static_cast<std::uint32_t>(value);
				return true;
			}
			return false;
		}

		[[nodiscard]] constexpr shader_profile parse_profile(
			const std::string_view line) noexcept
		{
			if (line == "ps_4_0") return shader_profile::ps_4_0;
			if (line == "ps_4_1") return shader_profile::ps_4_1;
			if (line == "ps_5_0") return shader_profile::ps_5_0;
			if (starts_with(line, "ps_")) return shader_profile::unsupported;
			return shader_profile::unknown;
		}
	}

	template <std::size_t SrvSlots, std::size_t ConstantBufferSlots>
	[[nodiscard]] constexpr bool parse(const char* const data,
		const std::size_t size,
		declarations<SrvSlots, ConstantBufferSlots>& output) noexcept
	{
		output = {};
		if (data == nullptr || size == 0) return false;
		std::string_view text(data, size);
		if (!text.empty() && text.back() == '\0') text.remove_suffix(1);
		if (text.find('\0') != std::string_view::npos) return false;
		while (!text.empty())
		{
			const auto delimiter = text.find('\n');
			auto line = detail::trim(text.substr(0, delimiter));
			if (delimiter == std::string_view::npos) text = {};
			else text.remove_prefix(delimiter + 1);
			if (line.empty() || detail::starts_with(line, "//")) continue;
			if (output.profile == shader_profile::unknown)
			{
				const auto profile = detail::parse_profile(line);
				if (profile != shader_profile::unknown)
				{
					output.profile = profile;
					if (profile == shader_profile::unsupported) return false;
				}
				continue;
			}
			if (detail::starts_with(line, "dcl_resource"))
			{
				std::uint32_t slot{};
				if (!detail::parse_register(line, "t", slot))
				{
					++output.malformed_declarations;
					continue;
				}
				++output.resource_declarations;
				if (slot < output.shader_resources.size())
					output.shader_resources[slot] = true;
				else ++output.out_of_range_declarations;
			}
			else if (detail::starts_with(line, "dcl_constantbuffer"))
			{
				std::uint32_t slot{};
				if (!detail::parse_register(line, "cb", slot))
				{
					++output.malformed_declarations;
					continue;
				}
				++output.constant_buffer_declarations;
				if (slot < output.constant_buffers.size())
					output.constant_buffers[slot] = true;
				else ++output.out_of_range_declarations;
			}
		}
		output.parsed = output.profile != shader_profile::unknown &&
			output.profile != shader_profile::unsupported;
		return output.parsed;
	}

	[[nodiscard]] constexpr const char* to_string(
		const shader_profile value) noexcept
	{
		switch (value)
		{
		case shader_profile::ps_4_0: return "ps_4_0";
		case shader_profile::ps_4_1: return "ps_4_1";
		case shader_profile::ps_5_0: return "ps_5_0";
		case shader_profile::unsupported: return "unsupported";
		default: return "unknown";
		}
	}
}
