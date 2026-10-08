# Launcher VR settings

The desktop launcher exposes **Play / Language / VR Settings / Help / About**.

## Disable risk settings before launch

Clicking **Launch game** runs a fresh check beside the launcher executable.
Missing/invalid supported game EXEs, `amd_ags_x64.dll`, `bink2w64.dll`, and the
OpenXR loader when OpenXR is selected are blocking red items. SSAA must be
explicitly saved as 1 (Off); other or unknown values also block startup. Shader
preloading and sun/spot shadow caching are yellow warnings, including profiles
that do not establish explicit safe values. Warnings may be accepted for that
launch only. There is no persistent warning-suppression option.

Each repairable row offers **Fix**. SSAA, shader preloading and shadow caching
are repaired separately with the existing atomic profile writer. A missing
OpenXR loader is restored from the matching SDK DLL embedded in the launcher.
Original game EXEs/DLLs must be restored from the game installation. OpenVR is
statically linked, and the TLS helper is already embedded; neither requires an
additional sibling DLL. File/config inspection and repairs run on the ordered
file worker. The native host performs a fresh gate before spawning the child,
so a new blocking problem or unacknowledged warning returns to the dialog.

**VR Settings > Debug > Disable all risk settings** immediately saves the native
game configuration without starting the game. It disables SSAA, shader caching
(frontend precaching and level preloading), cached sun shadows and cached spot
shadows. A status message beside the button reports success or a retryable error.
Other settings, bindings and any unsaved VR draft are preserved.

The action uses the same profile selection and atomic writer as VR settings.
It writes `r_ssaaSamples = 1`, `r_preloadShadersFrontendAllow = 0`,
`r_preloadShaders = 0`, `sm_cacheSunShadow = Disabled` and
`sm_cacheSpotShadows = Disabled`. Named and hexadecimal-hash assignments are
merged together, including duplicates. The game reads the saved values on its
next start; this action does not delete cache files or alter a running game.

## VR backend selection

**VR Settings > Basics > VR backend** uses the shared choice catalog: OpenXR
(default/recommended) and OpenVR (manual backup). A valid selection saves
immediately; a failed save restores the prior choice and remains retryable.
The upcoming game reads that saved choice in its child process, before
loading its binary and constructing the runtime. No launcher restart is needed.
The active game keeps one backend until it exits.

The choice is stored in the existing native profile as `vr_runtimeBackend`.
Save/reset, UTF-8 profiles and native enum indices share the normal setting
validation. A launcher start uses its saved choice; direct `-singleplayer` starts
use the saved choice unless `H2V_VR_BACKEND` explicitly overrides it for diagnosis.
Backend changes do not rewrite controller calibration or switch the vendor's
system-wide OpenXR registration.

