# Independent VR weapon firing

The adapter supports concurrent instances of one weapon definition and restores
the native launcher routing boundary. Headset acceptance remains specific to
the tested weapon, hand, and attachment combinations.


## Current player integration

While independent viewmodels own presentation, `independent_fire_runtime` evaluates
each held control-hand trigger after the carry writer settles releases and transfers.
Per-native-instance clocks retain separate cadence, semi-auto/burst state and
cooldowns across stow/handover. Handover, focus loss and stalls require neutral
trigger input; no overdue shots are replayed. Native command construction and
the simulation/prediction fire boundary suppress stock attack while this route
owns the selected bullet weapon, including a single held gun. Native projectile
weapons retain the selected native attack route described below. The legacy rendering path
retains its existing single-gun route when independent presentation is unavailable.

The native descriptor supplies firing mode, interval, initial delay and burst
cooldown. Each shot validates an exclusive loaded-feed key, fresh instance pose,
the existing physical magazine/cylinder readiness, current player/context and
eye-to-muzzle clearance. After one compared native debit, the existing mechanical
consumption owner settles before native ballistics can invoke damage/scripts.
The second weapon is revalidated after the first; native engine calls remain
sequential on the server owner. No selection swapping or akimbo state is used.

Committed shots route haptics and native `fireSoundPlayer` to their own weapon
and hand. Independent shots now also freeze their actual muzzle and native
`tag_brass` frames for the main-thread feedback consumer. Guarded explicit-weapon
queries resolve native muzzle and normal/last-shot shell effects, including
variants, and the native oriented FX queue owns particle lifetime and shell
physics. These frames retain the weapon's real ejection side when held left.
Revolvers retain their cases and bolt-action weapons do not eject on the shot.
`vr_weapon_fx_status` writes `minidumps/overlord-weapon-fx.txt`; counters measure
spawn requests, not proof of visible particles. HMD testing confirmed visible muzzle
flame and shell ejection before the instance refactor; their FX assets and lifetime
implementation were not changed by this refactor.

Each held weapon now has a stable, tree-less native DObj built from the current
character hands and that weapon's native first-person model/composite. Native
selection remains a compatibility projection; its equip/reload tree is not a
pose source for either owned object. The normal native view-weapon pass still
runs its side effects, while its DObj submission is suppressed inside that pass.
Owned objects use the verified native viewmodel scene identities 3999/3998.

Each object has its own grip solver, secondary motion, epoch cache and resource
generation. Character arms use immutable triangle subsets of the original skin;
the opposite arm is hidden when another weapon owns that hand. No replacement
character asset is introduced. Asset changes retire descriptors only after the
native renderer/fastfile lifetime barrier. Per-instance spring and shot motion
drive mechanical parts without borrowing the selected gun's animation.

Mechanical mailboxes are keyed by the complete physical weapon identity (native
definition token and lifetime generation). The server reconciles each held
instance and refreshes shared reserve counts before its transaction.
Selection changes cannot interrupt the other gun. Own-hand buttons and the
opposite hand's part lease are separate permissions: magazine release, slide
release and cylinder opening work while the opposite hand holds another gun.
Tracking loss/occupation cancels only that hand's part lease; held button edges
are not replayed when tracking or ownership changes.

The  identity migration also isolates carry, shot clocks, native object
allocation, pose records and HUD/FX lifetimes for equal-definition instances.
The clip ledger and native pickup/drop adapter now admit equal-definition primary
bullet weapons without auxiliary feeds or native clip aliases. Firing projects
each clip for its native call while retaining shared reserves. HMD testing accepted
same-definition dual instances. Multiple-copy checkpoint persistence
is still outstanding; see
[`vr-weapon-instances-todo.md`](vr-weapon-instances-todo.md).

Both native skin records can bind to one stereo pair. Magazine/loader placement
selects its own weapon's object, matrix buffer and epoch; queued records cannot
fall back to another gun or an older pair. The legacy HUD uses an explicit
compatibility projection. Owned firing consumes its exact solved instance pose,
not a shared latest pair of wrists. Visible waist storage uses the same native
weapon skeleton and authored control pivot as held presentation, submitted at
ordinary world depth with the complete hand model hidden. Its first pose does
not depend on a previously held scene or a cached view/world muzzle bridge.

