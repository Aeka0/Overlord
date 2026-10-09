# Overlord

English | [简体中文](README.zh-CN.md)

![Overlord](assets/steamvr/cover.png)

Bilibili/Aeka0: https://space.bilibili.com/10077845

Reposting is welcome; please include a link to this GitHub Repo.

Overlord is a VR mod for *Call of Duty: Modern Warfare 2 Campaign Remastered*. It adds native stereoscopic rendering, 6DOF head and hand tracking, physical weapon handling, and VR interactions for equipment, environments, and story sequences to the original campaign. The adaptations build on the game's existing weapon, ammunition, animation, and mission logic, preserving the original story presentation wherever possible.

### **Overlord is maintained by one person and is still in Beta, with no stable release yet. The project is undergoing intensive development. Many features have only been tested on the author's own machine, substantial compatibility and adaptation work remains, and stability is expected to be poor.**

### **Please keep expectations low and treat the current version as a test rather than a mature product. Please also avoid comparing it with projects that have benefited from a long period of refinement. Crashes and missing visuals have already been reported on many machines, and these problems are still being investigated and fixed.**

### **If you want a version that you can install and start playing with relatively little trouble, please wait for the stable release. If you would like to help improve stability, you are welcome to download, test, and report problems through [Issues](https://github.com/Aeka0/Overlord/issues). Thank you for your interest and support.**

### **You may still encounter adaptation issues in some scenes or story scripts during play. Custom mods made for the non-VR client are not expected to work with Overlord. Large code changes and refactoring may still take place, so developing mods at this stage is not recommended. Version updates may also break game saves and progress.**

## Known issues:

- If you encounter a `Create2DTexture` error, try disabling Shader Preloading in the game's graphics settings.
- MSI Afterburner may prevent Overlord from starting. This issue is still under investigation; for now, exit MSI Afterburner before launching Overlord.

## Main Features

- Stereoscopic rendering and head tracking: look around the game world through your headset and play the campaign in stereo.
- Weapon handling with both hands: hold and aim weapons with your controllers, with support for an off-hand grip, body holsters, and picking up weapons from the world.
- Physical reloading and chambering: operate magazines, slides, charging handles, magazine tubes, cylinders, or feed mechanisms according to each weapon's design. Different weapons retain their own handling procedures.
- Equipment and world interactions: adaptations for throwables, melee equipment, night vision goggles, and selected mission props.
- Campaign and story adaptations: VR controls and view adjustments for vehicles, mounted weapons, climbing, and scripted camera sequences.
- VR interface and settings: the native game interface presented in VR, plus a desktop launcher with English and Chinese interfaces, a first-time setup guide, and control help.
- Comfort and assistance options: adjust turning, controller alignment, selected visual effects, and assistance features, and configure the desktop spectator view.
- Optional features: players can adjust recoil, quick reloading, aim assistance, enemy melee damage, and other options.

## Requirements

- 64-bit Windows 10 or 11.
- A legally owned and installed copy of *Call of Duty: Modern Warfare 2 Campaign Remastered*.
- SteamVR and VR hardware that provides head and hand tracking through SteamVR.

The client uses Direct3D 11 and defaults to OpenXR, with OpenVR retained as a manually selected backup. The validated combination is Meta Quest controllers with SteamVR/OpenXR; other devices and runtimes require separate acceptance. Choose it under the launcher's VR Settings > Basics > VR backend; it applies when entering the game without restarting the launcher. See [runtime and rendering contracts](docs/vr-runtime-rendering.md) for selection and limitations.

Controller poses default to Legacy. The Standard pipeline using OpenXR coordinates remains a manually selected trial and requires separate headset acceptance. Select the pipeline under VR Settings > Calibration > Controller pose pipeline, then restart the game to apply it. Each pipeline retains its own calibration. Existing custom calibration remains in the legacy bank; choose a preset or adjust the standard bank separately. The position and angle descriptions list the selected pipeline's console commands for live in-game calibration.