OpenXR resolves its runtime during initialization: explicit `XR_RUNTIME_JSON`
wins, a positively identified connected Steam Link HMD can choose SteamVR/OpenXR,
and other connections retain the configured runtime. VD may use either VDXR or
SteamVR, so the presence of its Streamer alone does not decide the runtime.
OpenVR always uses SteamVR, including its supported VD and native-headset routes.
No additional runtime dropdown is required. `vr_status` reports the effective
runtime, selection source and evidence; `vr_reinit` repeats the initialization
decision after reconnecting. See [installation and runtime selection](client-installation.md#runtime-selection).

## First-use setup and quick guide

The six-step guide opens automatically only when the working directory contains
the supported, readable game executable and the effective native profile has no
VR assignments. The check shares the loader's executable selection and supported
size, reads only its signature, and does not launch the game. A missing executable,
failed profile read, existing VR configuration (including advanced VR dvars), or
missing bridge metadata never triggers setup. A flat-game profile without VR
settings is eligible. Skipping keeps the applied choices and initializes an untouched first run with the current defaults.

The steps are language; controller preset and turning; visual comfort; weapon
interaction and aim assistance; single-hand and ammunition penalties; author and
GitHub. Device choices reuse the shared calibrated preset catalog. Other devices
use neutral alignment; no device detection or unverified calibration is implied.
All preferences start from the current settings. Language, controller presets
and valid VR changes save immediately and synchronize the full settings editor.
Numeric input uses the shared limits: incomplete or out-of-range text is never
written. Failed toggles and dropdown choices return to the last applied value;
failed numeric edits stay available for correction or retry. Skip retains all
applied choices, discards unfinished numeric text and initializes an untouched
first run with the current defaults. Finish returns to the launcher. Read/write
errors remain retryable without overwriting the last successful configuration.

VR Settings > Basics > Quick guide reopens the same flow with the current draft,
including unrelated unsaved settings. Both editors share numeric limits, preset
data, validation and atomic profile merging. Native configuration inheritance
also works when launcher language preferences have already created the profile
folder. All settings categories and the guide have extra trailing content padding.

Guide behavior and styling live in `src/launcher-ui/src/App.tsx`,
`useSettings.ts`, and `styles.css`. Settings descriptions and help content reuse
the shared locale resources. Both editors use the native defaults, limits and
choice catalogs. Valid numeric guide changes are coalesced while typing; incomplete
text remains editable and is never written.

The Win32 host displays a local React page through WebView2. The production
HTML, JavaScript, CSS and original WOFF fonts are embedded in the client EXE.
The home page shows only the launch action and the editable runtime selector
over the original launcher artwork. Shared top navigation opens settings,
language, help and credits. Settings use
category navigation, independent sidebar/content scroll areas, shared rows and
a Reset control at the bottom of the left sidebar. Valid VR changes save
automatically; numeric typing is coalesced and invalid text is never written.
No page reserves a bottom action bar. The launcher uses a fixed dark theme and an integrated
title bar with native dragging, maximization and edge resizing. Typography retains the
original embedded Latin faces and Windows glyph fallback.

Launching creates a new instance of the same EXE in game mode, preserves
startup arguments and the selected backend, then closes the launcher. The game
process bypasses WebView2 initialization.

## Language

Launcher language defaults to English. English and Simplified Chinese apply
immediately, including navigation, setting descriptions, dropdown options,
validation/status messages, and accessibility labels. Switching languages
preserves the unsaved VR draft and active settings category. Chinese uses system
CJK fonts only for missing glyphs; supported glyphs keep the English decorative
fonts. Chinese headings use a heavier weight.

The native bridge atomically saves only the launcher locale ID in
`players2/h2-mod/launcher.json`, relative to the game directory. This separate
launcher preference does not write the game's language setting or VR dvars.
Missing, malformed, unsupported, or oversized preferences fall back to English.
Read/write errors are shown; a failed save keeps the previously applied language.

The game-language dropdown is a session-only preview with English and Simplified
Chinese choices. It does not save, change native game settings, or download assets.

### Localization source map

- `src/client/resources/launcher/locales/en.json`: English message catalog and fallback.
- `src/client/resources/launcher/locales/zh-CN.json`: Simplified Chinese translations.
- `src/client/resources/launcher/i18n.js`: ES5 translation, interpolation, fallback,
  and safe text/attribute updates.
- `src/client/resources/launcher/app.js`: Launcher interaction and state management.
- `src/client/launcher/localization.hpp`: Supported locale/resource registry and
  bounded preference parsing.
- `src/client/launcher/localization.cpp`: Embedded script composition and preference I/O.

Add a locale catalog, its RCDATA resource, and a registry entry to enable a new
language. Keep semantic keys and interpolation parameters consistent with English.
Use `data-i18n`, `data-i18n-aria-label`, and `data-i18n-title` for static content;
dynamic UI state stores message keys so it can be retranslated. Catalogs are
embedded in the executable; installed launchers need no external translation files.

## VR Settings

Visual comfort includes **Disable blur** (`vr_disableBlur`, off by default).
**Other > Hide all HUD** (`vr_hideHud`, off by default) hides native game HUD and
spatial HUD while preserving menus. See [presentation options](vr-presentation-options.md)
for rendering boundaries.

VR Settings has five categories: **Basics** contains visual comfort and controller
alignment; **Gameplay** contains turning, aiming and throwable speed; **Cheats** contains
no recoil, health protection, native notarget and ammunition modes, all off by default;
**Other** contains
live stream preview and Hide all HUD; **Debug** contains optional diagnostic probes. Switching categories keeps
unsaved changes and the scroll position of each category. The left sidebar's Save
button saves all categories; Reset restores the entire draft's defaults.

Cheats displays a recommendation to complete the campaign once before enabling
it. Demigod/God cannot prevent scripted deaths. Infinite reserves locks carried
reserve pools at 500 while retaining normal reload/chambering; Infinite ammo
reuses the existing sustained-shot behavior. See [native cheat integration](vr-official-cheats.md).

Gameplay's Enemy close combat group exposes `vr_enemyMeleeDamageScale`
(0.1–2.0, default 0.5, step 0.1) and `vr_disableDogPounce` (default on).
The multiplier applies to incoming enemy AI melee, including dog bites; 1.0
restores native damage. Disabling dog pounces selects the native ordinary bite
branch before the player knockdown sequence begins. Dogs remain able to attack.
These policies apply only when VR is enabled. See [enemy combat](vr-enemy-combat.md)
for the native boundaries and acceptance checks.

The former Ammo Drop preview is now the saved `vr_discardAmmoPenalty` toggle
(default off). Enabling it loses all remaining rounds when a discarded magazine
or speedloader expires or hits the world, and when live revolver rounds are
cleared. Releasing a held container at the waist recovers its exact payload;
catching a falling container preserves it. Forced cleanup is exempt. Save,
load, reset and launch use the shared boolean settings contract.

Gameplay's Throwables group exposes the general throw-speed multiplier
(`vr_grenadeThrowSpeedScale`): displayed as 0.1–4.0, default 1, step 0.1.
The shared schema converts launcher units to native velocity gain using a factor
of 2.5, preserving the default throw strength and existing profiles in this range.
Native console/config units are 0.25–10.0, default 2.5, step 0.25. The additional football multiplier
remains an advanced runtime setting and is excluded from the frontend schema;
launcher Save and Reset preserve its existing configuration line.

The embedded IE11 view uses a custom DOM scrollbar with wheel/touch scrolling,
thumb dragging, track clicks and keyboard arrows, Page Up/Down, Home/End. The
sidebar supports Up/Down and Home/End. Invalid input reveals its category before
receiving focus. The scrollbar hides when all content fits.

Dropdowns use a shared custom list with arrow-key navigation, Enter/Space to
confirm, Escape to cancel, and dismissal on outside click, scroll or resize.

**Basics > Controller alignment > Preset** controls the three position offsets
and three angles together. **None** sets all six to zero and is the default for
new profiles and Restore all defaults. **Meta Quest 3** applies inward/back/up
of -0.02/0.12/-0.1 meters and pitch/yaw/roll of -20/0/0 degrees, using saved
per-device offsets. These values are a starting point, not a measured fit for
every Quest 3 user. Manual values that differ from both presets display
**Custom**; selecting Custom preserves the current draft for editing.

The preset is inferred from the six numeric values on load, save and manual
input, so existing profiles retain their calibration and console edits are
recognized. Numerically identical input such as -20.0 still matches. Only the
six dvars are saved; there is no separate preset identifier or migration. Preset
selection leaves other unsaved settings and advanced wrist-pivot calibration
unchanged. Save or Launch applies the draft through the existing settings path.

**Gameplay > Recoil** raises the tracked muzzle after each confirmed shot using
the weapon's native gun-kick pitch velocity. Signed and descending native ranges
are valid; the expected absolute velocity is converted over a 50 ms impulse
with a posture multiplier: standing 3x, crouched 2x, prone 1x. Each shot samples
actual native movement posture. Cumulative climb is capped at 55 degrees;
the single-hand penalty adds a further 4x impulse when applicable, except while prone.
The muzzle settles back over time while
automatic fire accumulates climb. **Cheats > No recoil** defaults off and disables
muzzle recoil when enabled. Its checkbox inversely maps to the existing
`vr_recoil` dvar (`true` means normal recoil), so saved profiles keep their behavior.
The recoil switch is absent from the first-use guide; **Single-hand penalty**
remains in both the guide and Gameplay and applies 4 times the normal impulse when
the firing gun has no support grip and the player is standing or crouched. Choices are **All weapons / Long weapons /
Off**; the default is **Long weapons**. Long weapons include native rifle,
sniper, machine gun, submachine gun, shotgun, grenade-launcher and rocket-launcher classes.
TMP, Mini Uzi and PP2000 (including variants) are explicitly short weapons and
skip the long-only penalty; All weapons applies the 4x penalty to them while standing or crouched.
Both options save with the other VR settings.
The firing wrist and fingers pitch with the receiver about the tracked wrist
position. An attached support hand follows the foregrip with arm IK; free hands
and the other firing hand keep their own pose. Raw controller input is unchanged.

Weapon-specific recoil tuning is applied after the posture multiplier and before
the single-hand penalty. See the [recoil developer guide](vr-weapon-recoil.md)
for the current weapon table, native names, calculation, integration boundaries,
and regression tests.

| Control | Saved dvar | Default | Range |
| --- | --- | --- | --- |
| Turn style | `vr_turnMode` | `smooth` | `smooth`, `snap` |
| Smooth speed | `vr_turnSpeed` | 90 degrees/second | 15 to 360 |
| Snap angle | `vr_snapAngle` | 30 degrees | 5 to 90 |
| VR aim assist strength | `vr_aimAssistStrength` | 0 (off) | 0 to 100 |
| No recoil (Cheats; inverted checkbox) | `vr_recoil` | true (checkbox off) | boolean |
| Single-hand penalty | `vr_recoilPenalty` | `long` | `all`, `long`, `off` |
| Invincibility mode | `vr_cheatHealth` | `off` | `off`, `demigod`, `god` |
| Invisibility mode | `vr_cheatNotarget` | `off` | `off`, `on` |
| Infinite ammo mode | `vr_cheatAmmo` | `off` | `off`, `reserve`, `infinite` |
| Disable lens flares in VR | `vr_disableLensFlare` | false | boolean |
| Movement camera bob | `vr_cameraBob` | true | boolean |
| Live stream preview mode | `vr_recordingMode` | false | boolean |
| Preview exterior dimming | `vr_recordingDim` | 65% | 0–100%, step 1 |
| Detailed view diagnostics | `vr_debugViewProbes` | false | boolean; restart required |
| Automatic diagnostic reports | `vr_debugAutoSnapshots` | false | boolean; restart required |
| CPU performance capture | `vr_debugPerfCapture` | false | boolean; restart required |
| Scene surface diagnostics | `vr_debugSceneModels` | false | boolean; restart required; records only during an explicit capture |
| Menu input recording | `vr_debugMenuInput` | false | boolean; restart required; bounded startup/recovery CSV windows |
| Weapon event recording | `vr_debugWeaponEvents` | false | boolean; restart required; native reload, closed-bolt and equipment observations |
| Vehicle steering recording | `vr_debugVehicle` | false | boolean; restart required; recent snowmobile input history |
| Magazine well geometry | `vr_reloadWellDebug` | false | boolean; restart required |
| Handle slap geometry | `vr_hkSlapDebug` | false | boolean; restart required |
| Bolt and handle geometry | `vr_boltDebug` | false | boolean; restart required |
| Feed cover geometry | `vr_coverPushDebug` | false | boolean; restart required |
| Hand inward | `vr_handOffsetInward` | 0 m | -0.5 to 0.5 m |
| Hand backward | `vr_handOffsetBack` | 0 m | -0.5 to 0.5 m |
| Hand upward | `vr_handOffsetUp` | 0 m | -0.5 to 0.5 m |
| Hand pitch | `vr_handAnglePitch` | 0 degrees | -180 to 180 degrees |
| Hand yaw | `vr_handAngleYaw` | 0 degrees | -180 to 180 degrees |
| Hand roll | `vr_handAngleRoll` | 0 degrees | -180 to 180 degrees |

Live stream preview mode and its exterior dimming are under Other. It marks the desktop
capture angle in both eyes with a white frame, dims the exterior and displays
the localized caption below the frame. It follows the game language independently
of the launcher's language. The caption tells players that the preview can be
disabled from the launcher's VR settings page. The actual
desktop capture stays clean. Save and Launch use the existing saved dvar path;
turning it off removes all three guide elements. Dimming defaults to 65%; 0% keeps
the exterior brightness and 100% hides exterior scenery completely. The left-eye
guide maps the right-eye capture's angular bounds into its own projection, with
normal parallax for nearby objects. Existing console-saved values are loaded,
and Restore defaults turns the draft off and resets dimming to 65%. See
[recording-frame rendering](vr-runtime-rendering.md#recording-frame-in-both-eyes)
for clipping and composition guarantees.

Movement camera bob is under Basics > Visual comfort. It covers armed and
unarmed walking, sprinting, crouch movement and crawling; headset tracking is
independent. See [VR movement camera bob](vr-camera-bob.md) for native contracts
and headset acceptance checks.

Offsets translate the complete wrist frame, including its rotation center,
instead of changing the physical controller-to-wrist lever on every adjustment.
The physical baseline still rotates with the grip. The alignment difference is
expressed in the recentered player reference using the runtime raw-grip/aim
relation; it does not follow wrist rotation or HMD looking direction. Inward
remains mirrored per hand. This preserves the former placement when runtime aim
is aligned with the player reference, while changing other orientations so the
alignment difference no longer sweeps an arc. Shared definitions live in
`src/client/component/vr/settings.hpp`.

Advanced saved console controls `vr_wristPivotInward`, `vr_wristPivotBack`, and
`vr_wristPivotUp` describe the physical grip-local baseline (defaults
-0.02/0.12/0 m, range -0.5 to 0.5 m). They are separate from the launcher
alignment controls and are preserved by launcher saves. These offsets are a
configurable reference baseline, not an anatomical measurement or a universal
controller profile.
Existing saved pivot values are not overwritten by new defaults or presets;
testing the zero-Up correction on an existing profile requires explicitly setting
`vr_wristPivotUp 0`. Other devices can adjust it independently.

The three angle controls also live under **Basics > Controller alignment**.
They rotate both hands and their held weapons about the controller's local axes:
positive pitch tilts up, positive yaw turns left, positive roll tilts the right
side down. Negative pitch lowers the aim; zero on all three restores the runtime
aim orientation. Angle calibration does not change the position correction.
The initial -4-degree pitch trial was superseded by the None and Meta Quest 3
presets. Console commands use the same names, for example
`vr_handAnglePitch -20`, and take effect on the next input frame.
Live changes reset melee motion history for 200 ms so calibration cannot be
mistaken for a strike; the calibrated model remains visible and held.

Save writes the existing `players2/h2-mod/config.cfg` relative to the game
directory. Before the first profile initialization, the launcher reads the same
default/Battle.net source profile used by the game's existing initialization.
There is no separate launcher settings file or runtime override loop. The game
loads these saved dvars normally; console changes saved by the game can be read
on the next launcher run.

Save re-reads and merges only the shared settings registry, preserves other
configuration and bindings, and uses the existing atomic file writer. Input is
validated at both the HTML and C++ boundaries. Missing settings use the shared
defaults; invalid saved values are ignored. Reads are bounded to 4 MiB and bridge
payloads to 4 KiB. Errors remain visible and do not report a successful save.

Restore defaults changes the draft. Save commits it; Launch also saves a dirty
draft and refuses to launch if validation or persistence fails. Close without
saving discards the draft.

Debug options are read from the same bounded profile parser once, after the
launcher closes and before the game binary is loaded. Direct `-singleplayer`
starts use that same path. The loaded selection is immutable for the process:
console edits and `vr_reinit` cannot install or remove probes mid-frame.
`vr_status` reports the actual selection as `debug_loaded`, independently of
later dvar edits. Missing or malformed values leave probes off.

The Debug page also shows the running executable's build configuration. A
`Debug` build remains unoptimized and retains compiler debugging checks even
with every probe off. Use `RelWithDebInfo` for gameplay/performance comparisons:
it optimizes for speed, uses the release CRT and disables JMC/RTC instrumentation
while retaining PDB symbols. This is build information, not a saved setting;
the launcher cannot change compiler optimization in an already built executable.

Detailed view diagnostics enables frontend call stacks, view/global memory
hashes, initializer byte differences, the view event ring, optional parent-PC
capture and artifact/code snapshots. Automatic reports enables the watchdog's
periodic full-status/live-trace/artifact persistence; its existing terminal
checkpoint policy still applies. Startup, manual status, crash and stall
reports remain available independently. CPU performance capture installs the
four scene-job observation wrappers and creates the bounded recorder's worker;
recording still starts explicitly with `vr_perfStart` and stops with
`vr_perfStop`. If unloaded, `vr_perfStart` explains how to enable it next launch.

The renderer's required stereo transaction tracking, hook validation, bootstrap
execution/readback proof and eye-resource isolation are independent of Debug.
GPU timestamp queries remain hard-disabled in the client. See the
[rendering performance review](vr-render-performance-review.md) for findings
and the hardware comparison procedure.

The lens-flare option suppresses draws in the verified native VR flare hook.
Native geometry allocation and bookkeeping remain intact, and non-stereo draws
retain their previous behavior. This does not disable unrelated bloom, lighting
or every possible authored effect. `vr_flare_status` includes `disabled_draws`.

Aim assist strength maps linearly to the maximum angle from the VR barrel:
`0` disables it, `50` selects within 5 degrees, and `100` selects within 10 degrees
(cone half-angle). The console command `vr_aimAssistStrength <0-100>` takes effect
on the next admitted controller-muzzle shot and is saved through the normal game
config. The launcher edits that same value in **Settings > Gameplay > Aiming**.

Selection uses the native enemy AI set and native shoot-at point, with the
verified zodiac distance ceiling of 1300 game units and a muzzle-to-target cover
trace. It changes the bullet direction; it does not rotate the tracked head or
hand model. Native ammunition, pellet spread, penetration and damage remain in
the existing firing path. Scripted vehicle shots and NPC shots use their own
paths. `vr_fire_status` reports strength, angle, applied/no-target/error counts.
Zero performs no target or visibility queries. See
[controller firing](vr-controller-interaction.md#holding-authority-and-firing).

Validation:

- Generate with `tools/premake5 vs2022 --with-vr-tests`; build and run
  `vr-launcher-settings-tests` for config preservation, round trips, duplicates,
  malformed inputs and numeric boundaries.
- Run `npm --prefix src/launcher-ui test` for numeric drafts, unit conversion,
  localization, help search and message lifetime behavior.
- Run `python tools/verify_launcher.py --client <built-client.exe>` for the
  actual embedded WebView2 bridge and both launcher fonts.
- Real HMD acceptance still requires checking both turning modes, wrist
  alignment and flare visibility in-game with the compiled client.
