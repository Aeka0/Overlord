# KRISS Vector physical adapter

Offline candidate, 2026-09-09. `kriss` / `kriss_*` with base capacity 30 is a
candidate native family. Exact owned native instance, primary mode, capacity,
complete scene topology and prepared magazine assets remain required before
physical ammunition admission. Family matching does not merge weapon instances
or admit dual wield. No additional game capture was needed.

## Interaction

The shared M4-style closed-bolt rules provide B/Y magazine ejection, last-round
lock, button release and full manual handle chambering. A magazine inserted into
an empty closed action still requires a complete pull. Tactical reload supports
30+1; a full live cycle extracts one round, partial pulls do not. Automatic firing,
native shot replay protection, magazine escrow and interruption handling reuse
the existing shared paths. Support ownership excludes magazine/handle grabs and
requires a fresh pinch after leaving the foregrip.

With a loaded replacement magazine seated and released, an unbuttoned free-hand
slap inward on the left receiver release paddle also closes the follower lock.
It reuses M4's whole-hand impact and ownership gates. The target is the left face
of the native `j_switch` mesh, shared by both skins; it remains on the physical
left side when the holding hand changes. Empty magazines and slow touching do
not release the lock. Headset reach/feel for this added route remains pending.

The target is centred on the complete exposed paddle as of September 30.
The old foremost-tip centre could miss the far edge. Native boundary-coordinate
regressions cover both ends, both holding hands and both receiver skins through
the production sampler; the shared 2.5 cm tolerance is retained.

The native `j_reload` handle folds outward about local +Z before pulling back.
`reload_empty` frame 59 provides the 90-degree unfolded orientation; frames
59..61 supply 60.025mm axial travel. The authored manual range is 61mm with a
55mm full-stroke threshold. Acquisition uses the real folded tab, then grasp
presentation unfolds it automatically. The native left-hand frame-61 grasp is
transformed with the handle back to its forward, unfolded position; reference
hand/handle skin distance is 1.330mm. The contact marker maps back to the same
vertex on the folded tab. There is no separate folding input or ammo operation.

The optional shared folding descriptor reuses quaternion blending and the finite
part-return transition. Releasing returns the handle forward and folds it within
75ms, including a grasp with no axial movement. Instance/reference changes reset
the cosmetic transition. It never delays feeding or changes extraction thresholds.
The handle does not reciprocate with shots and returns forward on empty lock.

The receiver has a separate `j_bolt`; `j_handle` is actually the folding stock.
H2's manual reload and empty animations do not move `j_bolt`, while `fire` frame 3
provides a 39.402mm rear position. The adapter uses that distance for retained
empty-lock presentation and an authored linear manual follower. This is not a
sampled native handle/bolt coupling. Native shot animation is preserved. The
stock stays in its idle position through equip suppression, including Vector's
`first_pullout` spelling at both existing presentation boundaries.

The magazine has a tilted insertion rail. Rotations replace the parent-local
bind rotation, so the native tilt is applied once. The receiver mouth is
gun-local cm `(16.01332, 0, -6.39415)`; the lip marker is magazine-local cm
`(0, -0.48936, 0.04)`. The mouth lies above the protruding baseplate at Z=-18.73cm.
The shared rifle capture tolerances are retained. `reload` frame 36 provides the
replacement-magazine grasp, between clip-out at 10 and clip-in at 44.

## Assemblies and assets

Two exported 18-bone receivers are admitted: `h2_viewmodel_kriss_super_v_base`
and `_base_black`. Geometry and bone topology match, but each keeps its own
receiver identity and exact magazine source. Both use native `idle` frame 0:
the same support wrist, thirty finger rotations and two-hand aiming baseline.
The integral `tag_foregrip` requires no additional attachment model. There is no
underbarrel support variant.

Existing common optic and suppressor contracts validate aliases, cardinality,
parent topology and the independent silenced muzzle. Neither receiver has
heartbeat, laser, grenade or shotgun mounts; those assemblies are rejected.
Unknown attachments never silently reuse the foregrip recipe. Synthetic alias
combinations are test coverage, not an assertion that every campaign loadout exists.

All eight surfaces are rigid, with no mixed weights or crossing triangles.
Magazine surface 1 contains 458 body vertices / 504 triangles and 180 vertices /
224 triangles on `j_bullet`, a child of `tag_clip`. Exact receiver subsets retain
both groups independently; empty-round hiding does not remove the magazine.
Surface 5 contains bolt 148/128, handle 406/393, release 154/144 and trigger 300/305;
magazine removal leaves them visible. The world clip has only 315 vertices and
does not match, so it is not substituted. No original asset or new render hook
is modified; the existing bounded retained asset and visibility services are used.

Receiver SHA-256:

- Base: `b7801e00d2aec40edb6f9e5ef7bc2a9e4d263ab149167cc8e5c0a2b9321ece7b`
- Black: `a96b4c8361d02dea269af76b10e34722c4d3d43c0f8450c5632e23301239664a`

Pose headers retain animation names, frames and source hashes. Bolt retention
comes from `h2_wpn_smg_kriss_fire`, SHA-256
`bfbb51032a814b7a441e05fa28b25132290bcc5c77b352f9f8665cf224f994a0`.
The native notetracks supply `weap_kriss_clipout_plr`, `weap_kriss_clipin_plr` and
`weap_kriss_chamber_plr`; compound charging audio plays once on completion and
native firing audio is not duplicated. Raw assets, parsers and previews remain
local audit data, not build/runtime dependencies.

## Verification boundary

Client Debug x64 compilation and twelve VR regression executables passed.
Vector-specific coverage includes 336 skin/optic/suppressor/order/glove-label
combinations, exact mechanical roots, tilted-well clearance, native pose binding,
visibility groups, rejected assemblies and finite folding return. Shared reload
tests include both logical rear hands, both release paths, empty-follower pulls,
partial/full/repeated strokes, magazine ordering, interruption, ammunition
conservation and automatic shot replay. Existing M4, AK, ACR, pistols, Mini Uzi,
revolver and WARP regressions remain covered.

This is offline/synthetic acceptance. Native family/capacity admission, effective
sound resolution, both-eye independent-magazine rendering and HMD ergonomics
still need an in-game pass. Specifically inspect the folded-tab acquisition,
unfolding grasp and authored retained-bolt pose. No deployment or hardware
session was performed.
