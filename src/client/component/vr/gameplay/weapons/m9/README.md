# M9 interaction profile

Status: support grip/aiming accepted by the user in the HMD on 2026-09-06.
Physical reload is a compiled, default-enabled runtime candidate, not HMD-accepted.

- `profile.hpp`: receiver/assembly, aim policy, distances and blend duration.
- `hud.hpp`: camera-facing native ammo billboard size and muzzle-relative anchor
  offsets (Debug tuning candidate; real HMD acceptance pending).
- `poses.hpp`: idle wrist anchors, thirty parent-local finger rotations and
  three equip-rest weapon-part transforms.
- `actions.hpp`: equip clips whose visual part motion is suppressed.
- `mechanics.hpp`: closed-bolt detachable-feed rules (15-round magazine plus
  chamber, follower-aware B/Y release, full live extraction spends one round).
  Native reload plus chamber was HMD-accepted; physical transactions now have a
  compared server-thread adapter and remain pending HMD acceptance.
- `reload_interaction.hpp`: slide travel/contact tuning candidate in metres;
  shared gesture tests pass and native geometry/presentation is connected.
- `reload_poses.hpp`: magazine/slide finger poses, wrist-relative held-magazine
  transform and magazine-top/well geometry. Independent scene instances reuse
  the loaded native `h2_weapon_beretta_clip` asset; no exported mesh is bundled.
  Slide contact uses the sampled hand contact and rear-slide mesh region rather
  than treating an animation wrist as the surface. Tolerances remain candidates.
- `slide_grips.hpp`: original thumb-side grip and an additional overhand grip
  with the pinky toward the muzzle. Each has its own wrist/contact/finger pose;
  the shared selector and gesture controller choose and latch it on acquisition.

Shared arbitration, posing, IK and engine adapters live above this folder.
Each future weapon gets a sibling folder. Register receiver/attachment variants
in `weapon_profiles.hpp`; foregrip/launcher variants need reviewed grip poses.

## Provenance

Existing official-MW2CR Greyhound export `h2_wpn_pst_m9_idle.seanim`, frame 0,
SHA256 `0cde13a5250edbb9a906dd88cbe7b53a1860c34831084f5075b3fb688dc0e2e8`.
Hand hierarchy: exported `viewhands_us_army`. Parent-local curves compose to
wrists/tag_weapon, then inverse gun rotation gives weapon-local anchors.
Export centimeters are divided by 2.54 once. Composed bind wrists and muzzle
were cross-checked against live H2 bind matrices; quaternion signs are equivalent.

The authored rear hand is right; the left anchor/pose is native two-hand pistol
idle. Free fingers use each live model's rest rotations/lengths; gripping uses
sampled local angles. Only compact runtime poses cross the local extraction
boundary, not animations, meshes, parsers or external tools. Removing `.agents`
and all exports does not affect the build. The user accepted the base M9 grip
increment; this is not an exhaustive arm-model/attachment acceptance matrix.

## Behavior and limits

- New side-grip press within 10 cm acquires support. Release, separation over
  22 cm, assembly change, lost tracking/focus or recenter cancels it. Holding
  while entering the region never auto-grabs. Visual blend is 100 ms; logical
  release is immediate.
- Only the rear controller aims the pistol and owns firing. Left rear swapping
  needs a separately authored profile and is not exposed in this increment.
- Fixed wrist/gun relations and hand poses no longer inherit equip/inspect/
  reload hand timelines. Active draw/holster clips use reviewed rest transforms
  for bolt/hammer/magazine presentation. Native equip timing, ownership,
  notifications/audio, ammunition and reload state are not changed. This is
  presentation suppression, not instant equip or an animation-asset patch.
- With physical reload disabled, native weapon-part firing/reload motion remains
  outside equip suppression. With it admitted, mechanical magazine/slide state
  drives those parts, retaining a short native firing-recoil window. Magazine
  seating retargets only the loading arm; support leases exclude magazine/slide
  leases. Gun-inserted, hand-held and temporary dropped magazines can coexist.
- Only base `wpn_h1_pst_m9_vm` is admitted. Unknown extra weapon models reject
  the grip profile; sleeves remain hand-owned. Generic independent hands on
  unprofiled weapons are preserved, not claimed as support-grip acceptance.

## HMD checks

Open fingers; near/far squeeze; release; pistol aim invariance; change hand
models; switch away/back and pick up M9 without lift/holster/rack motion.
Fire while adding/removing support: no trigger rearm. Check two-eye consistency,
crossed hands, recenter, pause/dashboard and tracking loss. `vr_hands_status`
reports profile/variant/support/distance/blend/equip suppression;
`vr_input_status` reports side-grip action activity.

