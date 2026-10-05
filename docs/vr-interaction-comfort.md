# Weapon manipulation comfort

## Shared box-magazine orientation

`magazine_grasp_profile.hpp::with_controller_magazine` is the explicit profile
entry point for box magazines with a local +Z feed axis. It resolves a fixed
grasp once during immutable profile construction: reuse an existing fitted
body-wrap recipe, or retain the native recipe if no wrap is authored. It sets
selection, fallback index, controller tracking and pose count together. No
weapon-name switch, per-weapon angle correction, mesh fitting or asset scan
runs in the tracking loop.

M200 (Cheytac), M82A1, M21/M14, F2000, FAMAS, TAR-21, MP5K and UMP45 use this
entry point, with cosmetic variants inheriting it. SCAR uses the same entry
point with its previous wrap and tracking behavior. TAR-21 now uses its existing
wrap consistently instead of selecting a bottom pinch from outward palm roll.
M82 and M14 reuse their already fitted receiver-wrap for free magazines too.

The shared selector and wrist-basis calculation serve simulation contacts,
rendered hands and loose magazines. Cancelling the selected magazine-in-wrist
rotation makes the magazine follow calibrated controller pitch, yaw and roll
in either hand; it does not force world-up or copy the rifle's support wrist.
Original mesh size, curve, contact points, finger joints, insertion geometry,
release mechanism and sound bindings remain authored per model.

Legacy policies remain the default for profiles that do not opt in. Belt feeds,
pistol support catches and facing-disambiguated horizontal magazines cannot
enter this policy. Missing contacts, invalid transforms, unsupported grasp sets
or excessive pose counts leave the complete input profile unchanged. Dedicated
knife co-grasps still bypass ordinary magazine selection and controller tracking.
There is no per-frame switching between the new configuration and old policy;
a captured recipe remains fixed until its interaction ends.

Regression covers adopted families and skins, both hands, three-axis controller
rotation, same-frame contact sampling, retained grasps, unsupported profiles
and invalid authoring data. Headset comfort for the newly adopted families
still requires acceptance.

## Other manipulation poses

Mini-Uzi retains the accepted left support wrist. The right hand now reflects
around the actual folded foregrip centre (receiver Y = -1.6374 cm), rather than
around the left wrist or the receiver centreline. `support_mirror_center` is an
explicit asymmetric-hardware capability, shared by pose and carry contact
construction. It preserves the right-offset grip while mirroring anatomy;
the older fixed-wrist mode remains available to profiles that need it.

Mechanical transactions remain independent of authored hand poses and display
motion. A new pinch selects a hand pose from wrist orientation
relative to the gun. Held parts retain that selection through rotation, partial
strokes and release; different poses never move the real hardware to the other side.

| Weapon | Behavior |
| --- | --- |
| P90 | Original reverse grasp and forward grasp (thumb toward muzzle). The forward palm moves 5 cm rearward. Thumb and four fingers remain on opposite sides of the magazine centreline in both styles. Extraction, held magazine, insertion and contact resampling share the retained pose. |
| M200 | Right palm-up and left palm-down share one naturally curled M14-derived finger chain, fitted to the inclined Cheytac handle. The left hand flips that complete grasp about the handle's inclined plane. The right palm-down style remains available. Either hand can complete the mechanical lift/pull/push/lower cycle. |
| AK, M14/M21, Dragunov, M1014 | The left-down/right-up pinky-hook style redirects the otherwise protruding index finger from its root, while preserving the natural curl of its middle and distal joints. The left-up/right-down index style and M1014 native right index pose retain their previous fit. |
| M93R | Shared knife-plus-magazine and knife-plus-slide handling, including both knife directions and either hand. Returning the knife does not change an already held part's pose. |
| MP5K, UMP45, AUG, Tavor, SCAR, FAL | Left-hand native and palm-up grasps remain available. The right hand uses explicit index/pinky hooks from the shared AK pose family, fitted to the real left-side tab with its wrist outside the receiver. A held style stays fixed until release. |
| TMP | The right hand moves to the other end of the T-bar, placing index and middle fingers on the handle while the thumb stays open. |
| L86 | Both hands use the M14 shared index-side/pinky-side hooks on the actual right charging handle. Each facing has an explicit opposite-hand fit; selection follows wrist orientation and stays fixed during the stroke. |
| ACR | Left palm-up uses the restored historical underhand wrist with aligned index/middle hooks; the penetrating left wrist data is removed. Left palm-down keeps the pinky hook. Right hand retains both independent fits. |
| M4, M16 | Explicit left/right fits instead of reflecting a hand around the other handle-wing contact. M16 includes corrected palm/ring chains. |

