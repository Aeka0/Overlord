# Region performance and right-eye evidence recorder

This explicitly armed diagnostic does not change rendering. It adds no GPU
queries, readback, Flush, or scene admission rules. It uses frontend, stereo
owner, DrawIndexed and OpenVR trace boundaries, plus four loader-installed
call-site wrappers for H2's existing scene job waits and surface producer.
Those wrappers forward all original arguments and preserve the native order;
arming capture does not install hooks or run additional waits.

## One-run workflow

1. Enable **VR Settings > Debug > CPU performance capture** before launching
   with SteamVR/HMD. This loads the job probes and recorder worker; other Debug
   options can remain off. In the menu or level, execute
   `vr_perfStart` once. Wait for the `region capture started` message.
2. Visit a normal area, then a known slow area, then return to the normal area.
   Several seconds at each is enough; no exact timing or gunfire is required.
3. Optional labels: `vr_perfMark normal`, `vr_perfMark slow`,
   `vr_perfMark visual`. Mark after noticing a symptom; the earlier records
   are already being collected. A visual label is not an automatic diagnosis.
4. Execute `vr_perfStop` when finished. Wait for the saved-path message.
   Normal shutdown also stops the recorder. Do not arm GPU/scene census.

Files are written relative to the game working directory:
`minidumps/vr-region-capture/region-<unix_ms>-p<pid>.bin`.
The probe module defaults unloaded. Once loaded, recording defaults OFF and
auto-stops after 10 minutes or 128 MiB. A new capture
can start after the previous one has finished. Failure to write only disables
the recorder; it does not shut down VR or change visual output.

## Evidence and limits

- CPU wall times: frontend scene hook, each original eye owner call, native
  conversion, deferred dynamic upload, Present, WaitGetPoses and Submit.
- Successful right-eye Submit intervals measure application stereo submission
  cadence, NOT HMD display FPS/reprojection or GPU execution duration.
- Per-eye DrawIndexed calls and submitted index counts on the owner thread,
  not unique vertices/triangles or total GPU draws. Draw/instanced/worker-context
  calls are outside this counter. No extra per-draw API metadata queries.
- Eye origins and record identity at clone / owner-entry / owner-return.
- List 18/24 pointer at +0x90, count at +0x98, technique field, descriptor owner
  and whole-descriptor hash at those boundaries. Other lists 0..25 with a null
  pointer and nonzero count in that surface range are also retained.
- Owner failure code and eye/completed-mask, runtime changes and manual marks.
- Backend entry/return, claim, clone and admission CPU timings (including failed
  attempts); original initial/surface/FX completion waits; surface producer timing.
- At clone and native job boundaries: record/frontend/thread identities,
  frontend completion flags +0x541BE8/BEC/BF0/BF4, list24 type/owner. These reads
  are not an atomic snapshot or an admission proof. Match identities and time;
  engine arenas are recycled. A producer return precedes its caller's BF0 store.

The list hashes are CPU descriptor hashes, NOT GPU texture/material/RT binding
proof. The same hash does not prove the pointed-to payload was unchanged. A
null/nonempty range at an early initialization stage alone is not a fatal
invariant: compare the stages and actual consumer before drawing conclusions.
Capture does not dereference surface/material pointers or alter failed lists.
In particular it must not suppress transparent geometry to avoid a crash.

This supports the current combined investigation: right-eye transparency loss,
other surfaces turning black, and severe slowdown near some lights/effects.
Light proximity is a user-observed correlation, not an established cause.

## Bounded overhead and crash behavior

Producers write fixed-size records into a fixed 16,384-row MPSC queue. Producers
never wait for the writer: contention/full drops are explicitly counted. Only
the diagnostics thread creates/flushes files, at approximately 200 ms intervals.
OFF state performs cheap gates (draw counter is thread-local). Owner snapshots
read only the already-owned fixed-size scene record. Slow spans are classified
offline at >=33.333 ms; a continuous capture includes preceding/following data.

A crash can lose the last unflushed interval. The analyzer reports missing stop
footer, partial row tails, unmatched timing starts and recorded drop counts;
it must not treat those files as complete. C++ stream flush is not a guarantee
against disk/OS loss on BSoD. Keep native crash dumps alongside the capture.

