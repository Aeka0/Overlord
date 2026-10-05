# Revolver interaction: .44 Magnum

This adapter models the .44 Magnum as a dedicated cylinder feed. It preserves
native shot acceptance and ammunition accounting while presenting physical
opening, gravity clearing, loader transfer, and wrist-twist closure. Offline
regressions cover mechanical transactions and rigid-part rendering; headset
comfort and native submission remain acceptance checks.

The adapter uses the shared client-presentation boundary for pickup and switch
transitions. Rendering the final arm pose alone does not suppress native camera
motion or animation notetracks. Cylinder-specific state remains separate from
detachable-magazine feeds; shared ammunition, ownership, feedback, and rigid-pose
helpers stay behind their existing adapters.
Weapon-specific discovery belongs to
[`weapons/magnum44`](../src/client/component/vr/gameplay/weapons/magnum44/README.md).
Shared ammunition disposal belongs to
[reload penalty and ammunition disposition](vr-ammunition-disposition.md).

## Confirmed interaction contract

| Event | Required state / geometry | Authoritative result |
| --- | --- | --- |
| Rear-hand B/Y press | Cylinder closed | Begin opening; disable firing; retain all contents |
| B/Y while already open | Any contents | No action; never toggle closed or clear contents |
| Opening reaches fully open | Any contents/orientation | Complete opening, then evaluate the independent current gravity condition; no unconditional clear |
| Downward gravity condition is satisfied | Fully open; opening currently faces world-down; contents remain | Clear all remaining contents, including a fully loaded cylinder; no new orientation edge required |
| Keep opening downward | Any contact | Reject loader transfer; no consumption |
| Turn opening upward with loader in valid contact | Fully open; live + spent = 0; loader has rounds | Transfer all held rounds immediately, even if fewer than capacity |
| Valid rightward wrist twist | Open, empty or nonempty cylinder | Begin closing; retain contents; same twist sign for either rear hand |
| Close completes | Loaded or empty | Closed, but require a fresh trigger press before firing |
| Fire | Closed and live rounds > 0 | Native accepted shot consumes one live round and leaves one spent case |

No slide, last-round lock, independent chamber bonus, manual chambering or
Russian-roulette behavior. Loading three rounds guarantees the next three
accepted shots can fire; an arbitrary visual empty hole cannot veto a shot.
Remaining live rounds and cases survive opening/closing without a clearing event.

### World direction and opening delay

The cylinder opening faces backward, opposite the muzzle. Define an authored
opening unit axis and transform it through the cylinder/gun pose into world
space. Test it against world up/down, not camera up, controller screen-space
direction, head tilt or muzzle-down intuition.

Opening has finite mechanical travel. Use an authored opening duration/progress
owned by simulation, consumed by presentation. Neither the B operation nor the
opening-completion operation owns an ammunition-clearing transition. Downward
clearing is separate and can execute only after the cylinder is fully open;
rounds must not fall while it is still swinging out. Native 30fps animation
sampling, render completion and notetrack arrival are not the authority for this
boundary. Do not introduce a delayed automatic clear simply because B was pressed.

If the opening faces downward before or during opening and is still downward at
full opening, clear then and start the falling-round presentation. No
turn-away/re-entry gesture or extra dwell is
required. Both logical contents and visible rounds stay in the cylinder until
that boundary; do not clear at B and merely delay the cosmetic fall. Evaluate
the current direction at completion: if the player has already turned upward,
retain contents instead of executing a stale queued clear. Opening upward or
neutral never clears merely because the opening animation finished.

Upward fill and downward clear use separate configurable angular bands, with a
neutral band between them and boundary hysteresis. Exact angles and opening
duration require asset-pose/HMD tuning; no arbitrary extra seconds-long dwell
or cooldown is part of the rule. Continued downward orientation rejects filling,
so freshly transferred rounds cannot immediately clear in the same orientation.
An empty cylinder does not repeatedly emit shell effects or disposal transactions.

### Loader contact and transfer

Only the fully opened cylinder's rear loading neighborhood is eligible. Use a
finite, tolerant volume around that face (radial and axial allowance), not an
infinitely thin plane. Side/back contacts through the gun are not accepted merely
because some mesh bounds intersect. The approach needs reasonable alignment,
with numerical tolerance determined during HMD testing.

