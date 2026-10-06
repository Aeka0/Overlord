# VR diagnostics and acceptance workflow

Checked. This is the common evidence-collection entry point for black screens, hangs, crashes, performance issues, and interaction failures. See the [runtime guide](vr-runtime-rendering.md) for rendering contracts and [build and validation](development.md) for test targets.

For implementation responsibilities and status-report extension points, see the
[diagnostic module boundaries](../src/client/component/vr/diagnostics/README.md).

## Establish the identity of the failing run

Record the build commit and uncommitted changes, EXE/PDB hashes, actual process start time, level/checkpoint, triggering actions, SteamVR/HMD, GPU/driver, and relevant settings. Preserve matching binaries and symbols so a later rebuild does not lead to interpreting an old dump with a new PDB.

Save existing evidence before restarting. All `minidumps/` paths below are relative to the game's working directory, not the source repository root. Keep status, traces, dumps, and captures for one incident together and record their timing relationships.

## Choose the smallest useful observation

For headset-dependent reproduction, agree an explicit capture window with the
operator. Announce start immediately before the command and completion immediately
after it returns, including failures. Outside that window assume the headset is
off; running game/SteamVR processes are not evidence of a valid live reproduction.
See the [equipment visibility capture protocol](vr-equipment-visibility-capture.md)
for an agreed 30-second reproduction window, invalid-span marking and offline preparation.

| Symptom | Inspect first | Conclusions and limits |
| --- | --- | --- |
| HMD waiting while the desktop works | Backend, initialization stage, tracking, native pairs, Submit, and last error in `vr_status` | Increasing tracking counters establish the pose path only, not stereo image submission |
| Game and SteamVR use different GPUs | Graphics adapter identity / `graphics_mismatch` | Verify device matching before changing scene algorithms |
| One-eye artifacts, reflection jumps, or missing transparency | Pair/eye/generation, owner failures, resource isolation status, and region capture if needed | The same texture pointer or descriptor hash does not prove identical contents or timing |
| Slowdown near an area or effect | Owner, Present, WaitGetPoses, Submit, and input-completion phases in region capture | CPU wall time, application submission intervals, GPU time, and HMD display frame rate are different measurements |
| Console commands produce no output | Latest status snapshot, live trace, and thread stacks in watchdog dumps | The command may not have been scheduled or may be waiting on a lock; this does not establish SteamVR initialization failure |
| D3D11 error dialog or device removal | HRESULT, `GetDeviceRemovedReason`, and the last Present result and timestamp | A graphics error may produce no dump because the dialog path may not go through SEH |
| Process crash | Raw `.dmp`, `.txt` sidecar, exception code/context, and matching PDB | The faulting module identifies the failure location, not automatically the sole root cause |
| Incorrect weapon or hand behavior | Relevant gameplay status commands and interaction diagnostics | Separate input, mechanical state, native results, and visual presentation before adjusting model offsets |

## Status commands and saved files

`vr_status` prints complete VR status and attempts an atomic write to:

```text
minidumps/overlord-status-latest.txt
```

This file holds the latest snapshot and is overwritten. Copy it into the incident's evidence directory after capture. If writing fails, an older file may remain; check its timestamp and contents before attributing it to the current command.

Common topic-specific commands are listed below with their registered spelling and capitalization:

| Area | Commands | Further reading |
| --- | --- | --- |
| Core runtime | `vr_status` | Backend, poses, rendering, submission, and error snapshots |
| Controllers / hands | `vr_input_status`, `vr_hands_status` | [Controller interaction](vr-controller-interaction.md) |
| Weapon firing | `vr_fire_status`, `vr_dual_fire_status` | [Independent firing](vr-independent-weapon-fire.md) |
| Reload / chamber | `vr_reload_status`, `vr_reload_interaction_status`, `vr_chamber_status` | [Reload diagnostics](vr-reload-diagnostics.md) |
| Cylinder / tube feeds | `vr_cylinder_status`, `vr_tube_status` | Corresponding weapon topics |
| Carry / pickup | `vr_carry_status`, `vr_carry_selection_status`, `vr_interaction_status` | [Carry](vr-weapon-carry.md), [world interaction](vr-world-interaction.md) |
| Interaction geometry | `vr_interaction_debug_status`, `vr_reloadWell_status`, `vr_hkSlap_status` | [Interaction diagnostics](vr-interaction-diagnostics.md), [HK slap](vr-hk-slap-diagnostics.md) |
| HUD / narrative | `vr_weaponHud_status`, `vr_narrative_status` | [Weapon HUD](vr-weapon-hud.md), [narrative UI](vr-narrative-ui.md) |
| Terrain | `vr_tessellation_status` | [Shared tessellation](vr-shared-tessellation-and-head-pitch.md) |

These commands primarily provide snapshots. The shared file path above is documented here only for `vr_status`; do not assume other commands generate equivalent files. `vr_reinit` and `vr_recenter` change state, so preserve the original symptom evidence before running them.

### Controller input: one report after the problem

For an input problem, ask the player to close the VR dashboard, return to the
game, release the controller buttons and sticks, then try moving each hand,
moving a stick, and pressing a trigger. Run `vr_status` once and share
`minidumps/overlord-status-latest.txt`. The player does not need to compare
several logs or interpret individual counters. Do this before restarting the
game; input history is retained across runtime reinitialization within the
process, but a new process starts a new history.

