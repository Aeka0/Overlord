-- Quote the given string input as a C string
function cstrquote(value)
	if value == nil then
		return "\"\""
	end
	result = value:gsub("\\", "\\\\")
	result = result:gsub("\"", "\\\"")
	result = result:gsub("\n", "\\n")
	result = result:gsub("\t", "\\t")
	result = result:gsub("\r", "\\r")
	result = result:gsub("\a", "\\a")
	result = result:gsub("\b", "\\b")
	result = "\"" .. result .. "\""
	return result
end

dependencies = {
	basePath = "./deps"
}

function dependencies.load()
	dir = path.join(dependencies.basePath, "premake/*.lua")
	deps = os.matchfiles(dir)

	for i, dep in pairs(deps) do
		dep = dep:gsub(".lua", "")
		require(dep)
	end
end

function dependencies.imports()
	for i, proj in pairs(dependencies) do
		if type(i) == 'number' then
			proj.import()
		end
	end
end

function dependencies.projects()
	for i, proj in pairs(dependencies) do
		if type(i) == 'number' then
			proj.project()
		end
	end
end

newoption {
	trigger = "copy-to",
	description = "Optional, copy the EXE/PDB and VR resources to a custom folder after each build.",
	value = "PATH"
}

newoption {
	trigger = "dev-build",
	description = "Enable development builds of the client."
}

newoption {
	trigger = "with-vr-tests",
	description = "Generate the no-loader and mock-runtime VR smoke-test targets."
}

dofile(path.join(_MAIN_SCRIPT_DIR, "tools/premake_version.lua"))

dependencies.load()

workspace "h2-mod"
startproject "client"
location "./build"
objdir "%{wks.location}/obj"
targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}"

configurations {"Debug", "RelWithDebInfo", "Release"}

language "C++"
cppdialect "C++latest"

architecture "x86_64"
platforms "x64"
includedirs {"./src/common"}

systemversion "latest"
symbols "On"
staticruntime "On"
editandcontinue "Off"
warnings "Extra"
characterset "ASCII"

-- Keep Win32 A/W API selection separate from source/execution encoding.
-- Explicit UTF-8 also covers PCHs and test targets, regardless of host code page.
filter "action:vs*"
	buildoptions {"/utf-8"}
filter {}

if _OPTIONS["dev-build"] then
	defines {"DEV_BUILD"}
end

if os.getenv("CI") then
	defines {"CI"}
end

flags {"NoIncrementalLink", "NoMinimalRebuild", "MultiProcessorCompile", "No64BitChecks"}

filter "platforms:x64"
	defines {"_WINDOWS", "WIN32"}
filter {}

filter "configurations:Release"
	optimize "Size"
	buildoptions {"/GL"}
	linkoptions {"/IGNORE:4702", "/LTCG"}
	defines {"NDEBUG"}
	flags {"FatalCompileWarnings"}
filter {}

filter "configurations:Debug"
	optimize "Debug"
	buildoptions {"/bigobj"}
	defines {"DEBUG", "_DEBUG"}
filter {}

-- Use this configuration for gameplay/performance diagnosis. Retain full PDBs
-- without per-function debugger checks or the Debug CRT on render hot paths.
-- NoRuntimeChecks is the API supported by the bundled Premake 5 beta2.
filter "configurations:RelWithDebInfo"
	optimize "Speed"
	runtime "Release"
	flags {"NoRuntimeChecks"}
	-- beta2 treats justmycode as project-scoped; keep Debug's default by
	-- specifying its compiler switch only for this optimized configuration.
	buildoptions {"/bigobj", "/JMC-"}
	defines {"NDEBUG", "H2VR_PROFILE_BUILD"}
filter {}

project "common"
kind "StaticLib"
language "C++"

files {"./src/common/**.hpp", "./src/common/**.cpp"}

includedirs {"./src/common", "%{prj.location}/src"}

resincludedirs {"$(ProjectDir)src"}

dependencies.imports()

project "client"
kind "ConsoleApp"
language "C++"

targetname "h2-mod-vr"
filter "configurations:Debug"
	targetsuffix "-debug"
