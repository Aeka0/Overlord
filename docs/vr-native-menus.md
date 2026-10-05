# Native menus and 2D presentation in VR

## Loose UI modules and product identity

LUI startup resolves duplicate `ui_scripts/<module>/__init__.lua` entries before
executing any initializer. The highest-priority filesystem root owns the whole
module; cached modules remain available only when unshadowed. The selected path
also remains the base for its relative `require` calls. Discovery is fresh for
each VM startup, and surviving modules retain the existing base-to-mod order.
This allows a complete local package to coexist with AppData without registering
menu buttons and wrappers twice. The campaign buttons wrapper always restores
`InitScrollingList`, including when native construction raises an error.

The existing `Engine.Localize` bridge also owns a narrow allowlist of product
titles, product descriptions and project URLs. It corrects inherited fastfile
values at display time without rewriting credits, dialogue, documents or
internal asset identifiers. Project links resolve to H2-MOD VR; the legacy
donation action is labeled as a project page. Contributor links and the named
external Spec Ops mod retain their original targets.

`ui-script-modules-tests` reproduces the duplicate scrollbar with the production
Lua scripts, verifies the selected overlay opens cleanly, checks repeated menu
opens and fresh VMs, and exercises cleanup after construction failure. These
tests do not substitute for a live menu/headset acceptance pass.

Native draw ownership, menu capture, OpenVR presentation, and native input
routing use shared adapters. Controller-ray interaction is the primary
selection method, with stick navigation as a secondary method. The active menu
keeps its distance while popup ancestors move backward and return when their
child closes. The behavior is a project interaction contract; it does not claim
to reproduce another game's internal implementation. Offline verification does
not establish headset acceptance.

## Current candidate and controls

- [Native menu adapter](../src/client/component/vr/native_menu.cpp) observes both
  native roots on the LUI owner, validates userdata handles/generations, and tags
  each element's original draw command range at the verified render callback.
  It does not clone the LUI tree, replay its dispatcher or replace native actions.
  The native cache-hit function is also observed: a successful hit emits the
  cached element's commands and skips its original render callback. Both paths
  use the same ownership recorder; native caching and per-frame video texture
  selection remain intact.
  Presentation groups start at the verified `main_lockout`, `main_campaign` and
  `sp_pause_menu` roots. Closed entry-screen history is not an ancestor surface
  behind the campaign menu. Only native `isPopup` entries retain and move their
  parents backward; ordinary pages replace the visible group at active depth.
- [HUD capture](../src/client/component/vr/native_hud_capture.cpp) reuses the
  existing draw observer for immutable quad/text/2D-line menu images. The native arena
  reset boundary clears provenance; fingerprints and bounded lifetimes reject
  reused command data. Sorted ownership ranges keep lookup logarithmic.
  Capture brackets both the per-view HUD dispatcher and the native inlined
  global 2D loop. The latter is the observed frontend route; it keeps its
  original dispatch and tessellation flush, with no command replay.
  Arena membership uses the recorded extent, not a guessed address window.
  Selection stops at the current stream's native terminator; an unowned stream
  cannot publish an empty menu image over a valid capture. Opcode 25 line batches
  may span several LUI callbacks: only size/count are mutable, while payload and
  mode retain fingerprint checks. A complete batch must have one surface owner.
- [OpenVR presenter](../src/client/component/vr/menu_overlay.cpp) uses curved
  frontend surfaces and flat in-game surfaces. The background stays behind the
  menu stack with matching angular coverage; active generic popup backdrops and
  the native cursor have separate surfaces. Only ancestors retain old ink.
  Native hidden/destroyed parents return visually before fresh native output and
  settled depth permit interaction. Overlay properties update only when changed.
  Display availability follows the valid compositor/head pose. Controller input
  availability separately gates interaction and must not hide menu images.
- [Input policy](../src/client/component/vr/menu_surface.hpp) neutral-arms across
  stack, tracking, binding and hand changes. The existing
  [input adapter](../src/client/component/input.cpp) delivers absolute native
  pointer events and owned key transitions without moving the Windows cursor.
  Physical key ownership and developer-GUI/console capture remain authoritative.
