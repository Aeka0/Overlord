# Performance optimizations

This document consolidates the performance work implemented in Overlord: scene
scheduling, optimized builds, render hot paths, optional diagnostics, repeated
preparation, resource reuse, and launcher responsiveness. It links each area to
its owning implementation or detailed guide. Changes to weapon handling and
interaction feel are outside this inventory unless they also remove repeated
computational work.

Implementation, offline validation, deployment, and headset acceptance are
separate results. The validation statements below summarize existing engineering
records; this document does not introduce a new benchmark or claim an aggregate
FPS improvement.

## Recorded results

| Area | Established result | Measurement limit |
| --- | --- | --- |
| Early view publication and completed scene-input cloning | Headset testing accepted the production handoff and reported a very large scene-performance improvement in The Gulag. | No recording ran during acceptance; no measured FPS gain or post-change counter comparison is available. |
| Production render hot paths | Optimized and Debug clients were deployed after focused rendering probes passed; the operator reported a noticeable improvement. | No controlled same-scene before/after capture or attribution to individual changes is available. |
| Optional diagnostics and repeated preparation | Builds and focused launcher, hand-rig, backend, and WARP checks are recorded in the render performance review. | The repeated-preparation follow-up records no deployment, live timing capture, or headset acceptance. |
| Other resource, input, and launcher reuse | The current implementation contains the bounded reuse and worker separation described below. | These are source-level reductions in repeated work, without isolated game frame-time measurements. |

The [region capture guide](vr-region-capture.md) records the scene-handoff
acceptance. The [render performance review](vr-render-performance-review.md)
contains the profiling evidence and validation details for the render and
diagnostic changes.

Historical measurements explain why work was selected, rather than establish
the resulting speedup:

- With optional probes off, an older Debug executable spent 35-44 ms per owner
  eye pair in two observed windows and used 86-88% of one logical core on the
  owner/Present thread. Instruction samples repeatedly reached compiler JMC/RTC
  helpers, SRV rewriting, and dynamic-arena range validation.
- A separate Gulag/M14 EBR capture averaged 21.94 ms per owner transaction,
  including about 6.00 ms in nested dynamic-view synchronization. Scope auxiliary
  owner calls averaged 10.15 ms. Nested durations are inclusive.
- A later no-scope capture found memory-protection queries in 32 instruction
  samples with dynamic-arena return addresses. Scene and observation conditions
  differed, so differences between these captures are not optimization results.

## Optimized builds and build throughput

`RelWithDebInfo` provides a gameplay and performance baseline with speed
optimization, the release CRT, full PDB symbols, explicit `/JMC-`, and disabled
runtime checks. The configuration covers the client, common library,
dependencies, and tests; the embedded TLS helper comes from the same
configuration. Turning off launcher probes cannot remove compiler-inserted
checks from a Debug executable.

Release retains size optimization and symbols. Its `/GL-` and `/LTCG:OFF` policy
works around a verified v142 aggregate-publication miscompile; disabling WPO is
a correctness constraint, not a claimed speed improvement. Re-enabling it needs
toolchain qualification and Release runtime acceptance.

The existing build also uses multiprocess compilation, shared precompiled
headers for ordinary client sources, and MSBuild `/m` in the documented build
commands. These improve build throughput rather than game frame time. The
OpenXR loader is built with `RelWithDebInfo` for both client configurations.

Sources: [Premake configuration](../premake5.lua),
[build and validation](development.md), and
[Release qualification](release-validation.md).

## Scene scheduling and native parallelism

### Publish views before waiting for the scene payload

The frontend publishes immutable stereo view inputs immediately before the
native draw-surface generator. This lets the concurrent backend claim the
matching scene without waiting for that generator to return. It does not copy
unfinished surfaces or effects.

