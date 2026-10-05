# Shared scripted-breach control

The shared breach adapter coordinates scripted body ownership, VR hand/weapon
permission, camera orientation, and input continuity during native breach and
slow-motion sequences. Native scripts continue to own planting, combat
permission, weapon grants, animation, and mission outcomes.

## Ownership and phase transitions

The sequence service recognizes the active and passive native breach rigs on
any map before map-specific Oilrig or rappel adapters run. The common
`_slowmo_breach` module also owns its Gulag rescue animation, so the policy does
not depend on a map-name allowlist. A different scripted rig remains outside
this contract until it has an explicit identity and lifecycle adapter.

Before persistent `level.breaching` becomes true, planting owns the native body
and suspends VR hands, weapons, and movement. Native `slowmo_begins` sets that
state and transfers presentation to the VR player, even if the player remains
linked. Native combat permission remains authoritative. Unlink ends adapter
ownership. Consumed notetrack flags and elapsed-time guesses are not phase
identities.

During the linked sequence, `native_yaw` preserves the authored camera yaw while
excluding authored pitch and roll from the eye orientation. Full HMD orientation
and room-scale translation compose over that yaw. The mode publishes current
spatial coordinates so hand interaction does not depend on a cinematic camera
frame that withholds gameplay space. On unlink, rendered heading returns to
native command ownership without adding HMD yaw twice. Oilrig assassination and
rappel retain their separate camera policies.

## Slow simulation and input continuity

Native slow motion can reduce the cadence of server consumers while SteamVR
tracking continues to update at its own frame rate. Input frames carry producer
continuity so firing, hand arbitration, support grips, and physical gestures can
distinguish continuous movement from stale data. A slow server tick alone does
not reset a valid held input.

Input and geometry still have independent freshness limits. Focus loss, pose or
reference changes, sampling gaps, and gameplay/menu transitions cancel old
intent and require normal rearming. Render-rate tracking remains independent of
the native simulation scheduler. Do not remove stale-pose or native-ammunition
checks to make an interaction survive slow simulation.

M1887 lever motion and the shared cylinder wrist-flick detector use producer
continuity with a bounded maximum motion span. Unannotated gaps retain the
conservative consumer-gap rule. Spin and return increments remain bounded; the
adapter cannot synthesize motion that was not sampled.

Several motion detectors still require short sample spans because they depend on
swept contact, velocity, or release witnesses:

| Interaction | Constraint |
| --- | --- |
| Charging-handle and receiver-paddle slaps | Swept impact history is limited to 150 ms. |
| Magazine-latch strikes | The contact sweep is limited to 150 ms; ordinary insertion uses a separate path. |
| Belt-cover palm push | Cover push is limited to 100 ms. |
| M200 release-assisted bolt lock | Release velocity needs recent samples; direct bolt travel remains geometry-driven. |
| Knife, firearm, shield, and fist strikes | Melee motion history is limited to 120 ms. |

These interactions need a bounded render-rate motion history with server-side
settlement before widening any motion window. Validate producer discontinuities,
real pauses, same-frame releases, and tracking jumps. Do not remove the limits or
infer missing movement from a late consumer sample.

## Validation and acceptance

Offline coverage should include breach rig and phase classification, slow and
normal simulation cadence, input generation changes, release-to-rearm,
support-grip and magazine ownership, native camera yaw, HMD tilt, spatial-frame
freshness, and unlink heading continuity. Exercise the nested lever/cylinder
gestures as well as interactions that still use short sample windows.

Headset acceptance should verify one arm pair during planting, weapon interaction
and firing during slow motion, native camera handoff, door use at ordinary and
close range, and recovery after focus loss, recentering, unlink, and level
transitions. Structural reuse across Oilrig, Gulag, and other sequences does not
replace scenario-specific acceptance.
