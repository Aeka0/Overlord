#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "component/d3d11.hpp"

namespace vr
{
	class IVRSystem;
}

namespace vr::steamvr
{
	// OpenVR reads VR_PATHREG_OVERRIDE before VR_Init. This scope creates a
	// process-local copy of the user's path registry with a writable log path,
	// without modifying SteamVR's configuration on disk.
	class path_registry_scope final
	{
	public:
		explicit path_registry_scope(std::string& diagnostic);
		~path_registry_scope() noexcept;

		path_registry_scope(const path_registry_scope&) = delete;
		path_registry_scope& operator=(const path_registry_scope&) = delete;

		[[nodiscard]] bool active() const noexcept { return active_; }

	private:
		bool active_{};
		bool had_previous_{};
		std::wstring previous_value_{};
		std::filesystem::path registry_path_{};
		std::filesystem::path temporary_directory_{};
	};

	// Performs a non-invasive preflight of the SteamVR server IPC endpoint. This
	// avoids entering OpenVR's blocking VR_Init path when the caller is running
	// under a different Windows identity/session than the active vrserver.
	[[nodiscard]] std::string diagnose_ipc_environment();

	// Call after VR_Init and before initializing input. Registers the packaged
	// application identity/artwork and associates it with this process. An error
	// is diagnostic only; missing presentation assets must not disable VR.
	[[nodiscard]] std::string register_application();

	struct runtime_location
	{
		bool steamvr_manifest{};
		bool valid{};
		std::string manifest_path;
		std::string client_library_path;
		std::string runtime_name;
		std::string error;
	};

	// Resolves the active OpenXR manifest for selection and diagnostics. Vendor
	// manifests remain eligible without SteamVR; SteamVR additionally needs its
	// client factory for the short metadata connection.
	[[nodiscard]] runtime_location locate_active_runtime();
	// Resolves the installation selected by OpenVR's path registry, independently
	// of the system's active OpenXR provider. Does not start SteamVR.
	[[nodiscard]] runtime_location locate_installed_runtime();
	[[nodiscard]] runtime_location inspect_runtime_manifest(const std::filesystem::path& manifest);

	// Validates the DXGI 1.1 factory lineage required by OpenVR texture sharing
	// and that H2's D3D11 device and SteamVR select the same physical adapter.
	[[nodiscard]] bool validate_compositor_adapter(IVRSystem* system,
		ID3D11Device* game_device, std::string& error);
}
