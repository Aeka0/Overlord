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
4. Start SteamVR. The first successful connection registers this client in its library.

Keep the application manifest, steamvr/, vr_input/ and h2-mod/ resources beside
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
