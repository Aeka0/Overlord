# M14 EBR, M82A1 and WA2000 physical interaction

This guide covers the M14 EBR, M82A1, and WA2000 physical reload profiles using
reviewed model, animation, and native weapon contracts. Exact model admission
and headset acceptance remain specific to each listed assembly.

| Weapon | Native family / capacity | Magazine removal | Empty action | Button release |
| --- | --- | --- | --- | --- |
| M14 EBR, including arctic receiver | `m14`, campaign `m21`, or exact `m14ebr_thermal` / 10 | Physical pull or spare-magazine latch strike | Locks open; pull and release the action to close | No |
| M82A1 | `barrett` / 10 | Physical pull | Returns closed; charge after an empty reload | No |
| WA2000 | `wa2000` / 10 | Physical pull | Locks open; handle returns independently | Yes |

All three use the shared closed-bolt magazine/chamber rules, including tactical
reloads with a retained chambered round. A button cannot eject their magazines.
A 5 cm pull transfers the old magazine and its actual remaining ammunition;
partial pulls leave it seated. M14 also accepts the existing AK-style deliberate
spare-magazine strike at the rear latch: the old magazine drops while the spare
stays in hand. Its shared strike radius is 4 cm with 7.5 cm rearm separation;
speed, direction and one attempt per approach are still required. It preserves the chamber and empty lock; it adds no button release.
M82 and WA2000 do not accept latch strikes. Left/right holding and handover use the same per-weapon state and
transaction boundaries as existing physical weapons.

Contingency's resource pool also contains `m14ebr_thermal`: the primary viewmodel
is the existing 18-bone `h2_viewmodel_m14ebr_base`, with the seven-bone bipod and
five-bone `attach_h2_thermal_scope_2_vm`. Its exact native name now selects the
existing ten-round M14 recipe after scene admission. The unused 23-bone legacy
model slot is not newly admitted. Resource-pool presence is distinct from native
weapon registration: ordinary in-level `give` cannot create an unregistered
weapon merely because its resource exists. This change does not alter native
registration or thermal rendering. Assembly/name/capacity regression passes;
in-game acceptance of this exact variant remains pending.

M14's retained `m14_scoped` descriptor confirms its family, capacity, scope and
bipod recipe. M82's earlier native firing trace identifies `barrett` with ten
loaded rounds; `m82_bipod_stand_thermal` is a scripted turret helper, not the
playable weapon. Live H2 inspection confirmed WA2000 has ten rounds; the earlier
six-round candidate was rejected by native admission. Campaign M14 EBR uses
`m21_scoped_cloth_silenced`, `m21_soap` and `m21_scoped_arctic_silenced`, all ten
rounds. Their `attach_h2_silencer_03_vm` is explicitly bound alongside the scope
and bipod; matching the receiver alone is insufficient.
Exact scene, native name, capacity and mode checks remain mandatory; a mismatch
must report failed admission rather than mutate an unrecognized weapon.
M82's closed empty state follows the exported last-fire and empty animation
behavior; it has not been verified in a fresh native session.

## Poses and moving parts

Each receiver uses its own native idle wrists/fingers and magazine grasp.
M14/M21 share both AK edge-grasp styles, positioned at the M14's external
charging tab. Left wrists sit 3 mm closer to the receiver; independently fitted
right hooks wrap the tab with the palm outside the receiver. M82 uses the shared
index/pinky hooks for both hands at the exposed handle end (gun-local
26.399, -5.711, 8.918 cm). The legacy right-index override, including its malformed
ring-finger chain, is removed. WA2000 retains its native bilateral handle grasps.
Only the hand posture is mirrored for opposite-hand
manipulation; a side-mounted handle remains on its actual side of the gun.

| Receiver | Action bone | Travel | Source action frame | Magazine frame |
| --- | --- | --- | --- | --- |
| M14 EBR | `j_reload` | 9.459 cm; locked at 8.454 cm | Shared AK index/pinky edge styles | `reload` 47 |
| M82A1 | `j_bolt` | 17.183 cm | Shared AK index/pinky edge styles for both hands | `reload` 80 |
| WA2000 | `j_reload` | 16.440 cm | `reload_empty` 96 | `reload` 20 |

