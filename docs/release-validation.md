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

`writable_state.hpp` now pins the three affected objects to readable/writable,
non-executable `.vrstate` storage. `client_feature_parity_tests.py` requires a
unique emitted symbol whose complete range is writable for each object. The
original Beta 4 artifact fails the two added checks; the corrected artifact
passes them. This resolves the demonstrated storage defect at the artifact
level, while actual level loading remains a separate acceptance requirement.

## Required acceptance evidence

1. Audit the exact staged configuration's EXE and matching PDB. Check feature
   entry points, all required mutable storage and packaged resources. Review
   comparable mutable publication buffers when this failure class recurs.
2. Run the staged Release executable in a fresh process. Load a campaign level
   through the supported startup path and reach a playable scene. For a
   regression, repeat the same level and path that previously failed.
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
