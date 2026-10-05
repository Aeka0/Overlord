# Physical climbing in Cliffhanger

Cliffhanger physical climbing uses real ice-surface contacts to anchor the two
tracked picks. It is limited to the two native climbing phases. The native
script remains responsible for mission progression, checkpoints, jump and fall
outcomes, and the final story handoff.

## Contact and movement

- A pick tip sweeps the scene and may establish a support point only on an
  admitted surface within the active phase region. Surface eligibility follows
  the native material policy (`ice`, `plaster`, `rock`, or `snow`); visible ice
  is not necessarily tagged as ice.
- A new support requires Trigger or Grip to be held on the corresponding hand.
  Either input keeps the pick's position and orientation fixed. Releasing both
  permits withdrawal; the pick releases after about 2.5 cm of actual retreat.
  Tracking loss alone does not count as withdrawal.
- The initial scripted pick swing remains native. The adapter accepts its real
  `stab` callback and trace result as the first support. It does not resample a
  ray from an idle pose or treat a crack effect coordinate as the pick tip. If
  no valid native hit is available, the free hand system waits for a real
  contact instead of inventing a suspended support.
- Player movement is solved from tracked motion relative to the anchored picks.
  Rate limiting retains unfinished motion so the body continues to follow after
  the hands stop. A confirmed native collision clears only the blocked motion;
  it cannot accumulate into a later launch. Two supports share one body-motion
  result rather than doubling displacement.
- Hand, pick tip, and body clearance are checked separately. The body uses the
  native capsule query with a hanging shape that preserves the original top
  clearance. Remaining motion is projected along contacts and swept again, up
  to four contacts. The solver preserves valid upward motion and does not
  teleport through an overhang.

Fixed crack-FX coordinates and authored pick-point indexes are not physical
contacts or progression counters. The adapter does not draw contact markers.

## Presentation and ownership

During free climbing, ordinary VR arms and the independent left/right pick props
own hand presentation. The native story torso is hidden, and the player is
linked to an unanimated carrier. Hand solving does not use the hidden body's
shoulder positions or animation. Head translation uses the ordinary VR scale,
and the same carrier owns climbing locomotion. Native weapon disable remains in
force; showing VR hands does not restore firearm permission.

The native body regains presentation for scripted entry, falls, jumps, and
exit. At handoff, the hidden story object is aligned to the free carrier's actual
position, the native link and animation resume, and the temporary carrier is
removed. Native arms remain visible during scripted jumps so tracked arms cannot
override story animation.

The climbing adapter reuses the separate mission props described in
[Cliffhanger story props](vr-cliffhanger.md). It does not occupy ordinary weapon
slots, allow the picks to be dropped, or change C4 authorization.

## Checkpoint and mission transitions

World support points, heading, story object identity, entry/exit regions, and
phase progress are stored in native script variables so the game checkpoint
system owns their persistence. On restore, the adapter revalidates the entity
and link, restores valid support, and resets tracking and withdrawal baselines.
Old controller positions, pending movement, and partial withdrawal distances
are not restored. If resources or tracking are not ready, the bridge waits
without treating the delay as a fall.

First-climb entry, partner continuation, and step progress map to their native
flags and notifications. Progress uses carrier height and valid support, not
the number of repeated pick strikes. Native safe-point triggers remain active.
An exit transfers control only when the carrier reaches the authored exit region
with valid support. Native `spawn_soap`, jump, fall-height, `can_save`, and
`reached_top` behavior remains authoritative.

Direct-climb and jump checkpoints, checkpoint restore, early and late falls,
the first climb, the gap jump, and the final ascent are distinct transitions.
The adapter must not infer them from a sequence of local booleans accumulated
since the opening scene.

## Diagnostics and acceptance

`vr_cliffhanger_physical_status` reports story-object and carrier identity,
support state, phase, movement, collision blocking/sliding, restore state,
contact and withdrawal counts, and exit position. These values help diagnose a
transition; they do not establish mission acceptance.

Offline policy tests should cover retained movement under rate limits, collision
clearing only blocked motion, shared two-hand displacement, withdrawal distance,
recenter, duplicate contacts, tracking jumps, held-input support, and surface
clearance. Bridge checks and client builds do not replace headset acceptance.
Verify both climb phases, story handoffs, all fall branches, checkpoint entry,
pause, focus loss, recenter, one-hand tracking loss, and world collision in the
headset. Confirm native mission events occur once and no stale carrier or
support survives a restart.
