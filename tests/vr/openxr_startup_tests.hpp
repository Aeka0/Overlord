#pragma once
#include "component/vr/openxr_layer_policy.hpp"
#include "component/vr/openxr_startup.hpp"

namespace openxr_startup_tests
{
	inline void selection_policy()
	{
		using namespace vr;
		using tests::require;
		const steamvr::runtime_location vd{.valid = true, .manifest_path = "vdxr.json",
		    .client_library_path = "vdxr.dll", .runtime_name = "VirtualDesktopXR"};
		const steamvr::runtime_location steam{.steamvr_manifest = true, .valid = true,
		    .manifest_path = "steamxr.json", .client_library_path = "vrclient.dll", .runtime_name = "SteamVR"};
		steamvr::openxr_metadata link;
		link.hmd_connected = true;
		link.hmd_driver = "vrlink";
		link.remote_client_id = 1;
		link.remote_client_id_error = 0;
		link.reference.name = "steamvr_device_origin";
		const auto selected = openxr::choose_startup(vd, false, link, steam);
		require(selected.runtime_manifest == steam.manifest_path &&
		            selected.preference_source == "automatic_steam_link_vrlink" &&
		            selected.controller_reference.name == link.reference.name,
		        "connected Steam Link did not select SteamVR with its controller reference");
		// Live regression: the HMD was connected with actual_driver=vrlink, but
		// SteamRemoteClientID did not supply a nonzero value. It must not veto the route.
		for (const auto property_error : {0, 4}) // success with zero; TrackedProp_UnknownProperty.
		{
			auto without_id = link;
			without_id.remote_client_id = 0;
			without_id.remote_client_id_error = property_error;
			const auto recovered = openxr::choose_startup(vd, false, without_id, steam);
			require(recovered.preference_source == "automatic_steam_link_vrlink" &&
			            recovered.runtime_manifest == steam.manifest_path &&
			            recovered.selection_diagnostic.find(std::format("remote_client_id_error={}", property_error)) != std::string::npos,
			        "optional remote client metadata blocked connected Steam Link or hid its availability");
		}
		const auto explicit_vd = openxr::choose_startup(vd, true, link, steam);
		require(explicit_vd.preferred_runtime.empty() && explicit_vd.runtime_manifest == vd.manifest_path &&
		            explicit_vd.controller_reference.target == controller_pose_reference::basis::runtime_grip,
		        "Steam Link replaced an explicit VDXR override");
		require(openxr::choose_startup(steam, false, link, steam).preferred_runtime.empty(),
		        "already configured SteamVR received an unnecessary environment override");
		for (const auto& connection : {
		         steamvr::openxr_metadata{},
		         steamvr::openxr_metadata{.hmd_connected = false, .hmd_driver = "vrlink", .remote_client_id = 1},
		         steamvr::openxr_metadata{.hmd_connected = true, .hmd_driver = "VirtualDesktop", .remote_client_id = 1},
		         steamvr::openxr_metadata{.hmd_connected = true, .hmd_driver = "lighthouse"}})
		{
			for (const auto& configured : {vd, steam})
			{
				const auto retained = openxr::choose_startup(configured, false, connection, steam);
				require(retained.preferred_runtime.empty() && retained.runtime_manifest == configured.manifest_path,
				        "absent/ambiguous connection replaced the configured runtime or forced VD into VDXR");
			}
		}
		const auto missing = openxr::choose_startup(vd, false, link, {.error = "missing installation"});
		require(missing.preferred_runtime.empty() && missing.selection_diagnostic.find("missing installation") != std::string::npos,
		        "missing SteamVR installation was selected or lost its explanation");
		const auto unavailable = openxr::choose_startup(steam, false, {}, {}, "different Windows session");
		require(unavailable.preferred_runtime.empty() && unavailable.controller_reference.error == "different Windows session",
		        "IPC preflight failure lost its controller/selection diagnostic");
	}

