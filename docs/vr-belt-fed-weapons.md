# M240 and MG4 belt-fed interaction

M240 supports the base and arctic receivers. The captured
`m240_heartbeat_reflex_arctic` shares the 38-bone receiver hierarchy and bind pose;
its registered reload recipe preserves the arctic material on detached boxes.
Its standard heartbeat attachment uses the shared [physical sensor](vr-heartbeat-sensor.md)
mechanism, including Trigger grasp, continuous folding and native scan display.

MG4 supports both the base and arctic receivers. The live `gulag` arctic model
uses the same 37-bone hierarchy and matching bind pose as the base adapter;
its separately registered reload recipe preserves the arctic material on
detached ammunition boxes while sharing the existing grips and belt mechanics.

Both adapters use the shared `open_bolt.hpp` policy already used by Mini Uzi.
The last accepted shot closes the bolt. A deliberate empty-feed trigger edge
also releases it, including with the cover open, without spending ammunition.
Cocking never feeds or ejects a round. Box exchange, belt placement and cover
movement never cock or release the bolt. RPD is deferred until headset validation
of these two adapters.

## Player operation

Use the existing part-grab trigger with a free hand. Squeeze remains weapon and
support holding. Grasp the cover and rotate it upward; its angle follows the
hand and remains where released. With the cover sufficiently open, pull off the
box (M240 outward to the left, MG4 upward). Removal withdraws the old belt in the
same transaction, for both empty and nonempty boxes.

Release the removed box, draw a replacement from the body supply and insert it.
Release the seated box grasp, grasp the loose leading belt link and place it on
the feed tray. A short pulse confirms successful placement. Close the cover to
its latch. Cock the handle before, during or after this process if needed.
Repeated cocking preserves all ammunition and any already placed belt.

An installed box alone is not ready to feed. Closing an unplaced belt does not
magically load it; reopen and place it. A closed cover blocks box removal and
insertion. Empty covers may close. The firing gate requires a cocked bolt, a
loaded installed box, a placed belt and a latched cover. Holding the charging
handle excludes both dry release and firing. One free hand owns one component
at a time; an occupied hand must release its current part first.

## Architecture and lifetime

`belt_feed.hpp` owns access and seating state, separately from the sear state.
`belt_gesture.hpp` and `belt_presentation.hpp` handle hand-driven cover rotation
and belt placement. Shared detachable-container transactions retain the single
ammunition ledger, native compare/commit boundary, inventory identity, box
escrow, interruption handling and per-hand firing integration.

Nonempty removal moves the actual remaining count into the held box. The
existing magazine disposition policy applies when it is discarded. Reinsertion
never refills it. Partial covers, belt placement and cocking survive hand and
weapon transfer; controller leases do not. Failed native writes consume the
gesture rather than retrying later. Tracking discontinuities release the hand,
with the last committed mechanism state preserved.

After headset feedback, cover/chain capture radii are 15/14 cm, with a bounded
6/8 cm preference over nearby handle/box contacts. Direct handle or box grabs
outside that preference remain available. Cover capture includes wrist proximity
and relaxed contact rotation, so rolling the controller no longer loses a close
grasp. Belt seating allows an 11 cm radius and wider wrist angle; a loose belt
already within the enlarged radius still needs a deliberate approach before it
seats.

M240 additionally extends that cover region by 12 cm forward/backward/upward
and 3 cm to either side, with no downward extension. Extension stays in gun axes
around the moving cover wrist/contact. A directly touched loose chain wins an
exact overlap with the cover. Handle acquisition has a hard wrist-side plane at
the inner edge of the right-hand tab (gun-local Y <= -2.13 cm); finger offsets,
mirroring and ordinary contact slack cannot admit a wrist across that plane.
This restricts acquisition only, so an existing grasp still follows its normal
stroke and release behavior.

Normal support holding uses a bounded, smoothed elbow swivel to avoid the
installed box's authored bounds with 3.5 cm sleeve clearance, including the wrist
endpoint. A reach-clamped hand blends back to its authored foregrip over 100 ms
instead of retreating into the box. Up to 6 cm of smoothed virtual shoulder
advance restores bend room; extreme reaches extend the rendered arm to retain
contact, as mechanical part-hand constraints already do. Corrections derive
from the current base pose and cannot accumulate stretch across frames. Finger
shape, hand orientation, gun placement and raw controller interaction geometry
stay unchanged. The correction stops on support release, part interaction or
box removal, and resets on hand/reference changes. This is visual collision
reduction; unusual reaches still require headset review.
Elbow avoidance uses the body's own outward/down axes, transformed into the
receiver frame. It prefers the corresponding side/down bend and rejects swivel
paths that lift the elbow more than 2 mm above the uncorrected elbow, including
intermediate points along the transition. Receiver roll does not redefine up.

## Source evidence

Offline bindings use MW2CR exported receivers and original reload animations.
Recorded native definitions give both weapons capacity 100. Generated pose files
contain source animation hashes and frame numbers; exports are not build inputs.

