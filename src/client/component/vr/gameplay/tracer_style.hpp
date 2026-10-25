#pragma once
#include <array>
#include <cstdint>

namespace vr::gameplay::weapons::tracers
{
	enum class style { native, blaze, cerulean, royal, anime, independence_1776, rainbow };
	using rgb = std::array<float, 3>;
	inline constexpr style sanitize(int value) noexcept
	{ return value >= 0 && value <= int(style::rainbow) ? style(value) : style::native; }
	inline constexpr bool colored(style value) noexcept
	{ return value >= style::cerulean && value <= style::rainbow; }
	inline constexpr unsigned period(style value) noexcept
	{ return value == style::independence_1776 ? 3 : value == style::rainbow ? 7 : 1; }
	inline constexpr rgb color(style value, unsigned shot) noexcept
	{
		constexpr std::array<rgb, 3> independence{{{1.f,.04f,.03f}, {.04f,.25f,1.f}, {1.f,1.f,1.f}}};
		constexpr std::array<rgb, 7> spectrum{{{1.f,.04f,.03f}, {1.f,.35f,.02f}, {1.f,.9f,.02f},
			{.04f,1.f,.08f}, {.04f,.25f,1.f}, {.28f,.06f,.8f}, {.75f,.08f,1.f}}};
		switch (value)
		{
		case style::cerulean: return {.08f,.65f,1.f};
		case style::royal: return {1.f,.72f,.08f};
		case style::anime: return {1.f,.20f,.60f};
		case style::independence_1776: return independence[shot % independence.size()];
		case style::rainbow: return spectrum[shot % spectrum.size()];
		default: return {};
		}
	}
	struct sequence
	{
		style selected{style::native};
		unsigned next{};
		unsigned take(style value) noexcept
		{
			if (selected != value) { selected = value; next = 0; }
			const auto result = next;
			next = (next + 1) % period(value);
			return result;
		}
	};
	// Copy an admitted native definition. No shared asset writes, and no
	// alpha/geometry changes. Native is a true no-op, including drawInterval.
	template<class Definition> void apply(Definition& definition, style value, unsigned shot) noexcept
	{
		if (value == style::native) return;
		definition.drawInterval = 1;
		if (value == style::blaze) return;
		const auto tint = color(value, shot);
		for (auto& point : definition.colors)
			for (unsigned channel = 0; channel < tint.size(); ++channel) point[channel] = tint[channel];
	}
}
