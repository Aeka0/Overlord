# Gulag weapon binding and helicopter retention

## Captured M4 assembly

The  read-only capture found `m4m203_reflex_arctic` (30-round
rifle) paired with `m203_m4_reflex_arctic` (one-round launcher). Its first-person
assembly is `viewhands_udt`, `h2_viewmodel_m4_base_arctic`,
`attach_h2_laser_peq6_vm`, `attach_h2_m203_vm`, and
`attach_h2_red_dot_sight_vm_arctic`.

The unregistered arctic receiver prevented physical assembly selection. Its
24-bone bind poses are identical to the previously captured base M4. The skinned
surface's blend and index bytes also match: 3,910 magazine-only triangles of
5,900, with no crossing triangles. The M203 and optical attachment topology
passes the existing shared contracts.

The arctic grenadier now registers the existing rear/support poses and mechanics
with a separate immutable reload recipe. That recipe prepares magazine hiding
on the exact arctic receiver; it does not depend on the base receiver being
loaded. The independent magazine remains the existing native
`h2_weapon_m4_clip`. Native inventory identity, 30-round base capacity, attachment
parents and the separate launcher feed remain checked. Arbitrary receiver names
and unreviewed arctic support combinations are not admitted.

## Helicopter release policy

The loaded `maps/gulag_code::_id_B123` creates the opening helicopter's legacy
view controller in level field `0xCC0B`. That is not the final remastered parent:
`_id_CCE4` creates `player_rappel` in `0xC438`, links it to the helicopter's
`tag_guy2`, and links the player to that rig's `tag_player`. `_id_D313` unlinks
and deletes this animated rig during dismount.

The follow-up read-only capture found player parent object 26799/entity 1460,
matching `0xC438`. The legacy controller was object 27230/entity 499. This
explains the first implementation's failed headset retention: it only compared
against the superseded legacy controller. Both exact object identities now
qualify; the generic `player_rappel` animation name does not grant admission.

The Gulag sequence adapter compares the live linked parent with those exact
controllers. While either matches and the player is alive, it publishes the shared
`retain_weapon` policy used by Estate's playable dragging scene. The condition
does not depend on the selected weapon or a previously observed event edge.
It therefore survives switching, temporary empty-hand selection and checkpoint
entry. Native unlink, death or a different controller ends retention. Later
cellblock rappels and the final rescue do not match this controller.

Releasing the final holding hand in empty space consumes the release and keeps
the weapon; it queues no delayed drop. Support release, hand transfers, explicit
stowing, drawing and occupied-slot exchanges follow the shared carry rules.
This adapter does not suspend weapons. The helicopter camera aligns once to
the opening native camera heading, then adds its horizontal rotation delta to
the free viewing reference. The legacy controller supplies `TAG_aim`; the remastered rig
supplies `tag_player`. These are sampled from the current client DObj at the
camera boundary, before HMD composition, rather than from clamped player angles
or a server-rate angle snapshot.

Headset yaw stays independent and unrestricted. Native pitch and bank do not
replace physical pitch/roll. A rig change establishes a new delta reference
without forcing that rig's initial heading, recenter retains the accumulated
world look, and unlink uses the existing command-heading handback. Missing tag
data holds the free reference and is reported by `vr_sequence_status`; it does
not fall back to the constrained native view or replay a large delta on recovery.
Original travel and linked physical-head comfort scaling remain unchanged.

This corrects the previous one-time seeded policy, which also discarded later
scripted helicopter turns. It does not add new relative head limits or change
the Gulag knockdown, rock-removal or evacuation camera policies.

The subsequent Price knockdown and falling-rock assistance now use the same
rotation policy through their own reviewed parent relations. Common sliding is
handled across maps by the native `slidemodel` relation. See
[scripted free-look boundaries](vr-scripted-free-look.md).

## Evacuation rope admission

`maps/gulag_ending::_id_C18E` creates and marks `ending_rope1` usable before
the authored flare and rope sequence ends. `evac_begins` is set while the player
is linked to that sequence. Only after the player rig's `single anim / end`
does the script unlink the player and start `GULAG_SPIE_HINT` on the trigger
whose `script_flag` is `player_uses_rig`.

VR candidate selection and retained-use validation now gate that trigger and
the `ending_rope1`/`ending_rope` visual objects on the same sequence boundary:
alive, evacuation begun, player unlinked, and attachment not yet used. Early
arrival and the linked flare sequence cannot expose a VR target. The normal
native hint/use path retains authority once that boundary opens. Other rappel
triggers are unaffected. The unused-looking `rope_drops_now` initialization is
not treated as proof of a live readiness flag.

The subsequent live pre-evacuation capture (native time 754899) found two nearby
use triggers: entity 1766 with `player_uses_rig`, and entity 1978 with
`player_ropes`. The actor was unlinked and `evac_begins`, `time_to_evac` and
`player_uses_rig` were zero. The first gate already rejected 1766, but did not
cover 1978. That adjacent legacy trigger is now excluded entirely from VR use:
its old `maps/gulag_code::_id_BE66` extraction uses `hookup_rope_ent`, which the
remastered ending initialization deletes. The actual remastered route uses
`player_uses_rig`; the old trigger must not become a duplicate when that route
opens. Raw targets and state are retained in the ignored camera/rope evidence.

The evacuation use window requires `evac_begins` while the player is unlinked
and `player_uses_rig` is still zero. `rope_drops_now` does not own this
readiness boundary. The native weapon may remain zero with weapon-disable bit
`0x80` set; the sequence adapter admits only its reviewed rope targets.

That explicit sequence permission now keeps a world-use-only batch alive in
the shared hand coordinator. Mechanical suspension and escrow settlement still
run; no firearm, pickup, holster or equipment acquisition is offered. The usual
Grip history, arbiter, native use lease and command notification path are reused.
Only the reviewed rope targets can pass while guns are disabled. A held Grip at
the readiness transition cannot become a buffered attachment. This separates
world-use permission from native firearm permission without clearing script flags.

The ready state requests tracked empty hands while keeping native firearm
restriction intact. Linking, death, attachment, or loss of readiness removes
that hand-presentation permission.

The final native use pass has a distinct caller lifetime: `G_PlayerUse` executes
inside the original server frame, before MOD server scheduler callbacks. Do not
call the scheduler-only VM eligibility function from that native pass; the
shared target classification is published beforehand.

Server queries now publish only the target's classification, fenced by entity
generation and native timeline. The native use pass consumes that value without
entering the VM and rechecks current sequence permission, life and native link
state. Unknown/recycled targets and the legacy `player_ropes` route stay rejected;
ordinary targets still require weapon permission. The early-interaction gate is
unchanged. Verify evacuation and ground-weapon pickup separately in the headset.
Rope and intro camera ownership are documented in
[scripted free look](vr-scripted-free-look.md).
See [nightvision gestures](vr-nightvision.md).

## Validation boundary

CPU regressions cover the captured M4 hand/mechanical/module binding, exact
visibility source, native feed/capacity rejection, phase exit predicates, and
bilateral retained release with stow/draw/exchange/transfer. The weapon-grip,
controller-input, physical-reload and underbarrel suites pass. Debug and
RelWithDebInfo clients build successfully; the finished optimized client/PDB
contains the Gulag adapter and passes the existing feature-entry audit.

The live capture establishes the original assembly and loaded script lifecycle.
It is not an HMD acceptance run of the fix. Verify the M4's tracking and reload,
relaxed Grip during helicopter sniping, slot exchanges, and ordinary dropping
after landing in the new client.