Both `vr_input_status` and the saved report include a bounded `input_history`
section. Each hand pose and major action reports `ever_valid`, `valid_samples`,
`activity_samples`, `last_valid_age_ms`, its current condition and duration,
availability `losses`, and the last loss and rejection with backend, API result,
and age. A loss also records the input sequence, reference generation, runtime
input availability, and gameplay context. `runtime_focus_known=0` means that
availability was not queried at that boundary, for example after a tracking
failure. `publication_age_ms` includes neutral lifecycle publications;
`sample_age_ms` measures the last action-sampling publication separately.
`sample_fresh=0` identifies a stale or missing sample without inventing a new
disconnect event; the accompanying summary labels the recorded state accordingly.
`ever_valid=0` means it was never
observed as available in this process; `losses>0` establishes an earlier
available-to-unavailable transition. `age_ms=-1` means no such observation exists.

An active action with zero activity may simply be an untouched stick or released
button. An unavailable action is different. The producer records separate
conditions for input setup failure, runtime input unavailability, action
synchronization/query failure, inactive actions, invalid poses, and reported
device disconnection. `focus=0` alone cannot identify which one occurred.
The last API error and last loss survive recovery. `input_transitions` retains
the most recent eight changes in chronological order, including affected
channels, reasons and codes. This keeps a recovered controller failure visible
alongside a later dashboard focus loss when both fit in the retained history.
`discarded>0` explicitly marks older changes that have been overwritten;
identical unavailable frames do not fill this history. Availability loss is not proof of a
physical controller disconnect, and input availability does not establish
native acceptance of firing, ADS, or movement.

These observations reuse the existing input publications with fixed-size
counters and timestamps. They do not add runtime polling, per-frame console
output, automatic exports, or an unbounded recording.

## Performance or intermittent visual issues: one continuous capture

Before launching, enable **VR Settings > Debug > CPU performance capture**.
Keep the other Debug options off for the baseline measurement. `vr_perfStart`
cannot load missing hooks during gameplay; restart after changing the selection.
Detailed view probes and automatic report persistence now default off; manual
`vr_status`, crash reports and stall detection remain available. See the
[performance review](vr-render-performance-review.md) for the startup gates.

```text
vr_perfStart
vr_perfMark normal
vr_perfMark slow
vr_perfStop
vr_status
```

After starting, move from a normal area to the problem area and back. `vr_perfMark` adds optional labels and can be issued after noticing a symptom; exact frame timing is unnecessary. Wait for the saved-path message after stopping, then preserve the `.bin`. Recording defaults to off and automatically stops after 10 minutes or 128 MiB.

Analyze offline:

```bat
python tools/analyze_region_capture.py "capture.bin" --output "capture-report"
```

The analyzer writes `timeline.csv`, `lists.csv`, `phases.csv`, and `evidence.json`; choose local paths as needed. First check for dropped events, missing footers, truncated tails, and unmatched timing records. `clean_stop` does not mean loss-free data. Nested phases must not be added together as total frame time, and successful right-eye Submit intervals are not HMD display FPS.

See [region capture](vr-region-capture.md) for the full format, queue limits, and interpretation of data loss. Start with this lightweight recorder rather than enabling every GPU census or readback probe and changing the workload being measured.

## Crashes and soft freezes

Crash handling is implemented in [exception.cpp](../src/client/component/exception.cpp); VR status and watchdog reports are in [diagnostics.cpp](../src/client/component/vr/diagnostics.cpp). Available evidence can include:

- `h2-mod-emergency-*.dmp/.txt` and `h2-mod-crash-*.dmp/.txt`: exception context and raw reports.
- `overlord-live-trace.txt`: recent progress from the bounded trace.
- `h2-mod-soft-freeze-*`, `h2-mod-scene-stall-*`, `h2-mod-interop-stall-*`, and `h2-mod-backend-stall-*`: reports/dumps from the corresponding detection paths.

These files exist only when the relevant handling path triggers and writing succeeds. Preserve raw dumps and sidecars first; a missing or empty historical ZIP does not mean raw evidence is absent. Exception handling must not read an entire large dump into memory, format it, or compress it before saving the original evidence.

Start by identifying which thread stopped where, the faulting read/write address, whether execution entered the suspected API, and the Present result and device-removal reason. An asynchronous GPU-driver crash can occur after the operation that caused it. The last trace event is a timing clue, not a complete call stack.

For hangs, correlate owners/waiters with complete thread stacks. A missing `runtime_submit_result` does not justify ignoring the preceding GPU gate, and `Responding=True` does not rule out a rendering livelock. If the console cannot execute commands, use existing watchdog evidence or a live thread dump rather than repeatedly entering status commands in an attempt to restore scheduling.

## Record reusable validation conclusions

After resolving an issue, use the following structure and identify what was not covered:

```text
Status: implemented / offline tests passed / hardware accepted / pending validation
Build identity: commit, workspace changes, EXE/PDB hashes, running process identity
Reproduction: level, devices, configuration, actions, and symptoms
Evidence: files, time range, completeness, and missing data
Cause: established facts and associations that remain inferences
Fix constraints: ownership, generations, native behavior, and rejected approaches
Validation: commands and results; scope of the actual observations
Remaining limits: untested devices, levels, reconnection, long-term operation, etc.
```

User confirmation can establish visual acceptance within the tested scope without inventing counters or requiring another capture merely to record it. Do not report FPS or improvement percentages without measurements. When multiple changes ship together, do not claim their individual contributions have been established. Put reusable conclusions in `docs/`; keep local scripts, dumps, and failed experiments in ignored local directories.
