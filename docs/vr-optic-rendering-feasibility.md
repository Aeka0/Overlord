# Independent optic rendering: feasibility

Implementation follow-up: the [first extra-scene candidate](vr-native-ads.md#independent-scene-prototype)
now implements a covered eye-frustum crop at native extent, ordered between the
left and right consumers. The source review below records the original design
questions; the follow-up documents actual behavior and remaining limits.

Status: source review at `23ac2f5`. The interaction contract requires investigation
after the existing lens magnifier. This document proposes work; no third-view
implementation, native replay experiment, GPU benchmark or headset validation
was performed for this review.

## Assessment

Rendering a narrow world view into a separate scope texture is a credible next
step. H2 world rendering already executes for two derived cameras, and the MOD
already owns lens geometry, masks, reticles and presentation. However, those
paths do not yet provide a general auxiliary camera. A third view crosses
native scene ownership, shared GPU preparation, dynamic uploads, history,
target dimensions and stereo publication. It is not a shader-only change.

Start with one active ordinary scope and one additional scene view, consumed by
the selected aiming eye. Keep both HMD views at their runtime projection. This
is an explicit first prototype scope, not proof that one texture provides
correct binocular optics or a physically simulated telescope. Per-eye optical
views and thermal imaging remain separate acceptance stages.

Two levels of independence must be distinguished:

| Route | Benefit | Limitation |
| --- | --- | --- |
| Current same-eye image magnifier | Cheap local zoom with existing lens/reticle presentation | Enlarges pixels already rendered for that eye |
| Extra rasterized view of the completed shared scene | New samples and shading for a narrow view; reuses scene preparation | Cannot restore geometry, effects or detail levels already excluded upstream |
| Optic-aware scene preparation plus an extra view | Can provide suitable visibility and detail selection for the optic | Requires additional native frontend/LOD/streaming investigation |

The second route is the first experiment. Its image may be sharper without
being equivalent to a completely independent high-detail camera. Whether a
conservative shared frontend is sufficient, or a separate optic frontend is
needed, remains an evidence question.

## Source evidence and reusable work

Paths below are relative to `src/client/component/vr/`.

| Source boundary | Current behavior | Consequence |
| --- | --- | --- |
| [`gameplay/optic_runtime.cpp`](../src/client/component/vr/gameplay/optic_runtime.cpp), `bind`, `present`, `prepare`, `compose` | Admits reviewed lens/reticle surfaces, freezes solved lens geometry, binds it to a scene, samples the current eye image | Reuse asset admission, owner checks, lens pose and native reticle; replace the image provider |
| [`gameplay/optic_geometry.hpp`](../src/client/component/vr/gameplay/optic_geometry.hpp), `project` | Projects the rear aperture and derives an approximate eye box | Reuse the coordinate conventions and checks; calibrate the optical model separately |
| [`spatial_panel_renderer.cpp`](../src/client/component/vr/spatial_panel_renderer.cpp), `optic_ps`, `draw_optic` | Uses screen coordinates to sample a same-sized eye source; binds no DSV | An arbitrary-sized scope texture needs different sampling and extent contracts, plus depth-aware lens composition |
| [`engine_stereo_renderer.cpp`](../src/client/component/vr/engine_stereo_renderer.cpp), `derive_stereo_eye_slots`, `backend_target_prepare_probe_stub` | Finalizes two derived views and invokes the native owner for left then right | A proven integration seam exists; it currently admits exactly two views |
| [`engine_stereo_view.cpp`](../src/client/component/vr/engine_stereo_view.cpp), `clone_scene_records` | Copies one completed native scene into both eye records and repairs camera-dependent fields | Shared scene payload is reusable only within its established lifetime and visibility coverage |
| [`engine_stereo_dynamic_upload.hpp`](../src/client/component/vr/engine_stereo_dynamic_upload.hpp), `boundary`, `finish_eye` | Defers the left upload boundary and advances it on the right | Appending a scope after normal right-eye completion can outlive the shared input lifetime |
| [`engine_stereo_eye_resources.hpp`](../src/client/component/vr/engine_stereo_eye_resources.hpp) | Isolates SSR target 91 for natural, left and right consumers | A scope needs its own history identity or a verified view-local non-temporal path |
| [`engine_stereo_owner_pass.cpp`](../src/client/component/vr/engine_stereo_owner_pass.cpp), `end_eye` | Applies native display conversion, borrows the eye DSV, calls `copy_eye` | The main eye depth is borrowed only until another view reuses it |
| [`native_render_session.cpp`](../src/client/component/vr/native_render_session.cpp), `copy_eye` | Composes under the session mutex and publishes when the completed eye mask reaches `0x3` | Auxiliary rendering must finish before final publication; callbacks cannot reenter the native session |

`engine_scene_completion::await` already joins native CPU producers before
record cloning, outside the active GPU scope. Preserve that ordering. The
native owner also progresses engine state; blindly calling `R_RenderScene`
again or replaying captured commands is not an established extra-camera API.

## Camera and magnification contract

The camera follows the solved optic axis, not the center of the headset view.
Use the same frame's tracking, weapon pose and world/model rebase. The current
catalog has rear-lens geometry; it does not describe an objective, entrance
pupil, exit pupil or optical zeroing distance. Those values must not be
invented from a muzzle position or native `adsZoomFov`.

For an initial pinhole approximation, place the virtual camera at the selected
aiming eye, aim it along the optic axis, and map its narrow view to the physical
aperture. That reduces origin differences from the existing scene. An
objective-centered camera is another model and requires explicit treatment of
near occlusion, sight/barrel parallax and eye movement. Neither choice alone
simulates a real telescope.

For a centered eye, circular aperture radius `r`, eye-to-lens distance `d`,
and desired image magnification `M`, the idealized full scene angle is:

```text
apparent lens angle = 2 * atan(r / d)
scope scene angle   = 2 * atan(r / (d * M))
```

This is a derived pinhole/image-scale relation, not measured scope optics.
For the catalog M200 radius of 19 mm, a 100 mm viewing distance and 6x zoom,
the angles are approximately 21.52 degrees and 3.63 degrees. Dividing the HMD
FOV by six and mapping that entire image onto this small lens gives a different
on-screen magnification. Off-axis viewing needs consistent asymmetric bounds
or texture mapping, and the reticle and image must share the optical axis.

The existing fixed-size reticle is a valid prototype presentation choice. It
does not establish real angular subtensions, first/second focal-plane behavior
or ballistic zero. Acceptance must separately compare the reticle, muzzle ray
and distant impact point.

Select an eligible eye with hysteresis; do not alternate the camera every frame
near the eye-box boundary. In the first prototype, the other eye retains its
ordinary world view and physical scope shadow. If both eyes are intentionally
admitted later, explicitly choose and validate either a shared optical image
with per-eye pupil mapping or separate optical views. Do not silently copy one
HMD eye into the other or equate two camera renders with physical correctness.

## Native work that must be resolved

### Visibility, detail selection and pose timing

`derive_culling_union` encloses the two HMD frusta using a common orientation
and IPD expansion. `apply_fx_culling_union` repairs a separate FX camera. Shared
terrain tessellation also has GPU-side view constants; see
[terrain coverage](vr-shared-tessellation-and-head-pitch.md). The existing union
does not establish coverage for an arbitrary translated/rotated scope camera.

For the first shared-scene experiment, admit only an optic frustum whose full
depth range is covered by the prepared scene, or extend coverage before native
visibility jobs run. A narrow FOV alone is not proof of containment, especially
near cover. Inspect Umbra/occlusion, shadows, particles and shared tessellation
as well as ordinary CPU frustum planes. Audit LOD and texture streaming before
claiming newly resolved distant geometry or texture detail.

Current optic presentation consumes `weapon_render_pose::for_scene` after
skin/submission binding. Frontend view publication occurs before generator
jobs complete. Therefore a final scene-bound lens pose cannot simply be read
back from the later presentation callback to drive earlier culling. Trace its
availability at the required boundary. Either freeze a shared optic plan early
enough and validate later binding, or demonstrate conservative coverage for the
late-derived view. Do not mix the previous frame's gun with the current eyes.

### View ownership and scheduling

Introduce a bounded auxiliary-view identity distinct from OpenVR output eye
indices. Share camera derivation, owner execution, resource restoration and
history infrastructure; keep weapon-specific eligibility in gameplay. The
runtime still receives exactly two final HMD textures. Do not expand every
two-eye array or pass `eye=2` through existing APIs: `begin_eye` rejects it and
several protocols assign special meaning to left and right.

Separate the roles of shared-work producer, additional scene consumer, last
dynamic-input consumer, and runtime output. In particular, dynamic uploads and
arena advancement must finish after the last admitted scene consumer, exactly
once. Audit every mutable shared input before assuming that replaying a cloned
record also preserves particles, material uploads or shadow work.

An explicit scheduling candidate is:

```text
Complete native frontend inputs and freeze the frame's optic plan
  -> render/capture left color and required depth
  -> render/capture right color and required depth
  -> render the scope while shared inputs are still owned
  -> compose the ordered layers for each output eye
  -> finalize/publish the two-eye pair
  -> existing runtime submission and retirement
```

This is a design candidate, not a sequence the current code supports. It needs
separate capture/composition/finalization, retained per-eye source/depth, delayed
arena advancement, isolated scope outputs, and restoration of the natural H2
tail's inputs. An earlier scope pass could avoid some retained main-view data,
but must first prove the availability of shared preparation and harmless state
restoration. Select the schedule from those proofs, not from presumed cost.

Never render a native scene from `eye_composition::compose`: its contract
forbids waits and native-session reentry, and `copy_eye` holds the session
mutex. Do not modify an already published HMD target. Preserve layer order
(`world_equipment`, optics, HUD and later layers) when separating composition.

### Targets, history and resolution

Begin a bounded third-view correctness experiment at the existing native
extent. That avoids treating independent dimensions as already solved. A
production optic should then use its own bounded target set sized for the
projected aperture, with discrete size tiers and hysteresis.

Current `engine_scene_resolution::accepts_source` requires the runtime eye
extent; `render_native_display` assumes native-sized ping-pong targets. Audit
viewport/scissor, scene color, depth, screen-space buffers, postprocessing,
inverse dimensions and mip histories together before selecting 512 or 1024.
Do not resize global H2 video configuration each time aiming starts.

The scope's color/depth/history resources need a distinct view identity and
device generation. Camera/optic replacement, eye selection changes, substantial
projection changes and target rebuilds need a defined history-reset policy.
Avoid borrowing HMD SSR history or repeatedly seeding from another camera.
Reducing screen-space effects may be a scoped quality option only after its
native boundary is verified; do not globally disable effects as an optic fix.

Respect D3D11 output compatibility and read/write binding restrictions. A
small source SRV may be sampled into a large HMD target, but the color/depth
attachments bound for each draw must meet their own output requirements. See
[Microsoft output-merger rules](https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d10-graphics-programming-guide-output-merger-stage)
and [OMSetRenderTargets](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-omsetrendertargets).

### Occlusion and presentation

The current optic shader disables depth testing. An independently rendered
image still needs to be occluded by a hand, weapon housing or wall in front of
the lens. Reuse the borrowed `event.scene_depth` seam and the read-only reverse-Z
pattern in [`world_beam_renderer.cpp`](../src/client/component/vr/world_beam_renderer.cpp),
or retain equivalent per-eye depth if composition is postponed. Validate
viewmodel depth-hack space separately; merely attaching a DSV does not prove
that its depths match the physical lens projection.

Exclude the owning first-person weapon/arms, HUD and optic composition from
the auxiliary world view through an exact per-view native filter. Preserve
other actors, weapons and intended world effects. Global material changes or
an enlarged near plane are not substitutes. Mod-owned world effects may also
need the new view; avoid recursive rendering of the scope into itself.

Keep native display conversion/color space explicit: current optics samples
display-encoded input and writes linear output. A raw HDR scope texture cannot
be substituted into that shader unchanged. Retain the aperture and scope shadow;
draw the native reticle in its agreed optical coordinates after magnification.

## Performance and threading

One scope adds one scene consumer to two main consumers; one scope view per
eye adds two. The CPU cost of owner traversal, draw setup and shared-input
handling does not necessarily shrink with scope pixels. Smaller targets mainly
reduce pixel shading and bandwidth; narrow-view preparation may reduce geometry
work only if it actually changes native admission.

As an arithmetic illustration only, two 2000x2000 eyes have 8 million pixels:
one 512x512 target adds 3.3%, and one 1024x1024 target adds 13.1% of that pixel
count. A same-sized third view adds 50%. None of these numbers predicts frame
time or FPS. One 1024x1024 RGBA16F image plus a 32-bit depth image alone costs
12 MiB; ping-pong, histories, mip chains, retained eye inputs and in-flight
ownership add more. The native target inventory must determine the real budget.

Use the current optimized build and fixed resolution/map/refresh for baseline
and optic comparisons. Measure CPU owner time, GPU frame time, p95/p99 latency,
memory, submission cadence and reprojection. Existing CPU counters and
[region capture](vr-region-capture.md) are reusable; the source's GPU timestamp
instrumentation is hard-disabled, so do not report its empty counters as GPU
measurements. Use external runtime timing first or a separately validated,
bounded GPU measurement path. At 90 Hz the whole application frame budget is
11.11 ms, not an allowance for the scope alone.

Parallelize independent CPU math/preparation where useful, while keeping
native scene execution on its proven owner. A worker cannot concurrently use
H2's immediate context. Microsoft specifies serialized access to each context;
deferred contexts support parallel command recording, not free additional GPU
rendering or safety for H2's shared globals. See
[D3D11 threading](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-intro)
and [deferred rendering](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-render).

Only produce an optic view when an eligible lens/eye needs it. Bound view count,
target sizes and allocation caches; pool by device lifetime rather than
allocating every frame. Start with current-frame updates while aiming. A reduced
update rate is a later explicit tradeoff because gun motion produces visible
scope lag; headset reprojection does not update the gun-relative image itself.

## Verification sequence and decision gates

1. Trace the scene-bound optic pose, shared-work producer, dynamic-input
   retirement and target inventory. Define the bounded view identity, frame
   ownership and publication sequence before adding native replay.
2. Prove one extra ordinary view into an isolated texture at native extent,
   initially with a covered narrow frustum. Verify restoration of the main
   views and natural tail, dynamic inputs advancing once, and no extra
   simulation, tracking sample or runtime submission. This proves rendering
   mechanics only, not the final scope or its performance.
3. Prove independent target dimensions and history ownership. Compare actual
   geometry/texture detail, terrain, shadows, transparency and effects. If
   shared admission loses required content, resolve optic-aware preparation
   before calling this a complete independent optic.
4. Integrate the M200 lens first, then an ACOG. Validate magnification, eye
   relief, fixed reticle behavior, occlusion and aiming alignment. Maintain
   the ordered presentation layers and publish only complete output pairs.
5. Benchmark current image magnification against the real-render mode in the
   same optimized build. Test raising/lowering, rapid eye/hand changes,
   holstering/dropping, two held weapons, menus, lost tracking, recentering,
   extreme scale/magnification, invalid poses and device rebuilds. Reject
   nonfinite/degenerate geometry, near-plane crossings and excessive extents.
6. Revisit [thermal world isolation](vr-thermal-world-isolation-research.md)
   once ordinary optics pass.
   Native thermal material selection was observed, but correct thermal pixels
   and stencil coverage remain unproven. Scope-local native thermal/heat/history
   must preserve the ordinary outside world; grayscale is not thermal imaging.

If the auxiliary view is unavailable before execution, report that state and
keep valid main views running with an explicit lens fallback, such as darkness.
Do not silently present the old image magnifier as successful independent
rendering or reuse a stale owner/device texture. After a scope failure, main
views may proceed only if native state restoration is proven; otherwise retain
the renderer's existing rejection/quarantine behavior.

The next engineering milestone is a safely owned third view, followed by a
separately sized target. Until those pass, feasibility is architectural and
performance remains unmeasured.
