# Receiver bolt-catch slap

For the complete weapon list, magazine-latch strikes, button releases and HK
slap distinctions, see [VR impact-release overview](vr-impact-release.md).

M4, M16, SCAR, KRISS Vector and UMP45 accept an unbuttoned offhand slap on the left-side
bolt-catch paddle after a loaded magazine has been fully inserted and released
from the hand. This includes admitted optic/underbarrel variants. The gun must
still be follower-locked open. Existing release-button and charging-handle
routes continue to work.

UMP45 base/arctic/digital now retain follower hold-open after the last shot.
Its side paddle is available only with the upper charging handle lowered.
Manually latching that handle selects the separate downward HK slap target;
side-paddle motion cannot release it. The two detectors keep independent approach
histories. UMP controller release buttons remain disabled.

## Interaction and ownership

- The offhand must be free: no support grip, knife, magazine or action lease.
  Open-hand slaps need no button press. An established Grip+Trigger fist may
  also strike with its side. Trigger-only pinch and a new Trigger press remain
  excluded; the new press retains ordinary part-acquisition authority.
- The original 21 palm/finger contacts are supplemented by a solid palm envelope
  from the wrist heel to the four knuckle roots, including the thumb pad and both
  hand edges. It follows the admitted glove's root geometry and raw wrist.
  Render IK cannot create an impact, and distal finger curl cannot remove the
  palm's coverage.
- The target stays on the gun's physical left (+Y), regardless of which hand
  holds the rifle. The accepted impact direction is inward (gun-local -Y).
- Current tuning: 2.5 cm target radius, 7 cm separation to rearm, 2.5 cm approach
  travel, 0.5 m/s minimum hand speed, 8 m/s maximum wrist speed and a 60-degree
  inward cone. Slow touching, reverse sweeps, stationary-hand gun motion, stale
  samples and tracking jumps do not release the catch.
- One separated approach permits one transaction attempt. A rejected native
  compare consumes that approach; another slap requires moving away first.

The existing HK swept-impact implementation supplies the shared motion checks.
Receiver paddles combine the original contacts with a fixed 6 x 6 x 4 palm grid
in one detector. Every point inside the bounded envelope lies within 24 mm of
a sample, below the unchanged 25 mm target radius. The palm extends only 8 mm
behind the wrist, with skin allowances around the skeletal roots; it does not
make the forearm a striking surface. HK handle slaps retain their original 21
contacts and input rules.

Unusual world scales or palm bounds outside the fixed sampling budget omit the
optional envelope while preserving valid original finger contacts. They cannot
inflate the contact volume without limit or disable UMP's independent HK catch.

The coordinator's existing manipulation permission gates the operation; no
parallel hand owner or button-history path is introduced. The dedicated
`release_catch` transaction reuses closed-bolt release, feeds exactly one round
and emits existing action-close feedback. Unlike the combined button operation,
it cannot fall through to ejecting a magazine. Empty/absent magazines and a
closed or manually held-open action are rejected by the transaction itself.

## Source geometry

Targets were reviewed on existing exported receiver meshes. Export centimetres
are converted once to native units by dividing by 2.54. No external export path
is compiled into the application.

| Receiver | Source feature | Target in gun-local native units (X, Y, Z) |
| --- | --- | --- |
| M4 | Upper paddle of `j_clip_release` | 3.34130357, 0.78309145, 3.19522234 |
| M16 | Upper paddle of `j_bolt_catch` | 3.52376484, 0.80828852, 3.49133691 |
| SCAR | Upper left receiver paddle, part of `j_gun` mesh | 3.66350196, 0.91177746, 3.63592144 |
| Vector | Centre of the exposed left `j_switch` paddle, shared by base/black skins | 7.42558607, 0.89196201, 1.09648545 |
| UMP45 | Left receiver paddle behind the magazine | 5.12596979, 1.00368496, 3.51649983 |

The per-weapon definitions retain the receiver SHA-256 and contact centre. M4's
native bone name says clip release, but mesh inspection identifies the left
bolt-catch paddle. SCAR has no separate catch bone; its centre comes from the
visible paddle surface, not the charging-handle position.

Vector's target covers the complete exposed side of the paddle. All reviewed
left-surface vertices fall within 2.31 cm of its centre, inside the existing
2.5 cm tolerance. Its release authority, ammunition rules, and palm/fist input
gates remain shared with the other receivers.

## Validation and acceptance

Palm contact coverage includes heel and edge trajectories using the reviewed
marine-sniper hand skeleton. Offline mesh checks cover four glove models, both hands, and open
and closed poses: all sampled wrist/palm-weighted vertices between the heel and
knuckle row fell within the new contact coverage. This measures that selected
palm region, not arbitrary forearm or whole-mesh coverage.

Production contact-sampling regressions exercise all nine registered receiver
profiles with either hand, rotated guns, open palms and established fists. Slow,
reverse, stationary-world-hand, tracking-jump, remote, occupied-hand and pinch
cases stay rejected. A failed write cannot retry through another palm/finger
sample until the complete hand separates. New Trigger presses retain normal
acquisition without releasing the bolt. See
[palm geometry tests](../tests/vr/palm_contact_tests.hpp) and
[receiver palm tests](../tests/vr/receiver_palm_tests.hpp).

Automated checks cover the real empty-magazine replacement sequence followed by
an unbuttoned slap for all three rifles and either holding hand. They check
ammunition conservation, native-compare rejection/retry, duplicate input,
wrong-side/slow/stationary-hand contacts, tracking loss, occupied hands, and
invalid feed states. Geometry tests rotate and translate the gun while keeping
the catch on its physical left. Assembly tests include supported underbarrels,
optics and glove identities; HK slap and existing reload tests remain included.

Headset acceptance remains necessary for reach and feel: empty the rifle, insert
and release a fresh magazine, release the support grasp, then slap the left
paddle inward with the palm heel, thumb pad, knuckle-root region or side of an
already formed fist. Confirm one bolt-close event, a chambered round, and normal
firing. Repeat with both hands on M4/M16/SCAR/Vector/UMP. Empty-magazine touching,
an occupied support hand and moving the gun into a stationary hand must still
be rejected.
