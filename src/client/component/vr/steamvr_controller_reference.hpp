#pragma once
#include "controller_pose_reference.hpp"

namespace vr::steamvr
{
	struct openxr_metadata
	{
		controller_pose_reference::configuration reference;
		bool hmd_connected{};
		std::string hmd_driver;
		std::uint64_t remote_client_id{};
		std::int32_t remote_client_id_error{-1}; // -1: not queried; otherwise ETrackedPropertyError.
		[[nodiscard]] bool connected_steam_link() const noexcept
		{
			// Connection state and the literal driver identity are authoritative.
			// SteamRemoteClientID is diagnostic metadata, not a connection contract.
			return hmd_connected && hmd_driver == "vrlink";
		}
	};
	// Call only after the running server's IPC preflight. A short Background
	// connection copies connection identity and, when SteamVR is selected or a
	// Steam Link HMD is connected, static controller metadata. It closes before
	// loading OpenXR. No poses,
	// compositor calls, bindings, GPU resources or SDK handles are retained.
	openxr_metadata query_openxr_metadata(bool steamvr_selected);
}
