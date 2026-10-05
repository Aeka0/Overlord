#pragma once
#include <optional>

namespace vr::gameplay::hand_interaction
{
	// The coordinator supplies one semantic pinch edge and acquisition grant.
	// Standalone mechanical tests may omit the edge to exercise their local
	// neutral/reconnect guards without linking the engine coordinator.
	struct access
	{
		bool manipulation{true},acquire{true},supply{true};
		std::optional<bool> pinch{};
		bool parts{true},release{};
		bool support_catch{true},grip_release{};
		std::optional<bool> secondary{};
		bool preserve_discard{};
	};
}
