# VR diagnostic modules

The public API remains in `../diagnostics.hpp`. `../diagnostics.cpp` owns the
trace ring, callback timings, watchdog state, status snapshot collection, and
report persistence. Keep synchronization and mutable diagnostic state there.

Status formatting is split by subject:

| File | Report sections |
| --- | --- |
| `runtime_status.cpp` | Runtime, controller input, display settings, and head pose |
| `frontend_status.cpp` | View publication, scene input completion, culling, and target routing |
| `execution_status.cpp` | Output merger, draw hooks, execution census and owner admission |
| `scene_status.cpp` | Scene batches and dynamic upload observations |
| `history_status.cpp` | SSR history, consumers, and eye-resource isolation |
| `effect_status.cpp` | Census control and effect timeline comparisons |
| `owner_status.cpp` | Owner timing, GPU timing, readback, and model state |
| `census_status.cpp` | Dynamic FX invocation, binding, and arena evidence |
| `resource_status.cpp` | Resource operations, constant buffers, and resource candidates |

`status_sections.hpp` declares internal append functions using forward-declared
snapshot types. Each implementation includes its own source interfaces.
`format_helpers.hpp/.cpp` contain shared scalar and enum text conversions. Sections append
to the same stream in the coordinator's original order, preserving its numeric
formatting state. Reports already collected by the coordinator are passed by
const reference; do not copy large reports or independently resample those inputs.
Existing section-local observations remain at their original points in that order.

`input_status.hpp` formats the same controller history for `vr_input_status` and
the runtime report. Its fixed-size counters and timestamps are owned by the
existing controller publication lock in `../controller_input.cpp`; the formatter
does not query a runtime or reset observations. Keep action availability,
runtime focus, recorded loss conditions and native gameplay acceptance distinct.

The status file lock still covers the entire format-and-replace transaction.
Do not parallelize report sections or move formatting outside that lock: doing
so could mix stream state, reorder observations, or allow an older snapshot to
replace a newer one. Render/runtime producers remain independent of file I/O.

`runtime_code_snapshot.cpp` exports bounded, validated executable-memory ranges
for offline analysis. The renderer invokes it at the existing loader boundary,
after target validation and before its observation patches. It owns neither
render hooks nor live scene state, and performs no GPU or OpenVR calls.

`native_flare_probe.cpp` remains a separate explicitly invoked sampling tool.
Keep feature-specific probes separate from report formatting and runtime state.

## Renderer evidence

`renderer_evidence` owns bounded CPU capture storage, report formatting and
evidence persistence. Native hooks publish a one-shot copy with release/acquire
ordering. Once ready, the bytes and metadata remain immutable for the process.
The diagnostics control thread performs file I/O; frontend, backend and Present
callbacks never serialize reports or wait for persistence.

Terminal readiness requires the current process to persist the exact execution
report and its dedicated and aggregate manifests. Existing files from another
process cannot satisfy readiness. Retirement manifests identify obsolete probe
artifacts; the retired stereo replay implementation has no production entry.

Native hook installation, rollback, owner-thread transactions and rendering stay
in `engine_stereo_renderer.cpp`. The guarded baseline-registry capture retains
its existing control-thread observation contract and does not mutate the game.

Composition observers use exclusive registration handles. A diagnostic owns its
explicit layer and releases that handle during teardown; it cannot replace or
clear another layer. Registration does not drain an in-flight renderer callback.
Keep existing lifetime guards and owner-thread retirement in the component.