| Weapon | Cover / handle | Visible chain | Source hand frames: handle, cover, box, chain |
| --- | --- | --- | --- |
| M240 | `j_ammo_cover` / `j_reload` | 21 linked bones | 17, 81, 100, 174 |
| MG4 | `j_reload` / `j_bolt` | 14 linked bones | 18, 70, 105, 189 |

Source cover travel is approximately 61.5 degrees for M240 and 101.1 degrees for
MG4. VR extends M240 to 90 degrees around the same measured hinge; MG4 retains
its source angle. Handle travel is 68.6 mm and 174.3 mm respectively.
M240, MG4 and RPD (including digital) expose exactly two charging-handle
grasps per hand: the shared index-side and pinky-side hooks used by the AK
family. Their measured receiver-local contacts and accepted fits are retained;
the old native grasp is no longer a selectable third style. Both hooks use
complete canonical finger chains, including the ring finger. Opposite-hand
posture changes do not mirror the hardware onto the other side. Wrist facing
chooses the style on acquisition, and the existing lease keeps it fixed until
release.

Neither receiver has a separate animated internal-bolt bone. Their explicit
`concealed_bolt` contract keeps the sear mechanical while presenting only the
real handle. Mini Uzi retains its separately animated internal-bolt contract.
Optic attachment roots remain descendants of the cover and therefore rotate
with it. Native M240 reload notes use RPD sound aliases. RPD has a separate
adapter and independently moving optic bridge; see `vr-rpd-binding-plan.md`.

The rigid held/dropped model isolates the box body; the weapon skeleton poses
its articulated chain separately. Neither exported box has mixed-weight vertices
or triangles crossing into other parts. The chain uses source loose/seated poses
and bounded hand deformation with a fixed box end, not per-link rigid bodies.
The visible chain is bounded to 21/14 links regardless of the 100-round ledger.
All links follow the box's current posed transform, including the final seating
translation after ownership has already become installed. This also applies to
the unseated/laid chain during box withdrawal, avoiding a visible separation.
Discarded cosmetic boxes currently use the box-only rigid model. Headset review
must check chain curvature, hand fit, source materials and extraction direction.

Live M240 has 33 material surfaces. Its box occupies surface 10, with 2,467
vertices and 3,092 triangles using `m/mtl_h2_lmg_m240_magazine_base`. The former
32-surface extraction limit rejected its box asset and consequently all physical
reload contacts, despite successful grip binding. Rigid subset storage is now
allocated once from the native byte-sized surface count, retaining immutable
descriptor addresses. The source LOD, groups, indices and materials still require
validation; no source asset is modified.

## Validation

The rigid visibility cache is indexed by source surface and hidden-group mask,
with individually allocated immutable descriptors retained until asset unload
drains consumers. It grows within a 16 MiB metadata budget instead of a global
64-entry array. Live inspection of the old build confirmed 64/64 entries and
3,148 rejected filters: belt ammo-count combinations exhausted that array and
made detached boxes reappear on their receivers. Cache regression tests retain
borrowed descriptor/group pointers across 400 combinations and metadata changes.

Weapon-grip tests cover both exported hierarchies, cover-mounted optics, both
hands across the cover arc, independent box extraction and malformed chain
rejection. Reload tests cover both weapons/hands, empty and nonempty replacement,
four cocking orders, interruption, incorrect placement direction, failed writes,
neutral rearming, small cover-latch motion and empty release with any cover angle.
The existing Mini Uzi, detachable-magazine, tube and pump regression suites remain
enabled. Runtime reload diagnostics include cover fraction, belt seating, current
part lease and cover/chain/feed contact distances.
Rigid-part WARP tests also cover extraction from 33/255-surface receivers,
multi-material output at those limits and malformed LOD counts. Arm tests cover
short-arm wrist recovery for both hands, bounded shoulder movement, invariant
finger/gun poses, smooth convergence and clearing the constraint on release.
# Open-palm cover push (RPD / M240 / MG4)

Button opening and button latching remain available. The non-holding hand may push the cover shut from its outer face with an open palm when both Trigger and side Grip are released and the hand is not occupied by a magazine, bolt, support grip, or knife. Push detection uses the palm center derived from the raw tracked wrist after repositioning and the calibrated controller palm direction. It does not use the auxiliary grip point or the IK hand after snapping; fingers retain the free-hand pose.

The contact patch covers the lever section and reaches close to the rotation axis, based on the raw LOD0 mesh with interaction tolerance. The blue base rectangle uses the extents below and remains separate from the palm contact shape. Coordinates are in meters in the closed cover's hinge space and rotate with the cover. The lever extends along −X and its outer surface faces +Z. Do not reuse the enlarged button-grab range. The 6 cm palm radius extends another 1.5 cm toward both fingers and wrist along the raw anatomical hand axis projected on the cover, forming a capsule up to 15 cm long and 12 cm wide. Capsule-to-rectangle distance handles side and corner overlap without requiring the palm center to lie over the narrow cover skin. Turning the hand rotates its longitudinal extension without enlarging its side width.

