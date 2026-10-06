# VR runtime and rendering contracts

Native stereo rendering uses OpenXR by default, with OpenVR as an explicitly
selected backup. Game/headset acceptance covers the Meta Quest controller and
SteamVR/OpenXR test environment. Other devices and runtimes need separate
acceptance. Both adapters wait or fail when prerequisites are not met.

## Modules and responsibilities

These files are in the [VR infrastructure directory](../src/client/component/vr/):

| Module | Responsibility |
| --- | --- |
| [vr_component.cpp](../src/client/component/vr/vr_component.cpp) | Settings, commands, D3D11 lifecycle integration, and startup configuration |
| [runtime_backend.cpp](../src/client/component/vr/runtime_backend.cpp) | Backend selection and facade; selection is fixed after construction |
| [runtime.cpp](../src/client/component/vr/runtime.cpp) | Common host forwarding and status; both adapters execute on the actual Present owner, without a competing runtime worker |
| [openxr_runtime.cpp](../src/client/component/vr/openxr_runtime.cpp) | OpenXR session state, prediction, native pair admission/transfer and frame retirement |
| [controller_pose_reference.hpp](../src/client/component/vr/controller_pose_reference.hpp) | Validated rigid grip-reference conversion before shared wrist calibration |
| [steamvr_controller_reference.cpp](../src/client/component/vr/steamvr_controller_reference.cpp) | Startup-only copy of static OpenXR grip component metadata from the selected SteamVR device |
| [openxr_input.cpp](../src/client/component/vr/openxr_input.cpp) | Named controller profile recipes, Actions, predicted grip/aim poses and haptics |
| [openxr_menu.cpp](../src/client/component/vr/openxr_menu.cpp) | Native UI/movie textures, quad/cylinder layers and pointer mapping |
| [native_stereo_source.hpp](../src/client/component/vr/native_stereo_source.hpp) | Copied renderer-owned proof; SDK adapters do not inspect H2 records |
| [texture_blit.cpp](../src/client/component/vr/texture_blit.cpp) | Same-device state-restoring linear/encoded-premultiplied texture transfer |
| [menu_backdrop.cpp](../src/client/component/vr/menu_backdrop.cpp) | Shared immutable native menu backdrop publication |
| [openvr_runtime.cpp](../src/client/component/vr/openvr_runtime.cpp) | OpenVR initialization, Present boundaries, pose sampling, stereo submission, and pair retirement |
| [engine_stereo_renderer.cpp](../src/client/component/vr/engine_stereo_renderer.cpp) | Native hooks, view publication, native owner calls, and backend integration |
| [engine_stereo_binding.cpp](../src/client/component/vr/engine_stereo_binding.cpp) | Publication and claiming of scene and view identities |
| [engine_scene_completion.hpp](../src/client/component/vr/engine_scene_completion.hpp) | Waiting for H2 CPU scene inputs and checking identity |
| [engine_stereo_owner_pass.cpp](../src/client/component/vr/engine_stereo_owner_pass.cpp) | Per-eye transactions, model state, dynamic uploads, and resource scopes |
| [engine_stereo_eye_resources.cpp](../src/client/component/vr/engine_stereo_eye_resources.cpp) | Per-eye resource routing, history isolation, and binding restoration at scope boundaries |
| [native_render_session.cpp](../src/client/component/vr/native_render_session.cpp) | Native eye targets and the pair ownership ring |
| [gameplay/README.md](../src/client/component/vr/gameplay/README.md) | The downstream boundary from input snapshots to gameplay |

## Backend selection, defaults, and controls

The launcher's **VR Settings > Basics > VR backend** choice defaults to OpenXR
and offers OpenVR as a manual backup. It is saved as `vr_runtimeBackend` and
applied after the launcher closes but before loading the game binary or creating
the VR backend. Selecting it in the launcher needs no launcher restart. The
native saved enum retains the preference during subsequent game configuration writes.

For direct `-singleplayer` starts, an explicit `H2V_VR_BACKEND` overrides the
saved preference: `openxr`, `openvr` and the `steamvr` alias are accepted; an
unknown nonempty value fails with a diagnostic. Without that override, the saved
choice applies. Entering through the launcher UI uses the launcher choice even
when an older environment override was inherited. An active backend is immutable
once constructed; there is no automatic switch after initialization or frame failure.

