#pragma once

#include <utils/nt.hpp>
#include <cstddef>
#include <cstdint>

namespace tls
{
	struct image_view
	{
		std::uintptr_t base{};
		std::size_t size{};
		IMAGE_DATA_DIRECTORY directory{};
	};

	[[nodiscard]] bool has_template(const image_view& image);
	void initialize_game_tls(const image_view& game, const image_view& helper, void* thread_slots);
}
