# Weapon configuration

Weapon definitions are immutable C++ data. Use designated initializers so that
readers can identify each value without counting arguments or opening a second
file. Designators follow the field order in the corresponding configuration
type; do not change a type's layout to make one recipe easier to initialize.

## Where each setting belongs

| File | Responsibility |
| --- | --- |
| `profile.hpp` | Receiver/variant identity, hand binding, acquisition/release/blending, visible attachments and selected feed capability. |
| `mechanics.hpp` | Ammunition capacity, release policy, chamber behavior and explicit mechanical capabilities. |
| `reload_interaction.hpp` | Contact thresholds, physical action travel, manipulation policies and grasp counts. |
| `reload_profile.hpp` | The complete reload recipe: bones, geometry, sound sources, rendering details and exact native-family admission. |
| `poses.hpp`, `reload_poses.hpp` | Measured transforms and mesh/bone data, including the existing source evidence. Some older families also define their feed here. |

For examples, see [ACR interaction](acr/reload_interaction.hpp),
[MG4 belt feed](mg4/reload_profile.hpp), [Magnum cylinder](magnum44/profile.hpp)
and [SPAS-12 tube feed](spas12/profile.hpp).

## Units and defaults

- `physical_reload::profile` distances are metres. Its axes and cosine
  thresholds are dimensionless; pose counts are integer indices/counts.
- Manual magazine distances are metres, and latch strike speed is
  metres/second. Rotating-bolt angles are radians.
- Assembly acquisition/release distances are metres, and blending is seconds.
- Receiver poses, bone contacts and mesh bounds retain their documented native
  model units. Do not replace them with interaction thresholds or convert them
  again at configuration time.

The data types and named contact defaults live in
[`physical_reload_configuration.hpp`](../physical_reload_configuration.hpp).
They include only standard-library data and forward declarations, with no game
types, addresses, controller implementation or host lifecycle. Gesture logic
and validation remain in their respective controllers.

Select `physical_reload::defaults` and `profile_defaults` explicitly when a
recipe shares that policy. Keep measured stroke, lock position, pull direction,
contact exceptions and asset identity local to the weapon. Existing pistol
capture and M4/M16 family policies retain their own owners; matching numbers do
not establish that two mechanical families can share admission rules.

Use `split_sound_configuration` fields for cycle notetracks and splitting
options. `retain_close` preserves the existing close sound; `split_removal`
selects the first part of the existing magazine-removal recording. These
settings do not create a new alias or change the source WeaponDef.

Bare configuration objects retain their existing defaults, including zero
required fields. Shared contact values do not register a weapon or make an
incomplete recipe valid. Optional hardware remains explicitly bound to its
local immutable definition.

## Changes and verification

Keep configuration-expression cleanup separate from tuning or capability
changes. Preserve source comments, native names, sound keys, exact family
predicates and receiver topology. A new skin may reuse reviewed geometry while
still requiring its own asset and native-family admission.

For a readability-only migration, compare compiled configuration fields and
run the existing tests for the affected feed families. Catalog/compile checks
verify recipe integration; they do not replace in-game or HMD acceptance.
Separating configuration dependencies does not establish independent source
provenance or change applicable licenses.
