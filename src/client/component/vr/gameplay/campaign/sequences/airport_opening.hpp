#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

namespace vr::gameplay::sequences::airport::opening
{
	inline constexpr std::size_t weapon_statement_size = 12;
	struct layout
	{
		std::array<std::size_t, 8> replacements{};
		std::size_t finish{};
		static constexpr std::size_t statement_size(std::size_t index) noexcept
		{
			return weapon_statement_size + (index == 6 ? 2 : 0);
		}
		std::size_t resume(std::size_t index) const noexcept
		{
			auto next = replacements[index] + statement_size(index);
			// VM redirects execute their target directly, without chaining
			// another hook. Coalesce adjacent give/switch statements.
			while (++index < replacements.size() && replacements[index] == next)
				next += statement_size(index);
			return next;
		}
	};
	inline std::optional<layout> inspect(std::span<const std::byte> code,
	                                     std::uint32_t m240,
	                                     std::uint32_t prop) noexcept
	{
		// airport_code::_id_C8E1. Redirect whole, stack-neutral statements,
		// including their arguments and DecTop; never just skip a method call.
		if (!m240 || !prop || m240 == prop || code.size() < 32 || code.size() > 512)
			return {};
		constexpr std::array<unsigned char, 4> entry{0x32, 0x89, 0x79, 0x2e};
		constexpr std::array<unsigned char, 4> exit{0x89, 0xa0, 1, 0x2e};
		if (std::memcmp(code.data(), entry.data(), entry.size()) ||
		    std::memcmp(code.data() + code.size() - 9, exit.data(), exit.size()) ||
		    code[code.size() - 2] != std::byte{0x6a} || code.back() != std::byte{0x34})
			return {};
		// The final ammo initialization belongs to the discarded replacement.
		// Retaining the instance must not refill or reset a physical reload.
		constexpr std::array<std::uint16_t, 8> methods{
		    0x831a, 0x8319, 0x8320, 0x831a, 0x8319, 0x833d, 0x8301, 0x8320};
		const std::array names{m240, prop, prop, prop, m240, m240, m240, m240};
		layout result;
		std::size_t count{};
		for (std::size_t i = 0; i + weapon_statement_size <= code.size(); ++i)
		{
			constexpr std::array<unsigned char, 3> player_method{0x55, 0x1a, 0x03};
			if (code[i] != std::byte{0x53} ||
			    std::memcmp(code.data() + i + 5, player_method.data(), player_method.size()) ||
			    code[i + 11] != std::byte{0x6a})
				continue;
			std::uint32_t name{};
			std::uint16_t method{};
			std::memcpy(&name, code.data() + i + 1, 4);
			std::memcpy(&method, code.data() + i + 9, 2);
			if ((name != m240 && name != prop) || (method != 0x831a && method != 0x8319 && method != 0x8320 &&
			                                       method != 0x833d && method != 0x8301))
				continue;
			if (count == methods.size() || name != names[count] || method != methods[count])
				return {};
			if (method == 0x8301)
			{
				if (i < 2 || code[i - 2] != std::byte{0xa0} || code[i - 1] != std::byte{100} ||
				    code[i + 8] != std::byte{0xab})
					return {};
				result.replacements[count++] = i - 2;
			}
			else
			{
				if (code[i + 8] != std::byte{0xaa})
					return {};
				result.replacements[count++] = i;
			}
			i += weapon_statement_size - 1;
		}
		if (count != methods.size())
			return {};
		result.finish = code.size() - 1;
		return result;
	}
	// Latched by native script entry/exit and restored from the saved level
	// field. Rendering and grip ownership continue while shooting is blocked.
	bool active() noexcept;
}
