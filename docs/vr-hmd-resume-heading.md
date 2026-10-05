# HMD recovery and horizontal body heading

A captured standby/resume failure had a valid camera rotation but a shoulder
heading exactly 180 degrees away. Left/right controller roles were correct;
the left shoulder appeared 18 cm to the right of the actual camera, and vice
versa. The continuous Euler state was approximately pitch 162, yaw 74, roll -177,
equivalent to the physically upright pitch 18, yaw -106, roll 3 orientation.

## Separate responsibilities

`continuous_angles` remains the native input representation. Its nearest-branch
selection is needed for continuous pitch beyond the vertical poles. Rendering
continues to use the complete tracked rotation matrix.

`horizontal_heading` independently extracts the yaw of the tilt-free rotation
for shoulders and the body estimator. With H2 forward/left/up rows, the heading
is `atan2(forward.y - left.x, forward.x + left.y)`. This is the matrix form of
fused yaw, independent of the previous Euler branch; see
[Allgeuer and Behnke's orientation representation](https://arxiv.org/abs/1809.10105).
Pure pitch, including leaning past vertical, retains heading. Within about eight
degrees of a fully inverted up axis, where heading becomes undefined, it retains
the last defined value. Returning upright reacquires physical heading immediately.

Recenter has a different contract: upright tracking-to-world translation uses
the exact horizontal forward direction, without simultaneous pitch/roll coupling.
Only an upright, nonsingular reference is canonicalized there; intentional
over-pitch retains the existing input branch. The body heading is rebuilt in the
new reference. This keeps camera, controller positions and native command history
on their existing common basis while correcting the shoulder/body orientation.

## Regression

- `horizontal_heading_tests.hpp`, in `vr-hand-pose-tests`, replays the captured
  orientation 1000 times, both pitch poles, inversion/recovery, artificial turns
  and invalid input. Continuous native angles keep their branch while the body
  faces the physical direction.
- `game_view_tests.hpp`, in `vr-engine-stereo-probe-smoke`, creates an alternate
  branch through actual tracking publication, verifies shoulder side and body
  facing, then repeats after tracking invalidation/re-acquisition.
- Existing over-pitch, yaw-only recenter translation, tracking continuity,
  native input history and scripted camera tests remain enabled.

CPU replay confirms the coordinate correction. A fresh headset power-cycle is
still requires end-to-end acceptance across supported headset transport and
standby transitions.

## Camera ownership and reference recovery

The later Cliffhanger report exposed the same representation issue in camera
ownership, beyond shoulders/body orientation. The live headset was physically
upright (about 6 degrees pitch, -7 degrees roll), while continuous input retained
the equivalent 174/-134/173 branch in its local reference. Scripted camera entry
and handback were still consuming that Euler yaw as a physical heading.

All shared camera policies now use the matrix-derived tilt-free heading for
their reference bookkeeping. Native commands retain the separate continuous
Euler yaw paired with their pitch; scripted handback accepts both quantities
explicitly. Fully native views remain untouched and seed their exit against the
actual physical forward direction. Native-yaw scenes retain authored yaw deltas
while preserving the observation heading through tracking-reference changes.

Native command history stores the physical heading and cumulative reference
rotation alongside the Euler contribution. Reacquisition/recenter preserve the
physical facing even if the new reference chooses the other Euler branch.
Prediction still reading an older command resolves that contribution in the
current reference instead of dropping it to zero and introducing a half turn.

The camera regression replays the captured orientation with both Euler histories
through gameplay, free-head, fully native, native-yaw, seeded free-head, vehicle
and authored-yaw policies. It compares complete camera matrices before and after
tracking reacquisition and explicit recenter, and checks scripted exits. Separate
history checks cover older prediction and alternate-to-canonical recovery.
Existing pitch-pole, scope, hand/body and authored-motion tests remain enabled.
These are offline regressions; an actual HMD standby/wake remains the hardware
acceptance step.
