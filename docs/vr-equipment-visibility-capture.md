# Equipment visibility: coordinated capture

Preparation is offline. Do not open/read the game process or sample merely
because the game or SteamVR exists. Outside an explicitly coordinated window,
assume the headset is OFF. A focused controller snapshot does not prove otherwise.

## Operator protocol

1. Finish script/profile preparation and offline tests first. The operator need
   not wear or keep the headset on during this work.
2. Request fresh confirmation that the operator is ready to begin with the
   headset on. Explain the magazine/turning actions before requesting readiness.
   A brief delay taking the magazine must not terminate the capture. Never reuse
   confirmation from a previous window or wait silently for it during sampling.
3. Immediately BEFORE executing the capture command, send a plain-language
   message: "Now starting a 30-second capture. Hold the magazine in view and turn
   to reproduce its appearance/disappearance. Keep the headset on until I say
   capture has ended."
4. Execute exactly one window, without parallel analysis or other commands.
5. Immediately AFTER the command returns, send a plain-language end message,
   including whether the window was usable, aborted or empty. State explicitly
   that the operator may pause or turn off the headset. Tool output alone does
   not replace either start/end message. Announce failures as well as success.
6. Review the saved result offline. A second window requires its own readiness
   confirmation, start message and end message. Never silently extend/retry.

The default is one **30-second reproduce** window. The operator may take the
magazine, adjust their view and turn repeatedly to reproduce appearance and
disappearance within that window. Keep the magazine in view where possible.
After the end notice, record whether the symptom reproduced and any approximate
timing the operator can identify. The script cannot see or automatically label
HMD visibility; `reproduce` is deliberately not a per-sample visual label.
Optional `visible`/`missing` phases remain available for later controlled checks.
The explicit maximum is 60 seconds. Never extend the agreed deadline because
part of the window was invalid, or automatically start another window.

## Read-only witness

`tools/capture_equipment_visibility.py` opens the selected process with query and
read permissions only. It requires a local `h2-equipment-v1` JSON address profile
with an exact EXE SHA-256. Profiles contain build-specific addresses and belong
in the ignored incident/build directory, never in portable default configuration.
An EXE mismatch aborts before any offset-based reads. Refresh the profile from
matching symbols and verified native contracts rather than disabling that check.

Example, after the coordinated start message (substitute the PID and local
profile/output paths):

```text
python tools/capture_equipment_visibility.py --profile build/equipment-visibility-profile.json --pid PID --phase reproduce --operator-ready --seconds 30 --output build/equipment-reproduce-UNIQUE.jsonl
```

Paused/inactive gameplay, unfocused/invalid controller poses, stale snapshots and
no held magazine mark the corresponding samples **ineligible**, without stopping
the agreed window. Scene reads resume when readiness recovers. Ineligible samples
never contribute model observations; an invalid readiness check either before
or after the scene read rejects that observation. Timestamped `validity_runs`
and reason counts distinguish the invalid spans from usable clues. These are
readiness observations, not proof of continuous state between reads or visual
appearance/disappearance labels. An entirely invalid 30-second window ends on
time and reports no usable data. It never waits indefinitely for a headset.

Each JSONL row is flushed as written; a normal stop,
read failure or interruption gets an end record. A file without an end record is
partial. EXE/profile mismatch, process read errors, invalid native layout or
operator interruption still abort rather than interpreting unsafe data. A
`usable_window` only means the completed window contains eligible model clues;
it does not mean the whole window was eligible or the defect was proven.
The selected process is never paused, injected into or modified. The
tool does not launch the game, SteamVR or a recording service.

External memory reads are **best-effort**, not synchronized engine-frame captures.
Unchanged scene counts do not prove common frame ownership. Record raw scene
entries, visibility bytes, input identity/freshness and placement counters as
clues only. Do not decode final surface coordinates from a mutable frontend
pointer without matching frame/arena/surface ownership. No such speculative
decode is included in this witness. An empty completed window or a window with
only changing table bounds is not a successful reproduction. Compare results
with the operator's observations; in-engine instrumentation may still be needed.

Offline validation (no live process access):

```text
python tests/vr/equipment_visibility_capture_tests.py
python tools/capture_equipment_visibility.py --help
```

## Native surface-buffer exhaustion

