#pragma once
#include "directional_ui.hpp"
#include "narrative_ui.hpp"
#include <vector>
#include <functional>
#include <optional>
#include <span>
#include <limits>

namespace vr::native_waypoints
{
	struct group
	{
		std::uintptr_t arena{}, begin{}, end{};
		std::uint64_t timestamp{}, hash{};
		directional_ui::waypoint marker{};
		std::optional<directional_ui::rectangle> measured_bounds{};
		bool narrative{};
		bool progress{};
		std::optional<narrative_ui::backdrop> hint_backdrop;
		bool script_ink{},panel{};
	};
	enum class text_owner{none,story,progress,panel};
	inline bool owns_hint_backdrop(std::span<const group> groups,std::uintptr_t address,std::size_t size) noexcept
	{
		if (!size || size>std::numeric_limits<std::uintptr_t>::max()-address) return false;
		for (const auto& g:groups)
			if (g.narrative && g.hint_backdrop && narrative_ui::valid_backdrop(*g.hint_backdrop) &&
				address>=g.begin && address+size<=g.end) return true;
		return false;
	}
	inline text_owner owner(std::span<const group> groups,std::uintptr_t address,size_t size) noexcept
	{
		if(!size || size>std::numeric_limits<std::uintptr_t>::max()-address)return text_owner::none;
		for(const auto& g:groups)if(g.narrative && address>=g.begin && address+size<=g.end)
			return g.progress?text_owner::progress:g.panel?text_owner::panel:text_owner::story;
		return text_owner::none;
	}
	inline bool announcement(text_owner source,const void* data,std::size_t size) noexcept
	{return source!=text_owner::progress && source!=text_owner::panel && narrative_ui::announcement_command(data,size);}
	inline bool owns_script_ink(std::span<const group> groups,std::uintptr_t address,std::size_t size) noexcept
	{
		if (!size || size>std::numeric_limits<std::uintptr_t>::max()-address) return false;
		for (const auto& g:groups)
			if (g.narrative && g.script_ink && address>=g.begin && address+size<=g.end) return true;
		return false;
	}
	// Exact immutable native command ranges, expired on reuse of their allocator.
	// No GPU work, asset rebuilding, or command replay in the producer hooks.
	std::vector<group> for_stream(std::uintptr_t begin, std::uintptr_t end);
	// Record commands authored by another world-marker producer through the
	// same native allocator lifetime and atlas path as ordinary waypoints.
	// An owned producer may supply frontend font-measured bounds (including
	// overhang/shadow). Native observed groups retain conservative byte bounds.
	bool draw_world_marker(const directional_ui::waypoint&, const std::function<void()>& draw,
		std::optional<directional_ui::rectangle> measured_bounds = {});
	// Head-relative authored text shares allocator/fingerprint ownership with
	// world prompts, but is composed by the existing narrative canvas.
	bool draw_narrative_text(const std::function<void()>& draw);
	struct counters { std::uint64_t calls{}, recorded{}, matched{}, rejected{}, resets{}; };
	counters get_counters() noexcept;
}
