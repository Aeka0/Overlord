# Scripted body arm control

Scripted arm control lets a VR controller drive the arms of a native animated
body during an explicitly admitted story phase. The native body retains its
shoulder position, upper-body animation, root motion, camera, and mission
authority. The adapter solves only the requested arm chains.

## Cliffhanger opening

During the opening ledge sequence, the native `viewbody_arctic` supplies the
armature and initial pose. The arms remain fully native during the crouched
opening animation. When the native stand-up animation begins, its playback
progress blends each arm smoothly from the authored pose to its corresponding
tracked controller pose. The transition follows animation progress rather than
a guessed timer.

Each controller drives its matching shoulder, elbow, and wrist chain. The
current native shoulder position and segment lengths remain fixed. Finger bones
retain the current native local pose. The camera, torso, legs, and root
translation are never included in the IK solve. If either hand loses valid
tracking, that side returns to its native pose; a tracking recovery or body
replacement rebases from the current native pose.

The native `tag_weapon_left` and `tag_weapon_right` attachment points are below
their corresponding wrists. Mission ice picks attached there follow the arm and
remain owned by the native scripted sequence. The independent VR prop adapter
must not submit a second pair during this phase.

## Admission and ownership

`sequences::view::arms` publishes the body entity, model profile, control mode,
and per-hand mask. This ownership is independent of `suspend_weapons`: scripted
arms release ordinary weapon presentation and hand interactions, while native
movement, ledge input, and jump input retain their existing owners.

The Cliffhanger adapter requires the expected opening phase, a living player,
the actual linked body entity, and the expected `worldbody` animation. A camera
helper or unrelated linked object cannot qualify. Rendering revalidates the
entity's current DObj and exact model profile before solving. Estate's
`hide_body_arms` path remains a separate policy because it hides native arms and
uses another presentation rig.

The shared solver does not query the map, script VM, or global game state. A
future scripted scene can reuse it by publishing the body identity, arm mask,
native/tracked phase, and model profile through the same interface.

## Skeleton and frame lifecycle

`native_hand_rig` and scripted arms share DObj description, including model-root
attachment parents and duplicate-bone aliases. The `scripted_body` rig kind
allows approved wrist-descendant props without requiring firearm roots or a
muzzle. Validation for ordinary weapons and hands-only rigs remains strict.

The `scripted_arms_runtime` hook runs during `DObjCalcSkel` while the native DObj
lock is held. It completes the native skeleton first, then applies the shared
anatomical wrist offset, bilateral IK, and forearm twist. The elbow plane follows
the torso orientation, not free HMD rotation. Native and solved arm poses blend
in parent-bone space, and only descendants of the requested arms are written.

Each skeleton calculation may be processed once, including failed attempts.
Tracking recovery, a second eye, or a later tag query cannot retry the same
skeleton. The next animation frame creates a new solve opportunity. Native
attachment changes may rebuild rig bindings without replaying a completed
stand-up transition.

## Diagnostics and acceptance

`vr_sequence_status` reports the selected body profile, entity, control mode,
and hand mask. `vr_scripted_arms_status`, also included in
`vr_hands_status`, reports rig binding, tracking, per-arm blend weight,
processing, and rejection counts. Diagnostics are for fault isolation and do
not establish acceptance.

Offline checks cover crouch/stand phase selection, variable and paused animation
progress, per-hand tracking recovery, checkpoint rollback, duplicate processing
within a skeleton frame, fixed shoulders, unchanged non-arm bones, wrist-child
transforms, and atomic rejection of invalid data. Headset acceptance should
verify one visible arm pair, a smooth stand-up transition, independent left and
right poses, stable elbows during free head movement, and clean handoff when the
body changes, picks attach, or the player reaches the top.

Scripted arm control does not implement ice contact, support, pulling, or climb
locomotion. During free climbing the mission body relinquishes arm ownership to
the ordinary VR hand system; see [Cliffhanger climbing](vr-cliffhanger-climbing.md).
The shared solver remains available for other story scenes without changing
their existing policies.
