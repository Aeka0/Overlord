# RPD physical reload binding

Status: implemented using the shared M240/MG4 belt feed and Mini Uzi open-bolt
mechanism. Offline binding, mechanics and gesture tests pass; final hand fit
and spatial clearance still require headset testing.

## Verified assets

Read-only game inspection found `rpd`, `rpd_reflex` and `rpd_acog`,
all with capacity 100 and no alternate weapon. The loaded receiver
`h2_viewmodel_rpd_base` has 51 bones, 24 surfaces and one LOD, matching the export.
Its reload source is `h2_wpn_lmg_rpd_reload`, SHA256
`1f923260472fd933065c580299dcea0630f1e99096e3b64f247c7f12846fbc3c`.
The following approximate ranges were sampled every three frames; they describe
animation evidence, not final interaction thresholds.

| Part | Bone / hierarchy | Observed motion |
| --- | --- | --- |
| Optic bridge | `j_rail`, receiver child | About 123 degrees around local -X |
| Bridge release | `j_rail_release`, receiver child | About 57 degrees plus 1.15 cm translation |
| Top cover | `j_main_cover`, receiver child | About 93 degrees around local +Y |
| Bolt | `j_bolt`, receiver child | About 8.95 cm rearward travel |
| Handle | `j_bolthandle`, child of `j_bolt` | Travels with the bolt in this source |
| Drum | `tag_clip`, receiver child | Removed/replaced with a distinct hand pose |
| Belt | `j_bullet1` under drum, then a chain through `j_bullet20` | 20 articulated links |
| Drum details | `j_bullet_cover`, `j_ring1`, children of drum | Both animate independently |
| Receiver details | `j_lock`, `j_ammodoor`, `j_ammodoor2` | First two move during reload; second door stays still in this clip |

ACOG, reflex, EOTech and thermal attachment tags are descendants of `j_rail`.
Iron-sight tags belong to `j_main_cover`. Thus optics must follow the bridge,
while the cover and bridge remain separate articulated parts.

The drum and its two cosmetic children contain 5,800 exported triangles, with
no mixed vertices or triangles crossing into other parts. This is compatible
with the shared rigid-subset extraction approach. Read-only native inspection
confirmed all three rigid groups on surface 3, their surface-global indices,
live buffers and `m/mtl_wpn_h2_lmg_rpd_drum_base` material. This is admission
evidence; it does not replace headset rendering verification.
Both `j_bolt` (420 vertices) and `j_bolthandle` (238 vertices) have real geometry;
the M240/MG4 concealed-bolt declaration must not be copied onto RPD.

Model-specific attachments include `attach_h2_rpd_foregrip_default_vm` (one bone,
`tag_foregrip`) and `attach_h2_rpd_bipod_vm` (four bones, root `tag_bipods`). They
have explicit assembly contracts alongside the shared optic/silencer set.

Read-only `dcemp` sampling additionally found `rpd_digital`,
`rpd_digital_acog` and `rpd_digital_reflex`, all with capacity 100. Their
`h2_viewmodel_rpd_base_digital` receiver has the same 51 bone names, parent
indices and byte-identical bind matrices as the base receiver. The one-bone
`attach_h2_rpd_foregrip_default_vm_digital` likewise matches the base grip.
The digital receiver and grip now have explicit registrations, reusing the
existing poses, belt mechanism, optic bridge and shared digital optic contracts.
The digital reload recipe selects its own receiver as the rigid drum source,
preserving the skin when the drum is held or dropped. Unknown receiver names,
incorrect bone counts, attachment parents and ammunition capacities still fail
admission. Regression coverage exercises both skins with bare and optic
assemblies, including the captured digital ACOG and reflex models; headset
validation of the new skin remains pending.

## Interaction and integration

1. Press grip at the receiver's bridge-release control once. The bridge falls
   aside automatically; the pressing hand remains on the receiver lever.
   Holding grip cannot retrigger the release or turn into a bridge grasp.
2. Lift the top cover once the bridge is geometrically clear. Keep cover angle
   and bridge angle independently owned by the weapon instance.
3. Remove the drum; use the existing atomic removal of its old belt. Replace
   the drum and lay the belt using the shared box/belt workflow.
4. Close the top cover, then return the bridge. Prevent either moving part from
   passing through the other. Interruptions or hand transfers retain both angles.
5. Cock whenever desired, before/during/after replacement. Neither bridge nor
   cover motion changes the sear. A folded optic bridge alone should not invent
   a new firing gate; the shared cover/feed/sear rules still govern firing.

The shared belt mechanism has an optional articulated bridge and clearance
interlock, with no timed sequence or separate RPD ammo ledger. Cover movement
requires at least 90% of the bridge's measured 122.8-degree travel. An open
cover prevents bridge movement below that clearance. One free-hand component
lease uses spatial selection and mechanical state to distinguish bridge, cover,
drum, belt and handle. The fall uses a 0.22-second cosmetic transition; cover
and return-grasp admission wait for its visible endpoint. Manual return uses
the late native pushing pose. Partial manual angles survive release and
tracking interruption; failed release writes require a fresh press to retry.

An explicit rig contract validates the handle as a direct child of the separate
bolt. The presenter restores the independently operated handle after posing its
parent, avoiding double translation. The idle and fire clips do not animate
the internal bolt; VR retains it at the measured 8.95 cm rear position when
cocked and presents its return after a shot. Last shot/empty release leaves it
forward. This visual movement has no ammunition authority.

The drum lid and ring retain their idle poses and belong to the rigid drum
subset both in hand and when dropped. They introduce no extra reload step.
The drum latch (`j_lock`) and belt pad (`j_ammodoor`) stay at idle pose;
they no longer follow the top cover. Only the bridge release detail moves.
The articulated belt remains separate and follows the drum's actual placement
through insertion. All 34 mechanical rest poses bind within the native bone
capacity; attachment transforms continue to inherit from their real parents.

Useful native timing witnesses: scope open at frame 14, cover open 30, drum out
57, drum in 104, cover close 158, hit 176, scope close 200, chamber 229. These
provide source poses and sounds only; player movement must drive VR mechanics.

Authored hand frames are handle 236, cover 30, drum 56 and belt 140. Bridge
release uses frame 17 at `j_rail_release`; manual return uses frame 201 at
`j_rail`. These replace the earlier incorrectly shared frame-14 bridge pose.
Both hands retain the original handle grasp and gain index/pinky hook styles.
Tests cover the exported hierarchy with optics/default grip/bipod, all 20 belt
links, both hands across hinge travel, bad parent relationships, all handle
styles, empty/nonempty reloads, four cocking orders, bridge/cover interlocks,
failed writes and focus loss with round conservation.

Headset check: press the bridge release, lift cover, remove drum, draw/insert a new
drum, lay belt, close cover, return bridge. Repeat with an early/mid/late cocking
stroke, release each hinged part halfway, and try closing the bridge with the
cover open. Check optic motion, hand fit, drum cosmetics and belt curvature.
The pressing hand must not be dragged along by the automatically falling bridge.
