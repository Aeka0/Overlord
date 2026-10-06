# Independent instances of the same weapon

Status: HMD testing accepted same-definition dual instances in the headset on
, after the pickup/give and second-model/arm corrections. Duplicate
pickup is admitted for primary bullet weapons with no auxiliary feed or aliased
native clip key. The original `give deserteagle_gold` reproduction and candidate
pickup path are covered by the corrections below.
Persistence of multiple copies through native save files is not implemented.

Follow-up user acceptance confirmed same-definition dual holding, but exposed a
missing second model/arm. The skin commit still compared definition tokens when
deciding which DObj could publish the single native muzzle. Both copies therefore
tried that mailbox; rejection of the nonprojected lifetime aborted its own skin
acceptance and could also clear the primary muzzle. Publication and failure
cleanup now share an exact physical-identity policy. Both objects commit their
own matrices/arms; only the projected copy touches the native muzzle mailbox.
This follow-up has passed client, weapon-grip, physical-reload and spatial-panel
validation and was deployed with the P90 contact-priority correction;
headset testing confirmed dual-instance behavior, including this fix.

The  investigation was initially deferred; implementation resumed on
 with the instance identity boundary below.

## Implemented identity boundary

- `weapon_identity` separates the native definition token from physical lifetime
  generation. `hold::id` carries that identity through input, poses and feedback.
- `carry::inventory::reconcile_instances` accepts separately identified copies
  of one definition. Grip, support, draw, exchange, release and recovery address
  the complete identity. The native definition-only adapter still rejects
  duplicates; `find_definition` and `equip_definition` fail on ambiguity.
- Carry scenes, three mechanical feed mailboxes, spring returns, event cursors,
  shot clocks and heartbeat attachments use explicit instance keys. Native
  DObj allocation compares both token and generation. Held magazine/loader
  lighting handles belong to bounded presentation slots, not definition indices.
- Render-record and HUD capture queries include the instance identity. The same
  model, grip revision or native skin pointer cannot substitute for another gun.
  Trigger, support and queued feedback checks also include the lifetime.
- Instance-qualified ammunition and ballistic APIs retain the identity across
  compare/debit/settlement. A bounded server-owned `clip_ledger` stores each
  physical clip; `weapon_clip_projection` supplies the tested transaction core.
  Reserve pools retain their existing native ammunition-type keys. Native
  ownership still contains one record per definition, without cloned definitions.
- A legacy/physical-carry mode change can rebind a unique feed while interrupting
  its gestures and settling escrow. Two nonzero physical generations cannot
  inherit each other's chamber or escrow. World recovery restores the matching
  carry identity and retains its shot clock while that entity generation is live.

## Native projection and transfer boundary

- `native_carry::observe` publishes explicit instances from the clip ledger.
  Native changes refresh only the projected copy. Per-instance reload commits
  compare the shared reserve before writing either destination; a stale second
  reload cannot consume another gun's ammunition.
- Firing projects the evaluated clip for the native call and restores the previous
  surviving projection afterward. Carry presentation projects the preferred gun.
  Native callbacks cannot cause a removed lifetime to be restored.
- Pickup reserves physical capacity and parks the surviving clip before entering
  native `Touch_Item(..., automatic=0)`. Native manual pickup uses `G_GivePlayerWeapon`,
  whose existing-definition branch returns success without adding a second native
  inventory slot. Only the exact admitted entity/generation bypasses the manual
  equal-definition check; native script restrictions remain enforced.
- Hand candidate discovery also overrides `GetUseList`'s primary-item admission
  call at `0x140526EC7`, scoped to a live local hand query. The original function
  sorts owned/equal-akimbo weapons into the rejected tail and subtracts them from
  its returned count at `0x140526F8E`. A `Touch_Item`-only override cannot expose
  these items to the grip arbiter. Query and transfer now share the same supported
  duplicate and script-flag policy; ordinary native queries remain unchanged.
