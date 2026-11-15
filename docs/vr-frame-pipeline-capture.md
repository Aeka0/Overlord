# OpenXR frame pipeline baseline capture

This procedure determines whether native Presents are being spent without a
corresponding OpenXR world submission, and whether frontend admission is being
rejected during native production. It prepares evidence for a possible queued
prediction backport. It does not enable that backport or change frame ownership.

## Preparation

Use the optimized `RelWithDebInfo` client built with `frame_pipeline: schema=1`
diagnostics. Preserve its matching PDB. Select the usual headset/runtime through
the OpenXR backend and retain the current refresh rate, render scale, spectator
view, post-AA, frame cap and reprojection settings throughout the batch. The ZIP
records native current settings, process/build identity, runtime and adapter.
Record the selected headset refresh rate and runtime reprojection policy with
the operator's observations; the analyzer does not query either setting.

The existing [region recorder](vr-region-capture.md) is available for a later
per-call/stall investigation. Its per-frame scene/list snapshots and job probes
are unnecessary for this first rate/admission baseline. Do not enable CPU
performance capture, GPU census or deep scene probes solely for this batch.

Build and analyze offline before deploying. The preparation step does not launch
the game, start SteamVR, install files or collect hardware evidence. Install the
prepared client only after normal game closure and agreement on the capture
session. Do not replace files under a running game.

## One planned session

Agree the live window with the headset operator. Use one game launch and the
same runtime/settings for both cases. Prefer a checkpoint that has previously
shown poor performance and can remain in gameplay without scripted interruption.

1. Load the checkpoint and allow loading, shader preparation and initial VR
   startup to settle. The operator confirms a normal world image and active
   headset/controller tracking.
2. Capture the stationary case: look at a representative scene and press
   **Ctrl+Shift+F8** while the game has focus. Continue looking at that scene
   until the diagnostic ZIP is ready, about five seconds later. Close the
   revealed Explorer window and return focus to the game before the next case.
3. Capture the moving case at the same checkpoint: walk and turn normally, then
   use the same shortcut. Continue for that five-second request without pausing,
   opening a menu, loading another map or changing graphics/runtime settings.
4. Keep both ZIPs together as `stationary` and `moving`, alongside a short note
   about perceived judder, normal/poor performance, refresh rate and reprojection
   policy. Keep the original files intact; labels can be provided by the parent
   folder names. A slow capture is useful evidence and should not be repeated
   merely to improve its numbers.

These are two predefined windows in one session, not an iterative sequence of
probe restarts. If a menu, tracking loss or scripted transition contaminates a
window, retain it and record what happened. Review the existing data before
deciding whether a replacement window is necessary. The operator's visual/HMD
observations remain separate from counter evidence.

## Offline analysis

Run from the source repository, passing the actual capture paths:

```text
python -X utf8 tools/analyze_vr_pipeline_capture.py stationary.zip moving.zip --output output/frame-pipeline/baseline.json
```

The tool also accepts the unique capture folders left behind if ZIP creation
failed. It only reads `status-1.txt` and `status-2.txt` from each request; historical
`previous-status` and `existing-*` files never substitute for a fresh endpoint.
It does not attach to a process, query VR hardware, extract archives or upload
anything. Old clients without the new snapshot sections produce an explicit
format error instead of assumed zero counters.

## Measurements and interpretation

The complete status has four compact, machine-readable sections:

| Section | Meaning |
| --- | --- |
| `frame_pipeline` | Process, OpenXR session, device, tracking reference, runtime readiness and observation time/span |
| `frame_pipeline_counts` | Native/global and runtime Presents, successful `xrEndFrame` world/menu/empty partitions, runtime/owner failures and completed native owner pairs |
| `frame_pipeline_admission` | First failed prerequisite per frontend admission call: unarmed gate, invalid pair, production busy or pair unavailable; plus accepted calls |
| `frame_pipeline_cpu` | Cumulative sample counts and inclusive CPU wall-time sums for the native transaction and each eye |

Counters retain the current process history. Analysis subtracts the first fresh
snapshot from the second and uses the observed interval, rather than assuming
exactly five seconds or using process-lifetime averages. A session/device/reference
or map change, counter regression, paused/unready endpoint, runtime/owner failure,
menu-only frames or unarmed owner invalidates the clean baseline classification.
The raw observations and reasons remain in the JSON for investigation.

For a usable window, inspect `native_presents_per_world`,
`runtime_presents_per_world`, the corresponding rates and the admission deltas.
A ratio near two accompanied by many `production_busy` rejections is evidence
consistent with the proposed frontend/back-end overlap problem. It is not, by
itself, a promise that the backport will double FPS. Admission calls are not
frames, and several calls can refer to one pose family.

With queued predictions, `production_busy` rejects only the family currently
being produced; a different admitted family may proceed. The earlier baseline
rejected any family while production was active. This is the policy being
changed, so compare actual rejected calls and Present/world rates rather than
treating this counter as an unchanged measure of backend CPU utilization.
`frame_pipeline_queue` also records deferred/promoted family publications.

The endpoint snapshots are sequential observations, not an atomic renderer
transaction. Their observation spans are recorded. Readiness is checked at the
endpoints; the operator must describe any intervening pause or tracking loss.
CPU wall times include waits and driver calls. Production GPU timestamp probes
remain hard-unarmed: this capture does not establish GPU execution time, runtime
reprojection quality or actual HMD display FPS. If the usual runtime performance
graph is already available, retain its observation in the same batch; do not
enable an injection/overlay tool solely for this measurement.

## Focused validation

```text
python -X utf8 tests/vr/test_pipeline_capture_analysis.py
```

The fixture checks interval arithmetic, process/session/readiness changes,
counter resets, fresh ZIP provenance, incomplete captures and Unicode paths.
Native changes require the affected client builds described in
[development.md](development.md). Offline checks do not establish a real
OpenXR/headset result.
