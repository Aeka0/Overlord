# Render performance review: optional diagnostics

## Follow-up: production render hot paths 

An operator-coordinated The Gulag/M14 EBR capture with all launcher diagnostics
off identified production CPU work that those switches intentionally retain.
The initial 30-second recording averaged 21.94 ms per owner transaction and
about 6.00 ms in nested dynamic-view synchronization. Scope auxiliary owner
calls averaged 10.15 ms. A later valid, no-scope hotspot recording had 300/300
contexts and independently advancing stereo counters; 32 instruction samples
were in memory-protection queries with dynamic-arena return addresses. Scene
and observer conditions differed between recordings, so their timing difference
is not an optimization result. A separate recording made after VR dropped was
excluded from VR attribution and retained only as an operator-reported flat
comparison.

The implementation now addresses the common left/right/auxiliary paths:

- Small dynamic-arena spans query at most two current pages using
  `K32QueryWorkingSetEx`. Only valid, readable/writable protection results are
  accepted; unknown/nonresident/API-failure cases fall back to `VirtualQuery`.
  Guard/no-access pages are rejected without touching them. Permission reuse
  still ends at each synchronous capture/replace/restore operation: nothing is
  cached across native calls or frames. Identity checks, guarded memory access,
  verification and rollback remain in force. The API contract is documented by
  Microsoft for [page queries](https://learn.microsoft.com/en-us/windows/win32/api/psapi/nf-psapi-queryworkingsetex)
  and [valid protection attributes](https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-psapi_working_set_ex_block).
- Shader-resource binding validates the synchronous batch once, writes only
  its consumed temporary slots, and avoids clearing 128 pointers on every bind.
  Other entry points keep their admission checks. The existing per-pair negative
  identity cache has four-way buckets to reduce collision-driven `GetResource`
  queries without growing its bounded storage or extending raw-pointer lifetime.
- Restoration and boundary validation reuse exact positive mappings, whose COM
  references preserve identity. Unknown views retain resource queries. A negative
  routing entry is deliberately **not** treated as proof that no private eye
  resource remains: an independently created view may alias an eye texture.
- Completed/unarmed execution observers bypass argument construction and binding
  snapshots for ordinary Draw/Dispatch calls. Observer registration publishes
  a bounded atomic interest mask; registration is serialized off the draw path.
  Bootstrap admission, explicitly armed observers, independent draw consumers,
  ExecuteCommandList provenance and native calls remain active as required.

The auxiliary scene still performs its native owner invocation, uses separate
history, and preserves pending-left color/depth. Shared adapter optimizations
apply to that invocation too. Replacing its image, reducing quality, bypassing
native completion, or parallelizing owners that share native context/arenas is
outside this change; no native evidence establishes those transformations as
equivalent.

Validation: optimized client build; GPU-census/dynamic-arena, execution, and
eye-resource WARP probes. Coverage includes read-only/guard/protection changes,
resident-query use and fallback, exact restore, cache collisions/reset, null and
high SRV slots, both eyes plus auxiliary resources, residual private views,
quarantine/device invalidation and independent observer delivery. A stale test
stub was updated to the current target-registry type namespace. A small isolated
page-query comparison supports the new query choice but is not a game benchmark.
Both optimized and Debug clients were subsequently deployed. The operator reports
a noticeable performance improvement. There is no quantified post-change
same-scene comparison or measurement of each change's individual contribution.

### Remaining candidates after the reported improvement

This is a fresh source review, not a post-deployment hotspot ranking. Profile the
new executable before carrying forward the earlier CPU percentages.

1. **Decode each native HUD stream once, then share its metadata.**
   `native_hud_capture::select_commands` walks the same bounded stream twice per
   weapon source; narrative and remote selectors can walk it again. Command
   interception subsequently searches selected-pointer arrays for each active
   scope. A frame-local decoded index can retain command addresses, sizes,
   material classification and bounds for all selectors. Preserve native command
   execution, localization, glyph shaping, order, ownership and the original
   stream; do not retain borrowed command addresses across stream lifetimes.
2. **Track resource binding changes across eye boundaries.** Each ordinary view
   currently performs a full six-stage/128-slot pass for rebind, validation,
   restoration and validation again; the pair adds another validation. That is
   9 x 768 = 6,912 SRV slots visited in a successful ordinary stereo transaction,
   or 13 x 768 = 9,984 with the auxiliary view, including empty slots. These are
   source-level slot counts, not draws or measured milliseconds. Track modified
   stage/slot ranges with a complete-state fallback after opaque command lists,
   ClearState, automatic resource-hazard unbinding, context/device changes or
   unknown provenance. The final residual-private-resource proof must remain.
3. **Batch compatible presentation work per eye.** `draw_internal` currently
   finishes and executes a command list for an individual panel/optic draw.
   There is already a `draw_layers` path used by directional and remote HUDs;
   extend the existing renderer rather than introduce a second one. A shared
   recording scope can preserve layer order and flush at dependencies such as
   reading/copying the current output, blur, depth ownership or native callbacks.
   Dynamic constants must remain correct for every recorded draw. Measure actual
   active layers and list counts; registered callbacks alone are not draw counts.
4. **Consolidate immutable resource metadata.** Eye admission, native display
   transformation and session copy paths repeatedly query a held texture/view's
   descriptor, device and resource. Cache only facts attached to an owned COM
   identity and the appropriate generation; continue checking live registry
   selection, GPU readiness, context and pair ownership. This is narrower than
   skipping source validation and likely a smaller candidate than stream or
   binding work.
5. **Separate auxiliary scene cost and synchronization cost from adapter work.**
   Scope rendering still invokes the native owner a third time and preserves
   left color/depth around native scratch reuse. Investigate invariant native
   preparation only after verifying its consumers; merely lowering resolution
   does not establish a CPU improvement. Session copy currently holds its mutex
   across conversion/composition, while hand solving has its own serialization.
   Measure contention before changing either ownership model. Parallel native eye
   owners are not justified while they share native context and dynamic arenas.

## Follow-up: runtime probes and repeated preparation 

The following changes preserve native simulation and rendering ownership. They
reduce confirmed source-level work; no FPS gain or HMD acceptance is established.

- The existing Detailed view diagnostics switch now gates the backend's detailed
  event ring too. Backend tokens, query retirement, watchdog state and stereo
  admission stay active with this switch off.
- Launcher Debug adds Menu input recording, Weapon event recording, Vehicle
  steering recording, and four independent interaction geometry views (magazine
  well, handle slap, bolt/handle, feed cover). All use the same persisted,
  default-off, immutable startup selection in Debug and optimized builds.
  Menu CSV/report workers, reload sampling/export, event histories and dedicated
  input counters skip their work when not selected. Geometry channels allocate
  history only when selected; no 16 ms toggle polling remains, and no diagnostic
  eye consumer is registered when all four are off.
- Each hand solver caches static rig/profile/pose bindings across skeletal
  epochs, including stored and hands-only models. Keys retain bounded exact
  bone/attachment metadata and resource/asset generations; native pose epochs,
  controller input, ownership and mechanical state remain dynamic. The asset
  unload epoch also invalidates the existing same-epoch fast path. Reticle image
  identity is static, but GPU readiness is checked at presentation time so delayed
  upload, view loss and recovery do not leave a cached optic permanently absent.
- Auxiliary history clearing retains its per-mip RTVs for the isolated texture
  lifetime. Repeated Dragunov/thermal near-plane changes still reset and clear
  every history mip; they no longer recreate those RTVs each time.
- Menu observation batches sibling Lua properties in one scan without retaining
  native values across observations. Key comparisons use bounded copied string
  views, and direct fields still override inherited values, including false.
- Spatial panel instances share immutable compiled shader bytecode across
  devices. Device objects, command contexts, constants and draw textures remain
  local; the first instance compiles once, subsequent layers reuse the result.

Remaining review candidates are not implemented by this follow-up: tracked SRV
slot ranges, consolidated HUD command decoding, interaction-query reuse, asset
preparation budgets, presentation snapshot/lock separation and auxiliary scene
resolution. Resource-state and resolution changes first need native command-list
and post-processing evidence; boundary validation and the third view are retained.

The launcher selection replaces the old separate `vr_reloadBoundaryObserve`
switch. The four geometry dvar names remain saved settings, but edits take effect
at the next process start. See [launcher settings](vr-launcher-settings.md) and
[reload diagnostics](vr-reload-diagnostics.md) for usage.

Validation: the RelWithDebInfo x64 client builds; launcher configuration and UI
tests, hand-rig cache/recovery tests, backend probe smoke and eye-resource/spatial
panel WARP tests pass. A temporary native-menu harness compares 115 current/old
getter results including inheritance, userdata, missing values, cycles and depth
limits. No deployment, live frame-time capture or headset acceptance was performed.

This is a source review, not an HMD benchmark. The identified work is on CPU
paths before/around GPU submission; removing it reduces avoidable work but does
not establish which path dominates a particular level or quantify an FPS gain.

## Findings and changes

| Finding | Impact | Change |
| --- | --- | --- |
| `view_transaction_scope` captured up to 16 stack frames on every frontend scene transaction; view hooks repeatedly hashed slots/globals, scanned initializer byte differences, and published a timestamped atomic trace ring. | Persistent CPU observation cost in the scene producer. | `vr_debugViewProbes` gates the expensive observation and event ring, default off. Transaction tokens, identity checks, stereo derivation, culling and publication remain active. |
| `render_scene_stub` hashed `0x501C8` bytes before and after each of its first 256 transactions. | About 160 MiB of serial byte-wise FNV hashing over that startup window, in addition to normal rendering. | Only sampled when detailed view diagnostics is selected. |
| `capture_artifact` checked memory readability with `VirtualQuery` before checking whether the one-shot was already captured. | Repeated OS memory queries from completed diagnostic call sites in steady-state frames. | Gate disabled diagnostics and nonempty capture state before validating memory. |
| `derive_stereo_eye_slots` returned optional artifact capture success as rendering success after the views were already finalized and validated. | A diagnostic failure could reject valid stereo views. | Artifact failure remains diagnostic; it no longer vetoes valid view derivation. |
| The watchdog formatted/copied full reports and persisted evidence every two seconds until a terminal evidence bundle was complete. Some report structures are large; getters share locks with their producers. | Periodic allocations, CPU/memory traffic and possible producer lock contention, even though file writing itself is on a background thread. | `vr_debugAutoSnapshots` opts into this periodic work, default off. Preserve startup/manual/crash/stall reports and lightweight watchdog checks. |
| GPU timestamp instrumentation was already hard-disabled, but the owner path still locked and copied native-session status to calculate its eligibility arguments each pair. | An unnecessary render-path lock and status copy for an inactive probe. | Compile the entire eligibility preparation under the existing instrumentation gate. No GPU queries are re-enabled. |
| Region capture installed four native scene-job wrappers and started an idle worker even when never used. | Unneeded hooks and a worker in ordinary play; active recording adds bounded CPU/queue/I/O work. | `vr_debugPerfCapture` loads the wrappers and worker only at startup when selected. `vr_perfStart` remains the recording trigger. |

The Debug selection is shared by launcher parsing and the runtime, frozen before
game loading. It applies equally to normal launcher and direct `-singleplayer`
starts. Existing profiles without these keys default to all probes off. The
status report's `debug_loaded` line identifies the actual running selection;
changing a saved dvar mid-session does not change that selection.

## Required rendering work and remaining candidates

Do not disable `vr_engineProbe` to turn off diagnostics: despite its historical
name, its enablement controls production stereo view publication and owner
admission. Likewise, the D3D11 execution bootstrap and initial stereo readback
are one-time admission proofs; output/resource hooks implement eye isolation.
Removing those wholesale would stop or invalidate stereo rendering.

`engine_scene_completion::await` joins H2's CPU scene producers before cloning
eye records. The two native eye-owner invocations and dynamic state handling
also consume CPU. These remain candidates if steady-state submission is slow.
OpenVR's `WaitGetPoses` at Present-post is a compositor pacing and retirement
boundary; a long wait alone is not proof of debug overhead. Existing status
counters expose pose waits, queue contention, owner times and native-copy lock
waits. The source defaults the D3D11 debug layer off; a saved `d3d11_debug=1`
can still enable it in a Debug build and should be recorded for comparisons.
The Debug build uses debug optimization; compare identical build configurations
and inspect Release separately before attributing all CPU time to probes.

## Verification and hardware comparison

Offline checks cover all eight persisted probe combinations, default-off and
malformed values, immutable startup selection, valid unique stereo transaction
tokens with an empty diagnostic ring, existing enabled-probe behavior, and
launcher navigation/language/draft/save/reset/save-before-launch behavior.

Validated locally: Debug x64 client build, `vr-launcher-settings-tests`,
`vr-engine-view-probe-smoke`, and `npm --prefix src/launcher-ui test`.
HMD acceptance and frame-time comparisons have not been performed.

For HMD acceptance, use the same build, map, resolution, refresh rate and camera
route. Start with all Debug options off, then enable only CPU performance capture
and record a normal/slow/normal route using the
[bounded recorder](vr-region-capture.md). Compare CPU phase durations and
successful right-eye Submit intervals, then repeat with detailed view probes
and automatic reports individually. Check SteamVR frame timing/reprojection
alongside submission cadence; GPU utilization alone is insufficient.

Verify both eyes, head pose, culling edges, transparent effects and temporal
history with all options off. Check that `vr_perfStart` explains the unloaded
module, and that selecting capture on the next launch makes start/stop work.
Keep expensive diagnostic options off for normal play after the comparison.

## Follow-up from live CPU sampling 

With all four optional probes off, the deployed Debug executable still spent
35–44 ms per owner eye pair in two observed windows. The owner/Present thread
used 86–88% of one logical core. Direct RIP samples repeatedly hit compiler
JMC/RTC helpers, SRV rewriting, and dynamic-arena range validation. These are
observations of the old build, not measurements of the following changes.

- Add `RelWithDebInfo` with speed optimization, release CRT, PDB symbols,
  explicit JMC off and no debug runtime checks. The configuration applies to
  the client, common library, dependencies and tests; its embedded TLS helper
  comes from that same configuration. Debug and Release remain available.
- Add a bounded exact-pointer index to eye-resource view mappings. Original,
  left and right aliases resolve to the mapping index, then select the current
  eye. Hash collisions fall back to the full identity scan, never another
  mapping. The owning COM entries keep positive identities alive; resource
  rebuild/clear invalidates the index. Foreign entries still reset every pair,
  and now bypass the mapping scan on a negative-cache hit.
- Reuse `VirtualQuery` results only inside one synchronous dynamic-arena
  capture/replace/restore operation. A region read as writable can satisfy that
  operation's later write check; read-only regions never gain write authority.
  No cached permission survives a native rendering boundary or next API call.
  Bounds/overflow, backend/arena identity, guarded reads/writes, exact index
  values, verification and rollback remain in force.

Regression coverage includes pointer-index collisions and address reuse,
actual original/left/right/foreign SRV routing, persistent eye history and
device invalidation, and dynamic-arena permission changes between operations.
The arena test also verifies that a replace on separate backend/arena
allocations needs two memory queries rather than querying the arena again
before writing, and that a guard page is rejected without consuming its guard.

Generate using `tools/premake5.exe --with-vr-tests vs2022`, then build the client
and rendering probes with `Configuration=RelWithDebInfo`, `Platform=x64`.
The bundled Premake is beta2, so its supported `NoRuntimeChecks` flag is used
instead of the newer `runtimechecks` API. Relevant upstream references are
[optimization](https://premake.github.io/docs/optimize/),
[Just My Code](https://premake.github.io/docs/justmycode/), and
[runtime checks](https://premake.github.io/docs/flags/).