- Explicit console `give <weapon>` for an already-owned supported firearm runs
  native grant **and** ammo initialization inside the physical clip transaction.
  Native grant alone returns success without inserting another definition record.
  The transaction creates a new lifetime with the initialized clip, restores the
  original clip and retains the native shared-reserve result. The new gun enters
  the normal free holster/overflow placement without seizing an occupied hand.
  Ordinary script `giveweapon` remains definition-based. Capacity rejection is
  reported in the console before native ammo initialization.
- The native path still owns payload transfer and notifications. Entity free
  remains native except for the successful akimbo split boundary below.
  Only confirmed world consumption or pair retention with surviving ownership admits the
  reserved identity. Failure restores the parked clip without rewinding reserve
  changes or script effects. Raw clip writes are unavailable to instance consumers
  while the transfer is in progress.
- Drop projects the exact released clip. When another copy survives, only
  `Drop_Weapon`'s take call is skipped, the world reserve payload is zero, and the
  retired projection is replaced by a surviving clip. The last copy follows the
  original native removal path. Mechanical snapshots retain chamber/open-action
  state across the matching world generation, including the existing +1 restore.
- External native take invalidates all copies of that definition immediately,
  including take/give within one callback. External give respects the physical
  capacity and cannot introduce a clip alias for a duplicated definition.
  A native drop lacking a physical identity is rejected before spawning an item
  if the definition has multiple copies; controller releases provide that identity.
- Generic scripting level-start/shutdown callbacks invalidate the timeline even
  when a player pointer or game time repeats. VR-specific reset policy remains in
  the carry/ammunition adapters. World records and carry identities are rebuilt
  at level boundaries; no publication lock spans a native call.

## Native akimbo world pickups

The  implementation converts a world pair during an explicit VR grip
pickup. The first grab gives one independent single gun and leaves the original
world entity in place as a single gun. The next grab consumes that remaining gun
through the ordinary instance pickup path. Existing same-definition instances
retain their own ammunition and physical state.

- Native pairs use the same definition token, item flag `+0x1a0 & 1`, and two
  loaded payloads at `+0x194` / `+0x198`. The first grab imports clip one; the
  remainder receives clip two. Loose reserve at `+0x190` transfers only once.
  Native `-1` default-ammunition sentinels are preserved separately per clip.
- Capacity and clip-feed compatibility are checked before clearing the pair flag.
  The existing physical pickup transaction parks the previous gun's loaded feed
  and reserves the new identity before native grant/import/notifications run.
- Only `Touch_Item`'s final successful free call at `0x1404C5FE9` may retain the
  entity. The split checks entity generation, definition, player, timeline,
  surviving ownership and unchanged staged payload. Script deletion remains
  authoritative; no replacement entity is spawned or resurrected.
- The first successful grab also replaces the retained entity's authored pair
  model with the native single world model. The model is resolved before the
  pickup mutates anything; at the successful free boundary, native `G_SetModel`,
  `G_DObjUpdate` and weapon hide-parts rebuild its binding just as for a normal
  single-weapon drop. Entity identity, placement and the second clip are retained.
- Rejection restores the original pair flag only while that exact staged item
  still exists unchanged. Automatic touch/scavenging cannot split or consume it.
  Hand-query admission exposes the pair before transfer without changing it.
- This operates on supported primary bullet weapons without an auxiliary or
  aliased clip feed. It does not convert pre-existing native akimbo inventory
  created by scripts or saves, and does not change ordinary flat-game pickup.

Validation: Debug client build, weapon-grip and physical-reload suites pass.
Split regressions cover both clip payloads, existing same-model ownership,
reserve transfer once, default sentinels, restrictions and full capacity.
New native call/branch guards match the archived unmodified executable; startup
also verifies them before installing hooks. Headset pickup validation is pending.

 world-model correction: the RelWithDebInfo and Debug client builds and
