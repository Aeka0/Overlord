# Reload penalty and ammunition disposition

Magazine refunds and cylinder disposal use one default-off ammunition
disposition policy and native transaction boundary. The optional reload penalty
is not enabled; existing refunds remain penalty-disabled. Complete airborne
containers defer their refund while they can be [caught again](vr-falling-reload-items.md).

## Policy boundary

Separate weapon mechanics, owned ammunition and cosmetic dropped objects.
Mechanics decides that an item was deliberately discarded or cylinder contents
were cleared. A common disposition policy decides the fate of the live rounds.
Rendering only consumes the resulting effect; hiding, culling, ending an
animation or reclaiming a render slot never spends or refunds ammunition.

| Committed event | Penalty disabled | Penalty enabled |
| --- | --- | --- |
| Unclaimed complete magazine expires or hits the world | Remaining live rounds to reserve | Remaining live rounds lost |
| Unclaimed unused/partly loaded speedloader reaches disposal | Held live rounds to reserve | Held live rounds lost |
| Clear live rounds from revolver cylinder | Live rounds to reserve | Live rounds lost |
| Clear spent cases / release empty loader or magazine | No ammunition change | No ammunition change |
| Transfer magazine/loader into compatible feed | Move existing payload into weapon | Same; not a discard |

Cylinder live-round disposal uses the same common rule as the other deliberate
reload discards; spent cases never become reserve ammunition. Default-disabled
is the current policy at every runtime call site, preserving existing refunds.

Physical pulls and [grasped button-release catches](vr-button-magazine-catch.md)
detach the actual old magazine into a hand. Detachment is not a discard: its
payload stays under hand ownership until release or reinsertion. Released complete
magazines and speedloaders can now transfer into independent airborne ownership.
They keep their payload until caught, inserted or settled after impact/expiry.
Only successful native transfers activate these objects; old cosmetic events have
no recovery authority. Loose ammunition and ground recovery are not added here.

The accepted slide-racking rule (full extraction loses a live chamber round)
and ordinary shot consumption remain their own mechanics. This reload-discard
option must not silently make those operations refundable.

## Transaction contract and remaining policy work

Represent an owned reload item independently of whether it has live rounds:
item kind, ammo identity, weapon-instance provenance, item/transfer ID, revision,
holder and payload count. An empty used speedloader can remain held with payload
zero. Mesh lifetime and the existence of a hand-held object are not inferred from
`rounds > 0`.

Each candidate transaction carries:

- expected native loaded/reserve counts and the owning instance revision;
- the source payload and its single-use disposition/transfer identity;
- explicit reason (intentional discard, transfer, forced cancellation, shot,
  mechanical extraction, native grant or checkpoint reset);
- policy decision and policy generation captured for that commit attempt;
- resulting native projection, held/feed payloads, discarded-live delta and
  immutable presentation/feedback event.

Plan and validate first; publish only after native compare-and-commit succeeds.
Rejected commits leave all payloads/counts intact and emit no successful drop or
fill. Retried/predicted input, both eyes and replayed cosmetic events cannot settle
the same item twice. Reuse the current verified ammo-owner/thread boundary;
do not move game writes into the pose/render or asset-lookup paths.

Accounting for the current no-ground-recovery scope:

`usable = reserve + live weapon contents + unconsumed held payloads + independent falling/held/unsettled payloads`

`usable_after = usable_before + grants - accepted_shots - mechanical_extractions - discard_loss`

Refunds and transfers preserve usable total; spent cases do not contribute.
Use bounded counts and wide intermediate arithmetic. Keep discarded-round
accounting separate from firing statistics/shot effects, even if both remove
usable ammunition. Cosmetic dropped rounds must not acquire a second refundable
payload after the original payload has already been credited to reserve.

Implemented now: `ammunition_transfer.hpp` owns the shared native projection,
validated-transaction forwarding, positive-grant budget calculation and explicit
deliberate-discard/forced-cleanup policy primitive. `native_ammunition.hpp` owns
the fresh owned-slot compare-and-commit; detachable and cylinder state machines
retain their own revisions and operation semantics. Cylinder cleanup explicitly
uses the forced-cleanup reason. Existing pistol refunds preserve their default
semantics; enabling a toggle still requires auditing every cancellation reason
and propagating a captured policy decision through both transaction families.

Proposed toggle timing: read the policy when the irreversible discard commits,
not when the item was taken from the waist or when its mesh disappears. Switching
the setting afterward is not retroactive. This also gives one consistent answer
when a player holds a loader while changing the option. No user-facing command
name is promised until implementation.

## Forced cancellation and level boundaries

Recommended safeguard, distinct from the confirmed intentional-discard policy:
tracking/focus loss, pause, missing render assets and teardown are not deliberate
player discards and should not impose the penalty. Return any unsettled held
payload once to its valid owning inventory, while preserving cylinder/weapon
contents. Mere animation cancellation cannot undo a successful transfer.

For weapon switches, settle against the old instance, never a new weapon's ammo
pool. On death/checkpoint/level replacement, discard obsolete bookkeeping rather
than refunding pre-reset escrow into newly restored native counts. Native grants
must use the current reconciliation contract and must not fabricate an inserted
magazine, close an open cylinder or clear spent cases.

Save/load persistence and persistent grounded magazines remain separate designs.
Do not claim those are handled merely because a in-level discard has a policy.

## Integration order

1. Extract the existing detachable-feed refunds into an explicit shared
   disposition/transaction boundary with penalty disabled and regression tests.
2. Separate forced cleanup reasons from explicit release/drop input; preserve
   existing shot/manual-extraction behavior and instance safety.
3. Add independent cylinder feed/loader state using the same boundary. Do not
   add dummy slide or +1 fields to make revolvers pass detachable validation.
4. Add the optional policy and on/off conservation/replay tests for both families.
5. HMD-test intentional discards, partial loaders, contact rejection, policy
   changes while holding an item and loss of tracking. No asset tooling ships.

Steps 1 and 3 are present in the current cylinder candidate. Cleanup distinction
exists in the shared policy/cylinder path; the full cross-family policy audit,
runtime option, policy-generation handling and HMD on/off verification remain
future work. Pure policy tests exercise penalty choices and forced-refund
behavior without enabling a gameplay toggle or changing accepted pistol refunds.
