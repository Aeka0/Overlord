# Build and validation

Commands and targets come from [premake5.lua](../premake5.lua) and the [CI workflow](../.github/workflows/build.yml). This guide distinguishes offline checks from in-game/headset acceptance. See [release preparation](releasing.md) before distributing a build.

## Toolchain and first build

The current client targets Windows x64, Win32, and D3D11. The repository does not provide an equivalent Linux client build pipeline.

Use Visual Studio 2022 with the Desktop development with C++ tools and a Windows SDK. Premake selects `C++latest`, the latest available SDK, and the static CRT, and generates symbols for Debug, RelWithDebInfo and Release. Python is used for analysis and deployment tools; Node.js is used for launcher script tests. Neither is a client runtime dependency.

If the installed C++ toolset is Visual Studio 2019 v142, use its x64 MSBuild
and pass `/p:PlatformToolset=v142` when building the generated solution. The
Visual Studio 2022 project generator alone does not install a v143 compiler.

From the repository root in a VS Developer Command Prompt, run:

```bat
git submodule update --init --recursive
tools\premake5.exe vs2022 --with-vr-tests
msbuild build\h2-mod.sln /m /v:minimal /p:Configuration=Debug /p:Platform=x64
```

`generate.bat --with-vr-tests` also initializes submodules and generates the solution. Regenerate after adding source files or test targets, or changing Premake. Do not maintain generated `.vcxproj` files directly.

For Release, use the same command and change the value in `Configuration=Debug` to `Release`. Release enables optimization and treats compiler warnings as errors; a passing Debug build does not establish that Release builds successfully.

Artifacts are written to `build/bin/x64/<Configuration>/`. The client project is named `client`. Debug produces `h2-mod-vr-debug.exe` and `h2-mod-vr-debug.pdb`; RelWithDebInfo/Release produce `h2-mod-vr.exe` and `h2-mod-vr.pdb`. The embedded TLS helper keeps its configuration-local `tlsdll.dll` name. To build and run one target:

```bat
msbuild build\vr-weapon-grip-tests.vcxproj /m /v:minimal /p:Configuration=Debug /p:Platform=x64
build\bin\x64\Debug\vr-tests\weapon-grip\vr-weapon-grip-tests.exe
```

Both `/m` and the project-level `MultiProcessorCompile` setting are enabled. Large builds can run several compiler processes at once. If memory is insufficient, limit the number of parallel projects through `/m`. Preserve the first error rather than treating subsequent failures as independent root causes.

## Select validation for the change

These are registered standalone targets. Executable paths are relative to `build/bin/x64/<Configuration>/vr-tests/`. Select targets that cover the change; tests in headers are included by their corresponding `.cpp` entry points and are not run individually.