The OpenXR loader selects the registered active runtime or interprets the
optional `XR_RUNTIME_JSON` override. The manifest does not choose the API backend:
a SteamVR OpenXR manifest still uses OpenXR unless OpenVR is explicitly selected.
The public OpenVR client shim remains linked statically for the backup and
startup calibration metadata; see [openvr.lua](../deps/premake/openvr.lua).
Runtime discovery uses installed registrations and has no developer-machine path.

`openxr_loader.dll` is built from the pinned SDK and placed beside both client
configurations. Install and activate the vendor runtime separately. The currently
validated combination uses SteamVR/OpenXR and Meta Quest controllers.
See [installation and manual fallback](client-installation.md#runtime-selection).

Defaults are `vr_enable=1`, `vr_headTracking=1`, and `vr_worldScale=39.3700787`. Saved settings are applied automatically at startup; see [launcher VR settings](vr-launcher-settings.md). After changing these initialization settings during a session, run `vr_reinit`. Use `vr_recenter` for recentering. `vr_status` prints status and attempts to write `minidumps/h2-mod-vr-status-latest.txt`.

`engine_stereo` is the production scene path. The desktop backbuffer is never used to replace missing eye data; both eyes must come from one immutable view family. Texture pixel dimensions do not define the camera's optical aspect ratio; projection uses runtime optical parameters. Full head orientation and special pitch handling are described in [terrain tessellation and head pitch](vr-shared-tessellation-and-head-pitch.md).

## Startup execution evidence

The native owner proof waits for a complete execution census with no unknown
contexts or opaque command lists. Tracking can remain active while this gate is
waiting, so working head/controller input does not establish stereo submission.

Loading/menu canvas composition can overlap the first census frame. The shared
panel renderer marks its own private deferred context and finished conversion
lists. The census accounts for that known recording, state-restoring execution
on the expected immediate context, and any synchronous driver replay separately
from H2's native work. Independent GPU observers still see every call. Untagged
lists, submissions without state restoration, and unknown contexts retain their
original rejection behavior; actual distinct-eye GPU proof is still required.
The markers are resource-lifetime metadata, not cached raw pointer identities.
Status reports the separate `known_conversion(recording/execute/replay)` counts.

## Three distinct completion boundaries

### 1. View publication and scene payload readiness

`engine_stereo_renderer::publish_generator_views` publishes finalized view identity before calling the native draw-surface generator, allowing the concurrent backend to claim the same scene. It does not copy unfinished surface or FX data at that point.

`engine_stereo_owner_pass::begin` calls `engine_scene_completion::await` before cloning. The completion module runs H2's native job-pumping waits in initial -> surfaces -> effects order, checks frontend identity before and after waits, and then checks all four input flags. No eye/GPU scope has been entered at this point. The caller must not wait while holding resources required by the workers it is waiting for.

This preserves two distinct points: publish immutable views early, then read scene contents after native producers complete. Moving publication after the native function returns can miss the backend consumption window. Cloning earlier to reduce waiting can copy unfinished transparency or effect lists.

These flags establish CPU input completion. They are not GPU fences and do not establish that OpenVR has released textures.

### 2. Per-eye owner completion and submission texture reuse

Per-eye transactions save and restore native state while tracking pair, eye, frontend, record, owner thread, and device generation. The left eye executes the arena-owned native record. The private left-eye clone is retained data and must not be presented as an atomic observation at the end of left-eye execution.

Do not treat `$scene` / `$scene_pingpong` as eye indices or generate the second eye by simply repeating `R_RenderScene`. Per-eye execution depends on a complete owner transaction and coordinated isolation of camera state, models, dynamic uploads, targets, and history resources.

Shared terrain tessellation data must cover the visibility range of both eyes; final rendering still uses each eye's optical projection. Expanding the CPU frustum does not establish coverage of GPU-internal culling. See the terrain topic for the specific evidence.

### 3. Submit return and compositor ownership retirement

The current OpenVR path validates non-shared textures from the same H2 D3D11 device: pair and device generations must match, dimensions and format must match, and textures must have one mip, one array slice, one sample, default usage, RT/SRV bindings, no CPU access, and `MiscFlags=0`.

OpenVR queue calls are confined to the actual DXGI Present pre/post transaction. The renderer's `prepare_frame` records and validates renderer ownership; it does not independently call `WaitGetPoses` there. Submitted pairs are retained until the next successful `WaitGetPoses` boundary. A failed call does not prove retirement, so the code retains resources and waits for a later matching boundary.

`DoNotHaveFocus` is recoverable, including before the first Submit. Pose and input publications are invalidated while waiting. If one or both eye submissions lose focus, retain the entire pair until a successful `WaitGetPoses`, then resume the same ring and format candidate. Focus loss does not reject a format; a different hard error in either eye still follows the fatal retirement path.

Automatic initialization is attempted once per valid D3D11 device generation. The attempted generation survives session teardown, so a replacement device can initialize even if a failed Present already cleared the active session before its destroy callback. Missing graphics do not consume an attempt; repeated failure on the same generation requires `vr_reinit` or an enable transition. Disabled or terminally shut down VR does not restart merely because a device changes.

A desktop `ResizeBuffers` request also permits one same-device recovery attempt.
Its callback only queues the device generation; the next real Present-pre detaches
OpenVR before retiring submitted targets, resets temporal history and recreates
the VR session/conversion ring. This discards a pose family whose native H2 targets
were rebuilt during the window change. Consecutive requests before that boundary
coalesce; failure does not retry on every Present. Foreign-generation requests,
disabled VR and terminal shutdown cannot restart the session. No runtime/GPU work
or runtime lock is added to the resize callback, and no recenter is requested.

Historical experiments with a private compositor device, cross-device copying, and shared fences must not be reused as if they were the current design. The same-device, non-shared checks are interface contracts and must not be relaxed merely to admit a candidate texture.

## Desktop spectator projection

Optional head, hand and desktop stabilization is documented in [pose stabilization](vr-stabilization.md).

The desktop mirror samples the completed output-right-eye texture on the existing GUI owner.
It keeps that eye's origin and orientation and crops around its optical forward direction,
using a symmetric projection at the current desktop aspect ratio. Texture dimensions do not
define optical aspect. The existing linear-to-sRGB shader still handles desktop color.

`vr_desktopFov` is the requested horizontal FOV in degrees (default 95, range 30-120,
saved and applied immediately). For example, `vr_desktopFov 80` narrows the view.
The effective FOV is limited by the shorter side of each source frustum axis and the
window aspect ratio. A half-texel inset keeps bilinear sampling inside the source.
Increasing the requested FOV beyond this limit does not reveal more image or stretch it.
`vr_status` reports `requested_hfov`, `effective_hfov`, `fov_limited`, and mirror state.

The submission ring retains projection bounds recovered from the same immutable eye
slot as its image. It never fetches a newer runtime projection for an older image.
Missing or invalid metadata skips the desktop draw with `invalid_projection_or_extent`;
it does not alter HMD publication. Menus and pause continue through the native desktop UI.
The developer GUI acquires/releases input only on its own open/close transitions;
its rendering frames must not clear the shared native menu catcher. Opening and
closing the overlay over an already open menu preserves that menu's input state.
Native catcher add/remove/replace entry points also notify the overlay when the
engine takes ownership after F10 opens. A same-bit native claim cannot be detected
by comparing flag values; closing the overlay must not clear that newer claim.
These observers validate the supported H2 instruction bytes before installation.
This pass adds no scene render, pose smoothing, GPU readback, or runtime call. It does
not move the viewpoint to the head center or guarantee that a weapon is screen-centered.
The current source is a fully rendered rectangular H2 image; a future hidden-area mask
would need its own valid-region constraint beyond rectangular UV bounds.

Offline coverage: `vr-desktop-mirror-tests` checks asymmetric projection, aspect/DPI,
FOV limits, invalid inputs, exact ring metadata lifetime, and WARP color conversion.
In-game acceptance should check forward alignment, straight-edge proportions, aiming,
window resizing, pause/menu return, recentering, and runtime/device recreation.

### Recording frame in both eyes

`vr_recordingMode 1` enables a framing aid in both output eyes; `0` disables
it. The boolean is saved and defaults to off. This is a recording composition
guide, not a video encoder. `vr_recording_status` reports its enabled state,
successful draws, skipped frames and captions skipped for unavailable text/space.

The frame uses the same optical crop as the desktop mirror, following desktop
window aspect and `vr_desktopFov`. A white border with a dark outline remains
visible against light and dark scenery. `vr_recordingDim` controls exterior dimming
in percent (0–100, saved, default 65). Zero preserves exterior brightness; 100
makes exterior scenery fully black while retaining the white border and caption.
Both eye interiors stay unchanged. The left-eye frame maps the right-eye crop's
angular boundaries into its own asymmetric projection, clipped to its field of view;
it approximates capture framing and retains normal parallax for nearby objects. Line thickness
scales with eye render resolution. A centered caption below the frame follows
the game's `loc_language` through the shared `game_text::key::recording_preview`:
the localized live-preview caption for Simplified Chinese, or
“Live stream preview mode - Disable in the launcher's VR Settings” for English.
Other game languages currently use the shared English fallback. Launcher locale
does not select this in-game caption. The launcher exposes the same saved
`vr_recordingMode` boolean and `vr_recordingDim` under **VR Settings > Other**.

The caption uses a bounded, cached UTF-16 system-font texture at the eye's pixel
resolution, with grayscale antialiasing and a dark outline. It is generated only
when the device, translated text or font size changes and joins the existing guide draw. The full
line scales down to fit the available exterior width/height; if the crop leaves
no readable space below it, the caption is omitted instead of entering or changing
the captured picture. Neither Windows desktop DPI nor the native game's font
atlas changes the caption's text or placement.

The GUI owner publishes a small viewport/FOV snapshot. Both eyes share a single
snapshot of viewport, enabled state and dimming for each image family. Their owner
computes the crop from that image's immutable right-eye projection and blends the guide as
the last composition layer, without a scene copy, GPU readback or additional
render loop. Short CPU snapshot locks never span native/GPU work. The guide
publishes its exact crop back to the submission ring only after a successful
draw. Desktop presentation uses that saved crop for the same image, so changing
FOV/aspect between production and presentation cannot sample the guide. The next
produced frame picks up the updated GUI request. Ring reuse and mode-off frames
clear the saved crop; retirement retains it with the image.

The border lies outside an outward-rounded one-texel guard around the recording
rectangle. Every texel in the desktop bilinear sampling footprint is protected,
including fractional crop boundaries. Thus the normal desktop mirror and its
recording do not contain the frame, caption or dimming. A full right-eye/SteamVR capture
will include the guide. If the crop reaches a texture edge, that side of the
border can lie outside the eye image rather than narrowing the recording crop.

CPU/WARP regressions cover fractional/asymmetric crop footprints, landscape,
portrait and ultrawide aspects, resolution-scaled lines, bit-identical protected
pixels, exterior dimming, outlined border, GPU state restoration, right-first
completion and ring metadata lifetime. Headset framing/comfort acceptance is
pending; test window/FOV changes and pause/resume in addition to mode toggling.

## SSR history must match eye, image, and camera generation

A normal H2 owner can run between two VR pairs. The natural, left, and right resources for target91 are stored separately. Per-eye history matrices and images must correspond to the same eye; isolating only the right eye or only the matrices still allows natural-scene history to leak into VR.

Isolation must be in place before the eye's first SSR read. Rebind inherited state when entering an eye and restore natural bindings on completion or cancellation. Preparing resources only when the eye first writes mips at the end of its pass is too late.

Resource rebuilding depends on the underlying texture, descriptor, and generation. Switching to a different mip RTV of the same texture must not reseed history. During steady operation, do not copy history from the natural path or the other eye every frame to hide inconsistencies. Relevant validation targets are `vr-d3d11-eye-resource-isolation-probe`, `vr-engine-ssr-history-probe`, and `vr-engine-ssr-consumer-window-probe`.

## Diagnostics and thread boundaries

`runtime_backend::get_status` does not acquire the facade lock held across OpenVR pacing. The OpenVR implementation reads a published snapshot under a separate `status_mutex_`. Diagnostics must not be serialized behind long Submit or WaitGetPoses calls. Snapshots can differ in sampling time; subtracting cumulative counters from different subsystems does not establish per-frame causality.

Gameplay consumes tracking/input frames published by the runtime rather than polling SteamVR independently. Commands are modified before insertion into H2 command history, not again when prediction reads that history. Expensive diagnostics should require explicit activation, use bounded capacity, and keep file writing independent of rendering producers; see [region capture](vr-region-capture.md).

## Regression requirements

- Publish scenes before consumption and clone only after CPU input completion. Reject stale data when frontend identity or generation changes.
- Keep both eyes in the same family. Do not submit incomplete pairs or fill missing data from the other eye or the desktop.
- Isolate SSR before its first read. Natural-owner writes must not contaminate eye history; different views of the same texture must not trigger repeated seeding; cancellation must restore bindings.
- Do not reuse submitted pairs before a successful retirement boundary. Recentering, level transitions, and device recreation must not mix generations.
- On hardware, inspect binocular parallax, transparency, particles, reflections, terrain edges, and hand stability. Record offline tests and hardware observations separately; see [diagnostics and acceptance](vr-diagnostics-workflow.md).

## OpenXR frame and lifecycle contracts

Read the implementation through `on_present` -> `complete_native` ->
`on_present_post` -> `begin_prediction`. Present-pre finishes the previous
prediction; a matching Present-post opens the next one. Head, grip/aim spaces,
view poses and projection fields use that prediction's display time and LOCAL
space. A partial native eye pair retains one prediction, with a bounded wait
across Presents; it cannot resample tracking or acquire replacement world data.
Frame submission uses the original SDK poses/FOV and retires the native pair
after its same-queue texture transfer. Runtime waits never hold the GPU queue scope.

The game D3D11 device binds the session after adapter-LUID and feature-level
validation. Native admission needs copied content proof for the current device,
context, owner thread and dimensions. The native ring converts to linear
RGBA16F; a state-restoring blit transfers it to the supported XR target format.
Menu capture is encoded premultiplied color and is decoded before linear
composition. Fullscreen video requires the existing positive native playback
owner; an arbitrary desktop frame cannot replace missing stereo. The menu
uses a cylinder when supported and a quad otherwise, reported by `vr_status`.

The existing wrist lever is calibrated against the OpenVR device origin. For
SteamVR/OpenXR, the native host copies the selected controller model's static
`openxr_grip` component and applies its inverse to the SDK grip pose before
shared calibration/stabilization. No controller-specific numeric preset or
user-setting rewrite is used. SDK aim remains unchanged for firing and menu
pointers. `vr_status` reports `controller_grip_reference`, both reference identities
(selected SteamVR models or canonical Touch frame identifiers) and errors. Missing, ambiguous or invalid reference metadata rejects
the affected gameplay grip instead of applying the wrong lever.

The host's short Background query completes and shuts down before loading
OpenXR. It runs only after the existing SteamVR server passes user/session/IPC
preflight. It can copy the actual HMD driver and remote-client identity alongside
the controller metadata, without compositor, pose or GPU calls. OpenVR does not
participate in OpenXR frame pacing or submission. Standalone SDK tests inject
copied metadata and do not query a live runtime. After replacing SteamVR
controller hardware, run `vr_reinit` to refresh its static reference.

Runtime selection is separate from the user's OpenXR/OpenVR API choice.
`openxr_startup.hpp` keeps the decision independent from live discovery. Explicit
`XR_RUNTIME_JSON` wins. A connected HMD with actual driver `vrlink`
can select SteamVR/OpenXR, using OpenVR's runtime
path registry to discover the manifest. An already configured SteamVR runtime
needs no override. VD can use both VDXR and SteamVR, so VD and ambiguous/missing
connection evidence preserve the configured runtime instead of guessing from
process names. OpenVR continues to use SteamVR without a transport whitelist.
`Prop_SteamRemoteClientID_Uint64` is optional diagnostic metadata: the status
records its property error separately from whether the returned ID is nonzero.
A missing or zero ID does not negate a connected HMD's actual driver identity.

Automatic overrides are scoped to initialization, applied before the loader
is used and restored on success or failure. Every `vr_reinit` queries fresh
evidence after retiring the previous OpenXR objects. The system registry is
never modified, and explicit environment overrides survive cleanup. If the
chosen manifest disappears, initialization reports that failure instead of
silently loading another provider. Elevated processes cannot use the loader's
environment override mechanism and receive a specific error when an override
is required. Manifest/provider inspection and actual runtime/device admission
remain separate; only the runtime can confirm that a headset is available.

`vr_status` retains `runtime_override`, `runtime_override_manifest`,
`runtime_manifest`, `runtime_library` and `runtime_selection` through initialization
failure. `automatic_steam_link_vrlink` identifies automatic selection;
`existing_environment` and `system_active_runtime` identify configured choices.
These describe the effective initialization choice, not a permanently installed
environment variable. A no-HMD error includes the runtime name and reconnect/
`vr_reinit` guidance.

For `VirtualDesktopXR`, the activated Oculus Touch interaction profile selects
[the canonical legacy Touch frame](../src/client/component/vr/touch_controller_reference.hpp).
The reference follows the identical `openxr_grip.component_local` definitions
in SteamVR's CV1, Rift S, Quest, Quest 2, Touch Plus and Touch Pro controller
models: left/right X = +/−0.007 m, Y = −0.00182941 m, Z = 0.1019482 m,
right-handed X rotation = 20.6°. This defines the legacy calibration frame;
it does not identify a physical controller model or measure the user's wrist.
VDXR provides standard grip/aim poses, with its own OVR-to-grip conversion
([runtime implementation](https://github.com/mbucchia/VirtualDesktop-OpenXR/blob/f17345f7dbc7bc52395eaedb7aa15bfa13a675bf/virtualdesktop-openxr/action.cpp)).
Its internal OVR origin is not the application's legacy Touch origin.

The adapter requires the actual active `/interaction_profiles/oculus/touch_controller`
profile for each hand after action sync. Profile changes clear admission and
refresh the copied contract; missing or unrelated profiles reject only the
affected gameplay grip and report the reason in `vr_status`. No SteamVR
connection or install is needed on this VDXR path, and SDK aim is preserved.
Other OpenXR runtimes retain their own grip basis and may require a suitable
reference adapter or separate physical calibration. Runtime/profile admission
is separate from headset acceptance.

Initial and requested recenter origins require a focused session with both
position and orientation actively tracked in the view and head locations.
A valid inferred pose cannot seed the reference, while an established origin
can continue using a valid inferred pose. Temporary tracking/publication loss
invalidates current camera data without changing the origin; manual recenter,
VR enable and a new runtime session/connection establish a new origin. Real head translation
continues relative to that origin.

Controller bindings use core Oculus Touch, Valve Index and HTC Vive profiles.
Native gameplay consumes the common copied input frame. Focus loss neutralizes
buttons and hand tracking, fencing old edge history. Optional haptic delivery
does not disable rendering. Session/reference changes invalidate publications.
Resize records recovery intent; the next Present owner retires resources.
GPU work completes before swapchain destruction, and failed destroy handles
remain available for a later cleanup attempt.

The current native family bridge has one head rotation plus per-eye positions
and FOV. Its render head uses the eye midpoint and common orientation from one
`xrLocateViews` sample; offsets use that same basis and submission retains the
original SDK eye poses. The adapter compares normalized left/right orientations,
including quaternion sign equivalence. A separate `VIEW`-space query still gates
tracking/recenter eligibility, but its orientation is not a cant test:
[predictions may change between queries at the same display time](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrLocateViews.html).
Independently rotated (canted) SDK eye cameras are explicitly rejected
in native mode; supporting them requires a renderer-level camera representation
change and headset acceptance. Profile binding presence does not establish
support for every device exposing that profile.

For this rejection, `vr_status` and its saved status report retain
`reason=stereo_eye_orientation_mismatch source=adapter_guard` in `last_error`.
The record includes the measured eye angle and limit, normalized alignment,
frame pair, predicted display time, observation tick, session generation,
tracking validity flags and the raw left/right/VIEW-space quaternions in XYZW
order. Evidence is copied before frame cleanup, so subsequent Presents cannot
replace it with a cleared sample. The feature-unsupported result is generated
by the adapter guard; it is not an SDK query failure. Formatting occurs only
on rejection, without per-frame logging.

The strict mock rejects `xrEndSession` outside STOPPING. Explicit teardown
retires frame/GPU use before destroying a session in another state.
`xrEndSession` retires running state even on failure. Missing view-validity
bits close a zero-layer frame and invalidate tracking/stereo metadata while
retaining the session. These follow Khronos references for
[xrEndSession](https://registry.khronos.org/OpenXR/specs/1.0/man/html/xrEndSession.html),
[xrDestroySession](https://registry.khronos.org/OpenXR/specs/1.0/man/html/xrDestroySession.html)
and [view validity](https://registry.khronos.org/OpenXR/specs/1.0/man/html/XrViewStateFlags.html).

`vr-runtime-mock-smoke` covers lifecycle failures, exact display-time/pose/FOV
submission, partial native pairs, linear/encoded UI pixels, quad/cylinder menus,
pointer hit-testing, Actions, focus continuity, haptics and unsupported camera
rejection. It runs on WARP with SDK doubles; it does not establish compatibility
with an installed runtime or headset. In-game acceptance must check native startup
proof, binocular scene/weapon alignment, all controls, pause/frontend/briefing/video,
recenter, runtime focus, device/resize recovery and exit. Preserve OpenXR default
and manual OpenVR backup acceptance separately.
