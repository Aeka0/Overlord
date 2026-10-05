#include "component/vr/engine_scene_extent.hpp"

#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>

namespace
{
	void require(const bool condition, const char* const message)
	{
		if (!condition)
		{
			std::cerr << "FAIL: " << message << '\n';
			std::exit(1);
		}
	}
}

int main()
{
	using namespace vr::engine_scene_resolution;
	// Offsets are literal independent observations of H2's calculation/store
	// instructions, not derived from the production offsetof declarations.
	std::array<std::uint8_t, 0x48> original{};
	for (std::size_t i{}; i < original.size(); ++i) original[i] = static_cast<std::uint8_t>(i + 1);
	const auto put = [&](const std::size_t offset, const std::uint32_t value)
	{
		std::memcpy(original.data() + offset, &value, sizeof(value));
	};
	put(0x20, 1280); put(0x24, 680); put(0x28, 1280); put(0x2C, 680);
	put(0x30, 2560); put(0x34, 1361); put(0x44, 1);
	window_parameters parameters{};
	std::memcpy(&parameters, original.data(), original.size());
	require(apply_scene_extent(parameters, {2528, 2704}), "apply headset extent");
	const auto* changed = reinterpret_cast<const std::uint8_t*>(&parameters);
	for (std::size_t i{}; i < original.size(); ++i)
		if (i < 0x20 || i >= 0x30) require(changed[i] == original[i], "only four scene fields may change");
	for (const std::size_t offset : {0x20u, 0x28u})
	{
		std::uint32_t width{}, height{};
		std::memcpy(&width, changed + offset, 4);
		std::memcpy(&height, changed + offset + 4, 4);
		require(width == 2528 && height == 2704, "raster and base scene extents match runtime");
	}
	require(apply_scene_extent(parameters, {2528, 2704}), "idempotent apply");
	const auto accepted = parameters;
	for (const auto invalid : std::array<extent, 4>{{{0, 2704}, {2528, 0}, {16385, 2704},
		{2528, (std::numeric_limits<std::uint32_t>::max)()}}})
	{
		require(!apply_scene_extent(parameters, invalid), "invalid recommendations must not be clamped");
		require(std::memcmp(&parameters, &accepted, sizeof(parameters)) == 0, "rejected request leaves parameters unchanged");
	}
	for (const auto count : {0u, 2u, 4u, 8u, 16u})
	{
		parameters = accepted;
		parameters.supersample_count = count;
		const auto before = parameters;
		require(!apply_scene_extent(parameters, {2528, 2704}), "no silent H2 SSAA override");
		require(std::memcmp(&parameters, &before, sizeof(parameters)) == 0, "unsupported SSAA is not modified");
	}
	parameters = accepted;
	require(apply_scene_extent(parameters, {16384, 16384}), "exact D3D11 maximum");
	video_config config{};
	config.scene = config.scene_base = {2528, 2704};
	config.display = {2560, 1361};
	require(matches_scene(config, {2528, 2704}), "scene contract independent of desktop");
	config.scene_base = config.display;
	require(!matches_scene(config, {2528, 2704}), "native color with flat-sized scene base is incomplete");
	std::cout << "PASS: native scene extent CPU/ABI field contract; not an H2 GPU or HMD visual acceptance test\n";
}