| Change area | Project target | Relative executable path |
| --- | --- | --- |
| Controller input | `vr-controller-input-tests` | `controller-input/vr-controller-input-tests.exe` |
| Hand poses / rigging | `vr-hand-pose-tests`, `vr-hand-rig-tests` | `hand-pose/vr-hand-pose-tests.exe`, `hand-rig/vr-hand-rig-tests.exe` |
| Weapon registration / grip / carry | `vr-weapon-grip-tests` | `weapon-grip/vr-weapon-grip-tests.exe` |
| Chamber / ammunition transactions | `vr-closed-bolt-tests`, `vr-weapon-mechanics-tests` | `closed-bolt/vr-closed-bolt-tests.exe`, `weapon-mechanics/vr-weapon-mechanics-tests.exe` |
| Physical reload / cylinders | `vr-physical-reload-tests`, `vr-cylinder-tests` | `physical-reload/vr-physical-reload-tests.exe`, `cylinder/vr-cylinder-tests.exe` |
| Shared scene submissions / transfer identity | `vr-shared-runtime-contract-tests` | `shared-runtime-contracts/vr-shared-runtime-contract-tests.exe` |
| Pistol data | `vr-pistol-profile-tests` | `pistol-profile/vr-pistol-profile-tests.exe` |
| Moving rigid parts | `vr-rigid-part-tests` | `rigid-part/vr-rigid-part-tests.exe` |
| Melee / aim assistance | `vr-melee-tests`, `vr-aim-assist-tests` | `melee/vr-melee-tests.exe`, `aim-assist/vr-aim-assist-tests.exe` |
| Spatial panels / native HUD adaptation | `vr-spatial-panel-tests`, `vr-weapon-hud-lui-tests` | `spatial-panel/vr-spatial-panel-tests.exe`, `weapon-hud-lui/vr-weapon-hud-lui-tests.exe` |
| Independent optic crop / composition / upload lifecycle | `vr-optic-render-tests` | `optic-render/vr-optic-render-tests.exe` |
| Launcher settings | `vr-launcher-settings-tests` | `launcher-settings/vr-launcher-settings-tests.exe` |
| Region capture | `vr-region-capture-tests` | `region-capture/vr-region-capture-tests.exe` |
| Scene and view contracts | `vr-engine-stereo-probe-smoke`, `vr-engine-view-probe-smoke` | `probe-core/vr-engine-stereo-probe-smoke.exe`, `view-probe/vr-engine-view-probe-smoke.exe` |
| Desktop spectator projection / ring metadata | `vr-desktop-mirror-tests` | `desktop-mirror/vr-desktop-mirror-tests.exe` |
| Optional head / hand / desktop filtering | `vr-stabilization-tests` | `stabilization/vr-stabilization-tests.exe` |
| GUI input capture / native menu preservation | `gui-input-capture-tests` | `gui-input/gui-input-capture-tests.exe` |
| Native VR menu layout, input and overlay ownership | `vr-menu-surface-tests`, `vr-menu-overlay-tests` | `menu-surface/vr-menu-surface-tests.exe`, `menu-overlay/vr-menu-overlay-tests.exe` |
| Native menu/global UI hook ABI and far relays | `vr-native-menu-hook-tests` | `native-menu-hook/vr-native-menu-hook-tests.exe` |
| Native backend observation | `vr-engine-backend-probe-smoke` | `backend-probe/vr-engine-backend-probe-smoke.exe` |
| Per-eye resources / SSR | `vr-d3d11-eye-resource-isolation-probe`, `vr-engine-ssr-history-probe` | `eye-resources/vr-d3d11-eye-resource-isolation-probe.exe`, `ssr-history/vr-engine-ssr-history-probe.exe` |
| No-loader / mock boundaries | `vr-runtime-no-loader-smoke`, `vr-runtime-mock-smoke` | `no-loader/vr-runtime-no-loader-smoke.exe`, `mock/vr-runtime-mock-smoke.exe` |
| OpenVR focus recovery / device lifecycle | `vr-openvr-lifecycle-tests` | `openvr-lifecycle/vr-openvr-lifecycle-tests.exe` |

Premake also manages source and link dependencies for other D3D11 probes, including GPU census, dynamic execution, constant buffers, and readback. Select those targets when changing the corresponding mechanism; do not automatically run every `*probe.exe` based on its filename.

Script tests can run directly from the repository root:

```bat
python tests/config_validation_tests.py
python tests/background_update_tests.py
python tests/vr/test_region_capture_analysis.py
python tests/vr/aim_assist_adapter_tests.py
node tests/vr/launcher_settings_ui_tests.js
python tests/vr/client_feature_parity_tests.py build/bin/x64/RelWithDebInfo/h2-mod-vr.exe
python tests/vr/client_feature_parity_tests.py build/bin/x64/Debug/h2-mod-vr-debug.exe
```

The config validation test compiles the production config namespace and public
accessors in Debug and optimized Release modes, with in-memory file I/O and
deterministic language/channel validators. It checks malformed values, oversized
strings, typed defaults, and read/write behavior without accessing user profiles.
It does not validate the updater's choice of default release channel.

The background-update test compiles the production HTTP implementation, bounded
worker and updater request-ownership functions against the RelWithDebInfo curl
library. It uses loopback responses to check timeouts, cancellation, superseded
requests, stale UI notifications and shutdown; it never contacts the update
service or changes installed files. Update work runs on its own worker (one
running and one replaceable pending task), outside the shared async scheduler.

The client feature audit reads emitted function symbols from the exact matching
PDB without executing the game. It catches whole production components compiled
out by build guards, which policy-only tests cannot detect. HUD capture/source,
all HUD composition layers, waypoint routing and independent-hand skin visibility
must exist in both clients. Compiler debug instrumentation and optional probes
may differ; production functionality must not.

Launcher script tests use a simulated DOM. They do not validate layout, DPI behavior, light/dark themes, or interaction rendering in the actual embedded browser. WARP graphics tests establish the resource contracts under test, not H2's native call chain or the image seen in an HMD.

## Runtime dependencies and test dependencies

Weapon catalog consumers link the `vr-weapon-catalog` test library. Concrete
weapon recipes are compiled once there; tests that inspect a particular weapon
include its profile explicitly. Do not restore the full catalog as a transitive
header dependency to fix a missing declaration. The client compiles those same
implementation files directly without its engine PCH. Other client translation
units keep `<std_include.hpp>` before additional includes.

The production selector uses SteamVR/OpenVR; see the [runtime guide](vr-runtime-rendering.md). The repository still contains an OpenXR implementation and tests:

- `vr-runtime-mock-smoke` directly tests the OpenXR implementation and requires the mock `openxr_loader.dll` in the same directory. Passing it does not establish that production OpenVR submission works.
- `vr-openvr-lifecycle-tests` exercises the production OpenVR implementation with strict SDK doubles and WARP textures. It covers focus loss before/after submission, partial-eye submission, safe retirement, format-probe resumption, and initialization attempts after device replacement. It does not launch SteamVR or establish HMD acceptance.
- `vr-runtime-real-loader-probe` and `tools/build-openxr-loader.bat` validate the real OpenXR loader boundary.
- `vr-steamvr-hardware-probe` accesses the actual runtime and devices. Keep it separate from regression batches that require no hardware. It also does not replace acceptance inside the target game.
- CI builds RelWithDebInfo and Debug clients, runs the no-loader smoke test and validates launcher scripts. It packages client overlays without deploying to an update server. OpenXR loader artifacts and hardware probes remain separate tasks.

## Deployment and acceptance

In addition to the executable, the client's post-build steps copy these resources:

| Repository source | Destination relative to the game directory |
| --- | --- |
| `data/vr_input/` | `vr_input/` |
| `data/ui_scripts/vr_gameplay/` | `h2-mod/ui_scripts/vr_gameplay/` |
| `assets/steamvr/cover.png`, `cover-small.png`, `cover-capsule.png` | `steamvr/` |
| `assets/steamvr/<client-name>.vrmanifest` | `<client-name>.vrmanifest` beside the EXE |

The application manifest uses paths relative to its installed location. After
`VR_Init`, the client registers it through `AddApplicationManifest` and identifies
the current process before initializing input. Normal and Debug clients use
`h2mod.vr` and `h2mod.vr.debug` respectively, so their launch paths cannot overwrite
each other. SteamVR launch uses `-singleplayer` to enter the game directly; run the
EXE normally to access launcher settings. Registration persists across shutdown.
SteamVR uses the 920 x 430 horizontal cover through `image_path` and the separate
600 x 900 vertical cover through `image_path_capsule`. The 460 x 215 horizontal
export is included for manual artwork use. Both client manifests reference the
same artwork, and the build, release packager and paired deployment include all
three images. The release packager rejects missing artwork and manifest resource
references that are unsafe or absent from the package.
`vr_status` reports `application_registered` and `application_registration_error`;
registration failure does not disable rendering or controller input.

Local performance deployments include both builds from the same source checkout:

| Purpose | Configuration | Files in game directory |
| --- | --- | --- |
| Normal gameplay and performance baseline | RelWithDebInfo | `h2-mod-vr.exe`, `h2-mod-vr.pdb` |
| Debugging and controlled build comparison | Debug | `h2-mod-vr-debug.exe`, `h2-mod-vr-debug.pdb` |

```bat
tools\premake5.exe vs2022 --with-vr-tests
msbuild build\h2-mod.sln /t:client /m /p:Configuration=RelWithDebInfo /p:Platform=x64
msbuild build\h2-mod.sln /t:client /m /p:Configuration=Debug /p:Platform=x64
python tools\deploy_client_pair.py "<game-directory>"
```

The Python helper requires both EXE/PDB pairs, application manifests, matching
SteamVR covers and matching `vr_input` packages before modifying the destination.
It stages and hashes the four binaries, application manifests, covers, action
manifest and every declared default binding, preserves replaced files in a unique
directory under `output/deployments/`, and writes a deployment manifest.
A failed replacement attempts to restore all replaced files. Missing or
mismatched resource packages reject the deployment before any replacement.
Close either running game executable before deploying. Synchronize the UI script
resource group separately when changing those assets. The helper does not edit
player profiles or configure the machine.

`--copy-to=<game-directory>` remains a per-configuration post-build convenience:
it copies that configuration's named EXE/PDB, application manifest and all three
resource groups (input bindings, UI scripts and SteamVR artwork).
Build both configurations to populate both names; use the paired helper when
both builds must succeed before any replacement. This is a local generation
parameter; do not hardcode the actual directory in the repository. A complete
installation still needs the remaining project distribution data.

The two executables share the existing player configuration. For a performance
comparison run them one at a time with the same level, route, resolution, refresh
rate and optional probe settings. The launcher Debug page reports the compiled
build type. Probe switches cannot convert a Debug executable into an optimized
one. Compare the same source revision: rendering fixes and build optimization
changed together in the first follow-up, so that run cannot isolate their gains.

Before deployment, confirm that the target game process has exited and record hashes for the build and installed artifacts. Matching files on disk prove that the copies match. Confirming the running build also requires checking the process start time and module identity.

Hardware acceptance records should identify the level, actions, configuration, devices, and tested scope. For rendering changes, check both eyes, focus transitions, recentering, level transitions, and device lifecycle changes. Preserve results using the [diagnostics and acceptance workflow](vr-diagnostics-workflow.md); do not report offline PASS as hardware acceptance.
