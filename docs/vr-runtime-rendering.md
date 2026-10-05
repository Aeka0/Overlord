# VR runtime and rendering contracts

The production path uses native stereo rendering and OpenVR submission. The
runtime can still wait or fail when prerequisites are not met. Validate headset
behavior for each supported build and scenario.

## Modules and responsibilities

These files are in the [VR infrastructure directory](../src/client/component/vr/):

| Module | Responsibility |
| --- | --- |
| [vr_component.cpp](../src/client/component/vr/vr_component.cpp) | Settings, commands, D3D11 lifecycle integration, and startup configuration |
| [runtime_backend.cpp](../src/client/component/vr/runtime_backend.cpp) | Backend selection and facade; selection is fixed after construction |
| [openvr_runtime.cpp](../src/client/component/vr/openvr_runtime.cpp) | OpenVR initialization, Present boundaries, pose sampling, stereo submission, and pair retirement |
| [engine_stereo_renderer.cpp](../src/client/component/vr/engine_stereo_renderer.cpp) | Native hooks, view publication, native owner calls, and backend integration |
| [engine_stereo_binding.cpp](../src/client/component/vr/engine_stereo_binding.cpp) | Publication and claiming of scene and view identities |
| [engine_scene_completion.hpp](../src/client/component/vr/engine_scene_completion.hpp) | Waiting for H2 CPU scene inputs and checking identity |
| [engine_stereo_owner_pass.cpp](../src/client/component/vr/engine_stereo_owner_pass.cpp) | Per-eye transactions, model state, dynamic uploads, and resource scopes |
| [engine_stereo_eye_resources.cpp](../src/client/component/vr/engine_stereo_eye_resources.cpp) | Per-eye resource routing, history isolation, and binding restoration at scope boundaries |
| [native_render_session.cpp](../src/client/component/vr/native_render_session.cpp) | Native eye targets and the pair ownership ring |
| [gameplay/README.md](../src/client/component/vr/gameplay/README.md) | The downstream boundary from input snapshots to gameplay |

## Backend selection, defaults, and controls

The production facade selects SteamVR/OpenVR. The public OpenVR client shim is linked statically; see [openvr.lua](../deps/premake/openvr.lua). Runtime discovery does not depend on a hardcoded developer-machine SteamVR path.

The selector checks `H2V_VR_BACKEND` first: `openvr` or `steamvr` explicitly selects OpenVR; other nonempty values are rejected. Only without an explicit selection does it check `XR_RUNTIME_JSON`. A value containing `steamxr` or `steamvr` selects OpenVR; other nonempty values are rejected. This is the existing selection rule, not general validation of manifest contents. Environment-based selection happens during construction, so restart the process after changing it.

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
