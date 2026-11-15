# Sun-shadow reuse evidence capture

This opt-in observer collects H2 evidence with both eyes and any auxiliary view
executing every original shadow pass. During a requested capture, production
[main sun-shadow reuse](vr-sun-shadow-reuse.md) yields to the original drawing.
MWR-VR Beta 10 at
`8fdd7d0e37a4be71dcbc8f29b40543f9fcd110c6` motivates the investigation; its H1
comparisons do not establish H2 equivalence.

## Preparation

Build the optimized client and retain its matching PDB. After normal game
closure, deploy that pair. In the launcher's **Debug** settings, enable
**Sun-shadow capture**, save, and start the game normally. It defaults to off.
The selection uses the existing saved diagnostic settings and is frozen before
loading H2; changing it requires the next game start. No command-line argument
or separate launch script is needed.

With the setting off, ordinary diagnostics keep their five-second behavior and
perform no shadow copies. With it on, **Ctrl+Shift+F8** / `vr_diagnose` requests
the session below. Repeated requests while it is running are ignored. Turn the
setting off after the investigation to restore ordinary collection next launch.

Keep the current sun-shadow cache disabled, as in the previous frame pipeline
capture. Do not enable cache, GPU timing, GPU census, region/job recording or
other deep probes for this batch. The ZIP records native current settings,
process/build identity, adapter and runtime. Retain the usual headset/runtime
settings (previously Quest 3, 90 Hz, VD SSW Auto, unless the operator changed them).

Use an outdoor checkpoint with visible sun shadows and smoke/dust or transparent
casters. Bring a scope that actually uses an auxiliary view if available; iron
sights cannot establish that coverage. The historical
[transparent-shadow investigation](vr-transparent-shadow-stereo.md) identified
target 28 as an A8 atlas, 2048 × 6144 in that capture, and exposed a right-eye
dynamic-index defect. Those are historical observations, not assumptions for
the new resource descriptions. A8, depth and color storage are supported.

Preparation/build/WARP checks do not launch the game or establish HMD acceptance.
Coordinate the live window with the operator before starting.

## One session

Let loading and VR startup settle, confirm a normal world image, then press
**Ctrl+Shift+F8** once while the game has focus:

| Time from request | Operator action |
| --- | --- |
| 0–8 seconds | Look at the outdoor scene normally |
| 8–16 seconds | Walk and turn within that same scene |
| 16–24 seconds | Aim through the scope, keeping the sunlit scene visible |
| 24–29 seconds | Let outstanding readbacks finish |

One diagnostic ZIP opens automatically. Up to six pairs are attempted, four
seconds apart. A pending readback or worker comparison delays sampling instead
of building a backlog. Actual sample ticks and auxiliary flags identify
coverage; the intended phase names do not prove what the operator did. No game
launch, key injection or automatic capture restart is part of this procedure.
Record any unusual scene/eye behavior separately; the package cannot inspect
the physical headset display.

Analyze the ZIP or its JSON directly:

```text
python tools/analyze_sun_shadow_capture.py path/to/diagnostics.zip --output path/to/report.md
```

## Evidence and ownership

`sun-shadow.json` identifies its request, pair, device generation and owner.
Each view records sun/cache/transparent switches, partition counts, tile size,
target IDs, root/subpass calls, inclusive CPU wall times, Draw/Dispatch/
ExecuteCommandList counts, direct indices and dynamic-index ordinal ranges.
Subpasses retain list descriptors, consumed primary light-view bytes and the
first observed output views/resources/formats and viewport. Partition prefixes,
cache headers and native job flags are copied before and after the root call.
Opaque command lists and dropped observations are gaps, not zero draws.

The full native owner also records bounded output-binding groups and texture
copy/update/resolve/mip/UAV-clear and RTV/DSV-clear operations, classified as
before/inside/after the sun root. Resource identities allow finding other
writers that could clear the left result before right-eye reuse, or mask a
difference before the final readback. Binding groups resolve color slot 0 and
depth; MRT counts, opaque/deferred execution and shader/UAV writes still require
interpretation against the recorded resource bind flags and native callers.
This observer does not claim complete arbitrary shader-write provenance.

