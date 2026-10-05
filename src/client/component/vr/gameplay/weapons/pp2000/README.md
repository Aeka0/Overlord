# PP2000 physical interaction

The integral front grip uses two-hand aiming. The shared detachable-magazine
policy uses a twenty-round magazine, button release and chamber plus-one, with
no last-round hold-open or bolt-release control. Native automatic firing remains
native; this adapter owns magazine/chamber transactions after admission.

- Button release removes the magazine while preserving a chambered round.
  Fetch a spare at the waist and insert it through the main-grip mouth.
- Tactical replacement retains 20+1. The last shot leaves the action forward.
- After an empty magazine change, pull the top handle through its full stroke
  and release, or guide it forward, to chamber. Insertion alone does not chamber.
- Short pulls do not extract; a held rear stop extracts only once. A second
  complete forward/rear cycle can extract the next live round. Duplicate input,
  failed native compares and interrupted magazine escrow use the shared guards.

## Source and geometry

The native/animation candidate name is `pp2000`; the exported receiver is
`h2_viewmodel_p2000_base` (18 bones), SHA256
`769aefc737b8671dc1a281672b59849b666613440ad2738670c5bad179c385cb`.
The source export has no mixed-weight vertices.

`h2_wpn_pst_pp2000_idle` frame 0 supplies the rear/support wrists and 36
parent-local finger/palm rotations. `reload` frame 30 supplies the magazine
grasp. The real magazine is `tag_clip`, with a roughly nine-degree bind tilt;
its mouth and insertion axis are authored in that same frame. Its body has
624 vertices / 600 triangles. `j_bullets` is a single child stack group with
1,422 vertices / 2,016 triangles and shares a material with unrelated controls.
The existing receiver subset/visibility service selects body/stack by bone,
preserving shape, material and unrelated parts. The world magazine is not used.

The moving top rod is `j_reload`; its front tip is child `j_reload_end`.
`first_time_pullout` travels 64.19 mm; authored stroke is 65 mm with a 58 mm
full-stroke threshold. The native fire clip also translates the rod (67.67 mm
peak); the adapter retains that source firing motion instead of treating the
part as a non-reciprocating handle.

The left grasp uses `first_time_pullout` frame 9, carried from the slightly
deflected tip to its straight idle pose. The long rod is not rotated to mimic
tip folding. No separate folding control is introduced. Offline nearest skin
contacts are 0.492 mm at the tip and 0.427 mm at the magazine; these proximity
checks and reviewed source projections do not establish HMD comfort. Pose
headers retain animation hashes and convert centimetres to native units once.

Only reviewed attachments with actual receiver mounts are admitted: common
EOTech, red-dot, thermal and suppressor aliases. Missing ACOG, sensor, laser or
underbarrel mounts fail rather than selecting an unrelated support pose.
Native rifle-mode source notetrack keys provide clip-out, clip-in and one
compound chamber sound on completion; shot audio remains native.

## Verification

Tests cover 80 attachment/order/glove combinations, two-hand aim when vertical
or reversed, source parent contracts, handle contact and tilted mouth clearance.
Dedicated mechanics tests cover last-shot closure, empty replacement requiring
a rack, tactical 20+1, failed button compares, repeated rear contact and escrow
interruption. Shared button-magazine tests now initialize empty actions according
to each profile's hold-open policy, instead of fabricating a lock for all guns.

Live native WeaponDef name/capacity and HMD acceptance remain pending. Candidate
`pp2000` / bounded `pp2000_...` names still require the complete scene, twenty-round
base capacity, primary mode, unique ammo cells and native idle before writes.
Akimbo animation assets are outside this receiver/hand adapter's scope.
