#pragma once

#include "component/vr/engine_stereo_light_views.hpp"
#include "test_support.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <limits>

inline void expect_dynamic_light_eye_scope()
{
	namespace light = vr::engine_stereo_light_views;
	using vr::tests::require;
	// Literal H2 descriptor ABI, independent of the adapter's offsets. The
	// native instanced path reads marker(+0x100)+0x10, while non-instanced
	// WORLD_MATRIX0 uses backend+0x2D60. Both must subtract the same current eye.
	std::array<std::uint8_t, 8 * 0x130> descriptors{};
	descriptors.fill(0x5A);
	const std::array<float, 3> center{1000.0f, -2000.0f, 35.0f};
	const std::array<float, 3> point{1010.0f, -1998.0f, 36.0f};
	std::array<std::uint8_t, 16> natural{}, right{}, auxiliary{}, foreign{};
	const auto initialize = [&](const std::size_t i, const std::uint32_t technique,
		const void* record, const std::array<float, 3>& origin)
	{
		auto* const entry = descriptors.data() + i * 0x130;
		const auto pointer = reinterpret_cast<std::uintptr_t>(record);
		std::memcpy(entry + 0x100, &technique, sizeof(technique));
		std::memcpy(entry + 0x108, &pointer, sizeof(pointer));
		std::memcpy(entry + 0x110, origin.data(), sizeof(origin));
	};
	initialize(0, 0x25, natural.data(), center); // Spotlight.
	initialize(1, 0x26, natural.data(), center); // Muzzle-flash point light.
	initialize(2, 0x28, natural.data(), center); // Shadow/cucoloris spotlight.
	initialize(3, 0x2D, natural.data(), center); // Fog variant.
	initialize(4, 0x06, natural.data(), center); // Real shadow-map draw.
	initialize(5, 0x25, foreign.data(), center);
	initialize(6, 0, natural.data(), center);
	initialize(7, 0x25, natural.data(), center); // Outside published count.
	const auto original = descriptors;
	const std::array<const void*, 3> records{natural.data(), right.data(), auxiliary.data()};
	const std::array<std::array<float, 3>, 3> origins{{
		{1000.0f, -1998.75f, 35.0f}, {1000.0f, -2001.25f, 35.0f},
		{1000.0f, -1998.75f, 35.0f}}};
	light::scope scope;
	for (std::size_t eye{}; eye < records.size(); ++eye)
	{
		require(scope.install(descriptors.data(), 7, natural.data(), center, records[eye], origins[eye]),
			"dynamic light camera metadata was not installed");
		for (std::size_t i{}; i < 4; ++i)
		{
			std::uintptr_t record{};
			std::array<float, 3> origin{};
			std::memcpy(&record, descriptors.data() + i * 0x130 + 0x108, sizeof(record));
			std::memcpy(origin.data(), descriptors.data() + i * 0x130 + 0x110, sizeof(origin));
			require(record == reinterpret_cast<std::uintptr_t>(records[eye]),
				"dynamic light retained another view's record");
			for (std::size_t c{}; c < 3; ++c)
				require(point[c] - origin[c] == point[c] - origins[eye][c],
					"instanced lighting geometry differs from current-eye base geometry");
			const auto old_error = (point[1] - center[1]) - (point[1] - origins[eye][1]);
			require(std::abs(old_error) == 1.25f && (eye == 1 ? old_error < 0 : old_error > 0),
				"fixture did not reproduce opposite half-IPD lighting offsets");
		}
		for (std::size_t b{}; b < descriptors.size(); ++b)
		{
			const auto index = b / 0x130, offset = b % 0x130;
			if (index < 4 && offset >= 0x108 && offset < 0x11C) continue;
			require(descriptors[b] == original[b], "light scope modified geometry, light identity or foreign metadata");
		}
		const auto installed = descriptors;
		require(!scope.install(descriptors.data(), 7, natural.data(), center, records[eye], origins[eye]) &&
			descriptors == installed, "active light scope was overwritten");
		scope.restore();
		require(descriptors == original, "eye exit did not restore exact natural light metadata");
		scope.restore();
		require(descriptors == original, "repeated cleanup changed light metadata");
	}

	auto bad_origin = center;
	bad_origin[0] += 10;
	initialize(3, 0x2D, natural.data(), bad_origin);
	const auto stale = descriptors;
	require(!scope.install(descriptors.data(), 7, natural.data(), center, right.data(), origins[1]) &&
		descriptors == stale, "late descriptor mismatch left an earlier light partially modified");
	descriptors = original;
	bad_origin[0] = std::numeric_limits<float>::infinity();
	require(!scope.install(descriptors.data(), 9, natural.data(), center, right.data(), origins[1]) &&
		!scope.install(descriptors.data(), 7, natural.data(), center, right.data(), bad_origin) &&
		!scope.install(nullptr, 7, natural.data(), center, right.data(), origins[1]) &&
		descriptors == original, "invalid light scope mutated native data");
	require(scope.install(descriptors.data(), 0, natural.data(), center, right.data(), origins[1]),
		"scene without dynamic lights was rejected");
	scope.restore();
	require(descriptors == original, "empty light scope changed the unpublished slots");
}
