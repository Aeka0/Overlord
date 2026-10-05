#pragma once

namespace vr::diagnostics
{
	// Loader-time evidence only. Call after target validation and before installing
	// VR observation patches; this performs bounded file I/O, never GPU work.
	[[nodiscard]] bool write_runtime_code_snapshots() noexcept;
}
