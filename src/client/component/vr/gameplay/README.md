# VR gameplay boundary

New VR code that changes gameplay belongs in this directory: player-command
adapters, locomotion/turning policies, independent hand/weapon control, and future
weapon interactions. Keep runtime sampling, tracking-space math and stereo
rendering in the parent infrastructure layer.

Dependency direction is gameplay -> tracking/input snapshots and game adapters.
The runtime must not call gameplay rules or include these headers. In particular,
one runtime frame is published for all consumers; gameplay must not independently
poll SteamVR. Command mutation happens before insertion into H2 command history,
never when prediction reads that history.

`interaction_coordinator.cpp` owns domain dispatch. `hand_interaction/runtime`
owns arbitration/publication; `carry_interaction` exposes server-only carry
operations and `weapon_scene.hpp` contains copied scene data without runtime
calls. `hand_service` owns shared hand binding and presentation dispatch.

Weapon recipes are registered in `weapon_registry.cpp`; consumers include the
lightweight query/capability headers and explicitly include any concrete recipe
they inspect. Common pose descriptors/math/mirroring, body-supply layout and
falling trajectories are independent of a particular weapon or inventory ledger.
See the [current module boundaries](../../../../../docs/vr-gameplay-interaction-architecture.md#current-implementation-boundaries-2026-10-02).

Asset extraction/auditing and third-party research tools remain local, outside
production source and packaging. No exported models, animations or audio are
required by the controller input layer.

## Readability and host boundary

The VR directory has its own `.clang-format`. Apply it to files being changed;
do not reformat the surrounding H2-Mod infrastructure as part of a VR change.
Use named request fields for input flags and targets, and keep state changes,
lock scopes and native commit order visible in the control flow.

Carry decisions in `weapon_carry_grip.hpp` operate on copied inputs, validated
contacts and the VR inventory. `weapon_carry_runtime.cpp` and
`carry_interaction.hpp` remain host adapters: they admit the H2 server frame,
coordinate native selection and inventory commits, and publish presentation
snapshots. Keep game types, addresses, script calls and scheduler ownership out
of the decision headers. This boundary describes dependencies; it does not
establish independent source provenance or change the applicable licenses.
