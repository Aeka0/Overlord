# Dual magazine grasps and model fitting

M4, M16, AUG, FAL, TAR-21, and ACR use the shared wrap-grip and bottom-pinch
selection policy. Reviewed cosmetic and attachment variants retain their
family's authored contact geometry.

## Behavior

When newly grasping an ordinary magazine, use the physical controller orientation to choose a bottom pinch or a wrap grip. Orientation comes from the calibrated tracking target, before weapon-animation wrist offsets and IK. Left and right follow the physical hand; crossing the body midline or swapping the holding hand does not redefine them.

The orientation reference sets palm up to 0°, inward rotation to about 90°, and
palm down to 180°. Compute a signed turn about the tracked controller's forward
axis. Only an outward turn past 190° selects the bottom pinch; otherwise use the
wrap grip. Turning the controller toward the side of the body does not by itself
count as turning the palm. All six weapons default to the wrap grip when the body
reference is invalid or the controller's forward axis is too close to vertical
for a reliable measurement. The final 10° approaching palm up belongs to the
wrap region to prevent accidental switching at the 0°/360° seam.

The threshold identifies intent; it is neither an anatomical joint angle nor a validated comfort standard. Input continues to use the project's calibrated palm approximation. SteamVR `/pose/raw` must not be treated as the OpenXR standard grip palm axis.

The magazine's +Z long axis follows the calibrated controller's local up direction, including the player's actual pitch and palm turn; it is not locked to world vertical. Derive the corresponding wrist reference by inverting the current magazine grasp's in-wrist rotation. Hand IK and raw interaction geometry share this reference instead of inheriting the current rifle foregrip's animated orientation. Other parts, such as charging handles, retain their existing wrist reference.

The tracking frame is now explicit and independent of grasp selection. SCAR and
all registered AK variants also use this controller-relative magazine frame
after headset feedback. SCAR's free default is its fitted wrap; it no longer
starts with the original bottom cup. AK retains its native wrap and removes the
inherited support-wrist tilt. Changing this frame does not modify the fitted
hand-to-magazine transform or finger articulation.

The free-magazine grasp is latched after a successful draw and persists through movement. As of , a rifle magazine still in the gun always uses its wrap recipe, including acquisition and continued holding after insertion; inserting a bottom-pinched spare switches to that attached recipe. Removing it retains the wrap in hand. A rejected draw leaves no new pose; if a refund fails, the grasp remains with the ammunition still held. Knife + magazine uses its existing dedicated recipe. P90 retains its original forward/reverse grasp rule relative to the gun. See [button-release catches](vr-button-magazine-catch.md).

## Fine-tuning after acceptance

The ACR default wrap contact was previously about 7.2 cm from the magazine bottom, versus about 4.4–5.4 cm on the other five weapons. This pass lowers the wrap position by about 2 cm along the measured X/Z curve, adjusts the whole hand to its local tangent, and slightly corrects the rigid-part position for the new cross section. All finger joints retain their accepted values; the bottom pinch is unchanged. The hand was not simply translated vertically into the curved surface.

For all six weapons, the magazine-well initial capture radius rises from 45 to 55 mm, while axial depth and the range below the well each rise from 60 to 70 mm. Each boundary gains 1 cm as requested. `with_box_magazine_well` centralizes these parameters; only the six production profiles opt in explicitly, and their cosmetic variants inherit them. The common raw interaction profile is unchanged, avoiding effects on other weapons that reference the base profiles. The original angle requirements, 40 mm capture hysteresis, jump rejection, and ammunition transaction rules remain active.

## Model fitting

The shared hand-shape sources are M4 reload frame 31 and AUG grip_reload frame 28; source hashes are in `weapons/hand_poses/magazine.hpp`. The wrap grip uses the source middle-finger motion, transferred through each finger's anatomical bind frame to unify bending while retaining actual finger-root positions and bone lengths. Each weapon's `magazine_grasps.hpp` stores its magazine-specific in-wrist transform, contact point, finger corrections, and receiver source hash. Eighteen finger/palm joints switch together.

Free per-finger rotation fitting has been removed. The four fingers may use only shared MCP flexion, shared PIP flexion, and coupled DIP flexion. MCP joints always rotate around their own anatomical flexion axes without changing splay; the thumb receives only small adjustments. Copying each finger's local or global quaternion directly is unreliable because the source bone axes differ. Automated regressions constrain shared flexion and a fixed bending direction; offline checks use actual finger triangles to detect intersections.