## Offline analysis

`python tools/analyze_region_capture.py <capture.bin> [--output <directory>]`

- `timeline.csv`: one-second sample counts, mean/p95/max timings and draw counts.
- `lists.csv`: normal and anomalous list snapshots, stages and camera locations.
- `phases.csv`: raw phase begin/end and job state records, including identities,
  completion flags and list24 header. Wait identity is a predicate, not a record.
- `evidence.json`: marks, null/nonempty lists, owner failures, slow spans with
  camera positions, stereo submission gaps, capture-loss/completion metadata.
Nested phase timings overlap: do not sum them as independent frame costs.

When cumulative dropped-event counts increase, the analyzer conservatively
excludes timings that overlap the interval between the two writer health
reports. Without a unique invocation token, losing a begin/end can otherwise
pair reused record addresses across seconds. `clean_stop` only proves a stop
footer exists, not loss-free data. `possible_loss_windows_s` and
`timing_intervals_excluded_for_loss` describe this exclusion. Raw phase/list
rows remain available, but individual missing records cannot be reconstructed.

The binary is versioned `H2VREG01`: 8-byte magic, little-endian uint64 QPC
frequency/PID/Unix milliseconds, then 88-byte rows (11 uint64s). In `row`, type
is `region_capture::kind`; trace row a is the explicit stable ID mapped in
diagnostics.cpp, and pair/eye preserve the original API-specific a/b values.
Do not interpret every trace a/b as a stereo family/eye identifier.
Appended types 12/13/14 are phase begin/end/job state. They use pair=record (or
wait predicate), eye=phase ID, a=frontend. Phase end b is the return result;
claim returns a publication sequence, clone/admission a boolean. Job state
b packs BE8/BEC, c packs BF0/BF4 (low/high 32 bits), d=list24 type, e=list24 owner,
f=family when known. These types append to the format; old captures have none.

Tests: `vr-region-capture-tests` exercises queue saturation, concurrent
producers/loss accounting, read-only scene snapshots and draw counts;
`python tests/vr/test_region_capture_analysis.py` checks timing/anomalies,
partial crash files and submission intervals. These are not real-game visual
or performance acceptance tests.

## Scene input completion contract 

Production now publishes immutable stereo view inputs immediately before the
original frontend draw-surface generator. This publication does not copy the
unfinished scene payload. The backend retains its exact record/frontend/camera
checks and joins the native initial, surface and FX CPU completion predicates
before making the first stereo record copy. H2's existing job-pumping wait is
used before any private eye state or MOD GPU lock; the original owner calls and
their original waits remain intact. Capture arming does not enable this policy:
it is the production ownership contract, independent of recording.

Two additional recorded phases are `scene_publication` (6) and
`scene_completion` (5). Completion's final job-state checkpoint precedes clone;
all four completion flags must be nonzero there. This establishes the native
CPU input boundary, not completion of every GPU command list or an atomic
snapshot of all pointed-to data. Compare loss-free intervals and matching
frontend/record identities when evaluating results.

`vr_status` exposes `scene_input_handoff` attempts/published/failures and
`scene_input_completion` attempts/complete/pending/failures/last_error.
Here pending counts admissions that initially found unfinished CPU inputs;
it is cumulative, not the number currently blocked. After an idle snapshot,
complete should equal attempts with failures zero (an in-flight call may
temporarily differ). Early-publication failures must also remain zero.

Offline ordering tests cover native wait order, incomplete flags and identity
changes. HMD testing confirmed real-game acceptance of this change
and reported a very large scene-performance improvement in The Gulag. No
recording commands were run during that acceptance: there is no measured FPS
gain or post-fix counter comparison. This is real-game user acceptance, not a
conclusion from the offline tests alone.

Preserve early immutable view publication and completed CPU payload cloning
together as the accepted path. A normal -> slow light/effect area -> normal
capture remains useful for future regressions, not a requirement to repeat this
accepted test. This result does not certify the separate historical Gulag
loading/preload device-loss issue as fixed.
