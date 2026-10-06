# Overlord client overlay

This bundle requires a legally owned Windows installation of Call of Duty:
Modern Warfare 2 Campaign Remastered, a supported VR runtime with tracked controllers, and the
base H2-Mod data required by your installation. The game is not included.
Standard client-only archives require the base mod data separately. A locally
staged bundle may include compiled base fastfiles when explicitly supplied;
check `base_data_included` in `release-info.json`. Fastfiles are not compiled by
the client build.

1. Close the game before installing or updating.
2. Extract the bundle into the game directory, preserving its folder layout.
3. Run overlord.exe and complete the launcher's first-use setup. A Debug bundle
   instead contains overlord-debug.exe.
4. Connect the headset and wake both controllers before entering the campaign.
   Steam Link requires SteamVR; Virtual Desktop can use VDXR or SteamVR according
   to its selected OpenXR runtime.

Keep openxr_loader.dll, the application manifest, steamvr/, vr_input/ and h2-mod/ resources beside
the executable. Install updates manually from
[Overlord Releases](https://github.com/Aeka0/h2-mod-vr/releases).
Install Overlord updates from this project's release packages.

See the [project documentation](https://github.com/Aeka0/h2-mod-vr/tree/main/docs)
for controls, settings, supported behavior and remaining compatibility work.
Report reproducible problems in the
[project issue tracker](https://github.com/Aeka0/h2-mod-vr/issues).

The source and pinned dependencies are available from the
[source repository](https://github.com/Aeka0/h2-mod-vr).
Preserve LICENSE, THIRD_PARTY_NOTICES.md and licenses/ when redistributing.
Review docs/source-provenance.md before public redistribution.

## Runtime selection

OpenXR is the default. The application-local loader is included; the vendor
runtime is installed separately. The client keeps the user's API choice:
OpenXR uses an OpenXR runtime such as VDXR or SteamVR, while OpenVR always uses
SteamVR and does not restrict the headset's connection method. Runtime/device
combinations still require separate in-game acceptance.

Within OpenXR, an explicit `XR_RUNTIME_JSON` takes priority. Otherwise, a
connected Steam Link headset can select the installed SteamVR/OpenXR runtime
for this initialization, even if the system default still points at VDXR.
Detection requires the actual `vrlink` driver and a connected HMD. The optional
remote client ID is reported for diagnosis, but does not gate connection detection.
Merely running SteamVR or the VD Streamer is insufficient.
VD and other ambiguous connections retain the configured runtime; VD is not
forced into VDXR when the user selected SteamVR. The system registration is
never changed. Connect before starting the game, or use `vr_reinit` after
connecting to repeat selection. There is no mid-frame runtime switch.

Run overlord.exe normally for the launcher; a Debug bundle contains
overlord-debug.exe. Select **VR Settings > Basics > VR backend**. OpenXR is
recommended and OpenVR is the manual backup. The selection saves immediately
and applies when you click Singleplayer in that same launcher session. No
launcher restart is needed, and changing the selection does not switch an
already running game.

Direct `-singleplayer` starts use the saved launcher choice. An explicit
`H2V_VR_BACKEND` is still available as a temporary diagnostic override for
that direct path, from a Command Prompt in the game directory:

~~~bat
set H2V_VR_BACKEND=openvr
overlord.exe -singleplayer
~~~

Use `openxr` to force OpenXR or clear the variable to return to the saved
preference. New profiles default to OpenXR; `steamvr` is an alias for the
OpenVR diagnostic override. The launcher selection takes precedence when
entering through the launcher UI. `vr_status` reports the actual backend and
errors, along with the selected manifest, override source and connection evidence.
No automatic API-backend switch hides a startup or frame failure.

`XR_RUNTIME_JSON` optionally overrides the runtime within OpenXR; it does not
select OpenVR. The current native bridge explicitly rejects independent eye
rotations. See [runtime contracts and acceptance](https://github.com/Aeka0/h2-mod-vr/blob/main/docs/vr-runtime-rendering.md#openxr-frame-and-lifecycle-contracts).