physical reload suite (including akimbo split regressions) pass. The model setter,
DObj rebuild and accessor guards match the archived native code. Both client
EXE/PDB pairs were deployed after process-exit preflight. The corrected in-game
model transition remains unverified.

## Instance regression validation

Validation: Debug client build and weapon-grip, controller-input,
physical-reload, cylinder, spatial-panel, hand-rig and hand-pose tests pass.
New regressions cover equal-definition inventory and holster exchanges,
handover/drop/recovery, malformed snapshots, 15-instance capacity, 4,000
adversarial actions, cache exhaustion, same-tick shot clocks, independent native
skin records, stale HUD/FX lifetimes and legacy projection rebinding. Clip tests
also cover different initial loads, same-tick debits, shared-reserve contention,
projection changes, pickup rollback, exact recovery, script regrant, sparse-table
exhaustion and invalid-key rejection. Eight new native contracts match the archived
engine code; initialization rechecks them in the running engine. These are offline
checks, not native duplicate-pickup or headset acceptance.

Follow-up validation for the reported pickup/`give` failure: client,
physical-reload and weapon-grip rebuilds pass. Added tests cover both candidate
and transfer admission, script restrictions, stale/foreign items, automatic
scavenging, native akimbo, repeated give with a partly fired original clip,
held/holstered placement, second-hand draw, failed grant and the sixteenth copy.
Read-only inspection of the running image found the old transfer/give
hooks installed but `0x140526EC7` still calling unmodified native admission,
confirming the omitted candidate boundary. The new call is checked before hook
installation; this witness does not replace in-engine acceptance of the fix.

## Confirmed constraints

- Native akimbo, auxiliary-fire weapons and aliased definition feeds remain
  outside duplicate admission. They retain their existing single-instance path.
- Native ownership has a bounded 15-entry array and searches by weapon token.
- The definition-only `carry::inventory::reconcile` still rejects duplicate
  tokens. Runtime observation now supplies `reconcile_instances` instead.
- Native scavenging can convert both a ground gun's reserve payload and clip
  into player ammunition, then permit the ground entity to be freed. The current
  touch/scavenge protections prevent unrequested firearm consumption in VR.

## Required migration

1. **Identity boundary implemented:** weapon definition identity and stable
   carried instance identity are separate throughout ownership, render
   publication, input arbitration, reload/cylinder/tube feeds, shot cadence,
   FX and HUD lifetimes.
2. **Projection implemented:** per-instance clips with shared native reserves,
   compared writes and scoped native evaluation.
3. **Transfer implemented, native acceptance pending:** duplicate primary-feed
   pickup/drop and matching mechanical recovery use the same physical identities.
4. **Lifecycle audit implemented; persistence pending:** script take/give and
   level resets have explicit boundaries. Native saves still contain only their
   single definition record, so loading reconstructs one copy with a fresh
   lifetime. Additional copies and intermediate mechanical states require a
   versioned save extension before they can survive checkpoint loading.

The next increment is in-engine acceptance of native transfer postconditions and
headset interaction, plus save/checkpoint persistence. Auxiliary feeds need their
own per-instance state before duplicate admission can include them.
`vr_carry_status` now records each physical clip, native key, epoch, projection,
loaded count and shared reserve in `minidumps/overlord-carry.txt`.

## Acceptance cases

Two identical guns with different loaded counts must fire separately and in the
same frame without sharing cadence, reload gestures, ammo HUD or effects. Swap
hands, stow either gun, drop and recover either, and repeat with removed magazines
and open actions. Picking up one ground gun must not drain or delete another.
Exercise ownership capacity, failed pickup, entity reuse, loss of tracking and
checkpoint transitions before admitting the new model generally.
For the reported case, repeat `give deserteagle_gold` while the first gold pistol
is held and again while it is holstered; draw the new copy and compare each clip.
Also acquire a second ground copy through the hand ray while already carrying
one, and verify that the world entity is consumed only after the grip transaction.