- Frontend tracking continues at the existing Present owner without requiring
  a gameplay eye pair. Paused-menu pose/input refresh does not overwrite a
  pending stereo view family. Resizing invalidates menu images/input separately
  from native stereo ownership; device/VM changes invalidate the session.

The frontend explicitly enters a dark compositor backdrop using the foreground
fade/grid transition demonstrated in
[Valve's loading-overlay sample](https://github.com/ValveSoftware/openvr/blob/master/samples/unity_keyboard_sample/Assets/SteamVR/Scripts/SteamVR_LoadLevel.cs).
The foreground menu/background overlays remain separate. The transition is
removed only after a new accepted native stereo pair, or during shutdown, so a
menu-to-level transition does not expose a retained old scene. The explicit
frontend predicate, not a missing gameplay frame, selects this behavior.

In-game panels sit 0.5 m in front of the anchor, with a 0.9 m canvas
width. Their output texture is fixed at 1920x1080 (16:9), but their native layout
still depends on the desktop window. This is **not independent 16:9 layout**.
Completed native ink, cursor and mask are fitted proportionally
into that canvas with transparent padding; native layout, shader coordinates,
desktop resolution and desktop pixels remain unchanged. The original extent and
fit transform travel with each immutable frame, so ray hits map back to native
mouse coordinates and padding cannot accept clicks. Pause blur/darken subtrees and in-game popup
backdrops are excluded from the color panel. The shared `h1_ui_bg_vignette` is
routed into a separate layer. Its original vignette color stays visible below
the active menu and only darkens it. Blur coverage comes from the entire active
menu content rectangle, including its center and excluding any transparent
padding used to fit the native frame into the output texture. Blur is not forced
to the output texture's 16:9 ratio. Neither vignette alpha nor the presence
of a vignette texture controls the blur. The existing per-eye renderer copies
each current linear eye, builds its mip chain, and applies a prefiltered 5x5
binomial blur across that rectangle with 14-pixel tap spacing (bounded to 24).
The active menu's immutable canvas supplies geometry and freshness. Pixels
outside the rectangle remain unchanged. Partly visible menu planes preserve
homogeneous coordinates for GPU clipping instead of disappearing entirely on
a large head turn; fully behind-head planes remain rejected. Native reverse-Z
depth keeps nearer objects sharp when the eye supplies a depth view. Clear menu
ink remains in the OpenVR overlay; frontend cinematic/background ink is unchanged.

Entry screens and movies route either Trigger's held state to native Enter on
the LUI owner. This path is independent of ray hits and image readiness, and
neutral-arms on context, tracking-reference, binding, continuity or focus changes.
The briefing's own 1.5-second hold timer and permission checks remain authoritative.
The modal `LuiBriefingMenu` explicitly owns confirmation and movie presentation
even while the old map remains initialized. Its menu-stack entry must not
redirect Trigger to mouse clicks or retain the old game/menu presentation.
Only its two `PLATFORM_HOLD_TO_SKIP` localization keys receive the centralized VR
wording, which includes Trigger and Enter; flat mode keeps native localization.

Input diagnostics automatically preserve the first 30 seconds of enabled menu
input and a separate 30-second recovery window after at least 500 ms without
fresh focused input. Each window is bounded to 512 rows at 10 Hz. Async writes
replace `minidumps/vr-menu-input-startup.csv` and
`minidumps/vr-menu-input-recovery.csv`; the LUI thread performs no file IO.
Rows contain native input blocks, sample age, pose validity, pointer readiness,
overlay errors, trigger generations/edges, desktop mouse polls/moves/suppressed
polls, keyboard events and the last routing stage. A stopped
headset is recorded as unavailable input, not assumed to be an input defect.

Native pointer polling is not itself desktop input intent. The native absolute
mouse baseline is tracked separately from VR-delivered UI coordinates. Repeated
identical positions do not increment the physical-activity counter; while a VR
ray owns the UI cursor, those polls also bypass native delivery so they cannot
snap the cursor back. Real desktop movement immediately releases VR pointer
ownership and follows the original handler. A physical click or wheel restores
the last native desktop position before its button event, even without motion.
Flat mode and UI/focus exit retain
ordinary native delivery. Arbitration checks deliberate controller input before
desktop handoff and does not repeatedly re-arm navigation while desktop input
already owns it. This removes the startup starvation that previously cleared
after Alt+Tab stopped the native foreground mouse polling.

World dimming follows native `cl_paused` and `lui_pausemenu`, independent of the
current page instance. Closing the pause root to open achievements/options must
not brighten the world. The existing compositor fade owner holds the measured
native pause darkness, below menu overlays, until pause ends; it clears on focus
loss and shutdown and shares the owner with the frontend theater transition.
Excluded native menu fades retain command ownership, preventing the narrative
capture from reclassifying them as story fades and applying dimming a second time.
The original 120-byte rotated-UV compass quad is captured through its native
handler, so the trainer minimap does not reject the rest of the pause menu/mask.

The right controller owns the pointer initially. Press the other controller's
Trigger deliberately to transfer ownership; that edge transfers rather than
clicking a menu item, and a neutral release rearms selection. Leaving the panel
or losing tracking never transfers ownership automatically. Point the owning
controller at the active panel and press Trigger to click/drag.
Left stick navigation is secondary; after a stick step, Trigger or the pointing
hand's primary button confirms the native selection. Its secondary button goes
Back. Intentional ray movement returns to pointer control; small jitter does not.
Changing the owning hand cancels any old-hand drag before updating the pointer.

The new rebindable `menu_toggle` action sends native Escape (pause/menu Back).
Touch and Vive defaults use the left application-menu button. Index has no new
default assignment; bind this action in SteamVR or use native Escape, preserving
its system button and existing gameplay bindings. Custom existing SteamVR
bindings may also need the new action assigned. No system/dashboard button is
intercepted.

Touch X now binds the separate `menu_recenter` action: release a short press for
native Escape (pause/Back), or hold for one second to invoke `vr_recenter` once.
Long release never also pauses. Focus, binding, menu-context or tracking-history
changes require a neutral release before rearming. Recenter also invalidates the
menu anchor session, so paused panels relocate without waiting for gameplay to
resume. Touch A retains right-primary behavior; left-primary/HUD remains available
for custom rebinding but no longer shares X. Existing custom SteamVR bindings
must select this new action to obtain the new behavior.

`vr_menu_status` reports hook/request state, frontend/video state, visible menu
count, session/revision, command ownership rejections, image layers, pointer
readiness/hit and overlay API errors. `vr_hud_capture_status` adds unsupported
blend/depth/target counters plus menu command matches, copied draws, image layers
and empty capture passes. A rejected or missing active image cannot accept VR
clicks. Window resizing and native menu transitions require fresh publications.
`vr_menu_blur_status` reports spatial-mask pair composition, skipped frames,
failures and the most recent reason. CPU reconstruction and WARP tests do not
substitute for checking alignment/strength in the headset.

This candidate covers the captured frontend menu video through its original LUI
background, and the common native menu stack including pause/popups. A separate
loading-movie route probes native playback/scene state at Present without the
LUI VM. It keeps compositor pose sampling alive, copies only positively identified
2D movie frames into a bounded overlay texture pool, and ignores swap-chain alpha.
Gameplay continues to use native stereo; movie exit waits for a fresh accepted
eye pair before retiring the theater backdrop. Live movie acceptance is pending.
An initialized scene can also play a video as a world material, as with Gulag's
monitor feed. Video playback alone never owns the whole display. A fresh native
briefing menu or the original `cg_cinematicFullscreen` draw gate must claim
fullscreen presentation over a scene; previous theater state cannot retain it.
The gate is combined with actual playback, never used as evidence that a video
is playing. Playback mirrors the two predicates used by `Engine.IsVideoPlaying`:
started, or a named video without an unacknowledged replacement/stop request.
Both the Present probe and LUI snapshot use this shared predicate.
Trainer difficulty and No Russian use the common stack route but have not been
exercised with this candidate. Native clipping, effects, alpha/color, curvature
and pointer mapping require in-game/HMD validation. Unsupported draw/blend
categories reject capture instead of being called accepted VR output.

The existing capture path processes the unfiltered, unflagged native UI pass;
special filtered/flagged effects remain a live verification boundary. Offline
suites do not establish their visual coverage. The
ray/curve intersection used for interaction is OpenVR's own intersection for
the displayed overlay; no parallel custom cylinder-intersection implementation
or automatic alternate transport is installed.

## Presentation contract

### Fixed 16:9 native fidelity

The current presentation keeps native window layout, fits blur to its actual content
rectangle, retains depth history only for native popups, and enlarges the frontend
dark layer by 25 percent while preserving video/menu dimensions. Dark layers
sit 5 cm behind the displayed active menu, following its animated depth. Curved
dark layers use a correspondingly larger radius to maintain their angular
coverage and avoid intersections between overlapping overlay meshes. A second
independently rendered layout is not implemented.

The pause menu retains its original native elements, materials, fonts,
localization, spacing, decorative effects, animations, and actions. The VR path
uses the original LUI rendering and does not reconstruct the menu from strings
or button callbacks.

The VR menu preserves the original **16:9 appearance and behavior**. Use the
native 1920x1080 output as the reference for the fixed VR canvas. Supporting different
resolutions, aspect ratios, responsive reflow or stretching policies in the VR
menu is outside scope. Non-16:9 samples are diagnostic evidence about the current
desktop dependency, not additional visual targets or acceptance requirements.
The earlier requirement to leave desktop presentation unaffected still applies;
it does not require building a general-purpose multi-resolution VR layout engine.
Desktop aspect ratio may differ from VR;
requiring a 16:9 desktop is not an acceptable substitute.

Independent layout means that the VR view uses that fixed native 16:9 context
while the desktop retains its own native layout.
Changing the output texture, stretching desktop pixels, cropping a nominal
safe area, or deleting unsupported decoration does not implement that contract.
The current window-dependent capture does not provide a fixed VR canvas
independent of desktop layout.

Native menu trees contain dynamic descendants and decorations beyond the root rectangle. Their layout depends on viewport and animation state. The native layout walker can advance animations or invoke pointer callbacks, so it must have one owner and run once. Cached draw commands are tied to their source element and canvas; do not reuse them across different geometry.

The current implementation preserves one native update and the existing 16:9 menu context. An independent VR layout would require separate geometry, cache, viewport, and pointer ownership while leaving desktop presentation unchanged. That mode is not part of the current runtime contract. Input and presentation must use the same menu instance and generation. Open/close operations, animation completion, save/quit, and resume remain native operations and must run once.

| Native content | VR presentation | Native authority |
| --- | --- | --- |
| 2D frontend/main menu and its submenus | One curved theater screen in tracking space | Existing menu tree, focus, layout, animation and actions |
| Fullscreen prerecorded video/briefing | The same curved screen, preserving source aspect and letterboxing | Playback, audio, subtitles and permitted skip behavior |
| In-game pause and its submenus | A spatial menu panel over the stereo world | Pause state and menu stack |
| The Pit difficulty selection | A spatial menu panel over the stereo world | Difficulty selection and original script responses |
| No Russian content warning opened during gameplay | A spatial menu panel over the stereo world | Original warning, choice, confirmation and mission flow |
| In-engine scripted scenes | Existing stereo rendering | Existing scripted camera/control policies |
| Noninteractive subtitles, tutorial text and ordinary HUD | Their existing dedicated VR paths | Existing native producers |

LUI is a UI framework, not a modal-state classifier. Weapon widgets, story HUD
and interactive menus must not all become the same panel. A content warning
opened in the frontend belongs to the frontend screen; the gameplay version
belongs to a spatial panel. Do not identify either version by translated text.

Theater and spatial menus share source-canvas metadata, anchoring, focus and
input routing. Geometry and background treatment vary by presentation mode.
Menu layers share one stable anchor and have separate render publications and
depth offsets derived from the native stack. Native popup ordering and clipping
remain authoritative; layers do not become independently movable windows.

### Fixed active distance and receding ancestors

Define `D` as the active menu's depth from the session anchor and `gap` as the
spacing between visible levels. The top level uses `D`; an ancestor `k` levels
behind it uses `D + k * gap`. Opening a child introduces it at `D` and moves the
parent backward. Closing it returns the parent to `D`. A third level repeats the
same rule; the active panel never advances toward the player with stack depth.

For example, `D = 0.5 m`, `gap = 0.25 m` gives the following resting positions.
These are proposed tuning values, not measured Alyx values or headset acceptance.

| Stack | Active layer | Parent | Grandparent |
| --- | --- | --- | --- |
| Pause | Pause: 0.5 m | - | - |
| Pause -> confirmation | Confirmation: 0.5 m | Pause: 0.75 m | - |
| Pause -> submenu -> confirmation | Confirmation: 0.5 m | Submenu: 0.75 m | Pause: 1 m |
| Confirmation closes | Submenu: 0.5 m | Pause: 0.75 m | - |

Depth is relative to the retained session anchor, not a continuously head-locked
distance servo. Keep panel dimensions stable during ancestor motion, allowing
perspective to convey recession. Do not enlarge a small confirmation dialog to
fill the entire menu canvas. A crop/atlas optimization must retain its original
layout-to-surface transform and inverse input mapping.

Only the active layer receives ray, stick, Confirm or Back. Rays outside its
content or through its transparent area must not click the parent. Back requests
one native menu close and restores the native parent's selection; it does not
close every level or synthesize an unpause. A parent can become interactive only
after native focus is restored, it reaches `D`, and held input is rearmed.
Retarget interrupted animations from current poses; never enqueue an unbounded
chain of depth movements. Closing the whole menu cancels all pending transitions.

Ordinary submenu pushes, modal pushes, top replacement and stack reset must be
different transitions. Derive depth from the current presentation stack each
time instead of incrementing a global depth counter. Bound visible history and
texture memory; older ancestors can leave the visible range while the native
logical stack remains intact. The active layer and immediate parent must remain
available, and hidden history must never become a clickable fallback.

Proposed first headset-tuning values are a 3 m theater radius with a 90-degree
horizontal arc, and a flat in-game panel 0.5 m away spanning 60 degrees. These are
starting values, not accepted comfort settings. For a cylinder, use arc length
and source aspect consistently; do not mix chord width with texture width.
Preserve the complete source canvas and its safe area rather than stretching
it to the headset or desktop window aspect.

Anchor at the current head position and horizontal heading when opening a
surface, then retain that pose in tracking space. Head motion changes the view
of the surface instead of dragging it around. Recenter deliberately relocates
the anchor and invalidates pending input. Look-up/down degeneracy must retain a
valid horizontal heading. Modal panels remain readable through scene geometry;
they are spatial in disparity and pose, without world-depth occlusion.

Keep native text, icons and language-dependent wrapping. A small ray/cursor
needs contrasting light and dark outlines for visibility against either bright
or dark native menus. Pixel mapping uses the source viewport and UI placement,
not Windows desktop DPI or assumed 1920 x 1080 coordinates.

## Validation and acceptance boundary

Offline coverage exercises CPU layout and input policy, the presenter with strict OpenVR doubles and WARP textures, frontend-to-gameplay ownership, GUI input preservation, and spatial panels. PDB feature audits confirm that production menu capture and presentation code is present. These checks do not certify a frame from the live VR game.

Headset acceptance should verify frontend and pause menus, video, readable text and cursor placement, popup depth, click-through prevention, input focus, level transitions, and device/recenter recovery. Test controller rays and stick navigation separately.

## Confirmed integration boundaries

- [Runtime selection](../src/client/component/vr/runtime_backend.cpp) selects
  SteamVR/OpenVR. Adding an OpenXR cylinder layer alone would not reach the
  production backend.
- [OpenVR capture](../src/client/component/vr/openvr_runtime.cpp) accepts native
  stereo eyes for gameplay and explicit overlay images for menus. `capture_present` is intentionally empty, and
  `capture_engine_texture` requires a native stereo pair identity. Frontend
  presentation uses its own explicit source mode; it cannot be triggered just
  because a stereo frame is late or missing.
- [The desktop mirror](../src/client/component/vr/desktop_mirror.cpp) yields
  to native UI when cgame is absent, a catcher is active, or the game is paused.
  Keep the desktop UI usable, and capture before any mirror or developer GUI
  can contaminate a theater source.
- [Native HUD capture](../src/client/component/vr/native_hud_capture.cpp) and
  [spatial panel composition](../src/client/component/vr/spatial_panel_renderer.hpp)
  provide existing native draw observation, texture lifetime and color handling
  to reuse. Their selected HUD categories are not a complete menu capture path.
  A menu can contain clipping, custom draws and scene-dependent blur; capturing
  every LUI quad into an ordinary transparent texture is not proven sufficient.
- [Native input hooks](../src/client/component/input.cpp) already own key,
  character, mouse and LUI key dispatch. Extend the shared adapter there after
  validating the required event semantics, rather than installing competing
  hooks or moving the OS cursor.
- [Controller snapshots](../src/client/component/vr/controller_input.hpp)
  expose physical buttons, aim poses, freshness and continuity. Sampling belongs
  to the existing runtime boundary. Menus consume those publications; they must
  not poll SteamVR independently or depend on gameplay command generation.
- [SteamVR actions](../src/client/component/vr/steamvr_input.cpp) retain one
  `/actions/gameplay` sampling owner and add an optional menu-toggle action.
  Native menu ownership routes the shared physical controls to UI.
- [The Pit](vr-trainer.md#deferred-difficulty-menu) opens
  `difficulty_selection_menu` after `freezecontrols(1)` and `setblur(2, 0.1)`.
  The original script waits for `menuresponse` with `continue` or `tryagain`,
  reopening for other responses. VR must operate that original menu, not send
  guessed responses or set the difficulty directly.

The current rendering contract rejects a desktop image as a replacement for
missing stereo gameplay. An explicitly selected native 2D theater source is a
new presentation mode requested here; it does not relax that gameplay contract.

## State and ownership

Represent the background (`frontend`, `fullscreen_video`, `world`, `transition`)
separately from the interactive top menu and from the input owner. A menu can
appear above a video or a scripted world, and a loading transition may have no
interactive owner. Rendering availability is a separate fact from native state.
Discover the active native root as part of that snapshot: observed frontend menus
use `UIRootFull`, while observed in-game pause uses `UIRoot0`. Neither name alone
is a sufficient mode predicate; both roots can exist simultaneously.

Publish a bounded immutable snapshot containing source kind, active menu/stack
identity, visibility, interaction permission, source viewport/placement,
generation and timestamp. Per-layer snapshots identify the shared anchor,
depth, geometry, source menu instance and exact captured image, distinguishing
live interactive output from retained noninteractive history. Render and hit
testing must agree on those identities. Native hide/close invalidates live input
and live publications; only explicitly retained ancestor history can survive
while its stack entry remains. Clear all layers on session close, VM restart or
level/device change. An invalid capture never becomes active output. Do not
retain pointers into mutable LUI objects.

`cl_paused`, `CL_IsCgameInitialized`, catchers, and frozen-player flags are useful
supporting signals. None alone proves that a particular menu or video is active.
In particular, a script-frozen player is not necessarily paused, and an
initialized cgame does not rule out fullscreen video. Missing eye output never
selects theater mode. Console, developer GUI and SteamVR focus ownership must
suspend VR menu delivery without rewriting native catcher bits.

Use one menu session generation for input cancellation and surface lifetime.
Opening, replacing or closing a modal target releases only keys/buttons that
the VR adapter owns and discards queued input for the former target. Preserve
the existing [native/GUI catcher ownership contract](vr-runtime-rendering.md#desktop-spectator-projection).
Do not write `cl_paused`, force unfreeze, or rebuild the menu to recover a failed
native UI state.

## Ray and stick interaction

Use the controller's runtime aim basis from the shared snapshot for pointing,
independent of weapon barrel/grip transforms. If menu smoothing is needed,
apply the same filtered ray to visible feedback and hit testing. Never display
one ray and click using another.

Only one hand owns the pointer at a time. Explicit pointing/click activity may
transfer ownership while idle; a held click or drag keeps its owner until
release/cancellation. Tracking loss cancels the interaction instead of handing
a held press to the other controller.

Convert the ray/surface intersection to normalized source coordinates, then
through the captured viewport, letterbox and native UI placement transform to
the native pointer domain. Reject misses, back-facing/out-of-range intersections,
nonfinite coordinates and clicks outside the interactive source area. Confirm
the native mouse hook's coordinate units and absolute/relative semantics before
injecting events. Geometry alone does not establish correct native hit testing.

Ray hover updates the native pointer; trigger supplies the native press/release
sequence. Stick directions navigate the native focus graph, with a deadzone,
initial delay and bounded repeat rate. Confirm and Back use the native menu
actions. The most recently intentional modality owns selection: passive ray
jitter must not steal focus after a stick step. Physical mouse activity follows
the same arbitration. Sliders, scrolling, disabled items and nested confirmation
dialogs retain native behavior.

Entering/leaving UI, switching targets, losing focus/tracking, or recentering
requires a fresh release/neutral state before actions rearm. This includes a
trigger held while opening pause, a held stick while returning to gameplay, and
the confirmation press that starts a level/video. Bound event queues; coalesce
motion, preserve owned releases, and cancel safely on overflow. No click,
navigation repeat or skip request may survive into a new menu generation.

Add an explicit, rebindable menu-toggle action through the existing action
manifest/binding infrastructure. Preserve SteamVR's system/dashboard controls.
Audit available buttons per controller before choosing defaults; Vive trackpads
do not supply the same face buttons as Touch/Index controllers. Decide whether
separate menu action sets are necessary from that binding audit, while retaining
one runtime sampling owner. Do not hijack an existing gameplay action globally.
Playback skip must remain a deliberate native-supported action with fresh
input, never an automatic consequence of switching presentation modes.

## Rendering and scheduling direction

The candidate uses an application-owned OpenVR overlay surface: the
[vendored interface](../deps/openvr/headers/openvr.h) and
[Valve's interface specification](https://github.com/ValveSoftware/openvr/blob/master/headers/openvr.h)
provide curvature, absolute tracking-space transforms, mouse scale and ray
intersection. The presenter retains each immutable source lease until SDK
replacement/release and clears overlays before runtime teardown. A failed update
retains its attempted image until acknowledged clear; failed clear/destruction
retains textures until runtime detach. Live texture
updates, blur fidelity, pointer coordinates and pause behavior still need HMD
verification; the SDK doubles do not prove runtime visual acceptance.

The frontend screen must function before the first stereo scene. Native video
decode, audio, subtitle and UI animation timing stay native. Source capture is
GPU-local on the owning D3D11 context, with bounded textures and explicit device
and source generations. Determine the appropriate native completed-2D boundary;
do not blindly sample whatever happens to be on the final desktop backbuffer.

In-game menus capture the native menu tree once and preserve the stereo world
behind it. Scene-dependent dim/blur must affect each eye's own scene in native
order, with menu ink above it. Audit the complete producer/blend chain before
reusing tutorial blur or narrative fade classification. Keep existing subtitles,
weapon HUD and story fades from being captured or composed twice.

The multi-layer presenter needs separate publications for each menu's ink and
for any full-window backdrop effect. A flattened composition cannot be split
reliably by cropping rectangles.
Carry verified menu-root/instance ownership through the native producer and
command-capture boundary. A full-window vignette must not move backward as a
giant opaque sheet attached to a menu. Preserve its ordering and native effect
once at the appropriate composition boundary; avoid multiplying the darkness
by applying it again to every retained layer. Separating actual GPU draws from
these identified LUI subtrees is the next unverified integration seam.

Simulation pause must not freeze head tracking, menu hit testing or pointer
feedback. First measure whether H2 continues Present, UI dispatch and native
scene-owner work while paused and while playing fullscreen video. A compositor
overlay alone does not prove continued input sampling or correct paused-world
rendering. If a needed boundary stops, resolve that owner contract explicitly;
do not substitute a stretched desktop or replay stale eye pairs as fresh views.

Keep native LUI calls on the verified native UI/input owner, GPU work on its
existing context owner, and runtime calls within the established runtime owner.
Exchange short snapshots/events; never hold their locks across native dispatch,
GPU operations or compositor waits. Do not add a second pacing loop competing
with Present/WaitGetPoses. Overlay texture retirement must be established for
that API, not assumed identical to stereo Submit retirement.

Reuse the original LUI tree. Capturing a menu must not clone widgets or allocate
animation states per frame; prior pause investigations exposed finite native
LUI pool limits. Reuse targets at steady size, bound resolution/memory from the
start, and rebuild only at source/device lifecycle transitions. GPU readback and
continuous logging are diagnostic-only, never the interaction path.

## Implementation sequence and acceptance gates

1. Extend the captured pause/confirmation hierarchy and loaded-Lua contracts
   with actual push/pop/replace transition traces, fullscreen-video state,
   per-menu draw ownership, completed 2D rendering and input coordinates.
   Record frontend, trainer and content-warning captures. Confirm No Russian's actual menu producer and
   responses; the localization strings alone do not establish that contract.
   Measure Present, UI, scene and runtime-input cadence in each mode.
2. Add shared surface/session state and the native input adapter, then complete
   in-game pause with ray and stick end to end. Exercise submenus, sliders,
   confirmation dialogs, keyboard/mouse coexistence and return to gameplay.
   Preserve head movement during pause and verify backdrop/color fidelity,
   constant active depth, receding/restored parents, native-hidden ancestor
   snapshots and no click-through during rapid push/pop transitions.
3. Use the same pipeline for The Pit difficulty and the in-game content warning.
   Confirm `continue`, `tryagain`, mission continue/skip/cancel and checkpoint
   reload behavior through native scripts. Do not implement per-menu replicas.
4. Add the explicit frontend/video theater source and curved presenter using
   the shared input/surface contract. Verify startup before cgame, video start,
   completion/skip, loading, gameplay entry and return to the frontend.

Keep pure geometry/state/input policies separate from native adapters and GPU
presentation. Extract only helpers actually shared with existing HUD/input code;
do not move gameplay rules into the runtime or edit upstream reference projects.

Offline coverage should exercise surface/UV round trips, curved edges, unusual
aspects and viewport scales, invalid/stale generations, held-input transitions,
dual-hand arbitration, navigation repeat, focus loss, bounded queues, texture
lifetime and D3D state/color restoration. Stack tests must cover modal and ordinary
pushes, top replacement/reset, destroyed or hidden parent trees, matching saved
image identity, repeated Back, and interrupted depth animation. Source-seam verification must accompany
those tests; synthetic events do not establish a correct native adapter.

Headset acceptance must cover readable Chinese/English menus, bright/dark
backgrounds, curved-screen edge targeting, head translation/rotation, recenter,
pause/resume, native submenus, trainer retry/continue, content-warning choices,
video subtitles/audio/skip, window resize, alt-tab/dashboard, tracking loss,
VM/level/device changes and repeated menu cycles without LUI pool growth.
Record source/build checks, offline tests, and headset acceptance separately.