filter {}

pchheader "std_include.hpp"
pchsource "src/client/std_include.cpp"

-- The concrete asset catalog is the only TU that expands all weapon recipes.
-- Keep its MSVC 14.29 PCH workaround local; ordinary consumers use declarations.
for _, source in ipairs({"weapon_registry", "weapon_mechanics_profiles"}) do
	filter ("files:src/client/component/vr/gameplay/" .. source .. ".cpp")
	flags {"NoPCH"}
end
filter {}

linkoptions {"/IGNORE:4254", "/DYNAMICBASE:NO", "/SAFESEH:NO", "/LARGEADDRESSAWARE", "/LAST:.main", "/PDBCompress"}

files {"./src/client/**.rc", "./src/client/**.hpp", "./src/client/**.cpp", "./src/client/resources/**.*"}

includedirs {"./src/client", "./src/common", "%{prj.location}/src"}

resincludedirs {"$(ProjectDir)src"}

dependson {"tlsdll"}

-- Keep the Unicode CASC API isolated from the legacy game's ANSI PCH.
filter "files:src/client/launcher/game_language.cpp"
    flags {"NoPCH"}
    defines {"UNICODE", "_UNICODE"}
filter {}

links {"common"}

prebuildcommands {"pushd %{_MAIN_SCRIPT_DIR}", "tools\\premake5 generate-buildinfo", "popd"}

local function copy_client_resources(destination)
	postbuildcommands {
		'{COPYDIR} "%{wks.location}/../data/vr_input" "' .. path.join(destination, "vr_input") .. '"',
		'{COPYDIR} "%{wks.location}/../data/ui_scripts/vr_gameplay" "' .. path.join(destination, "h2-mod/ui_scripts/vr_gameplay") .. '"',
		'{MKDIR} "' .. path.join(destination, "steamvr") .. '"',
		'{COPYFILE} "%{wks.location}/../assets/steamvr/cover.png" "' .. path.join(destination, "steamvr/cover.png") .. '"',
		'{COPYFILE} "%{wks.location}/../assets/steamvr/cover-small.png" "' .. path.join(destination, "steamvr/cover-small.png") .. '"',
		'{COPYFILE} "%{wks.location}/../assets/steamvr/cover-capsule.png" "' .. path.join(destination, "steamvr/cover-capsule.png") .. '"',
		'{COPYFILE} "%{wks.location}/../assets/steamvr/%{cfg.buildtarget.basename}.vrmanifest" "' .. path.join(destination, "%{cfg.buildtarget.basename}.vrmanifest") .. '"',
	}
end

copy_client_resources("%{cfg.targetdir}")

if _OPTIONS["copy-to"] then
	postbuildcommands {"copy /y \"$(TargetPath)\" \"" .. _OPTIONS["copy-to"] .. "\""}
	postbuildcommands {"copy /y \"$(TargetDir)$(TargetName).pdb\" \"" .. _OPTIONS["copy-to"] .. "\""}
	copy_client_resources(_OPTIONS["copy-to"])
end

dependencies.imports()

project "tlsdll"
kind "SharedLib"
language "C++"

files {"./src/tlsdll/**.rc", "./src/tlsdll/**.hpp", "./src/tlsdll/**.cpp", "./src/tlsdll/resources/**.*"}

includedirs {"./src/tlsdll", "%{prj.location}/src"}

links {"common"}

resincludedirs {"$(ProjectDir)src"}