Re-evaluate eligibility on each newly consumed input. A loader may be held in
contact before the cylinder is empty or upward: once all conditions become true,
accept without requiring a new trigger edge, withdrawal/reentry, a native reload
timer or a manufactured cooldown. Stale tracking, identity changes and pose jumps
remain separately diagnosed guards, not unexplained time-based delays.

Successful native compare-and-commit is the single transfer point. At that point
the cylinder is loaded and the loader's ammunition payload becomes zero. There
is no post-transfer "loader has not left" mechanical state. An optional fast
round-insertion animation is presentation only: closing may start while it plays;
retarget or finish the visual against the current cylinder, never delay closure,
repeat the transaction, or restore the loader payload. Firing still waits for
mechanical closure and the fresh-trigger gate.

If loader and rounds can be independently rendered, retain the empty loader in
the off hand until deliberate release. Its presence is independent of its zero
round count. Current right-rear mode uses the left hand; future holding ownership
must not hard-code this. If separation is genuinely unavailable, the approved
fallback is to remove the whole loader on success. The current candidate instead
implements independent loader/case/tip rigid subsets, with live source-layout
validation and D3D WARP tests. Native in-game rendering still needs HMD
verification; an unavailable subset contract prevents physical admission rather
than silently discarding the empty-loader requirement.

Take min(capacity, reserve) rounds, including a partial loader. A zero-reserve
attempt must never create usable rounds. Deliberately releasing an unused/partly
loaded loader resolves its payload through the shared disposal policy. Dropping
an empty used loader changes no ammunition.

### Twist-to-close and input ordering

Use the gun/cylinder longitudinal axis with an authored rightward sign viewed
from the rear toward the muzzle. Do not mirror this sign for left-hand ownership.
Measure tracked controller quaternion change in one unchanged tracking reference,
then project its angular change onto the weapon axis. Ignore translation,
head-only rotation, artificial locomotion/snap/smooth-turn transforms, native
recoil animation and other-axis swings. A bounded signed angle/rate window plus
rearm threshold should reject isolated tracking spikes; calibration remains open.
Controller poses alone cannot perfectly infer the player's intent, so HMD tests
must include ordinary handling motions, not just deliberate closing gestures.

Open/opening/closing cylinders never fire. A held trigger while open or while
closing must not turn into a queued shot on closure. Current same-input ordering
is opening/clearing/loading/closing eligibility before new firing eligibility;
native already-accepted shots are observed once and never retroactively undone.
Each input sequence/operation/instance revision commits at most once. Closing an
empty cylinder is valid; pressing B again while open remains a no-op.

## Family state and shared boundaries

Use a separate cylinder feed state, not `closed_bolt::state` with special flags:

- action: closed / opening / open / closing; mechanical transition time;
- live count and spent-case count, both nonnegative, sum <= native capacity;
- cylinder genuinely clear iff both counts are zero;
- held loader object identity, ammunition payload and consumption identity;
- owner, weapon instance, tracking generation and transaction revision;
- trigger rearm and twist-recognition state.

Per-hole visible occupancy may be generated deterministically to match the
counts. It is presentation metadata, never a Russian-roulette firing gate.
Native loaded count projects to live count only; cases and held loader rounds
are not fireable native ammunition. Native automatic/key reload must not refill
or close an open cylinder. First admission and checkpoint import need explicit
native observation because a loaded-count integer alone cannot distinguish a
partial fresh load from a partly fired full load.

The current `detachable_magazine.hpp` explicitly excludes cylinder feeds, and
`physical_reload_profile` requires slide/magazine roots and slide poses. Do not
register Magnum in those tables or provide dummy slide bones to pass validation.
The candidate shares ammunition projection/disposition and positive-grant
budgeting (`ammunition_transfer`), owned native compare-and-commit
(`native_ammunition`), native fire/reload hook dispatch (`manual_feed_boundary`),
shot history, hand ownership, feedback, rigid pose helpers and exact-frame weapon
render binding. It retains separate detachable-feed and cylinder state,
gesture, instance-lifecycle and presentation adapters. These are separate
mechanical families, not a cylinder special case in pistol reload.