Stored skeletons read mechanical snapshots by physical instance identity. A
detachable feed retains its removed magazine and locked/open action; revolvers
and break-action weapons retain their cylinder/breech and chamber state. Before
mechanical admission, copied native loaded counts use the same read-only import
rules. Rendering never creates a mechanical instance, selects a weapon, feeds
ammunition, publishes a hand interaction, or restores pre-checkpoint state.
Back/overflow storage remains hidden.

The native object pool is bounded to two held and two waist weapons. Storage
uses scene IDs 4000..4003 within H2's existing 4032-entry scene tables; the native
configuration instruction is validated before installing the adapter. These
are render IDs, not server entity or client DObj handles. Empty hands retain
their existing 3999 entry. Resource generations change on instance and storage
location changes, and native asset unload remains the object retirement barrier.

`vr_hands_status` includes owned-object generations, epochs, build/submission
counts and independent solver profiles. `vr_reload_interaction_status` reports
per-instance input sequence, hand, part-access permission and gesture decision.
Native create/free and current-character arm partition were witnessed without
firing: Desert Eagle assembled with 79 bones, gun attached at `tag_weapon`;
both arm subsets had zero mixed-side vertices or triangles. These checks prove
resource contracts, not headset appearance or pickup latency. Regression tests
cover simultaneous rear buttons, opposite tracking loss, neutral rearming,
per-object skin binding, record reuse and multi-bone skin subsets.

`vr_dual_fire_status` writes `minidumps/overlord-dual-fire.txt`: instance IDs,
attempts, emitted shots, ammunition, rejection reasons and same-tick pairs.
Clock regressions cover L86/MG4 75/100 ms cadence, duplicates, neutral rearming,
semi-auto, burst completion and cooldown. The current native ABI and live L86,
MG4, pistol timing/sound fields were read without firing during investigation.
In-game player-trigger witness on : left M21 silenced (token 25) and
right M9 (token 49), eight/twelve emitted shots respectively and three same-tick
pairs. Both most recent shots used commandTime 18212 and each debited one round
(3 to 2, 4 to 3); selected-route applied/rejected counters stayed zero. Native
carry invariants and the current hand solve passed. This verifies controller
routing and native calls/debits; it does not prove character damage, headset
pose quality or feedback audibility. Projectile/alternate modules, native firing notifications,
statistics remain outside the proven primary-bullet adapter. The new articulated
instance presentation has passed headset acceptance within its tested scope.
The sections below retain the
original feasibility evidence and the wider completion gates.