Offline tests cover cancellation, hysteresis, interpolation, pistol/rifle aim
math, named-joint admission, model lengths, equip-rest posing and unknown
attachments. No long-gun/attachment variant is HMD-approved by these tests.

Physical controls and single-level acceptance checklist are in
`docs/vr-m9-reload.md`. Use `vr_reload_interaction_status` for explicit chamber,
magazine, held escrow and native-boundary status. The 1.2-second drop is cosmetic;
floor collision, ground pickup and intermediate save restoration
are not part of the current runtime candidate.

2026-09-07 feedback candidate: `feedback.hpp` maps committed removal, insertion,
full slide pull and closure to native notetrack keys. The shared audio adapter
resolves live alias values and uses positional client weapon playback. Grab/draw
have tactile feedback, short slide spring return has closure feedback, and native
shots add haptics without duplicating engine audio. Cleanup, rejected transactions,
stale input, pause and instance changes produce no delayed feedback. A free hand
may pinch while squeezing; real support ownership still excludes part ownership.
Entering a region while already holding trigger still cannot acquire it.

2026-09-07 tuning: extend waist regions upward20 cm only; guide ejection along
the well until fully clear, then free-fall; allow sideways alignment within the
mouth collar after approaching from below. Contact geometry and drop math are
shared in `gameplay/physical_reload_geometry.hpp`; weapon measurements/tuning
remain here. Insertion and rack corrections still require HMD validation.

Follow-up: the user reports these controls work but need stability tuning.
New candidate: capture12 cm below well (upper/radial bounds unchanged),8 cm
slide acquisition tolerance,18 cm lateral slide breakaway. A captured operating
hand follows only the part's allowed travel and grip orientation. Hold trigger
after seating to keep the loading hand attached; release or35 cm wrist separation
relinquishes it without undoing insertion. Shared `part_hand_constraint.hpp`
touches only the off-arm; physical input always comes from raw corrected targets.

Ordering/presentation candidate: a staged spare retains its below-mouth approach
while the gun is still loaded or its angle is being corrected. Slide display uses
current constrained hand travel; release and B lock-release have a75 ms finite
return to the mechanical stop (`slide_return_seconds`). The shared transition
never owns ammunition. Held rigid clips attach to the native consumed skin epoch
and current scene origin. These changes still require the HMD checks documented
in `docs/vr-m9-reload.md`; offline tests are not visual acceptance.

Follow-up: previous rigid clip scan failed HMD testing because ordinary scene
counts exclude transient viewmodels. Use record-scoped viewmodel skin readiness,
separate from HUD submission readiness. M9 well contact has a4 cm RELEASE margin
after actual aligned contact, not a larger initial grab radius. Native supply
credits go to reserve without changing chamber/magazine/slide/held escrow; see
`native_ammo_grant.hpp` and the detailed reload document for budget rules.

Latest follow-up: independent magazine rendering accepted. Insertion still
needed correction: a historical below-approach flag could block a currently
valid tip. That extra constraint is removed; actual contact/angle plus separate
spawn/teleport/failed-write exit guards decide insertion, without a dwell timer.
Continuous slide grasp now finishes at `close_travel` (3 mm) and can extract on
the next full pull without releasing trigger. An empty follower stops at locked
travel (47 mm), using one shared mechanics/presentation floor. The first manual
stroke from a loaded lock spends zero, forward return feeds, and the next full
pull spends one. Retain the original grab anchor across cycles. Offline tests
pass; insertion and repeated-cycle HMD acceptance are pending.

2026-09-07 additional slide grip: the overhand pose borrows fifteen parent-local
left-finger rotations from `h2_wpn_smg_mp5k_reload_empty.seanim`, frame 9, SHA256
`3c4c3e1bd72f0932df4fb98c2f90b8f2e461615c9465ab35df456202aa37d06c`.
Its wrist is retargeted across the M9 slide, pinky toward gun-local +X and web
toward the rear, with the palm-knuckle centroid 1 cm above the sampled slide
mesh top. Contact maps to the same rear-slide region as the original grip.
Only the hand shape is borrowed; live glove lengths and M9 rail/ammo/feedback
remain authoritative. Raw anatomical wrist orientation chooses the nearer pose
before testing that pose's contact distance. A new in-region trigger press
latches the choice until release/cancellation; turning while held never swaps
poses or bypasses the fresh-press gate. `slide_grip_pose` is 0 for the original
and 1 for overhand while held; `slide_grip_candidate` records the current raw
candidate. Offline mesh review, full build and relevant regressions pass;
overhand fit, comfort and selection near the angular midpoint need HMD checks.
