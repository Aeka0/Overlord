# Physical riot shield

The registered riot-shield profile has offline tests and compiles with the
client. Runtime bullet interception, audio/FX, native pickup/drop, and headset
fit require separate acceptance.

## Weapon and presentation

`weapons/riot_shield/` registers `h2_viewmodel_riot_shield_mp` with its reviewed
17-bone skeleton. Its native `tag_flash` bind is rotated 90 degrees, so the rig
resolver uses an explicit shield contract; ordinary firearm admission keeps its
forward-axis requirement. The socket remains a model registration reference for
the existing world/viewmodel alignment and exact-record skin cache. Its published
frame has `firing_capable=false`, and cannot authorize firing or an ammunition HUD.
No ammunition feed or reload profile is registered.

The native left grasp is sampled from `h2_wpn_eqp_riot_shield_idle`, frame 0.
Explicit `control_grips` retain that left pose without a mirror roundtrip.
The right grasp uses a 180-degree model roll about the front normal, swapping
the strap side while retaining the ballistic front. Both ends of this H2
viewmodel have glass. Shared `control_rotations` apply the same roll in skinning,
carry contacts, defense, melee and drop geometry; the wrist rotation compensates
so the anatomical hand itself does not roll. Both sides' fingers are authored
explicitly. Side detection uses `_ri_` / `_ri` suffixes, never the substring
`_ri` (which also matches the left ring finger). The shield uses one controlling hand;
support acquisition is disabled. Native animation timing never drives shield
position or physical strikes. Source hashes are recorded beside authored data.
Raw game models and animations are not distributed.

The shield is now mounted to the forearm, not only the wrist. The native idle
wrist-to-elbow axis is authored in model space. Shared two-bone IK chooses the
elbow bend toward that axis while retaining shoulder estimates and the existing
bone-length/reach limits. The shield then swings around the grip to align its
cuff axis with the solved forearm, retaining wrist roll. This constrains shield
pitch/yaw as a strapped object; it no longer rotates freely around the wrist.
The hand remains on the handle. The renderer, other held hand and server carry
pose use the same mount solve; shielding, melee and drop use that resulting pose.

The shared instance ledger represents ammunitionless inventory with an explicit
zero clip key and zero rounds, with one instance per definition. Reconciliation,
selection, pickup and retirement retain normal generation/ownership boundaries,
without observing or writing a native clip/reserve cell. A firearm returning an
invalid zero native key is rejected rather than classified as ammunitionless.
This also permits carry/release without an artificial magazine or dummy ammo.
The native drop entry checks identity separately from ammo availability; a
remaining unconditional ammo guard there was removed after first feedback.

## Shield geometry and melee

The shield is about 58 cm wide and 99 cm tall. Eight strips (16 front-facing
triangles) approximate its bowed front, including the ballistic glass. A circle
fit to the exported glass front has less than 0.5 mm radial residual; the
piecewise strips and conservatively trimmed rounded corners remain a simplified
collision surface. They are neither a full mesh collision nor an infinite plane.
Native +X is the front; normals follow the curve, independently of the HMD or
player's facing direction.

Each bullet segment is transformed into shield space and tested against the
front panels. The closest existing native intersection is the upper bound.
Rear-facing, parallel, zero-length, nonfinite and out-of-range segments cannot
create protection. Earlier walls, actors, or exposed player body intersections
win. Rendering and defense share the authored receiver frame through the carry
scene cache. Ownership, reference generation, input timestamp, grip/aim tracking,
focus and settling checks reject stale poses. Defense adds no render waits and
performs at most 64 panel tests per shield (the current profile uses 16).

Nine shield surface/edge samples reuse physical firearm melee motion, body-local
velocity, the 400 ms player cooldown, contact withdrawal and native NPC traces.
Shield strikes use the held shield's native melee-damage accessor without the
firearm one-third scaling. A separate shield tool kind sends `EV_MELEE_HIT`
with the shield weapon identity and no knife flag, so native weapon-specific
feedback participates. Accepted hits also use the native melee blood event.
Native `G_Damage` owns actor pain, mission policy and death; no stock auto-lunge
or hand animation replaces the physical swing. Firearm blunt damage/sound are
unchanged, and the player cooldown/contact-withdrawal gate remains shared.

## Outgoing fire

