# Catching falling magazines and speedloaders

Hold Trigger with an empty hand and intercept a falling magazine or complete
revolver speedloader. A new press at the instant of contact is not required.
Either hand can catch, and each hand may hold one item. Release Trigger to drop
it again. Grip remains independent. The caught container survives stowing,
switching or dropping its original weapon and renders on the hands-only rig when
no firearm is held.

Scope includes loaded, partial and empty detachable magazines, detached ammo
boxes/drums using the same feed, and complete .44 Magnum speedloaders. Loose
ejected cartridges, cases, shotgun shells and separated revolver rounds remain
cosmetic and cannot be caught. Knife co-grasps are not synthesized for these
independent acquisitions: interception requires an actually free hand.

## Motion and contact

Simulation and presentation share `reload_item::flight`. Ordinary controller-button
ejections follow the authored magazine rail before detaching into gravity.
Accepted spare-magazine strikes release immediately from the attached pose; a
released held item begins free fall from its current hand pose. As with the
previous cosmetic drops, the airborne window lasts at most 1.2 seconds. Native
world tracing ends it on a solid impact; ground pickup/resting item persistence
is outside this change.

Opted-in latch/paddle strikes immediately add a bounded impact vector and rotation
about the magazine body centre. A struck physical release button starts directly
under gravity without impact velocity or spin. Simulation and final
rendering consume the same immutable release motion, including cosmetic overflow;
see [latch release motion](vr-magazine-latch-contact.md#release-motion).

The capture shape uses the actual immutable magazine body or loader body bounds,
with 65 mm hand-contact slack. A bounded relative-motion box sweep catches fast
crossings between input samples. Lost tracking, a released Trigger, occupied
hands and discontinuities cannot supply an old swept approach. The unified hand
arbiter chooses between the hands and other interactions before publishing a
grasp. It cannot duplicate one object into two hands or replace a held weapon.

The selected hand-to-container transform and finger pose stay fixed while held.
SCAR/AK and the existing controller-aligned profiles retain their corrected
orientation policy. P90 retains its selected forward/reverse style. Insertion
uses the existing attached-wrap policy where authored.

## Reloading recovered items

Bring the held item to a compatible weapon carried by the other hand. Magazines
reuse the shared well-contact gate, including alignment, step rejection and the
15 mm exit/reentry margin; a catch inside the well cannot immediately reinsert.
The existing post-latch-strike insertion delay remains in force. Entering a new
weapon's well already overlapping requires withdrawal first.

Compatibility requires reviewed source identity and container geometry, rather
than guessing from capacity or caliber. Registered compatible skins and physical
copies of the same weapon are supported. Unrelated weapons cannot refill from
each other's containers. A successful magazine insertion transfers its exact
payload, consumes the independent item and retains the Trigger-held seated grasp.

A recovered speedloader fills an open, empty, correctly oriented cylinder using
the existing loading contact. It remains held as an empty speedloader afterward.
Its unchanged hand pose/body do not disappear when its payload becomes zero.
Empty loaders cannot manufacture ammunition.

## Ownership and transactions

An independent simulation-owned pool tracks each item's identity, revision,
origin, native reserve-pool identity, remaining rounds, holder and trajectory.
Its objects are not attached to the source weapon's holding lifetime.

Before releasing ammunition from a mechanical feed, reserve an item slot. Native
compare-and-commit must succeed before that item becomes active. A rejected
write cancels the reservation and leaves the source unchanged. Receiving feeds
likewise commit before the independent item is consumed or emptied. Rendering
never transfers ammunition or decides disposal.

`usable = native reserves + live feed contents + ordinary held reload payloads + independent item payloads`

Independent payloads include falling, held and pending-settlement items. Repeated
drop/catch cycles preserve the same payload and do not refund/refill it. An
unclaimed item settles its remaining rounds once on expiry/impact. The saved
`vr_discardAmmoPenalty` toggle defaults off: disabled returns the payload to
reserve; enabled loses every remaining round without requiring a native reserve
owner. Catching the item before settlement preserves its payload for insertion
or waist recovery. When the original weapon is no longer owned,
refund uses another owned weapon only if its validated native reserve identity
matches. Otherwise settlement waits for a valid owner of that pool. Failed
compares and full native ammo tables retain the unsettled payload for retry.

Tracking-reference cleanup and forced hand/feature cleanup are distinguished from
deliberate disposal. Held objects survive temporary tracking/focus interruptions;
obsolete falling trajectories settle safely after recentering. Level/checkpoint
timeline replacement clears obsolete items before any refund into restored ammo.
Mid-reload save persistence remains outside this change.

Release Trigger while holding a magazine or speedloader inside the existing
waist supply volume to recover its exact remaining payload, with either penalty
setting. This applies to ordinary reload payloads and independently caught
containers, including after switching or dropping the original weapon. A failed
native recovery retains the payload for retry; no falling discard is presented
for a waist return. Empty containers return no rounds. Forced cleanup refunds
even with the penalty enabled. Revolver live-round clearing also follows the
penalty; spent cases never consume usable ammunition.

The pool has 32 slots and never evicts unsettled ammunition to admit another
object. At saturation, new native drops settle immediately under the selected
penalty and use cosmetic falling presentation. Capacity limits, malformed payloads and failed commits cannot
duplicate or destroy already-owned items.

## Presentation and diagnostics

Caught objects use the existing independent equipment hand rig and exact native
hand-record binding. Each hand publishes its container pose revision with the
actual skinned wrists, so a weapon switch, recycled slot or later catch cannot
reuse another item's pose. Speedloader payload revision is separate from pose
revision to retain its body through filling.

Bounded submission tickets freeze identity and coarse placement for queued native
jobs. Falling objects refresh their analytic trajectory at native rigid-surface
packing, using one time sample for the whole job and the packed payload shared by
both eyes. Submission time no longer limits visible flight cadence. A published
rail snapshot continues into gravity at its authored deadline even before the
simulation publishes its detached flag; render sampling cannot commit detachment,
collision, catches or ammunition settlement. Held objects still resolve against
the exact hand record.

Cosmetic magazines, loose cartridges, shotgun shells, revolver cases/tips and
discarded launcher rounds share `falling_item_presenter`. Its retained tickets
keep immutable motion and model identity instead of consulting a recycled drop
slot. Cases and tips share the same absolute ballistic/scatter time. Handles are
not recycled within the queued-consumer window; overload omits new submissions.
Existing magazine/loader assets are reused; no mesh extraction, model creation or
ammunition writes run on render workers.

Moving-carrier rail presentation resolves the source weapon through the exact
native skin record and its current model origin, rather than following the
lower-rate server gun mailbox. The visual departure is retained at the authored
rail deadline, interpolating between the bracketing native attachment samples.
A later simulation publication cannot pull that departure backward. A source
weapon switch, foreign skin record or missing attachment cannot substitute a
different gun. Catch/release identity and tracking-reference changes reset the
visual rail owner. Simulation collision, catches and ammunition remain unchanged.

`vr_reloadItems_status` reports active item identities, kinds, phases, hands,
remaining rounds, origins and catch/release/insert/refund counters.

Regression covers empty/partial/full payloads, either hand, competing catches,
occupied-hand rejection, repeated drops, failed reservations/refunds, pool limits,
compatible/incompatible receivers, external insertion, retained empty loaders,
seated Trigger continuity, rail/gravity motion, swept catches, reference changes
and stale IDs. Headset acceptance is still required for reach, hand orientation,
the capture margin and the visible catch/insert transitions.