Before cloning scene records, the backend joins the native initial, surface,
and effects CPU producers in their original order and checks all four completion
flags. It uses H2's existing job-pumping waits before entering private eye state
or taking the MOD GPU lock. Workers can finish the work being awaited without
depending on resources held by the waiting stereo owner.

Early publication and completed-payload cloning are one contract. Moving
publication later can miss the consumption window; cloning earlier can capture
incomplete transparency and effects. Original native owner calls and their
waits remain intact. This is the change with recorded Gulag headset acceptance.

Sources: [scene-input handoff](vr-region-capture.md),
[runtime completion boundaries](vr-runtime-rendering.md),
[renderer](../src/client/component/vr/engine_stereo_renderer.cpp), and
[CPU scene completion](../src/client/component/vr/engine_scene_completion.cpp).

### Advance dynamic uploads once after their consumers

The native owner opens the next dynamic upload with `WRITE_DISCARD`. Stereo
defers that boundary during the left-eye invocation and advances it once at the
right-eye boundary, after the current allocation's consumers have finished.
An auxiliary scope view between the two eyes also defers advancement.

This avoids repeating an allocation-advancing native operation for each view
and preserves the current buffers while they are still needed. The policy
validates frontend identity, thread, order, and completion. A normal early exit
after deferral recovers the pending advance on the owner thread; an interrupted
original call is not blindly retried. The policy adds no D3D resources or waits.

Sources: [dynamic-upload cycle](../src/client/component/vr/engine_stereo_dynamic_upload.hpp)
and [owner integration](../src/client/component/vr/engine_stereo_owner_pass.cpp).

## Production render hot paths

### Dynamic-arena memory validation

The dynamic-arena adapter removes redundant memory-protection queries in three
ways:

1. Validate the complete descriptor span once for a synchronous capture, then
   use guarded field copies instead of querying each field separately.
2. Retain positive range/protection results only within one synchronous
   capture, replace, or restore call. A result already proving writability can
   satisfy that call's later write check; read-only results cannot.
3. For small spans, query at most two touched pages with
   `K32QueryWorkingSetEx`, avoiding discovery of an entire equal-protection
   region. Unknown, nonresident, or API-failure results fall back to
   `VirtualQuery`; guard and no-access pages are rejected without touching them.

Permission reuse ends before native rendering resumes. Bounds, arena identity,
guarded accesses, exact restoration, verification, and rollback remain active.
The shared native-memory copy helper also avoids adding a `VirtualQuery` call
to every owner-thread field read; callers still establish lifetime and snapshot
ownership.

Sources: [dynamic-arena adapter](../src/client/component/vr/engine_stereo_dynamic_arena.cpp)
and [native-memory copies](../src/common/utils/native_memory.hpp).

### Eye-resource and shader-resource routing

| Implemented change | Repeated work removed | Reuse boundary |
| --- | --- | --- |
| Bounded exact-pointer index for original and private-eye views | Repeated linear searches through known view mappings | Owning COM mappings keep positive identities alive; resource rebuild/clear invalidates the index. Collisions fall back to exact lookup. |
| Negative-cache lookup before the mapping scan | Scanning known eye mappings even for previously classified ordinary material SRVs | Foreign-view identities are reset at each pair boundary. |
| Four-way negative-cache buckets | Collision-driven eviction and repeated `GetResource` queries | Storage remains bounded; collisions never classify another identity as a hit. |
| One admission check for a synchronous SRV binding batch | Repeating the same context/pair admission for every entry | Other entry points retain their admission checks. |
| Fill only consumed temporary SRV slots | Clearing all 128 scratch pointers on every bind | Only initialized, requested entries are passed to the native binding call. |
| Reuse exact positive mappings during restoration and boundary validation | Repeated resource queries for already-owned immutable views | Unknown views still call `GetResource`. A negative routing result cannot prove that a view does not alias a private texture. |

Sources: [eye-resource isolation](../src/client/component/vr/engine_stereo_eye_resources.cpp)
and [pointer caches](../src/client/component/vr/engine_stereo_pointer_cache.hpp).