Before firing, two-sided panel tests check physical penetration along the
firing side's shoulder-to-elbow, elbow-to-wrist and wrist-to-muzzle chain.
Shoulder/elbow/wrist positions are captured from that weapon's actual solved
rig, with its world origin, units, instance and pose lifetime. Upper-arm and
forearm capsules use 3.5 cm and 2.5 cm radii. Segment-to-triangle distance handles
edges, endpoints and coplanar overlap rather than testing only the arm centerline.
These are conservative arm volumes, not per-triangle sleeve collisions.
The shield-holding arm is excluded. Going around the finite shield edge is valid;
putting the whole gun ahead of it while the firing arm crosses it is not.
Small endpoint tolerance prevents on-surface barrel bypasses.

A muzzle entirely behind the shield is allowed to fire when the arm/barrel do
not penetrate it. The shot consumes ammunition and produces its ordinary firing
feedback; native outgoing bullet/pellet traces then hit the shield on either
face. Back-side hit normals face back toward the firing gun. No forward-ray
test suppresses an otherwise legitimate shot before emission.

Independent bullet fire and underbarrel fire check before ammo debit/launch.
The compatibility path checks trigger admission and rechecks at native firing.
Missing/stale poses of a known held shield deny firing until tracking is current.
Released or stowed shields do not participate. Actual outgoing pellet traces
also test both faces, so dispersion that catches an edge stops on the shield
even when the central shot was allowed. Incoming fire retains front-only defense.

## Empty opposite hand

The common empty-hand service restores a genuinely free wrist from its own
tracked orientation and the current hand model's neutral basis. It does not
inherit the shield's grip rotation, right-hand roll or forearm mounting. The
existing per-hand gesture controller owns finger curls. Held weapons, knives,
reload parts and active world interactions keep their pose authority. This
same service applies whether a hand is shown beside a shield, a gun, or alone.

## Native bullet boundary

These boundaries were read from the existing locally captured H2 native image;
no live game was running during implementation. Runtime byte checks gate the
complete hook set before enabling the shield rig.

| Boundary | Role |
| --- | --- |
| `0x1404AC4E0` | Server BulletTrace; receives segment parameters and fills the 0x50-byte result |
| `0x140599938 -> 0x1406B73E0` | Per-entity query; only entity zero within a managed bullet trace receives a stack copy of its query and priority map |
| `0x1406B7727`, `0x140655AFA` | Witness priority pointer at query +0x78 and native skip of priorities below 2; setting hit-location 19 to zero omits only the old shield part |
| `0x1404ABD94`, `0x1404AB5F7 -> 0x1404A9B00` | Bullet damage/feedback angle checks; return false only for the managed local player |
| `0x1404ABC50` | Bullet processing; consumes only an unchanged, stamped synthetic shield result and returns false to terminate that pellet/penetration segment |
| `0x1404AB5D7 -> 0x1404AC950` | Native shield hit-event emitter, called with actual world contact, normal, shooter, incoming weapon and event 0x47 |

The original trace still owns all other geometry. NPC shield priorities and
angle checks are untouched. No shared priority map, model, player angles or
health is rewritten. A bounded thread-local sidecar invalidates reused results
and consumes a matching hit before native callbacks. The shield path bypasses
native durability and ricochet processing; the selected native weapon may be a
pistol held beside the shield, and must not be treated as shield durability.

The initial policy covers incoming ordinary bullets (native means 1/2) and physical NPC strikes. It does
not add explosion protection, back-holster protection or durability.
The player-body collider itself remains native. Non-bullet native
damage and explosive-bullet policies are unchanged. Lost tracking removes VR protection instead of
falling back to the player's old facing-based bullet shield.

## Verification and acceptance

`shield_tests.hpp` runs in `vr-weapon-grip-tests`: exported skeleton admission,
unknown/changed skeleton rejection, hand mirroring, no-feed lifecycle, curved
front/rear/side/window/corner intersections, world transforms, obstruction order,
invalid poses, stale tracking, reference/owner changes and single-use hit stamps.
Existing controller, hand-rig, melee and physical-reload tests cover the reused
paths. Runtime contracts are checked against the saved native code independently
of CPU geometry tests.

`vr_physicalShield` defaults to true. `vr_shield_status` prints and saves
`minidumps/h2-mod-vr-shield.txt`, with installation status, query/block/event
counts, filtered native player parts, stale-pose rejections, `fire_blocks` and
the `arm_blocks` subset.
`vr_melee_status` separately reports native shield hit/blood event submissions.

HMD acceptance: acquire the shield in either hand; inspect the handle and strap;
move it away from chest/head to expose the body; turn the head independently;
rotate/raise the shield; test front and rear fire, window and edge hits, a wall
in front, and a pistol in the other hand. Verify shield strike cooldown and
withdrawal, release/drop/reacquisition, holstering, pause, tracking loss,
recenter, checkpoint and level changes. Check that NPC shields still work.
Event submission counters are not proof that sounds or effects rendered.
