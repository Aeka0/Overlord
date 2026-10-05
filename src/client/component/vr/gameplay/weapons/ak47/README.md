# AK rifle interaction candidate

The registered `ak47_desert_reflex` composite also uses
`attach_h2_red_dot_sight_vm_desert`. Its two-bone hierarchy and bind matrices
match the reviewed tan sight byte for byte. The alias lives in the shared rifle
attachment table and still requires the receiver's `tag_red_dot` parent. This
does not admit unknown optics or change the support-hand pose.

2026-09-09: offline geometry/pose review, synthetic interactions and client build
verified. New in-game sampling was not required for this implementation. Native
runtime presentation and HMD ergonomics remain pending.

The admitted receiver is `h2_viewmodel_ak47_base`, including its `_arctic` and
`_digital` assets. Each supports bare handguard, GP-25 or underbarrel shotgun.
Only the engaged support wrist/fingers change with the underbarrel; the rear
grip and free/reloading-hand basis stay shared. Covers, reviewed optics, sensors
and suppressors use the common rifle attachment validator. Unknown attachments,
incorrect parents, duplicates or simultaneous GP-25/shotgun reject admission.
The suppressor may supply the rifle muzzle; an underbarrel never does.

## Operations

- A fresh offhand trigger press at the magazine body grabs the old magazine.
  Pull forward/down about 5 cm to remove it into the hand. Releasing early leaves
  it seated. Reinsert it to retain its actual round count, or release it to drop.
- Alternatively, draw a spare at the waist, keep the offhand trigger held, then
  sweep its upper-front edge forward against the magazine latch behind the well.
  The old magazine falls away and the spare stays in the hand for insertion.
  Strike contact and insertion are separate transactions; clear and reenter the
  well after striking. Mere overlap, a stationary touch, reverse motion and
  tracking jumps cannot release the latch.
- B/Y has no magazine or action release role. AK never locks open on the last
  round. Inserting into an empty chamber does not chamber a round: grip the
  right-side bolt handle, pull fully and release/return it. Two left-hand poses
  place either the index-finger edge or the little-finger edge against the tab.
  A fresh offhand trigger press chooses the closer wrist orientation and keeps
  that pose for the stroke; release before acquiring the other pose. The rear
  right hand stays on the grip. Short strokes do not feed or extract a round.
- A tactical reload preserves the chamber and permits 30+1. Unclaimed dropped
  magazine rounds return to reserve unless `vr_discardAmmoPenalty` is enabled;
  a held magazine released at the waist recovers its contents either way.
  Falling magazines can be caught through the shared reload-item ledger.
  A full rack extracts/spends a chambered live round; ground pickup is unsupported.

The existing support squeeze lease excludes magazine/bolt acquisition. A failed
native compare consumes the grasp/strike attempt; keeping contact cannot retry
the write. Tracking/focus loss, recenter, ownership changes and assembly changes
use the shared interrupt/refund path. All ammunition commits remain on the
simulation owner; render-time motion and contact geometry cannot write ammo.

No alternate-mode input, grenade operation or shotgun operation is added.
The native alternate-feed exclusion remains. Rifle admission still requires the
matching complete scene, exact native instance, 30-round base capacity, unique
native ammo cells, idle entry, visibility and prepared independent magazine.

## Geometry and source trail

`poses.hpp` samples rifle-mode `h2_wpn_asl_ak47_tac_idle`, `gl_idle` and
`shotgun_idle`, all frame 0. `reload_poses.hpp` uses `tac_reload` frame 19 for the
magazine hand. `bolt_grips.hpp` borrows the actual left-hand closed pose from
`h2_wpn_smg_mp5k_reload_empty` frame 9, also used by the M9 overhand adaptation.
Its two wrist orientations and contact points are fitted to the AK tab using
the exported `viewhands_us_army` skin. Source hashes are beside the authored data.
Positions are converted from export centimetres to native units once; finger
and palm rotations stay parent-local with live glove lengths. Each manipulation
pose includes 18 joints: the 15 finger segments, two palm joints and webbing.
The unanimated webbing uses the normalized reference bind rotation. The receiver's
additive translation and parent-local replacement rotation are resolved before
taking the gun-relative hand transform.

The 25-bone receiver binds magazine `tag_clip`, action `j_bolt2`, round siblings
`j_bullet01/02/03` and the latch at `j_trigger`. `j_gun_trigger` is the firing
trigger. Actual bolt handle geometry is on negative gun Y. The original right
hand cocking animation no longer supplies the manipulation pose. Both left-hand
edges contact the outer tab near gun-local (22.22, -5.76, 10.42) cm. The acquisition
box is (21.8, -5.9, 9.7)..(23.0, -3.0, 11.2) cm, corrected from a region about
10 cm behind the protruding tab. These are desktop-reviewed ergonomic candidates;
headset comfort and glove-dependent contact still need acceptance.

