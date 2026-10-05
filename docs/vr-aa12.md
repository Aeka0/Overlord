# AA-12 physical interaction

AA-12 uses MW2CR's closed-bolt behavior. It shares the detachable-magazine
transaction, per-weapon ownership and physical interaction system; it does not
introduce a general shotgun reload policy.

- Eight-shell magazine, manually extracted after a deliberate 5 cm pull.
- Tactical replacement retains the chambered shell and supports 8+1.
- No last-round hold-open or button-operated bolt release. Insert a replacement
  after running empty, then pull and release the top charging handle to chamber.
- A partial rack leaves the chamber intact; a full rack extracts once and feeds
  once on return. The handle reciprocates during firing.
- Both holding hands use the common mirrored hand poses. The contact is the
  exposed top handle, not the long internal action mesh.
- Existing native shot timing, pellet generation, sounds, muzzle flash, shell
  effects, world pickup, carry and HUD remain routed through the weapon instance.

## Source evidence

The read-only  inventory captured `aa12` and `aa12_reflex`: capacity 8,
fire mode 0, fire interval 150 ms, shotCount 8, boltAction 0, segmentedReload 0,
reloadAmmoAdd 8. One accepted shot debits one shell before native ballistics
generates its pellets. No per-pellet ammunition transaction is added.

The existing reload observation rejected any nonzero reloadAmmoAdd. Admission
now also accepts positive additions equal to the magazine capacity for an
explicitly registered feed. Segmented reloads, partial additions, unknown
families and unsupported capacities remain rejected. Zero-add observation,
including the existing separate cylinder path, is preserved.

Exported receiver `h2_viewmodel_aa12_base` has 14 bones, SHA-256
`e7505533790245f78285348e5726fd86d56eee2c6fbac7374aa7f7574191a8d2`.
The native wrappers resolve to this receiver, with `attach_h2_red_dot_sight_vm`
added for reflex. Native visibility masks select iron sights or the optic rail.
Other shared optics/accessories require an actual matching receiver mount;
unknown underbarrel assemblies cannot inherit this support pose.

| Part | Source bone | Parent |
| --- | --- | --- |
| Magazine | `tag_clip` | `j_gun` |
| Visible magazine shells | `j_bullet` | `tag_clip` |
| Reciprocating action | `j_reload` | `j_gun` |
| Top charging handle | `j_reload_end` | `j_reload` |

The handle child hierarchy is checked during assembly admission. Magazine
extraction includes 978 body triangles and, when loaded, 832 shell triangles
from separate source surfaces; no selected triangles cross bone-group borders.

Poses use `h2_wpn_sho_aa12_idle` frame 0, `h2_wpn_sho_aa12_reload` frame 50 and
`h2_wpn_sho_aa12_first_pullout` frame 17. Header comments retain animation hashes.
Offline skinned-hand review measured closest surface contacts of 2.012 mm for
the magazine and 0.148 mm for the handle. These are contact checks, not a
substitute for headset ergonomic testing.

The native maximum handle stroke is 101.858 mm. Both the final frame of
`fire_ads_last` and `empty_add` return `j_reload` to its forward rest, so an
open-bolt or follower-lock state would disagree with this game's presentation.
The magazine mouth is authored at gun-local Z 3 cm after geometry review.
Exported cm values are converted to native units once in the generated poses.
Export files are authoring evidence, never runtime or build dependencies.

## Validation

- `aa12_profile_tests.hpp`: 112 assembly permutations, native feed admission,
  parent/identity corruption, missing mounts, competing optics, magazine shell
  visibility, authored contacts and receiver-relative magazine clearance.
- `manual_magazine_reload_tests.hpp`: the former bullpup suite now also covers
  AA-12. Both hands, tactical/empty changes, used-magazine reinsertion, partial
  pulls/racks, full-cycle single extraction, failed commits, interruption and
  ammunition conservation all use the same reusable scenarios.
- Weapon-grip and physical-reload suites pass; Debug x64 client compiles.

Headset acceptance remains: bare/reflex AA-12, top-handle reach, magazine
grasp/insertion, eight accepted automatic shots, empty replacement plus rack,
tactical 8+1, and hand transfer/dual wield with an already adapted weapon.
