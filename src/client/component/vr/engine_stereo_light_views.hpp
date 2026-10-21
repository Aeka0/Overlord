#pragma once

#include "native_render_contract.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace vr::engine_stereo_light_views
{
	// H2 0x140751720 publishes camera-facing LIGHT_SPOT/OMNI descriptors in
	// frontend data, outside GfxViewInfo's camera-list array. Instanced XModel
	// consumers (e.g. 0x14073CD6D..90) read descriptor+0x110 instead of the
	// backend eye origin. These are lighting draws, not light-space shadow views.
	inline constexpr std::size_t descriptor_size = 0x130;
	inline constexpr std::size_t metadata_offset = 0x108;
	inline constexpr std::size_t metadata_size = sizeof(std::uintptr_t) + 3 * sizeof(float);
	using metadata = std::array<std::uint8_t, metadata_size>;
	[[nodiscard]] inline bool is_lighting_technique(const std::uint32_t technique) noexcept
	{
		return technique >= 0x25 && technique <= 0x30;
	}

	class scope
	{
	public:
		// Caller owns the completed frontend arena for the entire native eye
		// invocation. Validate/snapshot all entries before the first write. Restore
		// at eye exit (including failed admission), before another view consumes it.
		[[nodiscard]] bool install(void* descriptors, const std::uint32_t count,
			const void* natural_record, const std::array<float, 3>& natural_origin,
			const void* eye_record, const std::array<float, 3>& eye_origin) noexcept
		{
			if (active_ || !descriptors || !natural_record || !eye_record ||
				count > native_render_contract::maximum_dynamic_light_descriptors) return false;
			for (std::size_t c{}; c < 3; ++c)
				if (!std::isfinite(natural_origin[c]) || !std::isfinite(eye_origin[c])) return false;
			auto* const bytes = static_cast<std::uint8_t*>(descriptors);
			std::array<bool, native_render_contract::maximum_dynamic_light_descriptors> selected{};
			for (std::size_t i{}; i < count; ++i)
			{
				const auto* const entry = bytes + i * descriptor_size;
				std::uint32_t technique{};
				std::uintptr_t source{};
				std::memcpy(&technique, entry + 0x100, sizeof(technique));
				std::memcpy(&source, entry + metadata_offset, sizeof(source));
				// Exact scene ownership plus the native dynamic-light technique family.
				// Foreign records, inactive slots and shadow-map descriptors stay native.
				if (source != reinterpret_cast<std::uintptr_t>(natural_record) ||
					!is_lighting_technique(technique)) continue;
				if (std::memcmp(entry + 0x110, natural_origin.data(), sizeof(natural_origin)) != 0)
					return false;
				selected[i] = true;
				std::memcpy(saved_[i].data(), entry + metadata_offset, metadata_size);
			}
			const auto source = reinterpret_cast<std::uintptr_t>(eye_record);
			for (std::size_t i{}; i < count; ++i)
			{
				if (!selected[i]) continue;
				auto* const entry = bytes + i * descriptor_size;
				std::memcpy(entry + metadata_offset, &source, sizeof(source));
				std::memcpy(entry + 0x110, eye_origin.data(), sizeof(eye_origin));
			}
			bytes_ = bytes;
			selected_ = selected;
			count_ = count;
			active_ = true;
			return true;
		}

		void restore() noexcept
		{
			if (!active_) return;
			for (std::size_t i{}; i < count_; ++i)
				if (selected_[i]) std::memcpy(bytes_ + i * descriptor_size + metadata_offset,
					saved_[i].data(), metadata_size);
			bytes_ = nullptr;
			active_ = false;
		}

	private:
		std::array<metadata, native_render_contract::maximum_dynamic_light_descriptors> saved_{};
		std::array<bool, native_render_contract::maximum_dynamic_light_descriptors> selected_{};
		std::uint8_t* bytes_{};
		std::uint32_t count_{};
		bool active_{};
	};
}
