# Fixed M82 reticle assistance research

. Research only: no assistance behavior, settings, hooks or deployed
binaries were changed. Tracked-hand translation is the accepted interaction for
complete the mission objectives. Assistance is an optional refinement, not a
replacement for that accepted control path.

## What must move

The requirement is to change actual native turret aim. The reticle can remain at
the center of its native texture while the optical scene moves underneath it,
bringing the target toward the reticle. The native firing direction must follow
the same command. Moving only the reticle artwork would misrepresent the shot.
The anchored display plane and HMD viewing transform must remain unchanged.

The existing [fixed-sniper command adapter](../src/client/component/vr/gameplay/fixed_sniper.cpp)
already adds joystick and hand deltas to native client pitch/yaw before
[FinishMove](../src/client/component/vr/gameplay/controller_component.cpp)
packs the command. This is the proposed integration point for an additional
small assist delta. Native prediction, turret limits and the existing firing
route retain authority. Do not patch the rendered camera or issue another shot.

## Why the ordinary setting cannot be reused directly

[Ordinary assistance](vr-aim-assist.md) runs for an already admitted bullet shot
and changes `shot_geometry`; it neither updates usercmd angles nor moves the
reticle. Its geometry selector has a fixed 1300-unit range and a strength-scaled
cone reaching 10 degrees. Those are general firing-policy choices, not suitable
defaults for this narrow long-range optic.

For scale, the earlier mounted witness had native vertical half-FOV tangent
0.028844703, about 3.304 degrees of vertical picture. A 10-degree attraction cone
would extend far beyond that image. Candidate selection and correction speed
must be expressed relative to the optical image, not HMD FOV or desktop pixels.
The 1300-unit range also needs its own mission-specific review; this research
does not claim a live measured distance for every sniper target.

Reusable parts are native target-point access, entity lifetime tracking, hostile
team/civilian admission, visibility-trace conventions, and bounded snapshot
publication. Keep those shared where appropriate without changing the ordinary
helper's range, behavior or `vr_aimAssistStrength` setting. Stock gamepad aim
assist is not established as a drop-in replacement: these VR deltas are inserted
at a later custom command boundary, and its native input/target ABI would require
a separate audit before invoking it.

## Mission-specific candidate source

The loaded `maps/dcburning` script provides a much narrower source than all warm
objects or all actors in the level:

- `_id_B82D`, lines 2889-2900, spawns `hostiles_ww2_barret` into
  `level._id_B7B2`. It installs the sniper death counter and damage effects.
  Its initial `magic_bullet_shield` is removed after `crowsnest_has_been_cleared`.
- Lines 2901-2912 gate the sniper phase through
  `obj_commerce_defend_snipe_given` and `obj_commerce_defend_snipe_complete`.
- `_id_D445`, lines 5909-5920, decrements the remaining-target count and completes
  the objective after these targets die.
- `_id_BD6B`, lines 6327-6410, includes enemy Stinger/Javelin operators and prone
  or crouched spotters. These normal mission targets can set `ignoreme=1`.
  That flag alone must not reject them or be interpreted as civilian identity.
- `level._id_BFB9` / `vehicles_crowsnest_defend` contains protected evacuation
  vehicles. It is not an enemy candidate list. A visible thermal highlight,
  nearby objective marker or common `crowsnest` name is insufficient admission.

Proposed first scope: exact mounted M82 ownership, the active sniper objective,
and live hostile members of the mission target array. Query saved state each
server observation; never cache a mission phase across checkpoint rollback.
Do not force script flags, remove protection, or broaden admission to all
vehicles/neutral actors. Later M82 phases need their own explicit policy before
extending this target provider.

`getshootatpos` is an existing native point API. Its suitability for every
scripted standing/crouched/animated pose still needs confirmation; do not assume
it always denotes the visible chest. If additional anatomical points are needed,
validate native tags and their visibility instead of silently selecting heads.

## Recommended first behavior: weak guidance during deliberate adjustment

Preserve the accepted stop/release behavior:

1. Require a held, valid side-button clutch and a meaningful current manual
   adjustment. Merely wearing the headset, pressing fire, or holding a motionless
   hand should not start autonomous pursuit.
2. Acquire only a visible eligible target already very close to the reticle.
   Let the player bring aim into that small region; never search across the image.
3. Add a small, rate-limited correction toward that target through native
   pitch/yaw. Bound it relative to the current manual displacement as well as
   elapsed time. Manual input is applied immediately, without added filtering.
4. Stop correction when the hand/stick stops or the clutch is released. An
   intentional pull away immediately yields to the player and drops retention.
   Never accumulate a correction that fires later when input resumes.
5. Retain one target identity while appropriate rather than picking a different
   nearest actor every tick. Death, obstruction, phase changes, stale data,
   dismount or checkpoint rollback invalidate it. Do not jump to the next enemy
   automatically after a kill; require fresh manual acquisition intent.

This does visibly correct real aim while preserving hand control. Full automatic
tracking, snap-to-target, auto-fire and bullet-only redirection are outside the
proposed first version. It should have an independent opt-in setting.

Illustrative starting values, not measured tuning:

| Constraint | Proposed initial bound |
| --- | --- |
| Acquisition radius | 5% of optical image height |
| Retention exit radius | 8% of optical image height |
| Additional correction | At most 25% of the current manual screen displacement |
| Additional speed | At most 0.2 image heights/second |
| Idle/release correction | Zero |

At the recorded 3.304-degree vertical FOV these correspond approximately to a
0.165-degree acquisition radius, 0.264-degree exit radius and 0.661 degrees/second
absolute speed ceiling. Convert normalized image errors through current native
optical tangents. Zoom changes must not snap aim or change apparent attraction
radius. Use identical pixel scale on both axes (image height as the common unit),
not separately normalized width/height that turns a circular region into an oval.

## Thread ownership and performance

Native script queries, life/team checks and traces belong on the existing server
scheduler, only while this context is active. Publish bounded value snapshots
containing scope epoch, target entity plus lifetime generation, point, sample
time and eligibility/visibility result. No VM object or zone-owned pointer may
escape to the command/render thread.

The command thread reprojects fresh snapshots against the actual optical aim
after manual input, decides retention and computes the small correction. A
query failure or expired snapshot produces zero assistance while hand/stick
input continues. Never wait for target queries or rendering. Reuse the existing
entity-free generation witness rather than identifying a target by index alone.

Bound candidate count and trace work. A practical research starting budget is
the native 64-actor ceiling, followed by only a small shortlist for obstruction
tests (for example 4-8 per server observation). Validate camera visibility and
line of fire from the appropriate native origins; do not steer through cover
because the thermal shader highlights something. No GPU readback, image-based
person detector, per-frame entity-table scan or new polling thread is needed.

## Remaining validation before implementation/enablement

- Verify the native point, visibility and reachable turret angles for each
  scripted launch/spotter pose. Establish the actual mission distance envelope.
- Validate that moving the native command also moves the displayed target and
  native impact direction together, including recoil and shot timing.
- Test no-input/no-clutch invariance, head-only motion, pause/recenter, entity
  replacement, deaths, occlusion and mission-phase transitions.
- Test clustered enemies, fast manual sweeps, deliberate pull-away, handover,
  maximum zoom, slow frames and failed native queries without changing manual
  responsiveness. Tune with the operator rather than treating the values above
  as accepted settings.