### Skip inactive execution observation before preparing it

Ordinary Draw/Dispatch hooks call the original function directly when execution
admission is complete/unarmed and no relevant observer is interested. The check
happens before constructing invocation arguments or reading output bindings.
A bounded atomic interest mask represents registered observer channels;
registration is serialized outside the draw path.

Bootstrap admission, explicitly armed probes, independent draw consumers, and
`ExecuteCommandList` provenance still receive the calls they require. This
removes inactive observation work without removing production eye isolation.

Source: [execution observation](../src/client/component/vr/engine_stereo_execution.cpp).

## Optional diagnostics stay off the ordinary path

Probe selection defaults to off and is frozen before game loading for both
launcher and direct starts, in Debug and optimized builds. Saved edits take
effect on the next process start. The status report's `debug_loaded` selection
identifies what the running process actually installed.

| Area | Implemented reduction |
| --- | --- |
| Detailed view probes (`vr_debugViewProbes`) | Gate stack capture, slot/global hashes, initializer comparisons, timestamped trace publication, and the backend detailed event ring. The former startup record hashes covered about 160 MiB across the first 256 transactions. |
| One-shot artifacts | Check disabled/completed capture state before memory readability checks. Optional artifact failure no longer rejects already-valid stereo derivation. |
| Automatic reports (`vr_debugAutoSnapshots`) | Gate the former two-second full-report copying, formatting, and persistence cycle. Lightweight watchdog checks and startup/manual/crash/stall evidence remain available. |
| Disabled GPU timestamp probe | Compile out eligibility preparation, including a native-session status lock/copy, under the existing disabled instrumentation gate. GPU timing is not re-enabled. |
| CPU region capture (`vr_debugPerfCapture`) | Install its four scene-job wrappers and worker only when selected at startup; recording still begins explicitly with `vr_perfStart`. |
| Scene-model evidence (`vr_debugSceneModels`) | Keep equipment/scene capture observers detached while idle; start their bounded sampling only for an armed recording. |
| Menu input (`vr_debugMenuInput`) | Skip dedicated input counters, history sampling, and CSV/report scheduling when not selected. |
| Weapon events (`vr_debugWeaponEvents`) | Skip reload-boundary observation/export and diagnostic event histories while preserving functional weapon-event processing. |
| Vehicle steering (`vr_debugVehicle`) | Skip steering diagnostic history when not selected. |
| Interaction geometry (`vr_reloadWellDebug`, `vr_hkSlapDebug`, `vr_boltDebug`, `vr_coverPushDebug`) | Allocate each history only when selected; remove 16 ms toggle polling and register no diagnostic eye consumer when all four channels are off. |
| CPU wait timing (`vr_debugWaitTiming`) | Perform no clock/thread-ID queries or sample publication when disabled. Enabled producers use fixed storage and try-lock publication, dropping observations rather than blocking production work. |

Active recorders use bounded storage/queues and keep formatting and disk I/O
on their diagnostic paths. Geometry overlays use instanced batches and cached
device shaders without GPU readback. CPU wait timings are elapsed wall time;
nested intervals must not be summed as independent CPU costs.

These gates preserve transaction tokens, native identity checks, stereo
publication, culling, watchdog state, and required resource hooks. In particular,
`vr_engineProbe` controls production stereo admission despite its historical
name; disabling it is not a diagnostic optimization.

Sources: [startup probe selection](../src/client/component/vr/debug_options.hpp),
[render performance review](vr-render-performance-review.md),
[equipment visibility capture](vr-equipment-visibility-capture.md),
[reload diagnostics](vr-reload-diagnostics.md), and
[wait instrumentation](../src/client/component/vr/diagnostics/wait_timing.hpp).

## Reuse static preparation and GPU resources

### Hand rigs and optic binding