Open-travel completion is mechanical; cosmetic round travel is not. Separating
these avoids both premature shell drops and another animation-dependent loading
delay. No gameplay decision may depend on whether an object was culled or whether
the left/right eye happened to draw first.

## Asset facts and implemented boundary

Exported first-person model: `h2_viewmodel_colt_anaconda_base` (six round groups).
Live WeaponDef name `coltanaconda`, verified base/effective capacity 6. The
captured assembly has 68 hand bones plus 25 receiver bones. Runtime admission
requires this reviewed receiver topology and exact native binding, validated
independent parts, fresh tracked gameplay and native idle. Non-VR capture alone
does not admit the physical feed or disable native reload.

- `j_speed_loader`: 2,443 rigid vertices, 3,024 triangles; independently separable.
- `j_bullet01` through `06`: 321 rigid case vertices each; separate child
  `j_bullet_tip01` through `06` have 223 vertices each. The local geometry preview
  confirms distinct loader, cases and projectile tips.
- No mixed-weight vertices or cross-group triangles in this exported LOD0.
- Loader, rounds/tips and other moving parts share two material surfaces.
  Whole-surface hiding would remove unrelated geometry. The candidate uses
  rigid-part-group visibility/placement with fresh native contract validation,
  following the M1911/USP shared-surface lesson.
- `j_cylinder_ammo` and `j_speed_loader` are children of `j_gun`, not naturally
  children of the swinging cylinder or left hand. Role-based attachment must
  explicitly handle their authored transforms and ownership.
- No separately named loader model was established by this bounded export audit.
  Existing independent magazines use verified single-bone XModels; this multi-bone
  viewmodel cannot simply be sent down that path unchanged. Loaded cylinder,
  held loaded/empty loader and falling rounds can need multiple simultaneous
  instances of source groups. `scene_rigid_part` creates immutable one-group
  model subsets and owned index buffers, retaining the original vertex data and
  bind frame without editing source assets. Tests cover surface-global indices,
  index SRV variants, ownership, rejection and bounds. HMD acceptance must still
  verify the native submission path and simultaneous instances.
- Reload export is 93 frames at 30fps; inspect is 101 at 30fps. Reload notetracks
  include lift, clipout, shell-eject, clipin and chamber keys. These are pose/sound
  discovery evidence, not mandatory VR delays. The captured map confirms
  clipout, clipin and chamber-close keys used by opening, fill and closing.
  The shell-eject notetrack has no map entry. A separate read-only loaded sound
  pool audit verified `shell_eject_pistol` (16 native variations); the adapter
  explicitly selects that shared alias once per successful gravity clear, at
  the cylinder origin. This is not an inferred notetrack mapping or a simulated
  material-specific ground-impact sound. Empty/failed/repeated clears stay silent.
  A key named chamber does not imply a +1 mechanic.

Raw exports, parser source, reports and preview remain local-only and removable.
The playable mod must not depend on the export directory or ship extracted assets.

## Candidate controls, limits and verification

- `vr_cylinderReload` defaults to 1. Only admitted VR cylinder instances replace
  native key/automatic reload; ordinary non-VR and unsupported weapons retain
  native behavior. Disabling returns unconsumed held ammunition before releasing
  native ownership; it does not persist an open-cylinder state for stock gameplay.
- `vr_cylinder_status` reports a copied snapshot and presentation readiness.
  `contracts=7` means fire, reload and presenter boundaries registered; it does
  not alone mean current assets/tracking are ready. Phase 0/1/2/3 is
  closed/opening/open/closing. Check active/fault/reason and the presentation line.
- Initial tuning is 220 ms opening and 160 ms closing; world-down/up dot
  thresholds 0.50/0.35 with 0.08 hysteresis; loading volume radius 9 cm, 16 cm
  behind / 4 cm inside the rear face; alignment cosine 0.55. Closing requires a
  net rightward roll of 0.35 rad (about 20 degrees) in 220 ms, with current roll
  speed at least 12 rad/s (about 688 degrees/s), at least 0.25 rad (14 degrees)
  traveled above that speed, and roll at least half the total angular travel. Slow
  start/end frames do not erase the gesture. Total speed above 60 rad/s rejects
  a discontinuity; quiet total motion below 0.8 rad/s rearms the gate, including
  off-axis tracking noise. Duplicate timestamps, tracking gaps, opposite roll
  and fresh cylinder eligibility cannot replay old motion. These are revised
  HMD tuning candidates, not measured acceptance.
