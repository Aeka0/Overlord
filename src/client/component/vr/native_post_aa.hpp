#pragma once

#include "native_post_aa_contract.hpp"
#include "native_display_contract.hpp"
#include "engine_stereo_eye_resources.hpp"
#include <string>

namespace vr::native_post_aa
{
	// Called under H2's native GPU owner lock. Append only the histories used
	// by this record's mode; binding slot 0 remains the caller's SSR resource.
	[[nodiscard]] bool append_history_bindings(const void* record,
		std::array<engine_stereo_eye_resources::source_binding,
			engine_stereo_eye_resources::isolated_target_count>& bindings) noexcept;
	// AA consumes native RGBA8 PostFX output, including its alpha channel.
	// Validate that destination before the native display transform writes it.
	[[nodiscard]] bool select_display_target(const void* record, native_display_contract::route route,
		std::uint32_t& destination) noexcept;
	[[nodiscard]] bool apply(void* record, native_display_contract::route route,
		const view_identity& view) noexcept;
	// Publish history only after the complete pair has been admitted. A failed
	// pair invalidates every view written by it, including an auxiliary view.
	void finish_pair(std::uint64_t pair, bool successful) noexcept;
	void invalidate_device(ID3D11DeviceContext* context, std::uint64_t generation) noexcept;
	std::string status_text();
}