Each hand solver caches static rig, profile, attachment, and pose bindings
across native skeletal epochs, including stored and hands-only models. The
bounded key includes exact model/bone/attachment metadata and resource/asset
generations. Accepted and rejected bindings can both be reused until their
admission conditions change. Asset unload invalidates both binding reuse and
the existing same-epoch fast path.

Controller input, native pose epochs, ownership, mechanical state, and the
actual hand solve remain dynamic. An optic's native reticle image identity is
bound as static metadata, while live GPU readiness is checked at presentation
time. Delayed uploads or view loss therefore do not permanently cache an
unavailable reticle.

Sources: [rig cache](../src/client/component/vr/gameplay/hands/rig_cache.hpp),
[hand component](../src/client/component/vr/gameplay/hands/component.cpp), and
[optic runtime](../src/client/component/vr/gameplay/optic_runtime.cpp).

### Menus, panels, and temporal history

| Implemented change | Effect and lifetime |
| --- | --- |
| Batch sibling LUI properties in one table scan | Avoid repeated table walks and temporary key strings. Bounded copied string views preserve direct-field precedence, including `false`; native values do not survive the observation. |
| Share immutable spatial-panel shader bytecode | Compile the shader entry points once per process and reuse their bytecode across panel instances and replacement devices. Compilation failure is cached too. Device shaders, contexts, constants, and textures stay local. |
| Retain auxiliary-history clear RTVs per mip | Repeated scope/thermal near-plane resets clear every mip without recreating the RTVs. Views live with their isolated texture. |
| Preserve eye history until its resource identity changes | Rebuild from texture/descriptor/generation changes rather than switching mip views. Do not reseed from the natural scene or the other eye every frame. |
| Reuse same-device submission textures | Keep the native conversion/submission ring for its valid device/format generation and retire submitted pairs at the runtime's ownership boundary before reuse. |

Sources: [native menu observation](../src/client/component/vr/native_menu.cpp),
[spatial-panel renderer](../src/client/component/vr/spatial_panel_renderer.cpp),
[eye resources](../src/client/component/vr/engine_stereo_eye_resources.cpp),
[native render session](../src/client/component/vr/native_render_session.cpp), and
[runtime/history contracts](vr-runtime-rendering.md).

### Weapon meshes and asset lifetime

Magazine population geometry uses offline-authored face selections and prepared
immutable states instead of slicing meshes per frame. The shared immutable
surface cache uses a source/mask hash lookup instead of scanning every
ammunition-count variant. Individually owned entries keep borrowed descriptors
stable when the lookup table grows; storage has an explicit budget.

Checkpoint changes reuse subsets from the same loaded source. At the drained
native asset-unload boundary, magazine, cylinder, and tube presentation discard
borrowed frames/releases, free retained subsets, and reset the shared model
identity registry. This avoids retaining another asset set on every checkpoint
or backwards game-time transition and exhausting session resources. Nothing is
recycled while queued rendering may still consume it.

Sources: [magazine geometry](../src/client/component/vr/gameplay/magazine_fill.hpp),
[immutable surface cache](../src/client/component/vr/gameplay/immutable_surface_cache.hpp),
and [asset-lifetime findings](vr-reload-diagnostics.md).

## Input, presentation, and status work

| Implemented reuse or separation | Work avoided and correctness boundary |
| --- | --- |
| Published runtime input/tracking frames and shared semantic edges | Gameplay consumers do not independently poll SteamVR or recreate action edges. Native commands are modified before insertion into command history rather than again during prediction reads. |
| Frame-local world Grip query reuse | Arbitration, execution, and hover share the exact candidate, including an empty result, for the same hand/input/reference/ray. The next frame queries again; use/pickup still validates the live entity and permissions. |
| Vehicle gun placement once per scene record/submission | Resolve visibility and attachment for the complete gun, then reuse the result across parts instead of repeatedly reading changing owner/ammunition/time state. |
| Separate runtime status snapshots | Read published OpenVR status under its own status mutex instead of acquiring the facade lock held across Submit/`WaitGetPoses`. Diagnostic readers need not wait for compositor pacing. |

