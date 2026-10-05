# VR source map

Start with the subsystem you intend to change:

| Area | Entry and responsibility |
| --- | --- |
| Runtime and device lifecycle | `vr_component.cpp`, `runtime_backend.cpp`, `vr_runtime.hpp` |
| Native stereo | `engine_stereo_renderer.cpp`; preserves H2 transactions and owner threads |
| Presentation composition | `eye_composition.hpp`; explicit ordered layers with exclusive registrations |
| Shared rigid geometry | `spatial_math.hpp`; no skeleton, gameplay or native dependencies |
| Diagnostics | [diagnostics](diagnostics/README.md); bounded captures and control-thread persistence |
| Native entry points | [H2 SP adapter](h2/README.md); fixed signatures and documented query semantics |
| Interaction and gameplay | [gameplay](gameplay/README.md); simulation, arbitration and native commits |

The parent directory contains runtime, tracking and rendering infrastructure.
Gameplay adapters consume shared input and presentation snapshots. Native reads,
writes, hooks and asset retirement retain their verified thread and lifetime
contracts. A readable pointer or a cached pose is not proof of current ownership.

Use the [development guide](../../../../docs/development.md) to select a focused
test target. CPU, mock-runtime and WARP results do not establish HMD acceptance.
