# H2-MOD VR client overlay

This bundle requires a legally owned Windows installation of Call of Duty:
Modern Warfare 2 Campaign Remastered, SteamVR with tracked controllers, and the
base H2-Mod data required by your installation. The game is not included.
Standard client-only archives require the base mod data separately. A locally
staged bundle may include compiled base fastfiles when explicitly supplied;
check `base_data_included` in `release-info.json`. Fastfiles are not compiled by
the client build.

1. Close the game before installing or updating.
2. Extract the bundle into the game directory, preserving its folder layout.
3. Run h2-mod-vr.exe and complete the launcher's first-use setup. A Debug bundle
   instead contains h2-mod-vr-debug.exe.
4. Start SteamVR with the headset and both controllers awake before entering the
   campaign. The default OpenXR path uses the registered active OpenXR runtime.

Keep openxr_loader.dll, the application manifest, steamvr/, vr_input/ and h2-mod/ resources beside
the executable. Install updates manually from
[H2-MOD VR Releases](https://github.com/Aeka0/h2-mod-vr/releases).
The inherited upstream auto-update service is unconfigured for VR builds.

See the [project documentation](https://github.com/Aeka0/h2-mod-vr/tree/main/docs)
for controls, settings, supported behavior and remaining compatibility work.
Report reproducible problems in the
[project issue tracker](https://github.com/Aeka0/h2-mod-vr/issues).

The source and pinned dependencies are available from the
[source repository](https://github.com/Aeka0/h2-mod-vr).
Preserve LICENSE, THIRD_PARTY_NOTICES.md and licenses/ when redistributing.
Review docs/source-provenance.md before public redistribution.

## Runtime selection

OpenXR is the default. The validated setup uses Meta Quest controllers and
SteamVR/OpenXR. Start SteamVR and wake the headset/controllers before launching.
The application-local loader is included; the vendor runtime is installed
separately. Other runtime/device combinations require separate acceptance.

Run h2-mod-vr.exe normally for the launcher; a Debug bundle contains
h2-mod-vr-debug.exe. Select **VR Settings > Basics > VR backend**. OpenXR is
recommended and OpenVR is the manual backup. The selection saves immediately
and applies when you click Singleplayer in that same launcher session. No
launcher restart is needed, and changing the selection does not switch an
already running game.

Direct `-singleplayer` starts use the saved launcher choice. An explicit
`H2V_VR_BACKEND` is still available as a temporary diagnostic override for
that direct path, from a Command Prompt in the game directory:

~~~bat
set H2V_VR_BACKEND=openvr
h2-mod-vr.exe -singleplayer
~~~

Use `openxr` to force OpenXR or clear the variable to return to the saved
preference. New profiles default to OpenXR; `steamvr` is an alias for the
OpenVR diagnostic override. The launcher selection takes precedence when
entering through the launcher UI. `vr_status` reports the actual backend and
errors. No automatic backend switch hides a startup or frame failure.

`XR_RUNTIME_JSON` optionally overrides the runtime within OpenXR; it does not
select OpenVR. The current native bridge explicitly rejects independent eye
rotations. See [runtime contracts and acceptance](https://github.com/Aeka0/h2-mod-vr/blob/main/docs/vr-runtime-rendering.md#openxr-frame-and-lifecycle-contracts).
