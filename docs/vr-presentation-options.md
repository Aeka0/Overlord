# VR presentation options

The launcher saves two opt-in booleans, both off by default:

- `vr_hideHud`: Other > Hide all HUD.
- `vr_disableBlur`: Basics > Visual comfort > Disable blur.

`presentation_options.cpp` publishes these choices from the main pipeline when
`vr_enable` is on. Render consumers use atomic flags without native dvar lookup,
cross-thread waits, or changes to native configuration. Flat play is unchanged.

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

## Blur boundary

Disable blur skips the per-eye native fullscreen Gaussian pass, weapon HUD blur,
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
