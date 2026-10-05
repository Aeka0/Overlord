# Controller input and hand interaction

This guide describes the shared SteamVR input path and the boundary between
tracked controllers, native player commands, and weapon ownership. Weapon
mechanics remain in their topic adapters; see [weapon interaction
architecture](vr-weapon-interaction-architecture.md), [weapon
registration](vr-weapon-registration.md), and [physical reload
topics](vr-m9-reload.md).

## Input flow

SteamVR actions are sampled at the existing runtime frame boundary. One immutable
input frame is published for gameplay consumers; subsystems do not poll the VR
runtime independently. The frame carries tracking and producer continuity so
consumers can distinguish a held input from a stale or discontinuous sample.

The left stick supplies movement relative to horizontal HMD heading. The right
stick supplies smooth turning by default or snap turning when configured. Stick
click actions request sprint and jump without replacing analog movement or
turning. Device-specific bindings remain in `data/vr_input/`; ship that
directory with the client and let SteamVR retain custom bindings.

Input continuity changes when tracking, focus, recentering, calibration,
gameplay/menu ownership, or a long sampling gap invalidates the prior frame.
Server-side firing, hand arbitration, support grips, and mechanical gestures
consume that continuity. A slow server tick alone does not cancel a fresh held
input. Consumers use bounded sample and geometry freshness checks and fail
closed on stale frames.

## Native command boundary

Controller actions enter H2 through the existing command-construction path.
Insert input before a command enters native history; do not rewrite commands
again during prediction replay. Story scripts and native weapon state retain
their command ownership. VR adapters modify only the admitted command fields
required for the active interaction.

The render and command paths share hand/weapon ownership state. Only the current
control hand may request a shot for an owned weapon; a support hand, empty hand,
stale controller, or hand that lost its lease cannot fire. A tracked muzzle pose
may provide the shot direction, while native ammunition, cadence, projectile,
damage, and mission gates remain authoritative. See
[holding authority and firing](#holding-authority-and-firing).

Native sprint and jump are admitted through H2's existing command whitelist.
Scripted command overrides remain authoritative in story scenes. A signature or
identity mismatch rejects the adapter rather than installing a partial hook.

## Holding authority and firing

Each held weapon has one control owner. The other hand may support the weapon or
operate an explicitly admitted part, but it does not acquire firing authority
from render order or Trigger timing. Handoffs transfer the current weapon state
through the native ownership transaction and require fresh input where the
mechanic defines a rearm boundary.

The firing adapter validates the held instance, control hand, tracking
generation, weapon mode, native ammo/cadence state, and current VR permission
before requesting a shot. Native acceptance remains the final commit. A failed
native request does not consume local ammunition or queue a later shot.
Independent dual wield uses an instance-scoped shot request so native current-
weapon selection does not become a hidden second owner; see
[independent weapon firing](vr-independent-weapon-fire.md).

Native launchers and scripted vehicle weapons retain their dedicated firing
paths. Their lock, selected-module, script ammunition, and projectile rules are
not replaced by the ordinary tracked-muzzle path.

## Structural hand and weapon admission

Hand interaction uses the loaded model hierarchy instead of weapon-name or
hand-name whitelists. An admitted assembly must provide:

- One hand model with unique left/right shoulder, elbow, and wrist chains plus
  `tag_weapon`.
- One receiver model with a unique `j_gun`, connected to the hand model's weapon
  tag.
- A receiver-owned `tag_flash` within the validated muzzle hierarchy.
- Finite bind transforms, complete model ownership, and the established pose
  and IK bounds.
- A root-relative muzzle direction within the profile's conservative forward
  tolerance.

`hands/rig_builder.hpp` combines XModel parent lists, DObj model attachment
parents, and native duplicate-bone aliases. It validates the whole graph before
publishing an ownership mask. Missing, cyclic, or ambiguous links reject the
assembly. Appended arm or sleeve models do not automatically become weapon
parts. A second client viewmodel is treated as a conservative conflict until
its native ownership is known.

M9 and M4 assemblies share the same `viewhands_us_army` skeleton and native
weapon-tag relationship. AK demonstrates that nested optic/reticle aliases can
remain attached to the receiver without a second hand solver. These examples
demonstrate structural admission; they do not imply universal acceptance for
every skin, attachment, pose, or firing path.

## Skeleton evaluation and transforms

The adapter runs after native `DObjCalcSkel` completes, while the caller holds
the DObj lock. It resolves and solves a skeleton at most once per calculation
cycle, including rejected attempts. A second eye or later tag query cannot
reprocess the same skeleton. Only the admitted arm descendants are written;
fingers, weapon parts, recoil, reload animation, and unrelated bones retain
their native transforms.

Native global bone translations account for the current render-view offset.
Controller and native poses use one copy of the yaw-only tracking reference,
camera origin, and world scale for a solve. Grip position anchors the wrist;
aim orientation rotates the original wrist-to-gun relationship. Unreachable
targets use the configured arm-extension policy rather than changing the
native body or weapon root.

Saved wrist-pivot offsets are separate from the existing hand position and
angle alignment controls. They define the controller point that maps to the
anatomical wrist and are not an automatic calibration system. Adjust them only
when the tracked hand and rendered wrist have a consistent relative offset;
resampling or adding smoothing does not correct a wrong reference point.

## Verification and device acceptance

Offline checks should cover input continuity, stale frames, tracking recovery,
recenter, focus/menu transitions, command-history insertion, unique control
ownership, native rejection, and assembly-graph validation. Skeleton checks
should verify that non-arm bones and native weapon animation remain unchanged.

Headset acceptance should check left/right reach, elbow direction, wrist and
grip alignment, aiming while looking away from the weapon, hand transfer,
support-grip transitions, and each admitted weapon/attachment family. Repeat
with the actual SteamVR bindings and devices used for release. Structural
admission, a successful build, and CPU tests do not establish in-headset comfort
or native gameplay acceptance.