After the native eye owner completes, under the existing H2 immediate-context
mutex, both enabled outputs are copied to compatible private staging textures.
Targets come from that view. Invalid target 304 is checked before registry
access. Resource/SRV identity, device, description, sample count, mip/array
layout and format support are recorded/checked; unsupported resources remain
explicit gaps.

Readback uses [`Map(DO_NOT_WAIT)`](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-map)
on the immediate owner, at most 8 MiB of logical rows per later native pair.
There is no render-thread comparison, formatting, file I/O, GPU flush,
completion query, blocking GPU wait or new worker. The existing support worker
compares exact bytes, excluding RowPitch padding, and records the first
difference, differing byte count and nonuniform-pixel evidence. Private CPU and
staging payloads are each bounded to 768 MiB for one pending sample; comparison
publication prevents allocating the next sample while the worker holds the
first. Resources retire on the owner after cancellation or at device
invalidation/shutdown. Raw atlases are not placed in the bounded ZIP.

Same-format staging follows Microsoft's
[`CopyResource` contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copyresource).
The standalone `vr-sun-shadow-readback-tests` WARP target covers depth, A8 and
color copies, padded rows, bounded chunks and single-byte difference detection.
Headless submission is test-only. These checks do not prove live native ABI,
resource equivalence, all CPU side effects or visual correctness.

## Native source chain

Addresses/offsets below belong to the supported H2 executable, timestamp
`0x6023ED08`. They are observed roles, not portable engine symbols. Entry
instructions are checked before optional installation; a mismatch disables only
this observer. The existing dynamic-index owner remains authoritative.

| Native evidence | Observed role |
| --- | --- |
| Owner `0x1407A7DA0`, call `0x1407A7E3F` | RCX backend data, RDX view record, sun root `0x1407B8210`; spot root `0x1407B3AC0` is separate |
| Sun root `0x1407B8210 .. 0x1407B83CC` | Iterates data `+0x61ADA0` active partitions |
| `0x140736470` | Cached wrapper: view, list descriptor, partition, cache index, native context pointer; lists `41 + 9*partition + cache_index` |
| `0x140736600` | Main wrapper: view, list descriptor, partition, native context pointer; lists `26 + partition` |
| `0x140736A90` | Transparent wrapper with the same four arguments; lists `30 + partition`, gated by view `+0x220` |
| `0x140787230 -> 0x14078727E` | Existing geometry-executor view setup; primary light view is observed before original setup |
| `0x14077E500`, writes `0x14077E674 .. 0x14077E6B1` | view `+0x1FA0` tile, `+0x1FA4` configured 3/4 partitions, `+0x1FA8` main target 26/27, `+0x1FB0` transparent target 28/29 |
| `0x14078F3E0`, write `0x14078F5EC` | Disabled transparent path writes invalid sentinel 304 to view `+0x1FB0` |
| `0x140736C40`, `0x140736D05 .. 0x140736D17` | Transparent output temporarily samples main target through source state `+0x29A8` |
| Registry `0x150FD1700`, stride `0x40`, capacity 304 | First field is `GfxImage*`; its SRV resource must agree with `texture.map` |
| data `+0x542650 + partition*0x48270` | Partition prefix includes the native light view at `+0x10` |
| data `+0x5427D0 + partition*0x48270 + cache_index*0x8018` | Root's cache count/entry fields; bounded headers only |
| data `+0x540FC8` | Per-list native job flags, retained before/after root; delegated work may leave draw provenance opaque |

## Interpretation

Review all enabled outputs, meaningful shadow content, consumed light inputs,
dynamic-index ranges and native state progression together. Uniform clear
images, missing/busy/timeout samples, incompatible resources or unobserved work
cannot establish equivalence. Cache execution needs a side-effect explanation.
Auxiliary views remain ineligible for the first reuse design because they may
rewrite the shared maps between eyes. If transparent output differs, do not
skip the whole root.

The analyzer summarizes evidence; it never approves reuse. Future reuse must
still update shadows on the left each pair, consume the corresponding right-eye
dynamic ordinals, and preserve native execution for unsupported, auxiliary,
cache-active or mismatching cases. CPU durations include observer overhead;
copies/readback affect timing. This batch is not an FPS/GPU-time benchmark.