Sources: [runtime thread boundaries](vr-runtime-rendering.md),
[interaction architecture](vr-gameplay-interaction-architecture.md),
[world interaction](vr-world-interaction.md), and
[vehicle presentation](../src/client/component/vr/gameplay/vehicles/presentation.cpp).

## Launcher responsiveness

Launcher settings, preferences, preflight, and repair requests execute on a
worker rather than inside WebView2 callbacks. The request queue is bounded and
sleeps on a condition variable when idle. File mutations remain ordered.

Installed game-language enumeration has its own worker so a storage scan does
not occupy the ordered service queue. UI completion polling does not wait on
disk. The advisory CASC scan checks local language common-file entries without
reading payload spans or auditing every map/audio file. Saving a language
preference does not repeat the scan. Launch still waits for pending language
I/O to finish.

These changes reduce UI blocking and unnecessary storage work; no launcher
startup-time or gameplay-FPS improvement has been quantified here.

Sources: [launcher services](../src/client/launcher/services.cpp),
[language worker](../src/client/launcher/game_language.cpp), and
[language-selection behavior](launcher-localization.md).

## Related correctness work and remaining candidates

Scene-surface arena expansion, static transparent-surface quotas, and the
native model-lighting pool address capacity failures and missing geometry.
Scene storage expansion preserves native bank/allocator lifetimes and installs
its format/relocation changes at startup without copying streams per frame.
These fixes should not be counted as measured frame-time improvements.
See [surface storage](scene-surface-storage.md),
[static surfaces](scene-static-surfaces.md), and
[model lighting](scene-model-lighting.md).

The following review candidates are not completed optimizations: one shared
decode of native HUD command streams; tracked SRV slot ranges instead of full
boundary scans; broader per-eye presentation command-list batching;
consolidated immutable COM metadata; broader interaction-query reuse and asset
preparation budgets; and presentation snapshot/lock separation. The narrow
frame-local Grip reuse above does not imply that all interaction queries have
been consolidated.

Auxiliary scopes still invoke the native owner for a third view and preserve
pending-left color/depth. Reducing that view's resolution, replacing its image,
or running native eye owners concurrently has not been established as an
equivalent optimization. Current owners share native context and arenas.
Disabling native effects, reducing visibility, removing resource-boundary
proofs, or changing image quality is not part of the recorded hot-path work.

## Verification and future comparisons

Existing focused coverage includes scene-completion/upload ordering,
dynamic-arena permissions and query reuse, eye-resource routing and WARP
isolation, execution-observer delivery, hand-rig cache invalidation and reticle
recovery, launcher settings, and bounded wait diagnostics. The detailed guides
record which builds and probes were actually run; the presence of a test does
not establish headset acceptance.

For a useful new comparison:

1. Use the same source revision and `RelWithDebInfo` configuration. Record
   runtime/backend, device, eye resolution, refresh rate, graphics settings,
   and loaded probes.
2. Replay the same map/save and normal/slow/normal viewing route, including
   the same weapon and scope state. Begin with optional probes off.
3. Enable only the bounded recorder needed for the next run. Compare loss-free
   intervals with matching scene identities, advancing stereo counters,
   successful submissions, CPU phases, and compositor frame timing/reprojection.
4. Attribute waits carefully: DXGI Present, `xrWaitFrame`, swapchain waits, and
   `WaitGetPoses` include pacing/scheduling. GPU utilization or an inclusive
   owner duration alone cannot identify a bottleneck.
5. Check both eyes, tracking, culling edges, transparent effects, temporal
   history, scope transitions, and resource recovery alongside timing.

Use the [diagnostics workflow](vr-diagnostics-workflow.md),
[region recorder](vr-region-capture.md), and
[performance review](vr-render-performance-review.md) for capture details.
