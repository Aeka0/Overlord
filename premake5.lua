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
	description = "Generate VR unit tests, CPU/WARP smoke tests, and optional hardware probes."
}

dofile(path.join(_MAIN_SCRIPT_DIR, "tools/premake_version.lua"))

dependencies.load()

workspace "overlord"
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
	linkoptions {"/IGNORE:4702"}
	defines {"NDEBUG"}
	flags {"FatalCompileWarnings", "LinkTimeOptimization"}
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

targetname "overlord"
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

-- MSVC v142 LTCG places the mutable carry model frame in .rdata while retaining
-- its reset/publication writes. Compile this TU normally; keep Release's size
-- optimization and the rest of the client's LTCG. A /GL PCH cannot be reused.
filter {"configurations:Release", "files:src/client/component/vr/gameplay/weapon_carry_runtime.cpp"}
	flags {"NoPCH"}
	buildoptions {"/GL-"}
filter {}

linkoptions {"/IGNORE:4254", "/DYNAMICBASE:NO", "/SAFESEH:NO", "/LARGEADDRESSAWARE", "/LAST:.main", "/PDBCompress"}

files {"./src/client/**.rc", "./src/client/**.hpp", "./src/client/**.cpp", "./src/client/resources/**.*"}
files {"./assets/icon/Icon.ico"}

includedirs {"./src/client", "./src/common", "%{prj.location}/src"}

resincludedirs {"$(ProjectDir)src"}

dependson {"tlsdll", "openxr-loader", "launcher-resources"}
includedirs {"%{wks.location}/deps/webview2/include", "%{wks.location}/launcher-ui"}
resincludedirs {"$(ProjectDir)launcher-ui"}
libdirs {"%{wks.location}/deps/webview2/lib"}
links {"WebView2LoaderStatic", "ole32", "shlwapi", "version"}

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
	postbuildcommands {'{COPYFILE} "%{cfg.targetdir}/openxr_loader.dll" "' .. path.join(_OPTIONS["copy-to"], "openxr_loader.dll") .. '"'}
end

dependencies.imports()

project "tlsdll"
kind "SharedLib"
language "C++"

files {"./src/tlsdll/**.rc", "./src/tlsdll/**.hpp", "./src/tlsdll/**.cpp", "./src/tlsdll/resources/**.*"}

includedirs {"./src/tlsdll", "%{prj.location}/src"}

links {"common"}

resincludedirs {"$(ProjectDir)src"}

group "Dependencies"
project "launcher-resources"
	kind "Utility"
	files {"tools/build_launcher.py", "src/launcher-ui/package.json", "src/launcher-ui/package-lock.json"}
	prebuildcommands {'python "%{wks.location}/../tools/build_launcher.py"'}

project "openxr-loader"
	kind "Utility"
	files {"tools/build_openxr_loader.py", "deps/openxr/src/loader/CMakeLists.txt"}
	local loader_generator = _ACTION == "vs2019" and "Visual Studio 16 2019" or "Visual Studio 17 2022"
	prebuildcommands {
		'python "%{wks.location}/../tools/build_openxr_loader.py" --configuration RelWithDebInfo --generator "' .. loader_generator .. '" --toolset "$(PlatformToolset)" --output-dir "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}"'
	}

if _OPTIONS["with-vr-tests"] then
	dofile(path.join(_MAIN_SCRIPT_DIR, "premake/vr_tests.lua"))()
end

group "Dependencies"
dependencies.projects()
group ""