Magazines and hands keep their original dimensions. Fitting adjusts only wrist poses and joint rotations; it does not stretch the mesh or hand bones. Size ratios are used only to propose offline contact targets, never at runtime.

| Magazine | Approx. mesh thickness (cm) | Approx. mesh length (cm) | Special handling |
| --- | --- | --- | --- |
| M4 | 2.97 | 19.21 | Use the actual separate magazine and existing rigid-part offset; clear the real receiver underside |
| M16 | 3.54 | 19.21 | Fit its thickness and bottom separately; do not merely reuse the M4 transform |
| AUG | 3.98 | 20.90 | Wrap-grip source; add a complete fit for the bottom pinch |
| FAL | 4.11 | 24.39 | Use the actual in-gun magazine `tag_clip_02`, excluding the animation-only magazine |
| TAR | 2.74 | 19.10 | Include the `j_plate` floor plate; remove excessive downward pressure from an infinite well plane and inspect the real receiver underside |
| ACR | 3.84 | 20.92 | Independently fit the wider profile and curved contact surface |

These are axis-aligned bounds of exported meshes in this batch, not real-world product specifications.

The M4 base hand pose previously contained only 30 finger nodes for both hands. This pass adds source-idle metacarpals and webbing on both sides, bringing the total to 36 so palm data for both reload hand shapes and the right-hand anatomical mirror take effect. Hand-bone lengths are unchanged.

## Integration boundaries

- `magazine_grasp_pose.hpp` defines a complete grasp and extends the existing magazine-grasp list; these six weapons do not gain a parallel pose ledger.
- `magazine_grip_selection.hpp` returns transform, contact, hand shape, and index. Bottom pinch/wrap use the body-relative palm policy; P90 continues to use its wrist-facing policy relative to the gun.
- `physical_reload_contact_sample.hpp` selects the candidate within the same tracked frame before calculating in-hand magazine and insertion geometry. Simulation uses the existing latch-on-acquisition mechanism.
- The presenter uses the same resolver and latched index. Hand IK uses the magazine's own wrist reference while held, and raw geometry uses that reference too. Rigid magazine submission checks the pose index to prevent a new grasp from reusing an old hand-bone frame.
- `hands/pose_mirror.hpp` preserves rigid magazine coordinates while mirroring the hand and in-wrist contact. Pose selection does not alter ammunition rules, weapon mechanical axes, or insertion tolerance.
- `vr_reload_interaction_status` reports `magazine_pose`, `magazine_candidate`, and `magazine_pose_count` for later headset comparisons.

Each weapon has only two constant candidates. Selection performs bounded vector operations; model fitting, surface queries, and optimization all remain in offline tools.

## Validation and further headset checks

Offline checks use actual skinned glove and magazine triangles across six weapons × two grasps × two hands, totaling 24 cases. Compiled C++ profiles are checked against in-wrist transforms and mirroring. Local cross-section constraints preserve magazine curvature; a global convex hull would wrongly treat the curved back as solid and push fingers off the surface. Receiver clearance uses the real mesh underside rather than requiring the whole hand below an infinite well plane.

Some source meshes are not closed solids. Raycast interior points and nearest-surface distances are offline diagnostics, not mathematical proof of zero clipping. Contact distance, full-hand visual previews, and insertion depth must be reviewed together; one nearest fingertip does not establish a good fit.

Automated regression covers facing criteria, invalid input, body/controller rotation together, same-frame geometry, grasp latching, post-insertion holding, failed refund, new acquisition, knife recipes, and the original P90 rule. Existing hand-pose, weapon-grip, underbarrel, and central-interaction regressions check shared changes.

Static checks of US Army and TF141 gloves in wrap grip found no intersections
between different finger triangles for the reviewed weapons and hands. The
result applies only to those glove models and poses, not all gloves or dynamic
transitions, and does not establish perfect hand-to-magazine surface contact.

Offline regression covers physical reload, weapon grip, hand pose, underbarrel,
and central hand interaction. All 24 compiled profiles match the reviewed wrist
positions and mirroring. Receiver-mesh checks found no triangle intersections in
the twelve wrap-grip cases. The ACR profile includes a 2.5 mm whole-hand downward
clearance offset without changing individual finger poses. Millimeter-scale
hand-to-magazine fit error may remain, so static checks cannot establish
headset acceptance.