if _OPTIONS["with-vr-tests"] then
	local vr_runtime_sources = {
		"./src/client/component/vr/controller_input.cpp",
		"./src/client/component/vr/controller_input.hpp",
		"./src/client/component/vr/controller_haptics.cpp",
		"./src/client/component/vr/controller_haptics.hpp",
		"./src/client/component/vr/steamvr_input.cpp",
		"./src/client/component/vr/steamvr_input.hpp",
		"./src/client/component/vr/openxr_dispatch.cpp",
		"./src/client/component/vr/openxr_dispatch.hpp",
		"./src/client/component/vr/openxr_layer_policy.cpp",
		"./src/client/component/vr/openxr_layer_policy.hpp",
		"./src/client/component/vr/openxr_d3d11.cpp",
		"./src/client/component/vr/openxr_d3d11.hpp",
		"./src/client/component/vr/openxr_runtime.cpp",
		"./src/client/component/vr/openxr_runtime.hpp",
		"./src/client/component/vr/openvr_runtime.cpp",
		"./src/client/component/vr/openvr_runtime.hpp",
		"./src/client/component/vr/menu_overlay.cpp",
		"./tests/vr/native_menu_stubs.cpp",
		"./src/client/component/vr/runtime_backend.cpp",
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
	project "vr-weapon-catalog"
		kind "StaticLib"
		language "C++"
		files {"./src/client/component/vr/gameplay/weapon_registry.cpp",
			"./src/client/component/vr/gameplay/weapon_mechanics_profiles.cpp"}
		includedirs {"./src/client"}

	project "vr-menu-surface-tests"
		kind "ConsoleApp"
		language "C++"
		files {"./tests/vr/menu_surface_tests.cpp", "./src/client/component/vr/menu_surface.hpp"}
		includedirs {"./src/client"}
		flags {"NoPCH"}
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/menu-surface"

	project "vr-native-menu-commands-tests"
		kind "ConsoleApp"
		language "C++"
		files {"./tests/vr/native_menu_commands_tests.cpp", "./src/client/component/vr/native_menu_commands.hpp"}
		includedirs {"./src/client"}
		flags {"NoPCH"}
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/native-menu-commands"

	project "gui-input-capture-tests"
		kind "ConsoleApp"
		language "C++"
		files {"./tests/gui/input_capture_tests.cpp", "./src/client/component/gui/input_capture.hpp"}
		includedirs {"./src/client"}
		flags {"NoPCH"}
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/gui-input"

	project "vr-launcher-settings-tests"
		kind "ConsoleApp"
		language "C++"
		files {"./tests/vr/launcher_settings_tests.cpp", "./src/client/launcher/vr_settings_config.hpp",
			"./src/client/component/vr/settings.hpp", "./src/client/launcher/html/html_argument.cpp"}
		includedirs {"./tests/vr", "./src/client"}
		gsl.import()
		links {"oleaut32"}
		json.import()
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/launcher-settings"

	project "vr-camera-bob-tests"
		kind "ConsoleApp"
		language "C++"
		files {"./tests/vr/camera_bob_tests.cpp", "./src/client/component/vr/camera_bob_bridge.hpp"}
		includedirs {"./tests/vr", "./src/client", "./src/common"}
		flags {"NoPCH"}
		gsl.import()
		asmjit.import()
		minhook.import()
		links {"common"}
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/camera-bob"

	project "vr-native-menu-hook-tests"
		kind "ConsoleApp"
		language "C++"
		files {"./tests/vr/native_menu_hook_tests.cpp", "./src/client/component/vr/native_menu_hook.hpp",
			"./src/client/component/vr/native_ui_dispatch_bridge.hpp"}
		includedirs {"./tests/vr", "./src/client", "./src/common"}
		flags {"NoPCH"}
		gsl.import()
		asmjit.import()
		minhook.import()
		links {"common"}
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/native-menu-hook"

	project "vr-native-flare-tests"
		kind "ConsoleApp"
		language "C++"
		files {"./tests/vr/native_flare_geometry_tests.cpp", "./src/client/component/vr/native_flare_geometry.hpp",
			"./src/client/component/vr/native_flare_projection.hpp"}
		includedirs {"./src/client"}
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/native-flare"

	project "vr-aim-assist-tests"
		kind "ConsoleApp"
		language "C++"
		files {"./tests/vr/aim_assist_tests.cpp", "./src/client/component/vr/gameplay/aim_assist_geometry.hpp"}
		includedirs {"./src/client"}
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/aim-assist"

	project "vr-hand-pose-tests"
		kind "ConsoleApp"
		language "C++"
		files {"./tests/vr/hand_pose_tests.cpp", "./src/client/component/vr/gameplay/hand_pose_solver.hpp"}
		includedirs {"./src/client"}
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/hand-pose"

	project "vr-empty-hand-pose-tests"
		kind "ConsoleApp"
		language "C++"
		files {"./tests/vr/empty_hand_pose_tests.cpp", "./src/client/component/vr/gameplay/empty_hand_pose.hpp"}
		includedirs {"./src/client"}
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/empty-hand-pose"

	project "vr-hand-rig-tests"
		kind "ConsoleApp"
		language "C++"
		files {"./tests/vr/hand_rig_tests.cpp", "./src/client/component/vr/gameplay/hand_rig_builder.hpp"}
		includedirs {"./src/client"}
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/hand-rig"

	project "vr-controller-input-tests"
		kind "ConsoleApp"
		language "C++"
		files {"./tests/vr/controller_input_tests.cpp"}
		includedirs {"./src/client"}
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/controller-input"

	project "vr-spatial-panel-tests"
		links {"vr-weapon-catalog"}
		kind "ConsoleApp"
		language "C++"
		files {"./tests/vr/spatial_panel_tests.cpp", "./src/client/component/vr/spatial_panel_renderer.cpp",
			"./src/client/component/vr/overlay_text_texture.cpp",
			"./src/client/component/vr/native_caption_font.cpp",
			"./src/client/component/vr/gameplay/hand_attachment_pose.cpp",
			"./src/client/component/vr/spatial_lines_renderer.cpp", "./src/client/component/vr/world_beam_renderer.cpp"}
		includedirs {"./tests/vr", "./src/client"}
		gsl.import()
		links {"d3d11", "dxgi", "d3dcompiler"}
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/spatial-panel"

	project "vr-menu-backdrop-tests"
		kind "ConsoleApp"
		language "C++"
		files {"./tests/vr/menu_backdrop_tests.cpp", "./src/client/component/vr/spatial_panel_renderer.cpp"}
		includedirs {"./tests/vr", "./src/client"}
		gsl.import()
		links {"d3d11", "dxgi", "d3dcompiler"}
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/menu-backdrop"

	project "vr-optic-render-tests"
		kind "ConsoleApp"
		language "C++"
		files {"./tests/vr/optic_render_tests.cpp", "./src/client/component/vr/auxiliary_scene.cpp",
			"./src/client/component/vr/spatial_panel_renderer.cpp"}
		includedirs {"./tests/vr", "./src/client"}
		gsl.import()
		links {"d3d11", "dxgi", "d3dcompiler"}
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/optic-render"

	project "vr-melee-tests"
		kind "ConsoleApp"
		language "C++"
		files {"./tests/vr/melee_tests.cpp"}
		includedirs {"./src/client"}
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/melee"

	project "vr-official-cheats-tests"
		kind "ConsoleApp"
		language "C++"
		files {"./tests/vr/official_cheats_tests.cpp"}
		includedirs {"./src/client"}
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/official-cheats"

	project "vr-rigid-part-tests"
		kind "ConsoleApp"
		language "C++"
		files {"./tests/vr/rigid_part_tests.cpp", "./src/client/component/scene_rigid_part.cpp",
			"./src/client/component/vr/opaque_mesh_renderer.cpp"}
		includedirs {"./tests/vr", "./src/client", "./src/common"}
		gsl.import()
		asmjit.import()
		minhook.import()
		links {"d3d11", "dxgi", "d3dcompiler", "common"}
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/rigid-part"

	project "vr-weapon-hud-lui-tests"
		kind "ConsoleApp"
		language "C++"
		files {"./tests/vr/weapon_hud_lui_tests.cpp", "./tests/vr/weapon_hud_lui_tests.lua"}
		includedirs {"./deps/lua"}
		links {"lua"}
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/weapon-hud-lui"

	project "ui-script-modules-tests"
		kind "ConsoleApp"
		language "C++"
		files {"./tests/ui_script_modules_tests.cpp", "./tests/ui_script_modules_tests.lua",
			"./src/client/component/ui_script_modules.hpp"}
		includedirs {"./src/client", "./deps/lua"}
		links {"lua"}
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/ui-script-modules"

	project "vr-region-capture-tests"
		kind "ConsoleApp"
		language "C++"
		files {"./tests/vr/region_capture_tests.cpp"}
		includedirs {"./src/client"}
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/region-capture"

	project "vr-scene-surface-storage-tests"
		kind "ConsoleApp"
		language "C++"
		files {"./tests/vr/scene_surface_storage_tests.cpp"}
		includedirs {"./src/client"}
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/scene-surface-storage"
		asmjit.import()

	project "vr-weapon-grip-tests"
		links {"vr-weapon-catalog"}
		kind "ConsoleApp"
		language "C++"
		files {"./tests/vr/weapon_grip_tests.cpp"}
		includedirs {"./src/client"}
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/weapon-grip"

	project "vr-closed-bolt-tests"
		links {"vr-weapon-catalog"}
		kind "ConsoleApp"
		language "C++"
		files {"./tests/vr/closed_bolt_tests.cpp", "./src/client/component/vr/gameplay/closed_bolt.hpp",
			"./src/client/component/vr/gameplay/native_closed_bolt_policy.hpp"}
		includedirs {"./src/client"}
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/closed-bolt"

	project "vr-weapon-mechanics-tests"
		kind "ConsoleApp"
		language "C++"
		files {"./tests/vr/weapon_mechanics_tests.cpp"}
		includedirs {"./src/client"}
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/weapon-mechanics"

	project "vr-physical-reload-tests"
		links {"vr-weapon-catalog"}
		kind "ConsoleApp"
		language "C++"
		files {"./tests/vr/physical_reload_tests.cpp"}
		includedirs {"./src/client"}
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/physical-reload"

	project "vr-hand-interaction-tests"
		kind "ConsoleApp"
		language "C++"
		files {"./tests/vr/hand_interaction_tests.cpp"}
		includedirs {"./src/client"}
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/hand-interaction"

	project "vr-shared-runtime-contract-tests"
		kind "ConsoleApp"
		language "C++"
		files {"./tests/vr/shared_runtime_contract_tests.cpp"}
		includedirs {"./src/client"}
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/shared-runtime-contracts"

	project "vr-underbarrel-tests"
		kind "ConsoleApp"
		language "C++"
		files {"./tests/vr/underbarrel_tests.cpp"}
		includedirs {"./src/client"}
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/underbarrel"

	project "vr-quick-reload-tests"
		links {"vr-weapon-catalog"}
		kind "ConsoleApp"
		language "C++"
		files {"./tests/vr/quick_reload_tests.cpp"}
		includedirs {"./src/client"}
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/quick-reload"

	project "vr-vehicle-tests"
		kind "ConsoleApp"
		language "C++"
		files {"./tests/vr/vehicle_tests.cpp"}
		includedirs {"./src/client"}
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/vehicle"

	project "vr-nightvision-tests"
		kind "ConsoleApp"
		language "C++"
		files {"./tests/vr/nightvision_tests.cpp"}
		includedirs {"./src/client"}
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/nightvision"

	project "vr-cylinder-tests"
		links {"vr-weapon-catalog"}
		kind "ConsoleApp"
		language "C++"
		files {"./tests/vr/cylinder_tests.cpp"}
		includedirs {"./src/client"}
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/cylinder"

	project "vr-pistol-profile-tests"
		links {"vr-weapon-catalog"}
		kind "ConsoleApp"
		language "C++"
		files {"./tests/vr/pistol_profile_tests.cpp"}
		includedirs {"./src/client"}
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/pistol-profile"

	project "vr-engine-scene-extent-tests"
		kind "ConsoleApp"
		language "C++"
		files {
			"./tests/vr/engine_scene_extent_tests.cpp",
			"./src/client/component/vr/engine_scene_extent.hpp",
		}
		includedirs {"./src/client"}
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/scene-extent"

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

	project "vr-engine-view-probe-smoke"
		kind "ConsoleApp"
		language "C++"
		targetname "vr-engine-view-probe-smoke"
		files {
			"./tests/vr/engine_view_probe_smoke.cpp",
			"./tests/vr/std_include.hpp",
			"./src/client/component/vr/engine_view_probe.cpp",
			"./src/client/component/vr/engine_view_probe.hpp",
		}
		includedirs {"./tests/vr", "./src/client"}
		gsl.import()
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/view-probe"

	project "vr-engine-backend-probe-smoke"
		kind "ConsoleApp"
		language "C++"
		targetname "vr-engine-backend-probe-smoke"
		files {
			"./tests/vr/engine_backend_probe_smoke.cpp",
			"./tests/vr/std_include.hpp",
			"./src/client/component/vr/engine_backend_probe.cpp",
			"./src/client/component/vr/engine_backend_probe.hpp",
		}
		includedirs {"./tests/vr", "./src/client"}
		gsl.import()
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/backend-probe"

	project "vr-engine-scene-batch-probe"
		kind "ConsoleApp"
		language "C++"
		targetname "vr-engine-scene-batch-probe"
		files {
			"./tests/vr/scene_batch_probe.cpp",
			"./tests/vr/std_include.hpp",
			"./src/client/component/vr/engine_stereo_scene_batch_probe.cpp",
			"./src/client/component/vr/engine_stereo_scene_batch_probe.hpp",
		}
		includedirs {"./tests/vr", "./src/client"}
		gsl.import()
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/scene-batch"

	project "vr-engine-ssr-history-probe"
		kind "ConsoleApp"
		language "C++"
		targetname "vr-engine-ssr-history-probe"
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
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/ssr-history"

	project "vr-engine-ssr-consumer-window-probe"
		kind "ConsoleApp"
		language "C++"
		targetname "vr-engine-ssr-consumer-window-probe"
		files {
			"./tests/vr/ssr_consumer_window_probe.cpp",
			"./tests/vr/std_include.hpp",
			"./src/client/component/vr/engine_stereo_dxbc_declarations.hpp",
			"./src/client/component/vr/engine_stereo_ssr_consumer_window.hpp",
		}
		includedirs {"./tests/vr", "./src/client"}
		gsl.import()
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/ssr-consumer-window"

	project "vr-d3d11-output-merger-probe"
		kind "ConsoleApp"
		language "C++"
		targetname "vr-d3d11-output-merger-probe"
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
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/output-merger"

	project "vr-d3d11-resource-ops-probe"
		kind "ConsoleApp"
		language "C++"
		targetname "vr-d3d11-resource-ops-probe"
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
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/resource-ops"

	project "vr-d3d11-constant-buffer-probe"
		kind "ConsoleApp"
		language "C++"
		targetname "vr-d3d11-constant-buffer-probe"
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
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/constant-buffer"

	project "vr-d3d11-material-buffer-readback-probe"
		kind "ConsoleApp"
		language "C++"
		targetname "vr-d3d11-material-buffer-readback-probe"
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
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/material-buffer"

	project "vr-d3d11-particle-buffer-readback-probe"
		kind "ConsoleApp"
		language "C++"
		targetname "vr-d3d11-particle-buffer-readback-probe"
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
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/particle-buffer"

	project "vr-d3d11-eye-resource-isolation-probe"
		kind "ConsoleApp"
		language "C++"
		targetname "vr-d3d11-eye-resource-isolation-probe"
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
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/eye-resources"

	project "vr-d3d11-gpu-census-probe"
		kind "ConsoleApp"
		language "C++"
		targetname "vr-d3d11-gpu-census-probe"
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
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/gpu-census"

	project "vr-d3d11-draw-indexed-probe"
		kind "ConsoleApp"
		language "C++"
		targetname "vr-d3d11-draw-indexed-probe"
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
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/draw-indexed"

	project "vr-d3d11-execution-probe"
		kind "ConsoleApp"
		language "C++"
		targetname "vr-d3d11-execution-probe"
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
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/execution"

	project "vr-d3d11-gpu-timing-probe"
		kind "ConsoleApp"
		language "C++"
		targetname "vr-d3d11-gpu-timing-probe"
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
		targetdir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/vr-tests/gpu-timing"


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

group "Dependencies"
dependencies.projects()
group ""