Primary tac/GL/shotgun equip actions use the shared suppression policy and 13
parent-local receiver rest poses from `tac_idle` frame 0. This prevents residual
native right-hand cocking and equip-part motion from disturbing the tracked rear
grip or leaving the bolt displaced. The left-arm constraint and bolt rail use the
shared physical presenter; native firing recoil and alternate-feed actions keep
their existing paths.

Base receiver SHA-256:
`a28488bfea1a241a196707a2d10e5ec4bffc4e276f68b15d7b4a265a27ec126e`.
All three camouflage exports have identical geometry and skeletons. Their
magazine body and three rounds occupy one rigid surface among nine receiver
surfaces. The separate world clip has a different shape (rigid fit p95 about
1.85 cm, maximum about 2.24 cm), so this profile uses the actual receiver mesh.

`native_magazine_assets` composes immutable body + 0/1/2/3 visible-round subsets
through the shared `scene_models::rigid_part` service. It retains original GPU
vertices/materials and owns only selected indices and minimal descriptors.
Construction runs on the main asset owner, outside render/gameplay locks; all
four descriptors must register against the native source before publication.
Unsupported/cross-surface groups fail as a whole. No source assets are modified.
Descriptors are retained for queued work; bounded cache/identity exhaustion
closes admission and is reported by `vr_reload_status` (restart required).

Held and dropped magazines retain their own round-count selection; the empty
inserted magazine hides all three round subtrees. Native recoil still animates
the reciprocating bolt. Sound bindings are exported notification keys resolved
through the actual selected WeaponDef map at playback, never guessed aliases.

## Verification

`ak_profile_tests.hpp` covers 8,064 synthetic assembly permutations, camouflage,
topology rejection, bolt/body contact and world-transform invariance. This count
describes resolver coverage, not observed native combinations. The M4's existing
4,032 combinations still pass through the shared attachment validator.

`ak_reload_tests.hpp` exercises old-magazine escrow/reinsertion, spare strikes,
failed writes, repeated contact, empty/tactical reloads, full/partial strokes and
tracking interruptions. Both bolt poses cover acquisition, wrist rotation while
held, release/reacquisition and one ammunition transaction per completed stroke.
`ak_hand_tests.hpp` runs the shared presenter and part constraint with displaced
native equip bones, checks the tracked rear anchor and parent-local part reset,
and checks that only the left arm and bolt move throughout either stroke. Its
joint checks include palm/webbing rotations. Mechanics tests cover all eleven
operations in 161,051 five-operation sequences for each feed policy. Rigid-part
WARP tests cover nine surfaces, combined groups, native index layouts,
unsupported/cross-surface data and runtime descriptor identity. No test starts
H2 or an XR runtime.

Still to accept in HMD: support comfort for all three underbarrels, both left-hand
bolt contacts and their orientation selection, pull direction/travel, latch-strike
reach and tolerances, insertion, hand/mesh continuity, near-field occlusion, sound
and haptics. The current
magazine rail is a forgiving pull/insert interaction; it does not simulate a
separate front-hook rotational constraint or detachable latch physics.

### Charging fit and latch revision (2026-09-13)

Both hands use index/pinky hooks around the actual charging tab, with the left
wrist 3 mm closer to the receiver. The right hand has an independent facing and
finger fit; no outermost-glove translation is retained. Each style is latched
on acquisition and cannot switch during the stroke. M14/M21 and Dragunov reuse
the same hand fit at their own real handles.

Spare-magazine latch strikes now use the shared `rocking_magazine` policy:
4 cm contact radius, 7.5 cm separation to rearm, unchanged minimum speed,
travel and direction requirements. Mere overlap and tracking jumps do not eject.

### Desert receiver admission (2026-09-13)

The live `ak47_desert_grenadier` uses `h2_viewmodel_ak47_base_desert` and
`attach_h2_gp25_vm`. Its 25 receiver bone names/parents and all normalized bind
rotations match the existing base export; position differences are below
0.000002 cm. The attached GP25's nine-bone bind also matches its source export.
Desert now has a registered immutable reload/render recipe, shares the current
AK hands/mechanics, and derives dropped magazines from its own live receiver.
Assembly count follows the registered skins. Unreviewed receiver and attachment
names remain rejected; the alternate `gl_ak47_desert` feed remains separate.
The captured bone hierarchy is retained in `tests/vr/ak_desert_data.hpp`.

The bare `ak47_desert` was subsequently captured with
`attach_h2_ak47_cover_vm_desert` (one `tag_cover` bone). Its receiver bind digest
is identical to the grenadier variant. The cover is registered against the
receiver's `tag_cover`, preserving the shared bare AK grip and reload recipe.
The captured bare assembly has its own regression case; all four reviewed
camouflages are exercised with and without a cover. Unknown cover aliases and
incorrect attachment parents still reject the assembly.
