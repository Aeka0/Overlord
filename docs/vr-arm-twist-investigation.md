# Forearm and cuff twist investigation

Controller-driven axial forearm rotation reuses the native deformation bones.
Offline regressions and native-mesh skin previews cover the solve; headset
acceptance remains separate.

The reported symptom is rotation in place: the wrist turns, while the cuff and
forearm do not follow. Positional arm IK is already present. The missing behavior
is controller-driven axial rotation of the forearm's existing deformation bones.

## Confirmed chain

`hand_pose_solver.hpp::solve_pose` derives the shoulder/elbow/wrist positions
from a two-segment IK solve. Its `lower_delta` only aligns the old elbow-to-wrist
direction with the solved direction. Holding these positions fixed while rotating
the wrist leaves `lower_delta` unchanged. Wrist descendants receive the tracked
orientation, while other elbow descendants receive only that direction change.

All four available exported 68-bone viewhands models have this hierarchy:

```text
j_shoulder_le/ri
  j_elbow_le/ri
    j_wrist_le/ri
      fingers ...
    j_wristtwist_le/ri
```

The twist bones are siblings of the wrists, not descendants. They therefore
never receive the tracked wrist rotation through the current wrist subtree
updates. They have nonzero skin weights; this is not a missing model feature.

| Export | Vertices influenced by left / right wrist-twist bone |
| --- | --- |
| `viewhands_us_army`, `viewhands_player_us_army` | 5104 / 3321 |
| `viewhands_tf141`, `viewhands_player_tf141` | 8044 / 3173 |

Counts include surface vertices with any positive weight, including vertices
also influenced by other bones. They are not exclusive sleeve vertex counts.
The twist pivots project to approximately 73.5% of the elbow-to-wrist bind segment
in these exports. That placement does not establish an animation blend weight.

Native MP5K and Vector reload animations contain changing wrist-twist rotation
tracks. Some also contain `j_wristtwist_back_*` channels, but those nodes are not
present in the four audited model skeletons. An animation channel name alone
must not admit or synthesize an extra runtime bone.

An isolated C++ probe including the actual `hand_pose_solver.hpp` reproduced
the issue with an elbow-parented twist sibling. Rotating the target 90 degrees
around the solved forearm axis, while holding wrist position fixed, produced:

| Node | Rotation change | Position change |
| --- | --- | --- |
| Shoulder | 0 degrees | 0 |
| Elbow | 0 degrees | 0 |
| Wrist | 90 degrees | 0 |
| Wrist-twist sibling | 0 degrees | 0 |

This is offline source behavior, not an instrumented headset capture.

## Integration constraints

Correcting only the initial IK target is insufficient. `apply_poses` subsequently
sets the wrist's anatomical orientation from the weapon/free-hand basis. Reload
part constraints, mirrored alternate weapons and equipment presentation can
change that orientation again. The deformation pass should consume the final
anatomical wrist orientation immediately before the native matrix commit.

There are separate empty-hand, normal/independent-weapon and selection-transition
commit paths in `hand_component.cpp`; all must call the same implementation.
The completed solved matrices are already copied to the native skeleton, so a
new rendering or skinning hook is not indicated by this evidence.

## Implemented correction

`forearm_twist.hpp` binds the optional elbow-parented auxiliary bones inside the
admitted hand model. Missing, duplicate, foreign or malformed nodes leave that
arm's existing pose intact. Descendants must remain within the same model and
cannot include the wrist, another arm or any weapon bone.

The finalization pass reads the final anatomical wrist after all hand/weapon/
equipment overrides. It projects residual rotation about the solved forearm
axis relative to the elbow-transported anatomical bind basis. The helper receives
the full axial rotation; original skin weights distribute deformation along the
forearm. Wrist flexion is excluded. Helper pivot, tracked hand, fingers, weapon
and shoulder/elbow poses stay unchanged. Actual helper descendants follow their
parent's correction without accumulating it across repeated evaluations.

Native MP5K/Vector reload samples support this initial full-twist policy: median
helper/wrist axial ratios, measured relative to the elbow and each bind basis,
were 1.016/0.906 and 1.004/1.139 for left/right hands respectively. They are
authored animation samples, not a universal anatomical law or exact fitted rule.
No fixed half-twist or per-weapon coefficient is introduced.

At a near-180-degree cross-axis wrist bend, the axial rotation is undefined.
The pass retains the last valid twist in elbow-local space; without valid
history it uses neutral. Reference/model/resource changes, rejected tracking,
untracked hands and gaps over 150 ms discard history. Full axial quaternions
remain equivalent across sign changes and ordinary +/-180-degree roll crossings.

Empty hands, independent/native held weapons and selection transitions share
the same finalizer before their native matrix commits. `vr_hands_status` reports
`forearm_twist=final_anatomical_axial`, the applied hand mask and admitted bone
indices. This is presentation-only and publishes no new gameplay input.

The implementation follows these constraints:

1. Bind optional twist bones by semantic name, parent relationship, model
   ownership and finite bind pose. Retain each model's own anatomical basis.
   Missing optional bones preserve current IK behavior.
2. At finalization, derive a neutral anatomical wrist orientation from the
   model's bind relation transported with the solved forearm. Decompose the
   final wrist's residual rotation into swing and twist about the elbow-to-wrist
   axis. Only axial twist drives forearm deformation; wrist flexion must not
   become a sleeve bend.
3. Apply the twist to the admitted auxiliary bone and its actual descendants,
   retaining their solved attachment positions and bind offsets. Leave the
   shoulder/elbow locations, final tracked hand, fingers and weapon untouched.
   Distribution is based on the native sample and skin checks above, not a
   fixed half-rotation inferred from the bone name.
4. Keep finite/singularity checks and any angle continuity state inside the
   existing per-object solver context. Reset on model/resource/reference changes
   and tracking loss. Repeated solves must not accumulate rotation. Unknown
   topology must not acquire a guessed skeleton mapping.

Elbow swivel and shoulder/body estimation are separate subsequent work. They
are not required to correct this confirmed cuff-twist gap, and should not be
coupled to raw controller roll as a substitute for missing deformation.

## Verification and remaining acceptance

Hand-pose, empty-hand-pose, weapon-grip and physical-reload suites pass. Added
tests cover both hands, anatomical bases, translated/rotated frames, pure
flexion, full rolls through 180 degrees, quaternion sign equivalence, repeated
evaluation, singular fallback/reset and malformed optional bones. The native
US Army and TF141 meshes were skinned with matrices emitted by the actual C++
implementation at 0/90/180-degree wrist rolls. Both previews show sleeve motion;
all non-helper bone transforms remain unchanged and vertex outputs stay finite.

The extreme 180-degree previews retain visible narrowing in the transition
between elbow and twist weights, a limit of this original linear skinning and
fixed-elbow deformation. This version corrects cuff following; it does not add
new skin weights, volume correction or elbow/upper-arm roll redistribution.
In-headset checks remain necessary, especially for that extreme rotation.

- Stationary wrist rolls in both directions; distinguish rotation about the
  forearm from flexion across it. Elbow placement and tracked hand stay fixed.
- Both arms and both audited skin families, including mirrored weapon holds.
- Empty hands, rear grip, support grip, magazines, action manipulation, knife
  poses, two independent weapons and selection transitions.
- Quaternion sign equivalence, rotation across 180 degrees, bent/extended arms,
  tracking loss, recenter and model change without a sleeve jump or NaNs.
- Skin preview using the actual weighted vertices, followed by headset checks
  for cuff seams, twisting collapse and excessive winding.
