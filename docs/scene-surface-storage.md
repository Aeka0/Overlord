# Scene surface storage: why this ABI change must remain together

## Necessity and measured failure

Two explicitly coordinated airport captures reproduced equipment disappearing
when turning, while television textures also changed. Native logs repeatedly
reported `MAX_SCENE_SURFS_SIZE((1 << (16 + 2))) exceeded - not drawing surface`.

| Capture | Maximum cursor | Original capacity | Owned native build failures at an exceeded cursor |
| --- | ---: | ---: | ---: |
| `region-1790083444717-p51484.bin` | 313592 bytes | 262144 bytes | 780 |
| `region-1790084749092-p57876.bin` | 301376 bytes | 262144 bytes | 855 |

Both captures ended normally; they reported 150 and 192 dropped events. Their
operator confirmed reproduction. These counts are observations, not proof that
an individual concurrent allocator consumed all bytes. The proposed tighter
matrix culling volume did not resolve the symptom and was withdrawn. No extra
television camera pass was established. The original stereo visibility behavior
is retained for this storage change.

This module fixes a finite **per-frame packing arena**, not a persistent leak or
a performance cache. Native admission could succeed, yet packing could return
zero and clear a model's visibility because the shared arena had filled first.
Removing this module can therefore reintroduce missing geometry even if a short
test in another area looks correct.

## One indivisible format contract

The native `uint16_t surfId` encodes a byte offset divided by four. Its entire
range only addresses 256 KiB. Enlarging the capacity check without changing the
encoding wraps IDs into earlier surfaces. Moving storage without updating all
readers is equally unsafe.

The replacement keeps the same 16-bit draw-key field and uses eight-byte units,
addressing **512 KiB per native frontend bank**:

- Rigid records are `(boneCount + 2) * 32` bytes and skinned records are 56 bytes;
  both already align to eight. Their internal fields are unchanged.
- Hidden records occupy eight bytes instead of four. Every producer and stream
  walker must agree. The logical ID increment is still one, not two.
- Brush transforms retain 28 bytes of content in a 32-byte prefix. Following
  brush surfaces are 24-byte pointer triples. Their logical ID advance is three.
- Normal, depth, shadow, subdivision and debug draw paths share the new unit.
  The compiler-folded base expression and fused shadow offset encoder are part
  of the contract, even though they do not contain the literal arena offset.
- Motion-history offsets use a **different arena and four-byte units**. Do not
  change those shifts simply because they appear beside the hidden-record walk.

`scene_surface_storage_contract.hpp` is a reviewed generated table of 242 exact
instruction contracts: 89 arena/member references (65 base-only and 24 with
inlined field offsets), 59 packed-ID decoder pairs,
the additional folded/modular paths, writers, walkers, bounds and frontend-bank
identity guards. Counts are deliberate coverage checks, not optimization knobs.

All original instructions are verified before any patch is written. A mismatch
aborts startup; a failed write restores already-written sites. Do not weaken the
guards, ignore mismatches, remove individual patches, or enable mixed layouts to
work around an error. A changed native version needs a new complete audit.

The first expansion build crashed because the census searched only the literal
arena base. Native instructions also embed `arena + field`: the 22:19 dump read
a null brush-surface pointer through `+0x62b708` at `0x1407b9584`, and the 22:20
dump reached a null model-surface pointer through `+0x62b728` at `0x1407bb588`.
Both used the new index unit but the old storage address. The revised generator
audits base/member operands over the complete captured executable and retains
the field addend during relocation. Same-valued RVAs/string constants outside
the renderer are explicitly excluded; do not blindly replace every integer.
Regression tests require all 24 member accesses and both crash paths. Successful
startup byte verification proves the LISTED sites matched, not that the list was
complete; operand coverage and observed runtime behavior are separate checks.

The MOD's C++ packet readers are also format consumers. `plan_skin_packets` in
`skinned_part_visibility.hpp` must use `scene_surface_storage::record_bytes`,
not its own hidden/skinned/rigid stride constants. Leaving its hidden stride at
four rejected the expanded stream and bypassed original-magazine hiding: the
gun retained its magazine while the independent magazine fell. The regression
fixture includes a hidden entry with nonzero padding before skinned magazine
and receiver entries; it checks correct descriptor substitution without hiding
the receiver. A native-patch census alone cannot cover these C++ consumers.

