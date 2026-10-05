# FAL physical interaction

The ordinary and underbarrel-shotgun rifle assemblies share one twenty-round,
closed-bolt physical profile and two-hand aiming. The shotgun selects its native
rifle-mode support wrist/fingers; free-hand and rear-hand bases stay unchanged.
Shotgun switching, firing and tube loading are outside this adapter's scope.

- Pinch the seated magazine and pull forward/down by 50 mm to remove it into
  the hand. A partial pull leaves it seated.
- A held replacement magazine can strike the rear paddle toward gun-forward
  to drop the old magazine. This uses the shared AK swept latch mechanism with
  FAL-specific mesh contacts. The replacement stays held; ammunition is conserved.
- The release button never ejects a magazine. After last-round hold-open,
  insert a replacement and release the action with the button or a full handle
  pull/return. With no magazine, button release closes empty. An inserted empty
  magazine's follower prevents release.
- Tactical replacement preserves the chambered round (20+1). Inserting into an
  already closed empty chamber requires a full handle cycle. Holding a handle
  at the rear does not repeatedly extract ammunition.

## Source geometry and poses

Offline exports inspected on 2026-09-11:

- Receiver `h2_viewmodel_fn_fal_base`, 21 bones, SHA256
  `7dc28d5381843b0dfe3a643bd7b60a222fcc5720d215942bd4c1073bcf990144`.
- `h2_wpn_asl_fn_fal_idle` and `_shotgun_idle` frame 0 supply the native hand
  poses. `attach_h2_shotgun_vm` has five bones rooted at receiver `tag_shotgun`.
- `reload` frame 22 supplies the new-magazine grip. The original clip already
  performs a magazine strike; its thrown-magazine direction is not reused as
  the authored hand-pull axis.
- `first_pullout` frame 15 supplies the left charging-handle grip, translated
  with `j_bolt` back to closed rest. Closest reference hand/tab skin is 0.620 mm;
  magazine skin contact is 0.594 mm. These are offline proximity checks, not HMD
  ergonomic acceptance. Pose headers record individual animation hashes.
- `j_bolt` is the non-reciprocating handle group: first-pullout travel is
  143.65 mm, configured stroke 144 mm, full-stroke threshold 135 mm. The native
  regular and empty-fire clips do not move it. The asset has no separate
  internal-bolt bone, so last-round lock is mechanical while the handle returns
  forward, as with other non-reciprocating handles.

The visible seated magazine is `tag_clip_02` with child `j_bullet_02`.
`tag_clip` / `j_bullet` are the original animation's replacement copy. Both
magazines share a material: each body has 2,034 vertices / 2,974 triangles and
each round group has 669 vertices / 1,008 triangles. There are no mixed-weight
vertices. The existing exact receiver subset service supplies the independent
magazine, preserving body/material shape instead of substituting the world clip.

An optional shared physical-profile mask hides the animation-only magazine
subtree while physical ownership is active. It validates disjoint receiver
roots and rejects missing, duplicate or mechanically overlapping roots. It does
not hide the native replacement during ordinary nonphysical reloads.

The mouth is authored at gun-local Z 2.9 cm. Two complete oriented boxes cover
the magazine top and slanted base plate; the strike target remains the release
paddle's mesh centre. Successful impact removal delays insertion by 300 ms.
Distances are converted from export centimetres to native units once; gesture
thresholds use metres. See [shared contact rules](../../../../../../../docs/vr-magazine-latch-contact.md).

## Admission and validation

`fal` / bounded `fal_...` names propose a rifle candidate. A complete receiver,
validated attachment topology, twenty-round native base capacity, primary mode,
stable idle and unique inventory/ammo cells must agree before writes. Alternate
shotgun feeds and unknown attachments cannot inherit rifle authority. Native
clip-out, clip-in and compound chamber notetrack keys resolve through the actual
weapon; the chamber sound plays once on action completion.

Tests cover bare/shotgun, optics/suppressors, attachment order/gloves, both
mechanical hand owners, physical pull, tactical and empty magazine strikes,
button/manual return, failed transactions, repeated contacts, wrong-direction
and tracking-jump rejection, mouth clearance and duplicate-magazine visibility.
Live WeaponDef/loaded-variant capture and HMD acceptance remain pending. No
claim of coverage for unobserved receiver skins is implied.