The later handle pass bounds AK acquisition to the physical right receiver wall
for either hand. SCAR up/down selection uses calibrated controller palm facing
in gun space before any authored wrist offset; an already held style stays
latched. See [folding handle integration](vr-folding-handles.md) for F2000 and
PP2000 end folding and its geometry/acceptance boundaries.

Left-side right-hand fits resolve anatomical mirroring before applying handle
travel or catch rotation. MP5K, UMP45 and AUG therefore carry the complete hand
around the actual catch hinge, and allow either hook to regrasp the raised tab.
The shared fit does not mirror the hardware or change catch/ammunition rules.
Regression coverage includes both hands and skins, full and partial strokes,
rotated guns, raised acquisition, and regrasp/lower/release without duplicate
extraction. Offline native-mesh review covers both right-hand hooks at rest and
rear travel, including raised handles; headset acceptance remains pending.

M200's stretched ring finger was an authoring error, not the native animation.
The old substring replacement matched `ring` when searching for `_ri`, creating
duplicate `j_leng*` names and dropping the real ring chain. Authoring now matches
side suffixes at token boundaries and transfers each joint through the actual
glove's anatomical mirror basis. Native source hashes stay next to the poses.
M200 uses rechamber frame 11; M4/M16 use pullout frames 16/14; Tavor uses pullout
frame 11, when both index and middle fingers hold the tab, instead of the earlier
single-vertex fit at frame 13. Rotating-bolt motion uses the selected pose's contact,
including for raw input resampling and after a partial bolt release/regrasp.
Tavor's down-facing hand rotates around its contact to move about 1.6 cm forward
and 2.3 cm outward, with index/middle skin still within 3 mm of the tab.
For the asymmetric pinky hook, offline skinning places the index fingertip about
0.1 cm from the other fingers in the forward dimension and 0.4 cm vertically;
the earlier extra PIP/DIP curl left it about 3 cm forward and over 2 cm higher.
M200's right palm-up grasp was reauthored after the earlier Cheytac frame-11
four-finger fist remained visually cramped. It borrows the complete M14 native
knob-grasp chains. The wrist now follows the inclined handle rather than a
horizontal finger row; the intermediate and distal finger joints relax together,
with small knuckle corrections for clearance. The thumb retains its native curl.
The left-down hand reuses this exact finger array and flips the complete wrist
fit around the plane containing the barrel and the actual handle axis
`(0, .803701, .595033)`. Both left-hand style entries use that same fit, preventing
a wrist turn from bringing back the previous fist. This changes presentation
only; bolt authority and ammunition writes remain unchanged. Shaded glove,
receiver and scope meshes are reviewed with shared depth occlusion from several
angles. Surface penetration checks are diagnostic, not ergonomic acceptance;
headset appearance and comfort still require acceptance.

M79/Ranger automatic opening and closing, and M1887 assisted spin/return, evaluate
their display pose between input updates. The look-ahead is bounded to 50 ms and
disabled on stale tracking, faults, owner changes and reference changes. It never
writes ammunition or emits events. Closing does not display a mechanically
uncommitted closed endpoint. The M1887 stops at its empty-gun opening point and
waits at the catch until the authoritative controller confirms it.

Regression coverage includes anatomical-chain completeness, both-hand pose
selection, fixed held styles, P90 palm registration and failed cleanup,
M200 cycling with either hand, M93R co-grasp interruption, and sub-tick presentation
without mechanical writes. Offline skinning and native geometry reviews establish
the authored fit. Actual comfort, both-eye appearance and perceived smoothness
still require headset acceptance.