### Expanded consumer review

| Path | Format dependency and review result |
| --- | --- |
| M4 original-magazine hiding | Uses `skinned_groups` and the MOD packet parser; corrected to the shared record layout. |
| Empty-hand left/right arm hiding | Uses the same parser for prepared shoulder subtrees, regardless of ordinary part-visibility mode; covered by the same correction and mixed-stream tests. |
| Rigid weapon-part hiding | Receives one native-built packet and replaces its descriptor at `+0x28`; field location is unchanged, and it does not walk records independently. |
| Chest items, held/dropped magazines, cartridges and world weapons | Submit through the shared scene service/native packers. No changes to drop lifetimes, trajectories, capture leases or foreground flags were introduced by storage expansion. |
| Brush, depth, shadow, subdivision and debug renderer paths | Covered by arena/member relocation and index conversion; all compiled instruction contracts checked on the installed image. Runtime acceptance still matters. |
| Weapon/hand pose caches | Retain opaque native surface pointers and scene identities, not old scaled IDs or a private record stride. |
| Scene diagnostics and analyzer | Capture declares the active layout; old recordings remain interpreted as 256 KiB. External profiles are EXE-hash gated and must not be reused by editing only their hash. |
| Motion history, vertex/blend data, GPU triangle indices and FX mesh descriptors | Separate formats/arenas. Their four-byte operations, 16-bit indices or 56-byte descriptor sizes are not surface-record stride bugs and are not rewritten. |

Whole-image hidden-sentinel comparison review found the ten relevant native
surface readers already in the patch set, plus four relevant writers. Other
`-3` comparisons are not surface stream walkers. The current source review found
one MOD C++ stream parser, now centralized. Regression coverage includes hidden
entries at the beginning, middle and end, nonzero padding, 512 hidden records,
truncation/count failures and the existing rigid-group bound. These are source,
format and offline tests; they do not claim HMD acceptance of every weapon or
render technique. In particular, verify arm omission and shadows in gameplay.

## Bank lifetime and performance

Each native frontend retains its own bank at the native `0x1039300` separation.
The matching backend may still consume the prior frame while the next frontend
writes. The unused virtual gap in the static backing storage preserves this
address relationship; it is intentional, not a leaked allocation. Do not merge
the banks or reuse one scratch area across frames. The module does not touch the
gap at runtime. It keeps the native atomic allocation cursor and reset sequence.

The conversion is installed once at startup. Rendering does not run a new
per-model lookup, allocate memory per frame, copy an extra surface stream, or
take a new lock. The diagnostic Debug switch controls observation only; it does
not toggle this required storage/index format. Never hot-toggle the format.

## Maintenance and validation

`tools/generate_scene_surface_contract.py` accepts explicit `--text`, `--pdata`,
`--output` and optional `--audit` paths. It requires a clean owned native code
capture and Capstone, only for offline regeneration. No native dump or developer
machine path is required by the normal build or shipped runtime. Check the
entire generated diff; the census and unit/alignment tests must still pass.

Run `vr-scene-surface-storage-tests` for all 65536 ID values, offsets above the
old limit, the last addressable slot, packed and shadow draw keys, mixed hidden/
skinned/rigid streams, brush alignment and bank separation. Region-capture tests
also ensure new files declare the 512 KiB / eight-byte layout; old captures keep
their original 256 KiB interpretation. Native instruction preflight checks code
bytes only and is not an HMD reproduction capture.

For performance investigation, compare the same saved scene and viewing route,
frame timing, cursor high-water mark and actual missing-surface warnings. Inspect
the separate diagnostic switches before attributing capture overhead to storage.
Do not use reduced model count/FOV, disabled television materials, or preferential
drawing of equipment at the cost of missing world surfaces as acceptance.

Any rollback must revert the allocator bounds, storage relocation, all index
encoders/decoders, hidden strides, brush prefix and warning/layout reporting as
one change, then restart. A partial rollback can corrupt geometry instead of
merely restoring the old limit. The 512 KiB candidate still requires in-game/HMD
acceptance before the reported disappearance can be marked resolved.
