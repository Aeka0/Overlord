# Hand module

`pose_solver.hpp` owns skeleton constraints and arm solving. `pose_library.hpp`
binds copied skeleton descriptors; `pose_math.hpp` applies transforms to bones and
fingers. Rigid vector/quaternion math is shared in `../../spatial_math.hpp`.

`service.*` composes domain hand presentations from named input and returns knife
revision and reload-item metadata. `component.cpp`, `native_rig.hpp` and
`native_schema.hpp` adapt native skeletons and preserve their owner, generation
and asset-unload boundaries. The public pose descriptors contain no native calls.

Weapon-specific poses remain in `../weapons/`; arbitration remains in
`../hand_interaction/`. Neither a rendered hand nor a presentation token commits
gameplay ownership.