- First native import treats missing live rounds as spent cases, since native
  loaded count alone cannot distinguish a partially loaded clear cylinder from
  a partly fired one. A deliberate gravity clear establishes the known empty
  state. No extra chamber round is imported or manufactured.
- Logical load commits immediately. There is no additional round-seating
  animation in this candidate; native clip duration never delays transfer.
- Subset asset storage retains at most 32 immutable source sets for queued-render
  lifetime safety. Exhaustion rejects new subsets and requires a restart; it
  does not evict metadata still reachable by native work. Held/falling objects
  and cosmetic event buffers are bounded independently.
- Current runtime admission retains the existing right-rear ownership stage.
  Cylinder core/gesture tests include either rear hand and the same closing sign;
  this does not implement general left-hand weapon acquisition.
- Intermediate save/load restoration, recoverable ground items and a user-facing
  penalty toggle remain out of scope. Pure policy tests cover future penalty
  choices, but do not constitute implemented on/off gameplay.

Passed groups: cylinder, rigid-part (WARP), physical-reload, pistol-profile,
closed-bolt, weapon-mechanics, spatial-panel (WARP), weapon-grip,
controller-input, hand-pose, hand-rig and weapon-hud-lui. Cylinder tests include
100,000 bounded random operations with conservation/identity checks. These
establish CPU/GPU helper behavior, not native scene or HMD acceptance.

## Acceptance matrix

1. Press B while already downward: retain logical/visible contents throughout
   opening, then clear and begin falling only when fully open and still downward,
   without a new tilt or extra dwell. Turn upward before full opening: retain
   contents, with no stale queued clear. Open upward/neutral: do not auto-clear.
2. Open with 0 live + cases, some live + cases, full live, and truly empty; verify
   that only the last state accepts filling, and only when upward.
3. Hold a loader in contact while clearing, then turn up without withdrawing:
   transfer immediately when eligible, never while downward or neutral.
4. Partial supply of three: show three rounds, transfer exactly three, close,
   fire three accepted shots without empty-hole interruptions; then retain cases.
5. Open/close without clearing preserves counts. Empty closure works. Repeated B
   while open does nothing. Closing gesture is not mirrored for the left hand.
6. Close immediately after loading (and during cosmetic insertion if later added);
   move the empty loader away immediately;
   keep contact for many frames; no double fill or invisible timing barrier.
7. Loader/case/tip separation, simultaneous old contents + new loader, mixed
   surfaces, both-eye transforms, locomotion and near-frustum visibility.
8. Held trigger across closure, B/trigger in one input, body/head/locomotion motion,
   tracking discontinuity and rapid deliberate roll versus accidental swings.
9. Current default refund policy: old magazines, full/partial unused loaders,
   cylinder live-round clearing, empty objects, repeated cosmetic events and
   forced cleanup. Repeat with penalty on only when that runtime option is added.
10. Ammo-box grants, switching and tracking loss preserve feed authority and settle
    only the correct instance's held resources. Save/load of intermediate states
    remains separately scoped, following the existing pistol-stage limitation.
11. One native ejection sound on clearing live rounds, mixed live/cases, or cases;
    no repeat while remaining downward and empty. Fill/open/close sounds remain
    unchanged. Check audibility separately from successful sound API submission.

## Shepherd .44 sound identity

The Shepherd variant uses native identity `coltanaconda_shepherd` and the
existing six-round cylinder profile. Cylinder open/load/close feedback resolves
through the admitted profile's exact native name. Shared dispatch validates the
request, native observation, admitted identity, and capacity before forwarding
that identity to the native WeaponDef lookup. The native sound map remains
authoritative; no guessed aliases or audio assets are added, and gunfire
playback is unchanged.

Dispatch regressions cover open/load/close and casing references, and reject
mismatched tokens, capacities, unsupported names, akimbo identities, and invalid
observations before playback. Verify actual audibility separately from
successful native dispatch.
