return function()
	-- Return a registration function: the main script calls it after dofile restores
	-- its path context, so sources and dependency imports remain rooted there.
	-- Keep feature dependencies explicit next to each target.
	local function test_executable(name, output_directory)
		project(name)
		kind "ConsoleApp"
		language "C++"
		targetdir ("%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/" .. output_directory)
	end

	test_executable("vr-support-diagnostics-tests", "support-diagnostics")
		files {"./tests/vr/support_diagnostics_tests.cpp", "./src/client/component/vr/diagnostics/support_archive.cpp",
			"./src/common/utils/compression.cpp"}
		includedirs {"./tests/vr", "./src/client"}
		minizip.import()
		json.import()
		gsl.import()

	local vr_runtime_sources = {
		"./src/client/component/vr/controller_input.cpp",
		"./src/client/component/vr/controller_input.hpp",
		"./src/client/component/vr/controller_haptics.cpp",
		"./src/client/component/vr/controller_haptics.hpp",
		"./src/client/component/vr/steamvr_input.cpp",
		"./src/client/component/vr/steamvr_input_diagnostics.cpp",
		"./src/client/component/vr/steamvr_input.hpp",
		"./src/client/component/vr/openxr_dispatch.cpp",
		"./src/client/component/vr/openxr_dispatch.hpp",
		"./src/client/component/vr/openxr_layer_policy.cpp",
		"./src/client/component/vr/openxr_layer_policy.hpp",
		"./src/client/component/vr/openxr_d3d11.cpp",
		"./src/client/component/vr/openxr_d3d11.hpp",
		"./src/client/component/vr/openxr_runtime.cpp",
		"./src/client/component/vr/openxr_input.cpp",
		"./src/client/component/vr/steamvr_controller_reference.cpp",
		"./src/client/component/vr/openxr_menu.cpp",
		"./src/client/component/vr/texture_blit.cpp",
		"./src/client/component/vr/native_stereo_source.cpp",
		"./src/client/component/vr/openxr_runtime.hpp",
		"./src/client/component/vr/openvr_runtime.cpp",
		"./src/client/component/vr/openvr_runtime.hpp",
		"./src/client/component/vr/menu_overlay.cpp",
		"./src/client/component/vr/menu_backdrop.cpp",
		"./tests/vr/native_menu_stubs.cpp",
		"./src/client/component/vr/runtime_backend.cpp",
		"./src/client/component/vr/runtime.cpp",
		"./src/client/component/vr/runtime_backend.hpp",
		"./src/client/component/vr/steamvr_runtime.cpp",
		"./src/client/component/vr/steamvr_runtime.hpp",
		"./src/client/component/vr/frame_capture.cpp",
		"./src/client/component/vr/frame_capture.hpp",
		"./src/client/component/vr/scene_compositor.cpp",
		"./src/client/component/vr/scene_compositor.hpp",
		"./src/client/component/vr/engine_stereo_probe.cpp",
		"./src/client/component/vr/engine_stereo_probe.hpp",
		"./src/client/component/vr/engine_stereo_bridge.cpp",
		"./src/client/component/vr/engine_stereo_bridge.hpp",
		"./src/client/component/vr/engine_stereo_view.cpp",
		"./src/client/component/vr/engine_stereo_view.hpp",
		"./src/client/component/vr/engine_stereo_binding.cpp",
		"./src/client/component/vr/engine_stereo_binding.hpp",
		"./src/client/component/vr/engine_stereo_backend_target.cpp",
		"./src/client/component/vr/engine_stereo_backend_target.hpp",
		"./src/client/component/vr/engine_stereo_backend_view.cpp",
		"./src/client/component/vr/engine_stereo_backend_view.hpp",
		"./src/client/component/vr/native_render_session.cpp",
		"./src/client/component/vr/native_render_session.hpp",
		"./src/client/component/vr/engine_stereo_gpu_timing.cpp",
		"./src/client/component/vr/engine_stereo_gpu_timing.hpp",
		"./src/client/component/vr/head_pose_bridge.cpp",
		"./src/client/component/vr/head_pose_bridge.hpp",
		"./src/client/component/vr/vr_runtime.hpp",
	}

	local function configure_vr_smoke_executable(test_source, output_directory)
		kind "ConsoleApp"
		language "C++"
		files {test_source, "./tests/vr/**.hpp", "./tests/vr/diagnostics_stub.cpp"}
		files (vr_runtime_sources)
		includedirs {"./tests/vr", "./src/client", "./deps/openxr/include", "./deps/openvr/headers"}
		defines {"OPENVR_BUILD_STATIC"}
		json.import()
		gsl.import()
		links {"openvr_api", "d3d11", "dxgi"}
		targetdir ("%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/" .. output_directory)
	end

	group "Tests"
	test_executable("vr-aggregate-publication-tests", "aggregate-publication")
		files {"./tests/vr/aggregate_publication_tests.cpp"}
		flags {"NoPCH"}

	project "vr-weapon-catalog"
		kind "StaticLib"
		language "C++"
		files {"./src/client/component/vr/gameplay/weapon_registry.cpp",
			"./src/client/component/vr/gameplay/weapon_mechanics_profiles.cpp"}
		includedirs {"./src/client"}

	test_executable("vr-menu-surface-tests", "menu-surface")
		files {"./tests/vr/menu_surface_tests.cpp", "./src/client/component/vr/menu_surface.hpp"}
		includedirs {"./src/client"}
		flags {"NoPCH"}

	test_executable("vr-native-menu-commands-tests", "native-menu-commands")
		files {"./tests/vr/native_menu_commands_tests.cpp", "./src/client/component/vr/native_menu_commands.hpp"}
		includedirs {"./src/client"}
		flags {"NoPCH"}

	test_executable("gui-input-capture-tests", "gui-input")
		files {"./tests/gui/input_capture_tests.cpp", "./src/client/component/gui/input_capture.hpp"}
		includedirs {"./src/client"}
		flags {"NoPCH"}

	test_executable("vr-launcher-settings-tests", "launcher-settings")
		files {"./tests/vr/launcher_settings_tests.cpp", "./src/client/launcher/vr_settings_config.hpp",
			"./src/client/component/vr/settings.hpp", "./src/client/launcher/bridge_protocol.hpp"}
		includedirs {"./tests/vr", "./src/client"}
		gsl.import()
		links {"shell32"}
		json.import()

	test_executable("vr-camera-bob-tests", "camera-bob")
		files {"./tests/vr/camera_bob_tests.cpp", "./src/client/component/vr/camera_bob_bridge.hpp"}
		includedirs {"./tests/vr", "./src/client", "./src/common"}
		flags {"NoPCH"}
		gsl.import()
		asmjit.import()
		minhook.import()
		links {"common"}

	test_executable("vr-native-menu-hook-tests", "native-menu-hook")
		files {"./tests/vr/native_menu_hook_tests.cpp", "./src/client/component/vr/native_menu_hook.hpp",
			"./src/client/component/vr/native_ui_dispatch_bridge.hpp"}
		includedirs {"./tests/vr", "./src/client", "./src/common"}
		flags {"NoPCH"}
		gsl.import()
		asmjit.import()
		minhook.import()
		links {"common"}

	test_executable("vr-native-flare-tests", "native-flare")
		files {"./tests/vr/native_flare_geometry_tests.cpp", "./src/client/component/vr/native_flare_geometry.hpp",
			"./src/client/component/vr/native_flare_projection.hpp"}
		includedirs {"./src/client"}

	test_executable("vr-aim-assist-tests", "aim-assist")
		files {"./tests/vr/aim_assist_tests.cpp", "./src/client/component/vr/gameplay/aim_assist_geometry.hpp"}
		includedirs {"./src/client"}

	test_executable("vr-hand-pose-tests", "hand-pose")
		files {"./tests/vr/hand_pose_tests.cpp", "./src/client/component/vr/gameplay/hands/pose_solver.hpp"}
		includedirs {"./src/client"}

	test_executable("vr-empty-hand-pose-tests", "empty-hand-pose")
		files {"./tests/vr/empty_hand_pose_tests.cpp", "./src/client/component/vr/gameplay/empty_hand_pose.hpp"}
		includedirs {"./src/client"}

	test_executable("vr-hand-rig-tests", "hand-rig")
		files {"./tests/vr/hand_rig_tests.cpp", "./src/client/component/vr/gameplay/hands/rig_builder.hpp"}
		includedirs {"./src/client"}

	test_executable("vr-controller-input-tests", "controller-input")
		files {"./tests/vr/controller_input_tests.cpp"}
		includedirs {"./src/client", "./deps/openvr/headers"}
		json.import()

	test_executable("vr-spatial-panel-tests", "spatial-panel")
		links {"vr-weapon-catalog"}
		-- Debug retains the native snapshot fixtures and pixel buffers in main's
		-- stack frame; the default Windows stack overflows before checks can run.
		filter "system:windows"
			linkoptions {"/STACK:8388608"}
		filter {}
		files {"./tests/vr/spatial_panel_tests.cpp", "./src/client/component/vr/spatial_panel_renderer.cpp",
			"./src/client/component/vr/overlay_text_texture.cpp",
			"./src/client/component/vr/native_caption_font.cpp",
			"./src/client/component/vr/gameplay/hands/attachment_pose.cpp",
			"./src/client/component/vr/spatial_lines_renderer.cpp", "./src/client/component/vr/world_beam_renderer.cpp"}
		includedirs {"./tests/vr", "./src/client"}
		gsl.import()
		links {"d3d11", "dxgi", "d3dcompiler"}

	test_executable("vr-menu-backdrop-tests", "menu-backdrop")
		files {"./tests/vr/menu_backdrop_tests.cpp", "./src/client/component/vr/spatial_panel_renderer.cpp"}
		includedirs {"./tests/vr", "./src/client"}
		gsl.import()
		links {"d3d11", "dxgi", "d3dcompiler"}

	test_executable("vr-optic-render-tests", "optic-render")
		files {"./tests/vr/optic_render_tests.cpp", "./src/client/component/vr/auxiliary_scene.cpp",
			"./src/client/component/vr/spatial_panel_renderer.cpp"}
		includedirs {"./tests/vr", "./src/client"}
		gsl.import()
		links {"d3d11", "dxgi", "d3dcompiler"}

	test_executable("vr-melee-tests", "melee")
		files {"./tests/vr/melee_tests.cpp"}
		includedirs {"./src/client"}

	test_executable("vr-official-cheats-tests", "official-cheats")
		files {"./tests/vr/official_cheats_tests.cpp"}
		includedirs {"./src/client"}

	test_executable("vr-rigid-part-tests", "rigid-part")
		files {"./tests/vr/rigid_part_tests.cpp", "./src/client/component/scene_rigid_part.cpp",
			"./src/client/component/vr/opaque_mesh_renderer.cpp"}
		includedirs {"./tests/vr", "./src/client", "./src/common"}
		gsl.import()
		asmjit.import()
		minhook.import()
		links {"d3d11", "dxgi", "d3dcompiler", "common"}

	test_executable("vr-weapon-hud-lui-tests", "weapon-hud-lui")
		files {"./tests/vr/weapon_hud_lui_tests.cpp", "./tests/vr/weapon_hud_lui_tests.lua"}
		includedirs {"./deps/lua"}
		links {"lua"}

	test_executable("ui-script-modules-tests", "ui-script-modules")
		files {"./tests/ui_script_modules_tests.cpp", "./tests/ui_script_modules_tests.lua",
			"./src/client/component/ui_script_modules.hpp"}
		includedirs {"./src/client", "./deps/lua"}
		links {"lua"}

	test_executable("vr-killfeed-audio-tests", "killfeed-audio")
		files {"./tests/vr/killfeed_audio_tests.cpp", "./tests/vr/killfeed_audio_tests.lua"}
		includedirs {"./deps/lua"}
		links {"lua"}
		gsc_tool.import()

	test_executable("vr-region-capture-tests", "region-capture")
		files {"./tests/vr/region_capture_tests.cpp"}
		includedirs {"./src/client"}

	test_executable("vr-scene-surface-storage-tests", "scene-surface-storage")
		files {"./tests/vr/scene_surface_storage_tests.cpp"}
		includedirs {"./src/client"}
		asmjit.import()

	test_executable("vr-weapon-grip-tests", "weapon-grip")
		links {"vr-weapon-catalog"}
		files {"./tests/vr/weapon_grip_tests.cpp"}
		includedirs {"./src/client"}

	test_executable("vr-closed-bolt-tests", "closed-bolt")
		links {"vr-weapon-catalog"}
		files {"./tests/vr/closed_bolt_tests.cpp", "./src/client/component/vr/gameplay/closed_bolt.hpp",
			"./src/client/component/vr/gameplay/native_closed_bolt_policy.hpp"}
		includedirs {"./src/client"}

	test_executable("vr-weapon-mechanics-tests", "weapon-mechanics")
		files {"./tests/vr/weapon_mechanics_tests.cpp"}
		includedirs {"./src/client"}

	test_executable("vr-physical-reload-tests", "physical-reload")
		links {"vr-weapon-catalog"}
		files {"./tests/vr/physical_reload_tests.cpp"}
		includedirs {"./src/client"}

	test_executable("vr-hand-interaction-tests", "hand-interaction")
		files {"./tests/vr/hand_interaction_tests.cpp"}
		includedirs {"./src/client"}

	test_executable("vr-shared-runtime-contract-tests", "shared-runtime-contracts")
		files {"./tests/vr/shared_runtime_contract_tests.cpp"}
		includedirs {"./src/client"}

	test_executable("vr-underbarrel-tests", "underbarrel")
		files {"./tests/vr/underbarrel_tests.cpp"}
		includedirs {"./src/client"}

	test_executable("vr-quick-reload-tests", "quick-reload")
		links {"vr-weapon-catalog"}
		files {"./tests/vr/quick_reload_tests.cpp"}
		includedirs {"./src/client"}

	test_executable("vr-vehicle-tests", "vehicle")
		files {"./tests/vr/vehicle_tests.cpp"}
		includedirs {"./src/client"}

	test_executable("vr-nightvision-tests", "nightvision")
		files {"./tests/vr/nightvision_tests.cpp"}
		includedirs {"./src/client"}

	test_executable("vr-cylinder-tests", "cylinder")
		links {"vr-weapon-catalog"}
		files {"./tests/vr/cylinder_tests.cpp"}
		includedirs {"./src/client"}

	test_executable("vr-pistol-profile-tests", "pistol-profile")
		links {"vr-weapon-catalog"}
		files {"./tests/vr/pistol_profile_tests.cpp"}
		includedirs {"./src/client"}

	test_executable("vr-engine-scene-extent-tests", "scene-extent")
		files {
			"./tests/vr/engine_scene_extent_tests.cpp",
			"./src/client/component/vr/engine_scene_extent.hpp",
		}
		includedirs {"./src/client"}

	test_executable("vr-native-binding-tests", "native-bindings")
		files {"./tests/vr/native_binding_tests.cpp"}
		includedirs {"./src/client"}

	test_executable("vr-campaign-policy-tests", "campaign-policy")
		files {"./tests/vr/campaign_policy_tests.cpp"}
		includedirs {"./src/client"}

	project "vr-runtime-no-loader-smoke"
		targetname "vr-runtime-no-loader-smoke"
		configure_vr_smoke_executable("./tests/vr/no_loader_smoke.cpp", "no-loader")

	project "vr-openvr-lifecycle-tests"
		configure_vr_smoke_executable("./tests/vr/openvr_lifecycle_tests.cpp", "openvr-lifecycle")
		-- The fixture includes this implementation once to inject strict SDK doubles.
		removefiles {"./src/client/component/vr/openvr_runtime.cpp"}

	project "vr-menu-overlay-tests"
		configure_vr_smoke_executable("./tests/vr/menu_overlay_tests.cpp", "menu-overlay")

	project "vr-desktop-mirror-tests"
		targetname "vr-desktop-mirror-tests"
		configure_vr_smoke_executable("./tests/vr/desktop_mirror_tests.cpp", "desktop-mirror")

	project "vr-stabilization-tests"
		configure_vr_smoke_executable("./tests/vr/stabilization_tests.cpp", "stabilization")

	project "vr-engine-stereo-probe-smoke"
		targetname "vr-engine-stereo-probe-smoke"
		configure_vr_smoke_executable("./tests/vr/engine_probe_smoke.cpp", "probe-core")

	test_executable("vr-engine-view-probe-smoke", "view-probe")
		files {
			"./tests/vr/engine_view_probe_smoke.cpp",
			"./tests/vr/std_include.hpp",
			"./src/client/component/vr/engine_view_probe.cpp",
			"./src/client/component/vr/engine_view_probe.hpp",
		}
		includedirs {"./tests/vr", "./src/client"}
		gsl.import()

	test_executable("vr-engine-backend-probe-smoke", "backend-probe")
		files {
			"./tests/vr/engine_backend_probe_smoke.cpp",
			"./tests/vr/std_include.hpp",
			"./src/client/component/vr/engine_backend_probe.cpp",
			"./src/client/component/vr/engine_backend_probe.hpp",
		}
		includedirs {"./tests/vr", "./src/client"}
		gsl.import()

	test_executable("vr-engine-scene-batch-probe", "scene-batch")
		files {
			"./tests/vr/scene_batch_probe.cpp",
			"./tests/vr/std_include.hpp",
			"./src/client/component/vr/engine_stereo_scene_batch_probe.cpp",
			"./src/client/component/vr/engine_stereo_scene_batch_probe.hpp",
		}
		includedirs {"./tests/vr", "./src/client"}
		gsl.import()

	test_executable("vr-engine-ssr-history-probe", "ssr-history")
		files {
			"./tests/vr/ssr_history_probe.cpp",
			"./tests/vr/std_include.hpp",
			"./src/client/component/vr/engine_stereo_ssr_history_probe.cpp",
			"./src/client/component/vr/engine_stereo_ssr_history_probe.hpp",
			"./src/client/component/vr/engine_stereo_view.cpp",
			"./src/client/component/vr/engine_stereo_view.hpp",
		}
		includedirs {"./tests/vr", "./src/client"}
		gsl.import()

	test_executable("vr-engine-ssr-consumer-window-probe", "ssr-consumer-window")
		files {
			"./tests/vr/ssr_consumer_window_probe.cpp",
			"./tests/vr/std_include.hpp",
			"./src/client/component/vr/engine_stereo_dxbc_declarations.hpp",
			"./src/client/component/vr/engine_stereo_ssr_consumer_window.hpp",
		}
		includedirs {"./tests/vr", "./src/client"}
		gsl.import()

	test_executable("vr-d3d11-output-merger-probe", "output-merger")
		files {
			"./tests/vr/output_merger_probe.cpp",
			"./tests/vr/std_include.hpp",
			"./src/client/component/vr/engine_stereo_output_merger.cpp",
			"./src/client/component/vr/engine_stereo_output_merger.hpp",
			"./src/client/component/vr/engine_stereo_binding.hpp",
			"./src/client/component/vr/engine_stereo_view.hpp",
		}
		includedirs {"./tests/vr", "./src/client", "./src/common"}
		gsl.import()
		asmjit.import()
		minhook.import()
		links {"common", "d3d11", "dxgi"}

	test_executable("vr-d3d11-resource-ops-probe", "resource-ops")
		files {
			"./tests/vr/resource_ops_probe.cpp",
			"./tests/vr/std_include.hpp",
			"./src/client/component/vr/engine_stereo_resource_ops.cpp",
			"./src/client/component/vr/engine_stereo_resource_ops.hpp",
		}
		includedirs {"./tests/vr", "./src/client", "./src/common"}
		gsl.import()
		asmjit.import()
		minhook.import()
		links {"common", "d3d11", "dxgi"}

	test_executable("vr-d3d11-constant-buffer-probe", "constant-buffer")
		files {
			"./tests/vr/constant_buffer_probe.cpp",
			"./tests/vr/std_include.hpp",
			"./src/client/component/vr/engine_stereo_constant_buffer_probe.cpp",
			"./src/client/component/vr/engine_stereo_constant_buffer_probe.hpp",
			"./src/client/component/vr/engine_stereo_dynamic_upload.hpp",
			"./src/client/component/vr/engine_stereo_resource_ops.hpp",
		}
		includedirs {"./tests/vr", "./src/client", "./src/common"}
		gsl.import()
		asmjit.import()
		minhook.import()
		links {"common", "d3d11", "dxgi", "d3dcompiler"}

	test_executable("vr-d3d11-material-buffer-readback-probe", "material-buffer")
		files {
			"./tests/vr/material_buffer_readback_probe.cpp",
			"./tests/vr/std_include.hpp",
			"./src/client/component/vr/engine_stereo_material_buffer_probe.cpp",
			"./src/client/component/vr/engine_stereo_material_buffer_probe.hpp",
			"./src/client/component/vr/engine_stereo_constant_buffer_probe.cpp",
			"./src/client/component/vr/engine_stereo_constant_buffer_probe.hpp",
			"./src/client/component/vr/engine_stereo_resource_ops.cpp",
			"./src/client/component/vr/engine_stereo_resource_ops.hpp",
		}
		includedirs {"./tests/vr", "./src/client", "./src/common"}
		gsl.import()
		asmjit.import()
		minhook.import()
		links {"common", "d3d11", "dxgi", "d3dcompiler"}

	test_executable("vr-d3d11-particle-buffer-readback-probe", "particle-buffer")
		files {
			"./tests/vr/particle_buffer_readback_probe.cpp",
			"./tests/vr/std_include.hpp",
			"./src/client/component/vr/engine_stereo_particle_buffer_probe.cpp",
			"./src/client/component/vr/engine_stereo_particle_buffer_probe.hpp",
			"./src/client/component/vr/engine_stereo_resource_ops.cpp",
			"./src/client/component/vr/engine_stereo_resource_ops.hpp",
		}
		includedirs {"./tests/vr", "./src/client", "./src/common"}
		gsl.import()
		asmjit.import()
		minhook.import()
		links {"common", "d3d11", "dxgi"}

	test_executable("vr-d3d11-eye-resource-isolation-probe", "eye-resources")
		files {
			"./tests/vr/eye_resource_isolation_probe.cpp",
			"./tests/vr/std_include.hpp",
			"./src/client/component/vr/engine_stereo_eye_resources.cpp",
			"./src/client/component/vr/engine_stereo_eye_resources.hpp",
			"./src/client/component/vr/engine_stereo_output_merger.cpp",
			"./src/client/component/vr/engine_stereo_output_merger.hpp",
			"./src/client/component/vr/engine_stereo_resource_ops.cpp",
			"./src/client/component/vr/engine_stereo_resource_ops.hpp",
		}
		includedirs {"./tests/vr", "./src/client", "./src/common"}
		gsl.import()
		asmjit.import()
		minhook.import()
		links {"common", "d3d11", "dxgi"}

	test_executable("vr-d3d11-gpu-census-probe", "gpu-census")
		defines {"H2VR_EXECUTION_PROBE_TESTING", "H2VR_DYNAMIC_ARENA_TESTING"}
		files {
			"./tests/vr/gpu_census_probe.cpp",
			"./tests/vr/gpu_census_stubs.cpp",
			"./tests/vr/std_include.hpp",
			"./src/client/component/vr/engine_stereo_gpu_census.cpp",
			"./src/client/component/vr/engine_stereo_gpu_census.hpp",
			"./src/client/component/vr/engine_stereo_material_buffer_probe.cpp",
			"./src/client/component/vr/engine_stereo_material_buffer_probe.hpp",
			"./src/client/component/vr/engine_stereo_particle_buffer_probe.cpp",
			"./src/client/component/vr/engine_stereo_particle_buffer_probe.hpp",
			"./src/client/component/vr/engine_stereo_dynamic_arena.cpp",
			"./src/client/component/vr/engine_stereo_dynamic_arena.hpp",
			"./src/client/component/vr/engine_stereo_constant_buffer_probe.cpp",
			"./src/client/component/vr/engine_stereo_constant_buffer_probe.hpp",
			"./src/client/component/vr/engine_stereo_resource_ops.cpp",
			"./src/client/component/vr/engine_stereo_resource_ops.hpp",
			"./src/client/component/vr/engine_stereo_execution.cpp",
			"./src/client/component/vr/engine_stereo_execution.hpp",
			"./src/client/component/vr/engine_stereo_draw_indexed.cpp",
			"./src/client/component/vr/engine_stereo_draw_indexed.hpp",
			"./src/client/component/vr/engine_stereo_output_merger.cpp",
			"./src/client/component/vr/engine_stereo_output_merger.hpp",
			"./src/client/component/vr/engine_command_stream.cpp",
			"./src/client/component/vr/engine_command_stream.hpp",
			"./src/client/component/vr/engine_stereo_binding.hpp",
			"./src/client/component/vr/engine_stereo_view.hpp",
		}
		includedirs {"./tests/vr", "./src/client", "./src/common"}
		gsl.import()
		asmjit.import()
		minhook.import()
		links {"common", "d3d11", "dxgi", "dxguid", "d3dcompiler"}

	test_executable("vr-d3d11-draw-indexed-probe", "draw-indexed")
		files {
			"./tests/vr/draw_indexed_probe.cpp",
			"./tests/vr/std_include.hpp",
			"./src/client/component/vr/engine_stereo_draw_indexed.cpp",
			"./src/client/component/vr/engine_stereo_draw_indexed.hpp",
			"./src/client/component/vr/engine_stereo_execution.cpp",
			"./src/client/component/vr/engine_stereo_execution.hpp",
			"./src/client/component/vr/engine_stereo_output_merger.cpp",
			"./src/client/component/vr/engine_stereo_output_merger.hpp",
			"./src/client/component/vr/engine_command_stream.cpp",
			"./src/client/component/vr/engine_command_stream.hpp",
			"./src/client/component/vr/engine_stereo_binding.hpp",
		}
		includedirs {"./tests/vr", "./src/client", "./src/common"}
		gsl.import()
		asmjit.import()
		minhook.import()
		links {"common", "d3d11", "dxgi"}

	test_executable("vr-d3d11-execution-probe", "execution")
		defines {"H2VR_EXECUTION_PROBE_TESTING"}
		files {
			"./tests/vr/execution_probe.cpp",
			"./tests/vr/std_include.hpp",
			"./src/client/component/vr/engine_stereo_execution.cpp",
			"./src/client/component/vr/engine_stereo_execution.hpp",
			"./src/client/component/vr/engine_stereo_draw_indexed.cpp",
			"./src/client/component/vr/engine_stereo_draw_indexed.hpp",
			"./src/client/component/vr/engine_stereo_output_merger.cpp",
			"./src/client/component/vr/engine_stereo_output_merger.hpp",
			"./src/client/component/vr/engine_command_stream.cpp",
			"./src/client/component/vr/engine_command_stream.hpp",
			"./src/client/component/vr/engine_stereo_binding.hpp",
			"./src/client/component/vr/engine_stereo_view.hpp",
		}
		includedirs {"./tests/vr", "./src/client", "./src/common"}
		gsl.import()
		asmjit.import()
		minhook.import()
		links {"common", "d3d11", "dxgi"}

	test_executable("vr-d3d11-gpu-timing-probe", "gpu-timing")
		defines {"H2VR_GPU_TIMING_TESTING"}
		files {
			"./tests/vr/gpu_timing_probe.cpp",
			"./tests/vr/std_include.hpp",
			"./src/client/component/vr/engine_stereo_gpu_timing.cpp",
			"./src/client/component/vr/engine_stereo_gpu_timing.hpp",
		}
		includedirs {"./tests/vr", "./src/client"}
		gsl.import()
		links {"d3d11", "dxgi"}


	project "vr-mock-openxr-loader"
		kind "SharedLib"
		language "C++"
		targetname "openxr_loader"
		files {"./tests/vr/mock_loader.cpp", "./tests/vr/mock_control.hpp", "./tests/vr/std_include.hpp"}
		includedirs {"./tests/vr", "./deps/openxr/include"}
		gsl.import()
		links {"d3d11", "dxgi"}
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/mock"

	project "vr-runtime-mock-smoke"
		targetname "vr-runtime-mock-smoke"
		configure_vr_smoke_executable("./tests/vr/mock_smoke.cpp", "mock")
		dependson {"vr-mock-openxr-loader"}

	project "vr-runtime-real-loader-probe"
		targetname "vr-runtime-real-loader-probe"
		configure_vr_smoke_executable("./tests/vr/real_loader_probe.cpp", "probe")

	project "vr-steamvr-hardware-probe"
		targetname "vr-steamvr-hardware-probe"
		configure_vr_smoke_executable("./tests/vr/steamvr_hardware_probe.cpp", "steamvr-hardware")


end