## Attachment entry and release continuity

All weapon part presenters share `part_hand_transition.hpp`. Wrist translation
blends for 90 ms when attaching to a part, detaching, or moving between a seated
and free magazine. The finite smoothstep operates on a controller-relative
offset, so walking or turning the wrist continues immediately; normal tracking
has no persistent smoothing delay. Both hands use the same path, including
legacy magazine profiles, slides, charging handles, pumps, break-action barrels,
underbarrel parts and the heartbeat sensor. Ordinary support grips retain their
existing behavior. Tracking/reference/weapon changes discard the previous offset.

The transition is applied before loose-item render snapshots are frozen. A free
magazine follows the final displayed wrist; a seated magazine remains constrained
by the receiver. Contact sampling, insertion, extraction and ammunition writes
continue to use the raw controller and existing mechanical transactions.

When the rear hand releases a two-handed weapon, the ownership transaction
captures its orientation relative to the remaining support controller. The same
relation drives the hand solver and current-frame carry/contact sampling. Wrist
rotation therefore rotates the gun by the same relative amount. Regripping the
rear handle restores normal two-hand solving and invalidates the retained relation;
the temporary rotation never replaces the published rear-grip calibration.

## M200 release completion

M200 may complete its remaining lock rotation after an actual forward stroke
reaches the receiver, with at most 40 degrees left. Release needs at least
0.25 m/s at the handle contact in gun space and a closing direction component.
Slow movement, reverse motion, a pause, contact breakaway, stale tracking and
rejected native transactions leave the bolt where it is. The final release sample
participates in the angle/velocity check. One unchanged sample may reuse motion
from the preceding 35 ms; any measured slow or reverse motion replaces it.

The existing `move_bolt` transaction commits the lock once, without another feed
or extraction. The visual handle completes its rotation over 80 ms. Presentation
never adds firing or ammunition authority.

## Ejected cartridge variation

Revolver cylinder clearing and Ranger breech ejection use the shared
`ejection_scatter.hpp` policy. Each cartridge receives an event- and chamber-specific
bounded velocity and angular velocity once. The original exit is preserved for
60 ms, then a 30 ms velocity ramp introduces a small lateral spread and tumble.
The revolver bullet tip stays rigidly attached to its corresponding live case.
Speedloaders, hand-discarded ammunition and other break-action weapons retain
their previous trajectories. Counts and ejection timing remain mechanical.

These changes require headset acceptance for perceived wrist continuity, release
thresholds and cartridge appearance; offline regression cannot establish comfort.

## Arm axial rotation continuity

Upper-arm and elbow rotation use the common `hands::orient_arm` bend-plane
frame. A segment direction by itself does not define roll: the previous pair
of independent shortest-arc rotations became unstable when a segment moved
around the opposite of its native direction. The new frame maps the authored
arm plane onto the solved shoulder/elbow/wrist plane, retaining native bone
orientation within that frame. Straight source arms use an anatomical fallback.
Ordinary IK, support-arm clearance and forearm-mounted arms share this policy.
Joint positions, reach limits, wrist/finger targets and weapon ownership are
unchanged. Final wristtwist deformation remains a separate anatomical stage.

A bounded read-only capture observed a 152.63-degree elbow change
with only 3.47 degrees of forearm-direction change. Replaying those positions
through the production helper reduced the elbow change to 3.59 degrees; a
second 115.79-degree change became 2.39 degrees. Comparisons exclude unposed
bind/stored skeletons, intermediate matrices inconsistent with the old solve,
reference changes and large positional discontinuities. These are offline
comparisons of one captured trajectory, not post-deployment HMD acceptance.

Regression retains the compact captured geometry, mirrored two-degree circles
around both segments' antipodes, anatomical/world frame invariance, exact
segment alignment and repeat-solve idempotence. Small movement must not wind
an upper arm or elbow through a full turn.
