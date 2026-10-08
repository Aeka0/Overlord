#pragma once

#include <array>
#include <memory>
#include <string>
#include <string_view>

namespace vr::prompt_bindings
{
	enum class action
	{
		left_trigger, right_trigger, left_grip, right_grip,
		left_secondary, right_secondary, menu_recenter, menu_toggle,
		move, turn, jump, sprint, count
	};
	inline constexpr std::size_t action_count = static_cast<std::size_t>(action::count);
	inline constexpr std::size_t max_label_bytes = 256;

	// Runtime-owned metadata, copied with the input frame. HUD code never calls
	// a runtime, reads a binding file, or assumes a controller's face-button names.
	struct snapshot
	{
		std::array<std::string, action_count> labels;
		std::array<bool, action_count> known{};
	};

	inline bool valid_label(std::string_view label) noexcept
	{
		if (label.empty() || label.size() > max_label_bytes) return false;
		for (const unsigned char value : label)
			if (value < 32 || value == 127 || value == '^' || value == '<' || value == '>') return false;
		return true;
	}

	inline std::string_view label(const snapshot* bindings, action id) noexcept
	{
		const auto index = static_cast<std::size_t>(id);
		if (!bindings || index >= action_count) return {};
		const auto& value = bindings->labels[index];
		return valid_label(value) ? std::string_view(value) : std::string_view{};
	}

	inline void append(std::string& output, std::string_view value)
	{
		if (!valid_label(value) || output == value) return;
		if (output.empty()) output = value;
		else if (output.size() + 3 + value.size() <= max_label_bytes)
		{
			output += " / ";
			output += value;
		}
	}
}
