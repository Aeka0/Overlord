# VR presentation options

The launcher saves two opt-in booleans, both off by default:

- `vr_hideHud`: Other > Hide all HUD.
- `vr_disableBlur`: Basics > Visual comfort > Disable UI blur.

The same Visual comfort group and quick guide also expose independent Depth of
field (`r_dof_limit`) and Motion blur (`r_mbLimit`) switches. These edit the native
PC graphics quality limits in the existing profile: 0 means Off, and 1–3 mean
enabled quality. Both default to Off when absent. Existing enabled Low/Medium/High
values are retained when saving other settings; enabling an Off effect selects
High (3). Named and hexadecimal-hash assignments are merged without duplicates.
The next game start reads these settings through its normal configuration path;
they affect native graphics in both VR and flat play. No renderer hook or per-frame
dvar write is needed. Reset disables both effects.

For `vr_hideHud` and `vr_disableBlur`, `presentation_options.cpp` publishes the
choices from the main pipeline when `vr_enable` is on. Render consumers use atomic
flags without native dvar lookup, cross-thread waits, or changes to native
configuration. These two options leave flat play unchanged.

## Subtitles

Other's second option is Subtitles (`vr_subtitles`, default On). It saves through
the normal launcher Bool catalog, then the embedded `subtitles.lua` applies it
once per game process when `main_campaign` opens after profile loading. The
native `Engine.SetSubtitlesEnabled(controller, bool)` API updates the same
profile field as Audio's subtitle toggle; `Engine.SubtitlesEnabled` verifies it,
and `updategamerprofile` requests the native save only when the value changed.
Later in-game subtitle edits survive menu returns and Lua VM reloads. This is
independent of Hide all HUD and does not filter titles, fades or mission prompts.

## HUD boundary

Native HUD visibility is intercepted at the eight verified `cg_drawHUD` pointer
reads, including `Game.IsHudEnabled` and the ownerdraw fallback. The adapter
selects an immutable disabled value while hiding; otherwise it reads the actual
native pointer. It never writes `cg_drawHUD`, `hud_drawHUD`, or `cg_draw2D`.
The following native conditional branch is retained, and the bridge preserves
all state except the RAX value that the displaced load normally produces.

The eye compositor omits weapon/vehicle ammunition panels, action status text,
directional warnings and world markers. That choice is frozen across an eye pair.
Narrative capture excludes subtitles, titles, script text and progress displays,
including their blur backdrops, but retains native scene fades. Captured textures
carry their visibility policy so a previously visible text frame cannot reappear
after switching the option. Private Lua ammo sources retire while hidden and are
rebuilt from current owners on restore.

Native menu capture, LUI menu trees, menu input, optical scopes, weapon displays,
physical equipment, scene fades, damage-screen effects, recording guides and
explicit debug diagnostics keep their own presentation policy. Hiding HUD does
not suspend native scripts, ammunition updates, interactions or gameplay.

## UI blur boundary

Disable UI blur skips the per-eye native fullscreen Gaussian pass, weapon HUD blur,
narrative hint backdrops, and spatial menu background blur. Sharp foreground ink,
scene fades and menu input remain intact. It does not change depth of field,
texture filtering, scope optics or authored translucent materials.

With the switch off, the existing source policy still suppresses injury and
pause-menu fullscreen blur and retains other native blur sources. The switch
does not overwrite the native script blur curves or advanced HUD blur settings.

## Verification

The native read sites were checked against the running non-VR H2 image using
query/read access only. A standalone far-relay test executes the HUD bridge and
checks pointer selection, register preservation, flags, and native value retention.
WARP tests compare sharp/blurred pixels and menu-mask bypass; compositor tests
cover HUD exclusion and menu retention. Lua tests cover hiding and restoring
independent ammunition sources. In-game VR/HMD acceptance is separate.
