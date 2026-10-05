# Weapon heartbeat sensor

The shared sensor adapter binds the standard and arctic nine-bone heartbeat
attachment on reviewed ACR rifles and `m240_heartbeat_reflex_arctic`. An empty hand holds Trigger at the sensor and
moves its housing around the physical Z hinge. Trigger acquisition alone does
not toggle it. Acquisition covers the moving housing's native `XBoneInfo[2]`
bounds, with 5 cm of edge tolerance and a further 4 cm of reach along the gun's
forward axis. The original authored wrist contact remains available. Candidate
distance participates in the same part arbitration as magazines and handles.
Release finishes to
an endpoint with hysteresis; loss of tracking or focus cancels the hand lease
without completing a partial fold. A proven trigger release settles from the
last valid stroke even if the release-frame pose is invalid.
The opposite-hand pose mirrors the hand around the sensor, never the hardware.

Carry generation owns fold state. Changing the holding hand, selecting the other
gun or putting the rifle on the body does not create a new sensor. The carry
arbiter reserves the manipulating hand before support/body/world acquisition;
the same reservation excludes magazine and bolt manipulation. Gun release has
priority over the accessory lease.

Fold progress is a continuous gun-relative angle, with no search bins or wrist
pitch/roll contribution. It measures the actual wrist's lever arm, so a grasp
near the hinge can still complete the stroke. Axis-centred grips rebase when a
usable lever arm is established rather than inventing an angle. The shared
hinge accumulator also serves the belt
covers and retains overtravel at either stop until release. Support-only owner
changes do not cancel the sensor, and a physical holding-hand change releases
its grasp to the appropriate endpoint. Render presentation projects the latest
XR sample from a copy of the server gesture; scan state and hand arbitration
remain server-owned, while the housing and hand can follow the display cadence.

The wire follows a continuous cubic quaternion curve through the original
animation witnesses, parameterized by hinge angle and automatic tilt rather
than animation time. Those witnesses never discretize the interaction. The
screen's Y tilt is flat during manipulation and returns to the native open
angle over 120 ms after release. Fold endpoint settling reuses the finite part
return transition over 90 ms. The left grasp uses frame
10 of `h2_wpn_asl_masada_hb_close2open`; the right uses the glove mirror mapping.
The attachment root aliases the receiver's `tag_heartbeat`. XModel bind arrays
have independent origins, so they must not be composed as one model-space array.

The native tracker descriptor getter uses offset `0xE90`, not the stale `0xDB0`
comment in upstream reverse-engineering notes. Five witnessed scan/audio queries
use the held sensors' enable state in VR. This leaves native weapon/script mode
queries unchanged and avoids the 1100 ms alternate-weapon transition. ACR
`masada_*_mt_*` descriptors require the witnessed tracker on/off definition pair.
The captured M240 has a fixed tracker, no linked alternate definition, and both
native mode queries resolve to the same weapon. Both contracts must share the
native clip and reserve keys before bypassing alternate-feed ammunition rejection.
The M240 reuses the same Trigger gesture, hinge/wire pose, housing bounds and
screen renderer; its receiver mount supplies the weapon-specific placement.

The native renderer retains its materials, targets and sweep. Each open held
sensor supplies its solved world-space screen corners. Native corner tag order
determines winding; no hand/camera-fixed quad is substituted. With two open
sensors the existing native scan is shared, with separate geometry and fold
state. Holstered sensors do not scan or draw. VR-disabled execution retains the
native behavior. Call-site/entry byte checks reject a mismatched native build;
near relays avoid ASLR-dependent relative-branch failures.

`vr_heartbeat_status` reports acquisition, endpoint changes, server updates,
render projections, screen submissions,
geometry rejects and each held sensor's state. Submissions are not proof of
visible stereo pixels. Offline grip and physical-reload tests cover binding,
screen dimensions, both gesture directions/hands, invalid tracking and release.
Headset verification is still needed for grasp fit, visible sweep/targets in both
eyes, and two-gun manipulation while the other gun fires.
