# Release validation and delivery

Release candidates must pass artifact checks and runtime acceptance before
they are treated as deliverable. A successful build, test suite, launcher probe,
deployment or archive operation establishes only the result of that operation.
Any known unresolved crash, failed level load or missing game output blocks
delivery in the affected scope.

## Read-only publication-buffer incidents

The optimized v142 client retained memcpy writes while placing mutable
publication buffers in read-only `.rdata`:

| Version | Buffer | Evidence |
| --- | --- | --- |
| Beta 3 | `vr::gameplay::weapons::carry::render_models` | Confirmed level-load write access violation; 15,416-byte copy into read-only storage |
| Beta 4 | `vr::engine_stereo_output_merger::published_report` | Confirmed level-load write access violation in `end`; 204,840-byte copy into read-only storage |
| Beta 4 | `vr::engine_stereo_backend_target::published_report` | Artifact scan found 30,024 bytes of read-only storage despite assignment/reset paths; identified risk, not a separately observed crash |

The Beta 4 candidate passed all feature-entry-point checks and the previous
carry-only storage check. This missed the other publication buffers. Symbol
presence and one previously failing object's placement were insufficient.

The first workaround pinned three objects to readable/writable `.vrstate`.
`client_feature_parity_tests.py` requires a unique emitted symbol whose complete
range is writable for each object. That workaround corrected the observed
storage placement, but did not address the compiler's lost-write behavior.

Further isolated investigation on 2026-10-09 reproduced aggregate-assignment
miscompilation in MSVC v142 19.29.30154 without game or project dependencies.
With `/O1` or `/O2` and `/GL`/`/LTCG`, a nonzero-initialized mutable aggregate
can be placed in read-only storage or have its publication write eliminated.
An explicitly writable section fixes placement but does not reliably preserve
the write: the minimal writable-section reproduction still reads stale data.
Storage checks must therefore be accompanied by publication/readback semantics.
Release now disables WPO across every project, including common code and
dependencies, with `/GL-` and `/LTCG:OFF`. Ordinary `/O1` optimization and full
symbols remain enabled. The per-buffer section workaround is removed.
`vr-aggregate-publication-tests` checks publication/readback/reset semantics;
packaging runs it and audits the actual client EXE/PDB. CI includes Release.
Re-enabling WPO requires a compiler that passes this qualification and fresh
Release runtime acceptance; other toolchain versions have not been qualified.
The original WPO candidates remain unaccepted, regardless of storage checks.

The open-issue audit also identified a separate Beta 4 checkpoint/Resume fault
in issue #33: the engine reads a null FX-definition pointer in a client whose
three publication buffers are already writable. Its deeper cause is not yet
proven. An optimization workaround must not be claimed to resolve that incident
without an affected-scope retest. Issue #36 separately records a native display
transform failure followed by XR-session teardown; its reported runtime error
does not by itself identify an external driver failure.

## Required acceptance evidence

1. Audit the exact staged configuration's EXE and matching PDB. Check feature
   entry points, all required mutable storage and packaged resources. Review
   comparable mutable publication buffers when this failure class recurs.
   Run `vr-aggregate-publication-tests` built for that configuration and policy;
   writable storage does not establish that updates survived optimization.
2. Run the staged Release executable in a fresh process. Load a campaign level
   through the supported startup path and reach a playable scene. For a
   regression, repeat the same level and path that previously failed.
   Also exercise death/checkpoint reload and Resume from the main menu when
   validating changes to FX definitions, asset retirement or saved-state paths.
3. Validate the affected gameplay/rendering path and the VR runtime/controller
   combination claimed for delivery. Record desktop and HMD acceptance
   separately, with their actual tested scope.
4. Record the artifact identity and results: configuration, product version,
   PE/PDB identity, tested level/runtime/controller path and observed outcome.
   An unchanged version label does not identify an unchanged executable.

Executable, loader or gameplay-resource changes require fresh acceptance for
the affected runtime scope. Documentation-only changes do not require another
gameplay run. Never transfer acceptance from a different build configuration
or earlier binary to the staged Release candidate.

Candidates may be packaged and deployed locally for testing. Keep their runtime
acceptance marked pending and describe them as candidates for retest. Only mark
delivery ready after the required evidence is recorded and known blockers are
resolved. Outstanding runtime checks must remain explicit in status reports.
