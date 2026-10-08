#pragma once

#include "steamvr_controller_reference.hpp"
#include "steamvr_runtime.hpp"

#include <filesystem>
#include <format>
#include <string>

namespace vr::openxr
{
	struct startup_configuration
	{
		controller_pose_reference::configuration controller_reference;
		controller_pose_pipeline::mode pose_pipeline{controller_pose_pipeline::selected()};
		std::filesystem::path preferred_runtime;
		std::string preference_source;
		std::string selection_diagnostic;
		std::string runtime_manifest;
		std::string runtime_library;
	};

	// Policy consumes one fresh observation at initialization. Neither an installed
	// runtime nor a running streamer proves which route the user intends to use.
	// VD can feed both VDXR and SteamVR, so retain its explicit/system choice.
	inline startup_configuration choose_startup(const steamvr::runtime_location& configured,
	    bool explicit_runtime, const steamvr::openxr_metadata& connection,
	    const steamvr::runtime_location& installed_steamvr, const std::string& probe_error = {})
	{
		startup_configuration result;
		result.runtime_manifest = configured.manifest_path;
		result.runtime_library = configured.client_library_path;
		const bool steamvr_selected = configured.steamvr_manifest && configured.valid;
		result.selection_diagnostic = explicit_runtime ? "explicit XR_RUNTIME_JSON" : "system active runtime";
		if (!configured.runtime_name.empty())
			result.selection_diagnostic += "; configured_provider=" + configured.runtime_name;
		if (!configured.error.empty())
			result.selection_diagnostic += "; manifest_warning=" + configured.error;
		if (!probe_error.empty())
		{
			result.selection_diagnostic += "; connection_probe=" + probe_error;
			if (steamvr_selected)
			{
				auto& reference = result.controller_reference;
				reference.target = controller_pose_reference::basis::calibration_frame;
				reference.expected_runtime = "SteamVR/OpenXR";
				reference.name = "steamvr_device_origin";
				reference.error = probe_error;
			}
			return result;
		}
		result.selection_diagnostic += std::format(
		    "; hmd_connected={} actual_driver={} remote_client_id_available={} "
		    "remote_client_id_error={} remote_client_id_nonzero={}",
		    connection.hmd_connected, connection.hmd_driver.empty() ? "unavailable" : connection.hmd_driver,
		    connection.remote_client_id_error == 0, connection.remote_client_id_error,
		    connection.remote_client_id != 0);
		if (steamvr_selected)
			result.controller_reference = connection.reference;
		if (!explicit_runtime && !steamvr_selected && connection.connected_steam_link())
		{
			if (installed_steamvr.valid && installed_steamvr.steamvr_manifest)
			{
				result.preferred_runtime = std::filesystem::path(std::u8string(
				    reinterpret_cast<const char8_t*>(installed_steamvr.manifest_path.data()),
				    installed_steamvr.manifest_path.size()));
				result.preference_source = "automatic_steam_link_vrlink";
				result.runtime_manifest = installed_steamvr.manifest_path;
				result.runtime_library = installed_steamvr.client_library_path;
				result.controller_reference = connection.reference;
				result.selection_diagnostic += "; selected SteamVR/OpenXR for connected Steam Link";
			}
			else
				result.selection_diagnostic += "; configured runtime retained: " + installed_steamvr.error;
		}
		else
			result.selection_diagnostic += "; configured runtime retained";
		return result;
	}
}