The reported `MAX_SCENE_SURFS_SIZE((1 << (16 + 2))) exceeded` warning names the
262144-byte frontend surface-packing arena. Native rigid packing reserves bytes
through frontend+0x540e14 and returns zero when the allocation end exceeds
0x40000. Its draw-info stores a 16-bit offset in four-byte units; increasing just
the capacity check is not a safe arena expansion. The native preparation caller
clears visibility on a zero result. This is separate from a mod placement callback
omitting a model before native packing runs.

The diagnostic candidate adds optional observations at the existing scene/model
boundaries. It does not change the allocator, surface indices, render scheduling
or acceptance of models. Chest fallback now explicitly reports `retain`, with
the same placement behavior as its previous `unchanged` return.

After deploying this candidate, enable **Scene surface diagnostics** in launcher
debug settings and restart. It is independent of CPU performance capture and
defaults off. When off, the scene/model observers are not attached, even during
a CPU performance recording. When on but idle, they remain detached; no model
records, geometry copies or diagnostic file writes run. If both recording
options are off, the capture writer thread is not started.
Inside a coordinated window, `vr_perfStart 30` records
for 30 seconds and stops automatically. The no-argument performance command
retains its existing ten-minute bound. Do not start it without the operator
protocol above, and do not substitute file-write completion for an end message.

Analyze the saved binary with `tools/analyze_region_capture.py`. New artifacts:

- `surface_budget.csv`: arena cursor/model count at generator entry, after mod
  submission and native return, with native record index and draw type. Stage 3
  instead belongs to an owned model build and joins its model event ID.
- `rigid_models.jsonl`: owned model result, callback owner, record/entry/model
  identity, camera hash and submitted/resolved positions copied synchronously
  inside native preparation. Outcome 1/2 invokes native packing, 3/4 does not.
- `evidence.json` surface-budget summary: cursor high-water mark and native
  model failures observed while the shared cursor is over limit. Concurrent
  allocations can advance that cursor; correlation is not exclusive attribution.

Television animation coinciding with missing models is an operator observation,
not proof that the television renders another 3D view. Compare generator draw
types and per-view cursor growth to establish whether extra views, excessive
scene admission, or another allocator producer consumes the budget. The older
980 record-rejection counter delta cannot identify this by itself. Event loss,
partial captures and missing model/context rows remain explicit limitations.

##  coordinated native witness and correction candidate

The operator confirmed both equipment visibility changes and television texture
animation during region-1790083444717-p51484.bin. The 30-second recording ended
normally, with 150 reported dropped events. The analyzer observed a maximum
313592-byte cursor and 780 owned native model failures with the cursor over
262144 bytes. Recorded generator entries all used record index 0, draw type 3.
One frontend had all 852 stereo-publication observations and 211 over-limit
generator returns; the other had 4. Every captured generator entry started with
cursor zero. This points to per-frame scene pressure, not a persistent growing
allocation, and does not establish an extra television-camera pass.

The culling correction candidate replaces the matrix volume's constant angular
`halfIPD / near` expansion with a virtual rearward apex expressed entirely in
homogeneous projection. For optical outer slopes L<0<R and half-IPD h, choose
`d=max(h/-L,h/R)` rounded outward. Projection uses forward distance `z+d`,
including asymmetric X/Y constant terms, and depth numerator `near+d`. Both
translated eyes fit; the near plane remains `z=near`, while the surplus width at
far depth is a fixed world-space allowance instead of growing with distance.
Actual camera origins, view axes, rendered eye projections, model placement and
the native surface-index/capacity contract are unchanged. Legacy scalar-frustum
consumers retain their wider conservative envelope; only matrix-based culling
and shared tessellation receive the tighter volume.

CPU regressions cover both eyes' near/far corners, asymmetric bounds, near/IPD
sweeps, the right-edge guard and rejection of far points admitted only by the
old angular expansion. Native slot finalization was checked offline: it performs
full matrix multiplication/inversion, including the homogeneous constant terms.
The candidate still needs native/HMD acceptance and measured budget reduction;
it is not proof that every allocator consumer uses matrix-based admission.

Follow-up: the operator reported no improvement. The next coordinated capture,
region-1790084749092-p57876.bin, still observed 301376 bytes and 855 owned native
failures over the original limit. The culling candidate was withdrawn. The next
candidate is the complete [512 KiB storage/index conversion](scene-surface-storage.md).
Do not retain or reintroduce the unsuccessful culling candidate based solely on
its mathematical containment tests.