	inline vr::openxr::startup_configuration fixture;
	inline vr::tests::mock::get_statistics_fn statistics;
	inline unsigned queries;
	inline vr::openxr::startup_configuration query()
	{
		vr::tests::mock::statistics state;
		statistics(&state);
		vr::tests::require(state.instances_created == state.instances_destroyed,
		    "runtime selection overlapped a live OpenXR instance");
		++queries;
		return fixture;
	}
	inline std::wstring environment()
	{
		std::array<wchar_t, 32768> value{};
		const auto length = GetEnvironmentVariableW(L"XR_RUNTIME_JSON", value.data(), unsigned(value.size()));
		vr::tests::require(length < value.size(), "runtime override is too long");
		return {value.data(), length};
	}
	template <class Loader> void initialization(Loader& loader, const d3d11::device_snapshot& graphics)
	{
		using vr::tests::require;
		selection_policy();
		configure(loader, graphics, vr::tests::mock::scenario::happy);
		const auto before = environment();
		const auto base = std::filesystem::temp_directory_path() /
		    (L"h2v-startup-\u6d4b\u8bd5-" + std::to_wstring(GetCurrentProcessId()));
		const auto manifest = std::filesystem::path(base.wstring() + L".json");
		const auto library = std::filesystem::path(base.wstring() + L".dll");
		const auto cleanup = gsl::finally([&]
		{
			SetEnvironmentVariableW(L"XR_RUNTIME_JSON", before.empty() ? nullptr : before.c_str());
			std::error_code ignored;
			std::filesystem::remove(manifest, ignored);
			std::filesystem::remove(library, ignored);
			statistics = nullptr;
		});
		SetEnvironmentVariableW(L"XR_RUNTIME_JSON", nullptr);
		{ std::ofstream file(library, std::ios::binary); file << "fixture"; }
		{
			nlohmann::json data;
			data["runtime"]["library_path"] = utf8(library.filename().wstring());
			data["runtime"]["name"] = "VirtualDesktopXR";
			std::ofstream file(manifest, std::ios::binary); file << data;
		}
		const auto discovered = vr::steamvr::inspect_runtime_manifest(manifest);
		require(discovered.valid && !discovered.steamvr_manifest && discovered.runtime_name == "VirtualDesktopXR" &&
		            !discovered.client_library_path.empty(), "VDXR manifest discovery required an OpenVR factory or lost UTF-8 paths");
		statistics = loader.get_statistics;
		queries = 0;
		fixture = {.preferred_runtime = manifest, .preference_source = "automatic_steam_link_vrlink",
		    .selection_diagnostic = "test connected Steam Link"};
		vr::openxr::runtime_backend runtime{query};
		runtime.set_desired_enabled(true);
		runtime.set_scene_mode(vr::scene_mode::synthetic);
		if(vr::tests::process_integrity_level()>=SECURITY_MANDATORY_HIGH_RID)
		{
			// Hosted Windows CI is elevated. Verify the real loader policy's
			// rejection here; the remaining mock tests still run without overrides.
			require(!runtime.initialize(graphics),"high-integrity automatic runtime selection must fail closed");
			const auto status=runtime.get_status();
			require(status.state==vr::runtime_state::runtime_unavailable &&
				status.last_initialization_stage=="runtime_selection" &&
				status.last_error.find("high integrity")!=std::string::npos && environment().empty() &&
				loader.statistics().instances_created==0,
				"elevated automatic selection must reject before loading a runtime or changing the environment");
			SetEnvironmentVariableW(L"XR_RUNTIME_JSON",L"explicit-runtime.json");
			require(!runtime.initialize(graphics) && environment()==L"explicit-runtime.json" &&
				runtime.get_status().last_error.find("elevated processes")!=std::string::npos &&
				loader.statistics().instances_created==0,
				"elevated explicit selection must fail closed and preserve its original environment");
			runtime.shutdown();
			return;
		}
		const bool initialized=runtime.initialize(graphics);
		const auto status=runtime.get_status();
		require(initialized,std::format("automatic runtime initialization failed: state={} stage={} error={}",
			vr::to_string(status.state),status.last_initialization_stage,status.last_error));
		require(queries == 1 && environment().empty() && loader.statistics().instances_with_runtime_override == 1 &&
		            runtime.get_status().runtime_override_set_by_policy &&
		            runtime.get_status().runtime_override_manifest == utf8(manifest.wstring()),
		        "runtime override was not visible at instance creation, restored afterward, or reported accurately");
		fixture = {};
		require(runtime.initialize(graphics) && queries == 2 && environment().empty() &&
		            !runtime.get_status().runtime_override_active && loader.statistics().instances_with_runtime_override == 1,
		        "reinitialization reused an automatic override or skipped fresh selection");
		fixture.preferred_runtime = manifest;
		fixture.preference_source = "automatic_steam_link_vrlink";
		SetEnvironmentVariableW(L"XR_RUNTIME_JSON", L"explicit-runtime.json");
		require(runtime.initialize(graphics) && environment() == L"explicit-runtime.json" &&
		            runtime.get_status().runtime_override_source == "existing_environment" &&
		            !runtime.get_status().runtime_override_set_by_policy,
		        "automatic selection changed the explicit runtime environment");
		runtime.shutdown();
		SetEnvironmentVariableW(L"XR_RUNTIME_JSON", nullptr);
		runtime.set_desired_enabled(true);
		loader.set_scenario(vr::tests::mock::scenario::no_hmd);
		require(!runtime.initialize(graphics) && environment().empty() && runtime.get_status().state == vr::runtime_state::no_hmd &&
		            runtime.get_status().runtime_override_set_by_policy,
		        "failed initialization leaked its override or lost selection provenance");
		const auto instances = loader.statistics().instances_created;
		std::filesystem::remove(manifest);
		require(!runtime.initialize(graphics) && environment().empty() && loader.statistics().instances_created == instances &&
		            runtime.get_status().last_error.find("selected OpenXR runtime became unavailable") != std::string::npos,
		        "a vanished selected runtime silently loaded the system runtime");
		runtime.shutdown();
	}
}