| Weapon | Lever interval from hinge | Lateral Y range | Outer surface Z |
| --- | --- | --- | --- |
| RPD | 0.025–0.265 | −0.055–0.055 | 0.006 |
| M240 | 0.040–0.290 | −0.058–0.060 | 0.022 |
| MG4 | 0.045–0.315 | −0.058–0.050 | 0.019 |

After button operation ends, cover push has a 0.3 s cooldown. Holding a button on an already open cover does not consume the cooldown early. After cooldown, the palm must again be observed at least 2.5 cm outside the outer surface, then continuously approach inward to the 1.2 cm contact band. Palm facing only rejects clearly reversed orientations (dot product with the outer-surface normal must be below 0.2, allowing a nearly sideways palm). Approach speed must be at least 0.06 m/s. Detection has one-frame contact tolerance, but a palm already inside the cover, without a recorded outside approach, or teleported inside cannot start a push. Moving only the gun toward a stationary palm does not count as an active push.

The push accumulates signed hinge angle and overcomes the existing 3% snap zone at the fully open endpoint. It can only close the cover, never drag it open in reverse. After a slow push accumulates at least 8 mm, about 3.4°, with recent speed at least 0.03 m/s, releasing may carry it a short extra distance based on speed: approximately 0.12 s of current travel, at most 12°, decelerating to a stop within 160 ms. After at least 4 cm and 20° of accumulated push with recent speed at least 0.40 m/s, ordinary loss of contact or closing the palm may trigger a full inertial close. Stopping consumes velocity rather than storing a delayed close. Focus loss, tracking loss, timeout, recentering, holding-hand/weapon changes, position jumps, or failed native transactions cancel the action and inertia.

An intentional Trigger press to grab a part takes precedence and cancels the push/inertia without swallowing that grab input. All changes commit through the existing `move_cover` transaction and continue to honor the RPD sight-bridge interlock, belt state, ammunition conservation, and original sounds. Automated tests cover both hands, all three weapons, approach after cooldown, false-contact rejection, slow/short/inertial pushes, invalid input, and failed native writes. Physical contact placement and feel still need VR acceptance.

Fast-contact detection examines the entire motion from outside to the contact surface and starts counting push distance at the actual crossing point. It does not reject a motion merely because its endpoint has passed the old cover surface. Follow detection evaluates the candidate cover angle; if contact leaves the patch partway through, a fixed-iteration interval search finds the valid travel before judging inertia. The speed cap is 6 m/s, the one-frame local/world displacement cap is 20 cm, and the one-frame rotation cap is 0.8 rad. These continuity limits still do not allow interaction to start from inside without an outside observation. Regression includes 3.5 m/s fast pushes at 45/90/144 Hz.

Cover display reuses rigid-part return interpolation, following the mechanical angle with a short 45 ms transition between render frames. It does not delay mechanical latching or ammunition handling. Button-held cover manipulation still uses the live angle. Pausing, tracking-reference changes, and holding-hand identity changes clear the prior display transition.

Button-held cover and bridge grasps accumulate angle between consecutive samples, retaining overtravel beyond the physical stop. Crossing atan2's ±180° seam cannot reverse an opening motion. Continuing past fully open and breaking away leaves the cover open; a fresh grasp starts from the current mechanical angle.

RPD main-grip B/Y opens the optic bridge with either physical holding hand, on base and digital variants. A dedicated rear-hand `release_bridge` transaction reuses the existing opening motion and sound. It never closes the bridge, changes ammunition, or overrides an active manual bridge grasp. A conflicting or rejected press is consumed rather than queued. Manual release/return and the cover interlock remain available. The RPD cover pose uses canonical `j_ring*` names for the ring palm and three finger joints; both hands apply the original animation through the existing anatomical mirror path.

Before accepting a new target, interpolation first advances the old segment to the current render time, then starts the next segment from that value. Otherwise a target updated every frame repeatedly remains at t=0 and the visible position freezes. Regressions cover both intermediate frames toward a fixed target and mechanical angles changing every frame at 45/90/144 Hz.

Contact entry uses continuous outside approach and speed in meters per second; it does not require a fixed millimeter distance in one frame. The former 2 mm-per-frame minimum rejected an ordinary slow push on entry to the contact band, after which the palm was already in the band and could not trigger again. Regression now covers continuous 8 cm/s approaches at 45/72/90/120/144 Hz and the full path from a raw controller sample through the actual sampling function into the reload state machine.

`cover_push` in `vr_reload_interaction_status` reports the current gate/stage (for example `cooldown`, `armed outside`, `palm facing away`, `outside outer lever`, or `pushing`). The same line shows palm-sample validity, hinge-local position/normal in meters, and side-Grip state. Diagnostics are formatted only on manual query; the frame loop does not write logs.
