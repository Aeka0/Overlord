#pragma once
#include "game/assets.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace vr::gameplay::native_fx::checkpoint
{
	enum class variant { world, followed };
	inline constexpr std::string_view prefix = "__overlord_fx/";
	inline constexpr std::size_t source_name_limit = 255;
	inline constexpr std::size_t variant_capacity = 136; // 128 shell graph nodes + 8 followed definitions.
	inline constexpr std::size_t native_capacity = 2048;
	inline constexpr unsigned effect_size = 0x140, handle_unit = 16;
	inline bool valid_effect_handle(unsigned handle) noexcept
	{
		return handle % (effect_size / handle_unit) == 0 &&
			handle / (effect_size / handle_unit) < native_capacity;
	}
	struct identity { variant kind{}; unsigned attached{}; std::string_view source; };

	inline bool valid_source(std::string_view source) noexcept
	{
		return !source.empty() && source.size() <= source_name_limit &&
			source.find('\0') == source.npos && !source.starts_with(prefix);
	}
	inline std::string name(variant kind, std::string_view source, unsigned attached = 0)
	{
		if (!valid_source(source)) return {};
		std::string result(prefix);
		if (kind == variant::world) result += "world/";
		else
		{
			result += "follow/";
			constexpr char hex[] = "0123456789abcdef";
			for (int shift = 28; shift >= 0; shift -= 4) result += hex[(attached >> shift) & 15];
			result += '/';
		}
		result += source;
		return result;
	}
	inline std::optional<identity> parse(std::string_view value) noexcept
	{
		if (!value.starts_with(prefix)) return {};
		value.remove_prefix(prefix.size());
		identity result;
		if (value.starts_with("world/")) value.remove_prefix(6);
		else if (value.starts_with("follow/") && value.size() >= 16 && value[15] == '/')
		{
			result.kind = variant::followed;
			const auto mask = value.substr(7, 8);
			const auto parsed = std::from_chars(mask.data(), mask.data() + mask.size(), result.attached, 16);
			if (parsed.ec != std::errc{} || parsed.ptr != mask.data() + mask.size()) return {};
			value.remove_prefix(16);
		}
		else return {};
		if (!valid_source(value)) return {};
		result.source = value;
		return result;
	}

	// A bounded checkpoint dictionary, not a DB asset pool. Descriptors remain
	// owned by their presentation caches and must be removed before those retire.
	class registry
	{
		std::array<game::FxEffectDef*, variant_capacity> definitions_{};
		std::size_t count_{};
	public:
		bool publish(std::span<game::FxEffectDef* const> definitions) noexcept
		{
			std::array<game::FxEffectDef*, variant_capacity> pending{};
			std::size_t added{};
			for (auto* definition : definitions)
			{
				if (!definition || !definition->name) return false;
				const auto length = strnlen_s(definition->name, source_name_limit + 64);
				if (!parse({definition->name, length})) return false;
				bool existing{};
				const auto inspect = [&](game::FxEffectDef* other)
				{
					if (other == definition) existing = true;
					return other == definition || std::strcmp(other->name, definition->name) != 0;
				};
				for (std::size_t i = 0; i < count_; ++i) if (!inspect(definitions_[i])) return false;
				for (std::size_t i = 0; i < added; ++i) if (!inspect(pending[i])) return false;
				if (existing) continue;
				if (count_ + added == variant_capacity) return false;
				pending[added++] = definition;
			}
			std::copy_n(pending.begin(), added, definitions_.begin() + count_);
			count_ += added;
			return true;
		}
		bool append(std::span<game::FxEffectDef*> native, std::size_t& count) const noexcept
		{
			if (count > native.size() || count_ > native.size() - count) return false;
			std::copy_n(definitions_.begin(), count_, native.begin() + count);
			count += count_;
			return true;
		}
		void remove(variant kind) noexcept
		{
			const auto end = std::remove_if(definitions_.begin(), definitions_.begin() + count_, [kind](auto* d)
			{
				const auto id = parse(d->name);
				return id && id->kind == kind;
			});
			count_ = static_cast<std::size_t>(end - definitions_.begin());
		}
		std::size_t size() const noexcept { return count_; }
	};

	// Only the observed, already empty root tail can be retired without its lost
	// definition. Live particles, child ownership, bolts and shared references
	// require that definition and must reject the checkpoint before any consumer.
	inline bool empty_orphan_tail(std::span<const std::byte> bytes, unsigned handle) noexcept
	{
		if (bytes.size() < 0xe8) return false;
		const auto read = [&]<class T>(std::size_t offset)
		{
			T result{}; std::memcpy(&result, bytes.data() + offset, sizeof(T)); return result;
		};
		if (read.template operator()<std::uint64_t>(0) ||
			read.template operator()<unsigned>(8) != 0x04008001 ||
			read.template operator()<unsigned>(0x34) != handle ||
			read.template operator()<unsigned short>(0x4e) != 0xffff) return false;
		for (std::size_t offset = 0xc; offset < 0x28; offset += 4)
			if (read.template operator()<unsigned>(offset) != 0xffffffff) return false;
		return read.template operator()<unsigned short>(0x28) == 0xffff;
	}
}