The repository includes default input bindings for Oculus Touch (Meta Quest), Valve Index, and Vive Controller. Meta Quest controllers are the primary validated devices; the Index OpenVR binding also has limited hardware testing, described below. Other combinations may require adjustments to runtime bindings and controller alignment settings.

Pirated or cracked copies of the game are not supported. Mod releases do not include the full original game assets or provide download links for the game.

## Installation and First Use

When Beta and stable releases are published, installation packages will be available on the [Releases page](https://github.com/Aeka0/Overlord/releases). Before a release is available, refer to the source build instructions below.

1. Make sure the original game installation is complete.
2. Extract the VR client package into the game's root directory and run `overlord.exe`.
3. Start SteamVR, make sure your headset and controllers are connected, then click "Singleplayer".

When using the OpenVR backup, the client registers Overlord with SteamVR after its first successful connection. The standard and Debug builds register separately, with their own launch entries.

Keep the client and its resources in a layout similar to this:

~~~text
Game directory/
├─ overlord.exe
├─ openxr_loader.dll
├─ overlord.vrmanifest
├─ steamvr/
├─ vr_input/
└─ h2-mod/
~~~

Building the client does not automatically compile ZoneTool assets into fastfiles. Before a release, the source of the base data, package contents, and complete installation procedure still need to be confirmed. Developers can refer to the release preparation documentation.

## Basic Controls

Button names vary by controller and runtime binding. The following uses the common names Grip and Trigger:

| Action | Basic method |
| --- | --- |
| Move | Use the left stick. |
| Turn | Use the right stick. Choose smooth turning or snap turning in settings. |
| Hold a weapon | Move your hand near the appropriate grip point and hold Grip. |
| Fire | Hold the weapon's firing grip and squeeze Trigger with that hand. |
| Handle ammunition and parts | Use Trigger with a free hand to interact with magazines, ammunition, slides, or charging handles. The action depends on the weapon's design. |
| Holster a weapon | Move the weapon to a compatible body holster and release Grip. |

Magazine removal, loading, and chambering differ between weapons. For example, some weapons use a button to release the magazine, while others require you to pull it out manually. Pump-action shotguns, revolvers, and break-action weapons also have their own loading procedures. If the chamber is empty, you may still need to operate the slide, charging handle, or corresponding feed mechanism. Follow the instructions for the specific weapon in the launcher's "Help" page.

### Valve Index controllers (OpenVR)

The default Knuckles binding uses the physical thumbsticks for movement and
turning. Click the left thumbstick to sprint and the right to jump; right-stick
vertical movement uses the configured stance controls. Tap left A to pause/back,
or hold it to recenter. Right A shows the weapon HUD, and B uses the corresponding
hand's secondary weapon action. Squeeze the grip to hold and relax to release.
Firing responds to trigger travel before the mechanical click; capacitive trigger
touch remains a separate input.

These bindings were exercised with Index controllers and a Pimax Dream Air in
OpenVR. This does not establish OpenXR or general device acceptance. An existing
custom SteamVR binding can override the shipped defaults; select the default
Knuckles binding to use these mappings.

## Settings and Usage Tips

For your first session, configure controller alignment and turning before adjusting other options to suit your preferences. The first-time setup guide saves valid selections immediately; changes on the full "VR Settings" page are applied with the Save button.

The launcher offers visual, interaction, assistance, and cheat options. Get familiar with the default controls first, then adjust these as needed. Debug options are intended for troubleshooting and are usually unnecessary during normal play.

Launcher language and game language are managed separately. Switching the launcher's English or Chinese interface does not download or switch the original game's language assets.

Install updates manually from this project's [Releases page](https://github.com/Aeka0/Overlord/releases). The current VR version does not have an automatic update service.

## Current Status and Limitations

The project is still in development and release preparation.

- All interactions have been checked only on the developer's own machine, so coverage across all systems cannot be claimed. Support may vary between weapons, attachments, mission props, and story scenes.
- Device compatibility and performance need to be verified with the actual headset, controllers, runtime environment, and game scene. Device-specific adaptation has so far focused on Meta Quest 3.

The project does not currently publish unverified minimum hardware specifications, make performance promises, or guarantee an issue-free campaign. Confirmed compatibility, known issues, and version changes will be listed in release notes.

## Building from Source

Clone the repository with Git and initialize its submodules. Downloading GitHub's source ZIP does not include the required submodule contents.

Install Visual Studio's Desktop development with C++ tools and the Windows SDK, then run the following in the appropriate Developer Command Prompt:

~~~bat
git clone --recurse-submodules https://github.com/Aeka0/Overlord.git Overlord
cd Overlord
generate.bat
msbuild build\overlord.sln /t:client /m:2 /p:Configuration=RelWithDebInfo /p:Platform=x64 /p:PreferredToolArchitecture=x64 /p:CL_MPCount=4
~~~

`generate.bat` generates a Visual Studio 2022 project. The default toolset is v143. For an existing Visual Studio 2019 v142 toolchain, generate with `tools\premake5.exe vs2019`, use its x64 MSBuild, and specify `/p:PlatformToolset=v142`.

| Purpose | Build configuration | Client file |
| --- | --- | --- |
| Normal play and performance checks | `RelWithDebInfo` | `overlord.exe` |
| Development and troubleshooting | `Debug` | `overlord-debug.exe` |

Build output is placed in `build/bin/x64/<Configuration>/`. Use the executable and resources produced by the same build; do not mix mismatched versions.

See the [development guide](docs/development.md) for build details, checks appropriate to each change, and resource deployment. Other technical topics are listed in the [documentation index](docs/README.md). These developer documents are currently written mainly in English.

## Reporting Issues and Contributing

Search [existing issues](https://github.com/Aeka0/Overlord/issues) before submitting a report. Reports in English or Chinese are welcome. Please include:

- The version or commit you are using.
- Your headset, controllers, Windows and VR runtime versions, and selected OpenXR/OpenVR backend.
- The mission, checkpoint, weapon, or interaction where the problem occurs.
- Steps to reproduce the issue, along with expected and actual behavior.
- Relevant error text or trimmed log excerpts, if needed.

Do not upload game files, access credentials, or unchecked full memory dumps to public issues. See the [security policy](SECURITY.md) for reporting issues that involve sensitive personal information.

Read the [contributing guide](CONTRIBUTING.md) before working on the project. Changes should reuse existing components where possible, preserve the responsibility boundaries of native game logic, and clearly distinguish source checks, automated tests, and validation on actual hardware.

## License and Acknowledgments

The project code is licensed under [GNU GPLv3](LICENSE). Third-party code, fonts, and other resources retain their respective licenses and copyright notices. See the [third-party notices](THIRD_PARTY_NOTICES.md) and [source provenance](docs/source-provenance.md).

The VR adaptation is independently maintained by [Aeka0](https://github.com/Aeka0). This fork is not affiliated with or endorsed by the upstream project or its authors and do not disturb them. Maintenance and support for the VR changes are handled by this project.

The underlying code builds on H2-Mod and its earlier upstream projects, [IW6x](https://git.alterware.dev/alterware/iw6-mod) and [S1x](https://git.alterware.dev/alterware/s1-mod).

Thanks also to the following projects and contributors:
[momo5502](https://github.com/momo5502),
[JariKCoding](https://github.com/JariKCoding/CoDLuaDecompiler),
[xensik](https://github.com/xensik/gsc-tool),
[ZoneTool](https://github.com/ZoneTool/zonetool),
[quaK](https://github.com/Joelrau), and the related work of Valve, Khronos, and REFramework.

This is an independent community mod, not an official game release. Game names and trademarks belong to their respective rights holders. The project is intended for academic research and must not be used for piracy or attacks that circumvent copy protection. Users are responsible for any misuse.