WA2000's separate `j_bolt` follows the native handle take-up curve: the first
8.22 cm moves the handle alone, then the bolt follows to approximately 11.19 cm.
The shared bolt presenter retains that bolt position while the action is locked
and the non-reciprocating handle returns. Native firing still animates the bolt.
Both physical knobs are part of `j_reload`. The left hand uses the original
left-knob grasp; the right hand reflects the complete grasp about the receiver's
measured symmetry plane to reach the right knob. The profile explicitly opts
into bilateral contact mirroring. Ordinary single-sided handles still mirror
only hand posture around their existing contact. Acquisition and the latched
visual pose use the same per-hand grasp; wrist rotation cannot switch knobs.

M14's `j_bullet` is a direct `tag_clip` child in the native skeleton, but its
geometry depicts the chamber cartridge. An explicit receiver-local chamber
anchor overrides magazine motion; visibility follows `chamber_loaded`, and
the original cartridge stays with the chamber when the magazine is detached.
The counted magazine uses separate immutable copies below the measured feed lips;
its 0–3 population never includes the chamber in its ammunition count.
M82 explicitly retains
`tag_clip -> tag_bullet2 -> tag_bullet -> tag_bullet_single`, including the
intermediate carrier, rather than flattening its skeleton. The common binder
accepts only the declared parent chain; other weapons keep their existing
strict parent checks. WA2000 reuses the existing receiver-parented round path
for `j_ammo`, computing its magazine-local transform from the source binds.

Detached magazines use receiver-derived rigid body/round subsets. Offline
geometry checks found no triangles crossing those selected part boundaries;
the loaded native rigid surface/GPU checks still decide runtime admission.
No source asset, global material or depth rule is changed by these profiles.

The source scope and bipod assemblies are admitted through the existing bounded
attachment binder. The native scope's `tag_scope_ads_on` subtree is hidden in
VR, keeping its ordinary physical scope geometry. M14's unused suppressor-cap
variant is hidden according to the selected muzzle assembly. Unreviewed or
conflicting attachments are rejected. Magnified optic rendering is outside
this physical interaction adaptation. Magnification and deferred thermal
rendering are documented in [native ADS](vr-native-ads.md) and
[independent optic-rendering feasibility](vr-optic-rendering-feasibility.md).

## Checks and headset acceptance

`vr-weapon-grip-tests` covers 268 source-based assembly combinations, attachment
order, exact parent chains, finger/part binding and native name/capacity
isolation. `vr-physical-reload-tests` covers all four receiver recipes in both
hands: physical extraction, partial pulls, tactical reinsertion, action cycling,
empty reload/button differences, ammunition conservation and failed native
commit/retry. Existing weapon suites and spatial HUD checks also pass.

The HUD now uses whole-centimeter parameters relative to the rear grip:
17 cm forward, 2 cm gun-local left, 11 cm up; camera-facing placement adds
12 cm outward and 4 cm up. The left hand adds a further 6 cm outward, for an
18 cm outward screen offset. Both hands retain the original ammo art and font.

In the headset, check all three weapons in either hand, support grip and
handover, empty and partial magazine removal/reinsertion, and each action's
empty-lock behavior. Check the side-mounted action reach, magazine alignment,
scope visibility and both HUD positions. `vr_hands_status` and
`vr_reload_interaction_status` distinguish profile/asset rejection from a
gesture that has not reached its contact or travel threshold.

Only compact authored transforms and contracts ship in the source tree.
Pose headers retain animation hashes; export paths, parsers and raw geometry
are not build or runtime dependencies.

M14/M21 charging capture now uses a 6 cm margin and the shared right-receiver
side boundary to avoid the magazine during reloads. See
[weapon interaction refinement](vr-weapon-interaction-refinement.md).