The interaction requires two different held weapons to fire independently, including
in the same simulation tick, without using native akimbo. This document refines
the [weapon architecture](vr-weapon-interaction-architecture.md#46-hand-roles-and-wield-topology)
and consumes the [parent authority and carry contracts](vr-gameplay-interaction-architecture.md).
It records the design and the original bounded in-game proof; the integration
status above supersedes the original proof-only status.

## Selected native launchers

Owned presentation previously suppressed native attack for every held weapon,
but the independent adapter only emits type-1 bullets. This blocked unprofiled
launchers before their normal fire/ADS behavior. The trigger-ready and native
event wrappers also rejected every non-bullet weapon.

Verified type-3 projectiles now retain native selected-weapon input, prediction,
cadence, debit and event handling. The independent clock skips them; it can still
service a bullet weapon in the other hand. This does not implement an independent
nonselected launcher or permit duplicate launchers. Thrown ordnance and unknown
delivery types are not admitted to this controller-fire contract.

The shared `G_FireWeapon` parameter builder at `0x140518B91` precedes both the
class-6 grenade branch (`0x140518CB6`) and missile branch (`0x140518D96`). Its
four geometry vectors receive the exact current controller muzzle; projectile
creation, attacker, trajectory, fuse, native spread and acquired target stay in
the original engine. Bullet aim assistance does not modify projectile launches.
Eye-to-muzzle obstruction and current physical ownership/tracking remain required.

RPG/AT4 retain the native aim-during-fire-delay behavior through the descriptor
accessor `0x1406A15D0` and ADS gate `0x140694444`. The PM fire lock requirement at
`0x1406993FD` is unchanged, so Stinger/Javelin cannot fire without a native lock.
No ADS fraction, lock flags or weapon definitions are overwritten. Runtime
signatures check these branches before admitting launcher input; `vr_fire_status`
reports projectile readiness and delivery (0 unsupported, 1 bullets, 2 projectile).

Read-only live descriptors confirmed M79, Stinger, Javelin, AT4 and RPG as type 3.
Native signatures matched both the archive and running engine. Client and input,
grip and reload tests passed. Launcher firing/locking and muzzle direction still
require headset acceptance of the deployed candidate.

## 1. Authority and routing decision

VR owns each weapon instance/module's trigger gate, readiness, cadence, burst
progress and mechanical transitions. A verified engine adapter owns native ammo
mutation and invokes native ballistics, projectile creation and damage with an
explicit weapon binding and the real player as attacker. Native inventory,
mission state and damage remain authoritative. Mechanical planners remain shared
with single-weapon handling; do not create a second reload or ammo subsystem.

```text
canonical left input  -> current left grip  -> instance A/module shot simulation
canonical right input -> current right grip -> instance B/module shot simulation
                                                |
                             bounded requests due at this simulation time
                                                |
                              server-owned validated shot adapter
                                 /              |             \
                         native ammo     native ballistics   committed feedback
```

Same and different weapon profiles use this one architecture. Native dual-wield
attachments, akimbo side state/timers, two copies of the native selected-weapon
state machine, and rapid selection changes are prohibited implementation paths.
Do not temporarily rewrite the live player/entity's selected weapon or substitute
a fake player/NPC to make a native caller appear compatible. A native lower-level
ballistic routine may be reused only after its complete dependency contract is
verified. Unsupported contracts remain explicit gaps, not an alternating-fire
approximation.

## 2. Evidence from the current checkout

The existing [native firing documentation](vr-controller-interaction.md#holding-authority-and-firing)
and source expose these boundaries:

| Source | Current behavior | Implication |
|:-------|:-----------------|:------------|
| `gameplay/weapon_interaction.cpp` | One `holding_state`, one muzzle, and equality with the native selected/entity weapon | Current controller firing is single-weapon |
| Same file, `fire_stub` | Local event enters `0x140518A40`; geometry is injected after parameter construction at `0x140518880` | This high-level route still inherits native current-weapon state |
| `gameplay/native_ballistics.hpp`, shared with the selected route | Geometry plus weapon/mode/alternate/definition fields; reserved 12 bytes stay zero in the audited primary bullet route | One packet layout; an independent packet was exercised in game |
| Same file, `bullets_stub` | `0x1404AA300` receives entity, spread, parameters, attacker, event parameter and range override | Candidate reusable bullet execution boundary; argument availability is not proof of selection independence |
| `gameplay/reload_boundary_component.cpp` | Native fire/consume/reload hooks observe simulation and prediction separately | Reuse boundary ownership and diagnostics; current consumption route still serves selected native shots |
| `gameplay/native_ammunition.cpp` | `observe_carried` / `commit_carried` access native ammo independently of physical reload admission; the existing reload API keeps its feed restrictions | Shotguns need not pretend to have a detachable-magazine reload profile; duplicate/shared loaded feeds still need a separate instance mapping |
| `gameplay/hands/component.cpp` | A secondary native viewmodel is explicitly rejected by the current single-held solver | Two independently animated weapon instances need a presentation increment too |

Addresses identify the currently guarded executable, not portable API contracts.
The audit found that `0x140518A40` clears the complete 0x50-byte packet and
`0x140518880` writes the four geometry vectors, leaving bytes 0x30..0x3b zero.
`0x1404AA300` forwards to `0x1404AAD60`, which reads the explicit weapon,
alternate and mode fields for pellet count, range and penetration. Its event
argument is `playerState.commandTime` at +0x4c, used for native time/seed handling.
It is not the value at playerState +0 and must not be replaced with a fabricated
per-hand timestamp.

### 2.1 In-game results,

Native image: SHA-256
`9d554376fbe78248a423f6b776634453142fafa1218645cd64b2499967512aae`.
The exact authoritative image hash is recorded by the loader's target-identity
report; the guarded native entry bytes remain the admission check in source.

| Test | Observed result |
|:-----|:----------------|
| AA-12 selected; owned Barrett fires | Barrett loaded 10 → 9, reserve 100 unchanged; 5 native trace records carry token 27; selected AA-12 ammunition unchanged |
| Model 1887 + Intervention in one server callback | `commandTime=53248`; both loaded 5 → 4; reserves 70 / 100 unchanged; recorded shotgun traces carry token 40, sniper traces token 26 |
| Selection and inventory check | Client selected token, entity token, playerState token, 15 native inventory entries and metadata, weapon state and timer compare equal before/after |
| Unowned weapon request | Rejected before a shot or ammunition write |

The pair used two distinct downward origins. The diagnostic stores at most 32
trace samples per shot (the shotgun reached this bound); this is not a count of
every shotgun collision. Native range/penetration and pellet generation were
retained. Character damage, fire notifications/statistics, native shot audio,
muzzle effects, recoil, HMD presentation, cadence and independent hand controls
were not verified by this ground-impact experiment.

### 2.2 Reproduction and current API boundary

In a Debug build, while holding two loaded native inventory weapons in an idle
playable scene, execute `vr_fire_owned_probe <owned_name> [second_owned_name]`.
This explicitly fires one real shot from each requested weapon toward the ground.
The bounded report is written to `minidumps/overlord-independent-fire.txt` off the
simulation thread. The command is absent in Release builds and never fires on
startup. It does not grant weapons or modify selection to make a test pass.

`native_ballistics::fire_owned` requires the current server scheduler scope,
matching command time, a primary base bullet weapon with an unambiguous owned
ammo record, a valid orthonormal muzzle frame, a clear eye-to-muzzle sweep and a
successful compared ammo debit. It then invokes one native bullet operation with
the real player as attacker. The probe uses native ADS baseline dispersion.
No native shot event or second debit is generated. Reentry is rejected; an
entered native call is never automatically retried or refunded.

This is the execution boundary, not the completed shot scheduler. Before wiring
it to player triggers, add the per-instance admission, exclusive loaded-feed
mapping, mechanical settlement, spread policy, input suppression and feedback
contracts below. In particular, `emitted` means the native call completed;
it does not certify that a character was damaged or a script accepted the shot.

## 3. Per-instance shot simulation

Each module keeps a next-shot simulation time, trigger mode, burst/cycle state
and monotonically increasing shot sequence. Ownership generation plus instance,
module and sequence form a shot identity; physical hand identity is metadata,
not the identity of the gun or its cooldown. Handover/stow/draw must not reset a
cooldown, complete a reload, refill ammo or transfer an unfinished trigger command
to another weapon. A grip change cancels stale requests and requires a fresh
trigger after neutral input; already committed shots remain committed.

A due request freezes the instance/module binding, ownership/grip revision,
input frame, context/reference generation, simulation time, muzzle geometry and
weapon-specific spread/aim policy. Randomness must be reproducible for a shot
without using one shared advancing hand-dependent seed. Trace every native RNG
dependency before deciding how the adapter supplies or preserves its seed.

The owning simulation evaluates grip transfers/releases first, then admits
requests against the resulting bindings. Both eligible guns may fire in one
tick; settlement uses stable order on the engine owner thread. They are two
independent transactions, not an all-or-nothing pair. Empty ammo, reload or an
obstructed muzzle on A must not block B. A script/context/ownership change caused
by A can invalidate B, which must be revalidated before its side effects.

No worker threads call non-thread-safe engine shooting functions concurrently.
Independent pure pose/geometry work may use immutable snapshots. Requests and
shot history are bounded; simulation stalls, focus loss and ownership changes
must not produce a catch-up burst. Normal cadence is measured in simulation
time and supports the validated frame-rate range, not one shot per render frame.

## 4. Native ballistic and ammo adapter

The first investigation follows the existing bullet boundary down its callees.
Determine whether weapon definition, mode, perks, range, damage flags, pellet
count, penetration, tracer data, RNG, attacker and script notifications come
from the explicit parameters or from the player's current weapon/state. Recover
every packet field and event argument used along the path. Do not assume the
high-level parameter builder can build weapon B while the player has A selected.

Prefer verified descriptor-based engine operations. If the candidate bullet
routine still depends on the selected weapon, move the boundary lower or add a
scoped explicit-parameter adaptation to the dependent operations. Do not patch
global assets/player state or recreate the entire damage system as a shortcut.
Projectile launchers need their own verified creation/ownership/fuse contract;
a hitscan proof does not admit them automatically.

For each shot, the adapter must:

1. Validate ownership, grip/context generations, mechanics, muzzle obstruction,
   descriptor support, and the complete execution contract before side effects.
2. Resolve current native loaded/reserve bindings and compare against the planned
   transition. Two instances require distinct loaded feeds; aliasing must be
   rejected or explicitly partitioned through a proven mapping. Reserve pools
   remain shared where native ammo keys say so.
3. Settle the shared mechanical plan and native debit exactly once on the owning
   server boundary. Assign one consumption owner: either a verified native debit
   or the compared writer, never both. Do not spend again from presentation or
   replayed prediction.
4. Execute the explicit-weapon ballistic request once under that shot identity,
   retaining the real player as attacker. Publish resulting instance feedback
   and accepted state once.

The relative ordering of debit, native execution and reentrant callbacks must be
proven before this is advertised as an atomic commit. The candidate bullet API
returns `void`; a function call alone does not establish successful execution.
Preflight/compare failure emits no bullet. Once effects may have occurred, an
uncertain result must fault/reconcile the instance, not retry damage or refund
ammo blindly. A bounded in-flight identity prevents reentrant duplicate shots;
no gameplay mutex may span native execution or scripts.

The new route must suppress duplicate stock local attack/consume/fire events
for admitted independent instances at verified boundaries, including mouse
input and command prediction. Merely ceasing to OR controller `BUTTON_ATTACK`
is insufficient. Unsupported contexts, NPCs and unrelated script shots retain
their own routes. Fire/switch notifications, statistics and scripts querying a
single current weapon need an explicit compatibility contract; fake selection
changes cannot supply it. Definition, source hand and instance must also reach
audio, recoil, muzzle effects, tracers and HUD without borrowing the selected
weapon's identity.

## 5. Carry and handover integration

Pistol/control-only profiles give the current control hand exclusive pose
authority. If that hand deliberately releases while the other valid support
grip remains held, promote the remaining hand to control with its authored grip.
Preserve the gun and shot clock, invalidate the old muzzle/request revisions and
rearm the new trigger. Before transfer the support hand does not steer the gun.
Long-gun handguard carry remains non-firing until control is acquired. Both hands
releasing in one input frame is one final-release transaction. These rules are
specified by the parent arbiter and weapon profiles, not duplicated in shooting.

Both physical hands need independent pose/part/feedback records when holding two
guns. Reuse existing profile discovery, mechanics, scene submission and hand
solving; extend their instance keys and remove selected-weapon assumptions at
their boundaries. Drawing two rigid copies alone does not establish independent
firing, reload or second-hand grip support.

## 6. Bounded proof and acceptance gates

1. Read-only dependency audit: trace the explicit-parameter bullet call, debit,
   event dispatch and parameter builder. Record executable identity, consumed
   fields, selected-weapon reads, side effects and exact owning thread.
2. One-instance replacement: make the new adapter reproduce a supported pistol's
   normal shot, final shot and empty rejection, proving no duplicate stock shot,
   debit, recoil, feedback or script event. Existing single-weapon behavior is
   the comparison baseline, not a second simultaneously active writer.
3. Non-selected instance: keep A natively selected while an owned B executes one
   admitted shot with B's descriptor/muzzle/ammo. Record selection before/after,
   native debit, trace/damage attribution and event identity. The existing guarded
   single-weapon path cannot be used as the implementation of this experiment.
4. Different weapons in one tick: use two supported profiles with different
   rates and directions. Verify independent holds/releases, cadence, final-round
   behavior, one-sided obstruction/reload, and both results at the same simulated
   time. Neither selected state nor native akimbo changes to accomplish it.
5. Failure and lifecycle: duplicate/stale requests, ownership removal during a
   callback, shared reserve operations, same-frame handover/releases, queue
   overflow, frame gaps, stow/draw cooldowns and drop/pickup mechanical identity.
6. Presentation and HMD: authored left/right control poses, pistol handover,
   two independent part animations, muzzle effects/audio/haptics, visible impact
   origin and full carry/reload flow. Later admission separately covers weapons
   with attachments, projectiles, same-definition copies and checkpoint restore.

Pure simulation tests can verify ordering and invariants; they do not prove a
native packet, a damage result, script compatibility or headset ergonomics.
The execution portion of step 3 passed, along with the same-tick native execution
portion of step 4. Damage attribution to characters, scripted compatibility and
the rest of steps 2–6 remain open. The next integration must supply real per-hand
instances and mechanical/trigger admission, not treat the diagnostic pair as a
completed dual-wield implementation.
