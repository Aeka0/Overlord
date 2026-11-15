#pragma once

#include <cstdint>
#include <json.hpp>

namespace vr::engine_stereo_owner_pass { struct transaction; }

namespace vr::diagnostics::sun_shadow
{
	// Launcher Debug selection, frozen before loading H2. No live hook installation.
	void install() noexcept;
	bool enabled() noexcept;
	bool capturing() noexcept;
	std::uint64_t request();
	void cancel(std::uint64_t request) noexcept;
	// Called on the existing support worker; only immutable CPU evidence crosses
	// this boundary. Comparisons/JSON never run on a render or runtime thread.
	void drain(std::uint64_t request, nlohmann::json& samples);
	nlohmann::json finish(std::uint64_t request);
	void stop() noexcept;

	void begin_owner(const engine_stereo_owner_pass::transaction& active) noexcept;
	void end_owner() noexcept;
	// Caller owns H2's immediate-context mutex at the completed native eye.
	void capture_eye(const engine_stereo_owner_pass::transaction& active) noexcept;
	void end_pair(std::uint64_t pair, bool complete, std::uint32_t completed_mask) noexcept;
	void geometry_view(const void* parameters, const void* primary_view,
		const void* rebase_view) noexcept;
	void invalidate_device() noexcept;
}
