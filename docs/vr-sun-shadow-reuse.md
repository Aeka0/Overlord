# Same-pair main sun-shadow reuse

The H2 implementation reuses the completed left-eye depth atlas only for the
observed uncached, depth-only synchronous sun path. All other modes execute H2
normally. It never shares spot shadows, transparent shadows, scope/auxiliary
views or a previous frame's atlas. The same-pair ordinal-consumption pattern is
informed by MWR-VR Beta 10 `8fdd7d0e37a4be71dcbc8f29b40543f9fcd110c6`
(`begin_shared_shadow_pass`); the H2 admission, native visibility and resource
writer contracts are retained independently.

## Evidence and native ownership

The 2026-11-15 capture from optimized `337b7be` contained one complete main-sun
comparison (pair 12555). Both 2048 × 6144 R24G8_TYPELESS atlases contained
nonuniform pixels and matched across 50,331,648 logical bytes. The two active
partition light views, list descriptors, partition data and native job flags
matched. Each eye issued 812 draws: 651 indexed, 146 indexed-instanced and 15
indexed-instanced-indirect. Counted direct/instanced indices totalled 5,906,658;
indirect arguments were not decoded. Both sun ordinal ranges were 0..2.
Cache, transparency and auxiliary view paths were inactive. This is evidence
for that path, not all missions or a measured HMD/GPU-time speedup.

The owning backend `0x1407A7DA0` saves view `+0x200`, checks it at
`0x1407A7E26`, and calls `0x14076D600` at `0x1407A7E30`. The latter scans
frontend data `+0x540D0C`, indices 1 through `GfxWorld::lastSunPrimaryLightIndex`
(world `+0x44`; `game::gfx_map` at `0x14EE49000`). The existing layout declares
that field immediately before `primaryLightCount` at `+0x48`. It is a sun-primary
visibility selection, not evidence of a new asynchronous renderer job.

Frontend `0x140778820` clears the 0x104-byte bitset at `0x1407788F6..905` and
calls the existing selection producers (`0x140767D40`, `0x14072FA40`). Relevant
OR writes include `0x14076790B`, `0x1407680B7` and `0x140768180`; consumers use
the corresponding primary-light indices. The optimization patches only the
already-admitted call at `0x1407A7E3F -> 0x1407B8210`. It changes neither the
frontend selection nor the backend gate.

Earlier samples did not enter this root. Three still showed 14 draws per eye
with the main-shadow DSV bound, plus a clear through `0x140787F80` (return PC
`0x14078807F`). A bound DSV alone cannot identify a draw's depth-write state or
semantic purpose. Those draws are not called worker jobs or treated as absent
shadow work. Instead, any intervening write/binding that could affect reuse
conservatively preserves right-eye execution.

## Admission and state progression

Each exact production transaction owns a fresh range and resource lease. The
left eye always renders. Before its root, under H2's existing graphics mutex:

- The thread, record and frontend data must match the transaction; no auxiliary
  view or active diagnostic capture is allowed.
- View cache `+0x2FB8` and transparency `+0x220` must be zero, main target 26,
  transparent target 304, tile 2048, configured partitions 3, active partitions 2.
- The resource must be the observed 2048 × 6144 R24G8_TYPELESS, single-mip,
  single-array, non-MSAA native depth/SRV texture on the owner device.
- Main list flags 26/27 must select the synchronous path. Delegated native list
  progression remains original. The 0x104-byte visibility bitset, both 0x130
  list descriptors and both 0x180 partition prefixes are snapshotted.

The synchronous context helper `0x1407A9120` tails to `0x14071C820`, which copies
the existing context pointer at `0x140C019D0` to the caller's stack. It does not
advance a native work queue. Cache wrappers remain excluded. After left return,
the snapshot must remain unchanged and the completed dynamic range must be
nonempty and within the existing boundary capacity.

Before right reuse, the same native GPU mutex closes writer observation. The
right input snapshot must match, the watched image must still be armed, and the
right ordinal cursor must equal the left range start. A successful skip consumes
the exact left range without replaying geometry. Later native geometry therefore
uses its original ordered snapshots, and natural post-left arena restoration
still belongs to the existing owner. No input mismatch rejects the VR frame;
it simply executes the original right sun root.

## Intervening writers

The existing invocation, texture-operation and clear observers are armed before
left rendering, not after it. Own left-root writes are expected; a different
thread remains observable even while that root runs. After left return, depth
draws through any alias DSV, resource destination copies/updates and clears
invalidate sharing. An unknown output binding, foreign draw context or arbitrary
command list also preserves the original right pass. The admitted resource has
no UAV/render-target binding, so it cannot be a compute/UAV or color target.

Only the production owner's marked, state-restoring private conversion lists
and their recording/replay contexts retain their existing provenance exception.
The execution owner now exposes that scope after bootstrap census admission has
closed as well; unknown nested lists still invalidate the watched image. The
writer guard queries the actual depth binding after a restoring list, because
driver replay can update the binding observer before D3D restores native state.
The WARP checks cover this stale-observation case explicitly. The
observer channels are detached after the right decision, pair retirement or
device invalidation. Reports contain scalar process-lifetime counters:
`sun_shadow_reuse: eligible_left/skipped_right/refused_right/dirty_refusals/`
`saved_dynamic_boundaries`.

No GPU readback, timing query, flush, new worker, cross-frame cache or general
shader-state capture is needed for production reuse. Diagnostic collection
temporarily retains both full passes for evidence; the launcher diagnostic
switch is not an optimization switch.

## Validation and limits

`vr-d3d11-execution-probe` exercises the production writer guard with WARP:
left writes, foreign-thread and alias clears, unrelated targets, read versus
destination copies, opaque versus marked command lists, and retirement. The
range policy checks light/visibility mismatches, unfinished left state, dirty
images and incorrect/overrun ordinal ranges. Existing execution/census and
conversion checks run in the same probe. Client Debug and optimized builds
cover the actual native integration.

These are offline checks and earlier baseline image evidence. They do not
constitute headset acceptance of the new skip. Theoretical work reduction is
limited to eligible right roots; 812 draws is the captured example, not a fixed
per-frame saving or an FPS promise. Broader formats, cascade counts,
transparency, cached maps and auxiliary views deliberately keep native drawing.
