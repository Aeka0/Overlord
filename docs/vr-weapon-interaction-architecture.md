# VR weapon interaction and mechanical state architecture

For the current code entry points and the steps to add a reviewed assembly,
see [Weapon registration](vr-weapon-registration.md).

This document defines the shared architecture for native VR arms, hands, and
weapon manipulation in H2-Mod VR. It separates gameplay authority, mechanical state, presentation, and native engine adapters so implementation does not inherit the timing assumptions of first-person animation.

The parent [VR gameplay and interaction architecture](./vr-gameplay-interaction-architecture.md)
owns canonical input frames, spatial frames, body/hand solving, interaction
leases, gameplay contexts, combat policy, and cross-domain engine authority.
This child document owns weapon assemblies, feed/chamber/action mechanics,
weapon-part interactions, and weapon-family profiles. Where a generic service
overlaps, the parent contract has precedence.

The terms **confirmed**, **decision**, and **pending** have precise meanings:

- **Confirmed** describes behavior or data already observed in the current
  project, game assets, or the reference project.
- **Decision** is the implementation direction unless later evidence requires a
  recorded revision.
- **Pending** requires runtime probing or HMD validation before production use.

## 1. Problem statement

The original game renders a complete first-person animation for firing,
reloading, and switching weapons. Arm, finger, and weapon-part motion are tied
to that animation's timeline. That model is unsuitable as the authority for a
native VR interaction system because the player's hands determine when a
magazine is removed, inserted, or when an action is operated.

The original game also has no independent "round in chamber" mechanic. Empty
lock is animation-only and does not participate in firing or ammunition logic.
Native VR interaction therefore requires a new mechanical state layer rather
than a direct migration of existing reload animations.

**Decision:** original animation timing is not authoritative. Gameplay-relevant
VR transitions are driven by physical interaction and mechanical state. Stock
animations may remain sources for poses, notetracks, sound, effects, recoil,
and fallback presentation.

## 2. Goals and non-goals

### Goals

- Independently control upper arms, forearms, wrists, finger joints, and useful
  weapon activity bones.
- Support left- or right-hand weapon control, optional support-hand grip, and
  more than one independently tracked held weapon without hard-coding a
  permanent primary hand.
- Support typed waist/back storage, releasable hand grips and extraction-only
  native overflow under the parent architecture's sections 9.1-9.3.
- Support independently scheduled same/mixed-weapon firing without native
  akimbo, through the dedicated [shot adapter](vr-independent-weapon-fire.md).
- Support physical magazine removal/insertion, chambering, empty lock, action
  release, and segmented loading where appropriate.
- Keep the mechanical model independent of OpenVR/OpenXR so it can run with
  desktop synthetic poses and deterministic recorded input.
- Preserve H2's authoritative damage, ammo inventory, scripts, checkpoints, and
  weapon ownership through an explicit adapter.
- Centralize weapon-family behavior and permit explicit per-weapon overrides.
- Reject or reconcile invalid external state instead of silently creating or
  deleting ammunition.
- Keep per-frame work bounded and avoid coupling unrelated renderer, pose,
  interaction, and gameplay flows through blocking waits.

### Non-goals for the first implementation

- Persistent magazines with individual remaining-round counts.
- A universal realism simulation for NPC weapons.
- Reproducing the original reload animation frame by frame.
- Extending plus-one to unreviewed weapon families before native integration is validated.
- Arbitrary mixed-weapon dual wield before H2 ownership, independent firing, ammo,
  save/checkpoint, and script transaction boundaries are verified or replaced
  deliberately.
- Inferring every weapon's mechanics solely from `WeaponDef` flags or bone
  names.
- Treating desktop synthetic input as final ergonomic acceptance.

## 3. Confirmed project facts

### Current H2-Mod VR project

- Controller input, independent hands and muzzle-directed native firing are now
  implemented. Current acceptance and the M9 profile increment are recorded in
  [controller interaction](./vr-controller-interaction.md); the larger mechanical
  architecture below remains a design target, not implemented chamber/reload state.
- `game::playerState_s` in `src/client/game/structs.hpp` exposes only a small
  prefix and does not currently declare weapon state, weapon timers, clip ammo,
  or reserve ammo fields.
- `game::usercmd_s` exposes command buttons and weapon identifiers, so command
  mediation is possible, but command injection alone does not provide a native
  mechanical reload commit.
- `WeaponDef` exposes useful hints including `worldClipModel`, runtime ammo and
  clip indices, reload-add values, `boltAction`, and `segmentedReload`. These are
  inputs to profile discovery, not a complete mechanical specification.
- H2 assets explicitly distinguish `ATTACHMENT_UNDERBARREL`. `WeaponDef` also
  contains an `altWeapon` link and attachment-specific animation, sound, effect,
  reload-timer, and notetrack override tables. Their exact runtime relationship
  still requires probing, but an underbarrel attachment cannot be represented as
  presentation-only metadata.
- `WeaponDef` has right- and left-handed animation tables, a dedicated
  `akimboStateTimers` table, dual-wield reload/fire timing fields,
  `dualWieldViewModelOffset`, and `noDualWield`. This confirms native akimbo
  concepts but does not prove that two sides expose independent chamber, clip,
  firing, or weapon-definition state.
- The currently declared `usercmd_s` has one `weapon`, one `offHand`, and one
  known `BUTTON_ATTACK` bit. The exact native command/event route for left- and
  right-side akimbo fire remains pending.
- Initial asset inspection found common shoulder, elbow, wrist, and multi-joint
  finger bones in standard first-person arm rigs. Special rigs such as fast-rope
  and minigun variants require explicit exceptions.
- Weapon models contain useful activity bones for magazines, bolts, triggers,
  charging handles, safeties, and other moving parts, but availability and naming
  are not uniform.

### `ref/CallOfDuty4_VR`

The reference project proves that controller-driven magazine interaction can be
integrated with a Call of Duty first-person weapon. Its manual reload state uses
`Ready`, `HoldingLoaded`, `Ejected`, and `HoldingFresh` stages. After insertion,
however, it opens a 400 ms commit window and emits the original
`BUTTON_RELOAD`. It therefore remains coupled to the stock weapon state and
reload timing.

Relevant reference locations:

- `ref/CallOfDuty4_VR/src/vr/vr_openxr.cpp`: manual magazine state near line 2076.
- The same file: insertion commit window near line 16645.
- The same file: `BUTTON_RELOAD` emission near line 19223.

**Decision:** reuse the reference project's discoveries about interaction
anchors, input handling, model access, and edge cases where applicable. Do not
copy its stock-timed reload commit or rigid wrist-rooted arm architecture.

## 4. Architectural boundaries

```text
OpenVR/OpenXR       Desktop debug       Recorded traces
      \                  |                   /
              global input frame service
                         |
                immutable VrInputFrame
                         |
        +----------------+----------------+
        |                                 |
  global body/hand service        weapon mechanics FSM
        |                                 |
  solved body/hands                  validated event
        |                                 |
        +---------- weapon presenter -----+
                                          |
                                engine weapon adapter
                                          |
                          H2 ammo / damage / scripts
```

### 4.1 Input frame dependency

The global input service samples a runtime-independent provider once at a defined
frame boundary and publishes one immutable `VrInputFrame`. Weapon mechanics and
presentation consume the same frame and must never poll OpenVR/OpenXR or a debug
provider independently.

The parent architecture requires providers for:

- `OpenVrPoseProvider` for production HMD/controllers.
- `DesktopDebugPoseProvider` for mouse, keyboard, and scripted virtual hands.
- `RecordedPoseProvider` for deterministic interaction regression tests.

The weapon domain uses the frame ID, reference-space generation, pose validity,
and semantic action edges supplied by that service. The renderer, global
interaction system, weapon mechanics, and gameplay adapter must not wait on one
another's worker threads.

### 4.2 Body and arm integration

The parent body/hand service owns torso inference, shoulders, arm IK, and generic
hand solving. The weapon domain supplies grip targets and constraints. The
shoulder remains anchored in inferred torso space, not fixed in world space. A
soft clavicle/shoulder constraint follows head/body orientation with damping and
reach limits. Weapon handling requires:

- controller grip as the wrist/end-effector target;
- a stable elbow pole target derived from torso and controller geometry;
- a two-bone upper-arm/forearm IK solve;
- wrist orientation offsets from the active interaction anchor;
- bounded shoulder translation when the target is near maximum reach.

The shared solver must handle unreachable targets, coincident joints, controller
loss, hand crossing, and sudden recentering without NaNs, elbow inversion, or
unlimited arm stretching. Weapon code must not create a second torso or arm
model.

### 4.3 Hand pose system

The parent hand service owns the reusable pose library and blending. The weapon
domain requests semantic, state-driven poses and supplies weapon-relative wrist
anchors. Initial pose identifiers are:

- `open`
- `primary_grip`
- `support_grip`
- `mag_grip`
- `charging_handle`
- `slide_release`
- `shell_pinch`
- `device_grip`

Each pose stores finger-joint rotations relative to a wrist/interaction anchor.
Runtime controller curl data may be layered where available. Pose transitions
use short smoothing intervals, but gameplay commits occur at mechanical
thresholds rather than when a pose blend reaches an animation frame.

Stock XAnim and notetracks such as clip-out, clip-in, and chambering events may
be used offline to seed pose assets. Extracted poses become independent data and
must not retain the source animation's timing as authority.

### 4.4 Weapon assembly and module profiles

The [variant composition plan](vr-weapon-variants.md) refines native identity,
attachment roles, reviewed recipes, resolved-rig lifetime and test coverage for
this design. It preserves the primary/underbarrel authority boundaries below.

Mechanical behavior is centralized in data instead of distributed weapon-name
checks. A held weapon is an assembly: one shared chassis can expose a primary
fire module and an optional underbarrel fire module. Each module owns its own
feed, chamber/action, ammo binding, muzzle, loading points, and activity bones.

```cpp
struct VrWeaponModuleProfile
{
    FireModuleSlot slot;
    EngineWeaponBinding engineBinding;

    FeedSystem feedSystem;
    FiringSystem firingSystem;
    EmptyAction emptyAction;
    ChargePolicy chargePolicy;
    EjectPolicy ejectPolicy;

    InteractionAnchor magazineWell;
    InteractionAnchor magazineRelease;
    InteractionAnchor chargingHandle;
    InteractionAnchor actionRelease;
    InteractionAnchor loadingPort;
    InteractionAnchor muzzle;
    InteractionAnchor aim;

    ActivityBoneSet activityBones;
    HandPoseSet handPoses;

    int nominalLoadedCapacity;
    bool allowPlusOne;
};

struct VrWeaponAssemblyProfile
{
    ChassisBinding chassis;
    HandedGripSet grips;
    SupportGripRegion supportRegion;
    InteractionAnchor modeSelector;

    VrWeaponModuleProfile primary;
    std::optional<VrWeaponModuleProfile> underbarrel;
    SelectorPolicy selectorPolicy;
    HandednessPolicy handednessPolicy;
    WieldPolicy wieldPolicy;
};
```

Discovery from `WeaponDef`, model tags, bone names, and animation notetracks may
produce a candidate profile. Production support requires validation and an
explicit override for exceptions. Unsupported weapons use a deliberate fallback
profile rather than partially active mechanics. `WeaponDef::noDualWield` governs
native akimbo admission; it neither enables nor universally prohibits independent
VR dual wield. VR eligibility requires reviewed handed grips/mechanics and a
verified per-instance shot adapter. Native akimbo flags and attachments are not
the backend for that adapter.

Initial weapon families:

1. Closed-bolt detachable-magazine pistol.
2. Closed-bolt detachable-magazine rifle/SMG.
3. Tube-fed segmented-reload shotgun.
4. Open-bolt or belt-fed automatic weapon.
5. Manually cycled weapon.
6. Single-shot launcher.
7. Scripted/device weapon with no chamber model.
8. Composite rifle with a single-shot underbarrel grenade launcher.
9. Composite rifle with a segmented-reload underbarrel shotgun.
10. Two independently held supported assemblies through the dedicated shot adapter.

### 4.5 Composite weapon interaction rules

**Decision:** an underbarrel attachment is not treated as a separately held
weapon and switching fire mode does not replace or reconstruct the shared gun
pose. The assembly retains one primary grip, one support-grip solution, and one
chassis transform while routing firing, aiming, recoil, muzzle effects, and
loading interactions to the selected module.

Primary and underbarrel mechanical state persist independently. A rifle may, for
example, retain a chambered primary round while its empty grenade launcher is
open for loading. Switching back to the rifle must not close, reload, or reset
the launcher, and operating the launcher must not change the rifle magazine.

The off hand is a contended resource. The global interaction arbiter grants a
per-hand lease to at most one candidate. Weapon interactables expose explicit
priority in this order:

```text
committed loading/action manipulation
    > candidate magazine/shell/grenade interaction
    > support grip
    > free hand
```

Once an interaction lease is committed, incidental overlap with the primary
magazine well, underbarrel loading port, grenade belt, or support grip cannot
steal it. The parent arbiter invalidates the lease on valid completion,
deliberate release, cancellation, tracking loss, reference-space/context change,
weapon switch, or bounded timeout. Weapon mechanics retain only lease IDs and
validate them against the input frame/context generation before using motion.

Initial underbarrel behavior is profile-specific:

- A single-shot grenade launcher uses its own chamber and breech/action. If the
  weapon retains a spent case, firing transitions the chamber to `Spent`; the
  case must be ejected or removed before a new round can be inserted. Closing the
  action makes the module ready without changing the primary rifle state.
- An underbarrel shotgun uses its own tube/magazine count, chamber/action, and
  segmented shell insertion. Shell insertion transfers one round per accepted
  interaction and never targets the primary rifle's ammo indices.
- Attachments remain statically mounted in the first implementation. Physically
  removing, attaching, or exchanging an underbarrel device is out of scope.
- If required loading-port models, activity bones, or a verified engine
  transaction are unavailable, only that module uses an explicit native-input
  fallback; the primary module remains physically interactive.

Mode selection is a confirmed transaction, not a render-animation inference.
The first implementation may request H2's native alternate-weapon toggle and
wait for the engine adapter to confirm the selected module. A physical selector
control may request the same transaction later. Firing is gated by the confirmed
active module and that module's readiness only. Physical loading and action
manipulation may target either installed module regardless of which module is
currently selected for firing.

### 4.6 Hand roles and wield topology

**Decision:** dominant hand is a user preference and default binding, not a
permanent gameplay role. Each fire-capable held instance binds one `controlHand`
and may bind the other hand as `supportHand`. The control hand owns the weapon grip,
trigger, and base transform. The support hand owns only a support-region lease
or a direct-part interaction lease. A long gun may also remain carried by a
grasp that has no trigger authority after its control grip is released.

```cpp
enum class WieldTopology
{
    None,
    SingleCarry, // Held at a non-control grip; cannot fire.
    SingleOneHand,
    SingleTwoHand,
    DualMatched,
    DualMixed,
};
```

#### Left-hand control

Left-hand operation swaps semantic control/support roles and trigger routing. It
does not mirror the whole weapon model: ejection ports, selectors, charging
handles, loading ports, and underbarrel controls remain on their authored side.
Profiles provide an authored left-control grip and hand pose where asymmetric
geometry makes mirroring unsafe. `szXAnimsLeftHanded` may seed poses or reveal
engine behavior, but stock left-handed animation timing remains non-authoritative.

Body-slot placement and handed UI are provided by the parent architecture. The
weapon profile only declares whether control anchors are `AuthoredBoth`,
`SafeToMirror`, or `OneSideOnly`. An unsupported side fails to an explicit
accessibility/native-input fallback rather than producing an invalid wrist pose.

#### Control hand plus handguard support

For a two-handed weapon, the control-hand grip is the hard reference. The
support hand acquires a bounded region on the handguard/foregrip rather than one
universal point. The two-hand solve:

1. aligns the weapon control grip to the control-hand pose;
2. finds the closest valid point/orientation in the support region;
3. rotates the weapon around the control grip using the support hand as a soft
   direction/roll constraint;
4. applies bounded compliance when controller separation does not match the
   authored weapon instead of stretching arms or teleporting either hand.

Leaving the support region beyond hysteresis releases only the support lease.
The weapon remains in the control hand and degrades to its one-handed readiness,
stability, recoil, and weight policy. Releasing support is also the normal path
to free that hand for magazine, shell, action, or underbarrel interaction.

#### Grip release and control transfer

The parent release transaction resolves surviving grasps before stow/drop.
For a long gun whose remaining hand holds the handguard, retain that grasp as
non-firing carry until a valid control grip is acquired. For pistols and other
profiles with exclusive control-hand pose authority, the design decision on
 promotes a still-gripped support hand to full control when the old
control hand deliberately releases. The old control hand becomes free. Rebase
the weapon to the new hand's authored control anchor, clear the old support
binding and update trigger ownership as one transition. Do not mirror asymmetric
weapon geometry or feed/reset ammo. Before release, support on these profiles
does not override the control hand's weapon transform.

Same-frame release of both hands creates one final-release request; tracking
loss cannot promote support or drop the gun. Handover retains the same instance,
cadence and mechanical state, but invalidates old muzzle/ownership revisions and
requires neutral/fresh trigger input on the new controller. Its already-held
side grip does not require a new grip press. Presentation may blend the new
authored grip; shooting waits for a valid matching muzzle, without making render
animation timing the authority for ownership. A released hand is immediately
available to the shared arbiter; magazine/part leases do not silently become gun
control. Exact left-control poses and transfer ergonomics require HMD acceptance.

#### Independent dual wield

Dual wield is two held weapon instances, not a two-handed pose of one instance.
Each side has an independent transform, muzzle/sight geometry, trigger intent,
mechanical state, activity bones, revision, and transaction identity. Even if
both instances use the same `WeaponDef`, firing or reloading one side cannot
implicitly mutate or reconstruct the other side.

Both hands are control hands while dual wielding, so neither is available as a
support/reload hand. Physical reload requires first stowing or otherwise
releasing one confirmed weapon instance, then using the freed hand on the other
weapon. A configurable assisted reload may be added for accessibility, but it is
an explicit transaction policy and never an invisible third hand.

Typed waist/back slots determine whether stow can free a hand. Exchanging with
an occupied slot retains an occupied hand; it does not make physical reload
possible by itself. When no eligible empty slot exists, confirmed world
placement/drop is needed. Back overflow is extraction-only and cannot receive
a stowed gun. These rules supersede the earlier three-weapon carry target.

`DualMatched` and `DualMixed` describe the two VR assemblies' profiles only.
Both use the same independent shot architecture, not native akimbo. Native
matched-pair assets may require a separate import/ownership mapping; that work
is not a prerequisite or substitute for independent firing.

#### Mixed dual wield

The VR-domain structures may hold two different assembly profiles, but mixed
dual wield is capability-gated. It requires verified independent H2 ownership,
selection, shot, ammo, damage/projectile, switch, checkpoint, and script
transactions for both weapon IDs. The current declarations do not prove those
contracts.

Each instance needs its own trigger gate, mechanical readiness, shot cadence,
burst progress and frozen muzzle context. Two requests from the same simulation
time may settle in deterministic order on the owning engine thread; independent
fire does not require concurrent engine calls. Per-weapon failure must not block
the other eligible weapon, except for shared context or ownership invalidation.
Native reserve pools remain shared where their ammo bindings require it; loaded
feeds and chamber state remain per instance, with no duplicate ammo writer.

**Decision:** never emulate mixed dual wield by switching H2's selected weapon
back and forth per frame or per trigger pull, or by using native akimbo channels.
The  design decision requires a dedicated VR shot scheduler and explicit
weapon-parameter ballistic adapter. Its contract, current evidence and native
validation gates are in [independent weapon firing](vr-independent-weapon-fire.md).
Until proven, independent firing remains an explicitly unavailable capability;
do not report native akimbo or alternating single-weapon fire as mixed support.

Composite weapons with underbarrel modules are not assumed dual-wieldable. Their
profile must explicitly opt in after both the base and alternate module bindings
are proven for each instance.

## 5. Mechanical state model

Do not create one large reload enum. Feed, chamber, action, interaction, and
synchronization are orthogonal state axes.

```cpp
struct VrWeaponModuleState
{
    FeedState feed;             // Inserted, Detached, Integral, NotApplicable
    int feedRounds;

    ChamberState chamber;      // Loaded, Empty, Spent, NotApplicable
    ActionState action;        // Closed, Open, Cycling, LockedOpen, HeldOpen
    InteractionState interaction;
    SyncState sync;

    std::uint64_t revision;
};

struct VrWeaponAssemblyState
{
    WeaponId assemblyWeapon;
    FireModuleSlot activeModule;

    VrWeaponModuleState primary;
    std::optional<VrWeaponModuleState> underbarrel;

    std::uint64_t revision;
};

struct VrWeaponInstanceState
{
    WeaponInstanceId id;
    VrWeaponAssemblyState assembly;
    std::uint64_t revision;
};

struct VrHeldWeaponBinding
{
    WeaponInstanceId id;
    HandId holdingHand;
    std::optional<HandId> controlHand;
    std::optional<HandId> supportHand;
    InteractionLeaseId holdingLease;
    std::optional<InteractionLeaseId> supportLease;
    std::uint64_t revision;
};

struct VrWieldState
{
    WieldTopology topology;
    std::array<std::optional<VrHeldWeaponBinding>, 2> instances;
    std::uint64_t revision;
};
```

The instance registry retains mechanical state for confirmed owned weapons,
including waist/back/overflow weapons. `VrWieldState` references at most two held
instances; it is not the inventory or the carry-capacity limit. The parent carry
service owns locations and admission. Stow/draw/hand transfer updates bindings
without recreating assembly state. Instance identity must distinguish separate
copies of the same weapon definition and ownership lifetimes. Exact checkpoint
restoration remains a separate pending contract.

`holdingHand` owns the primary surviving grasp. `controlHand`, when present,
equals it and permits trigger routing; it is empty for handguard-only carry.
`supportHand` is a distinct additional grasp of the same instance. Confirmed
world drop/pickup transfers mechanical state through an entity-generation binding
under the parent lifecycle contract, rather than discarding it on ownership loss.

Representative interaction states include `Idle`, `Ejecting`, `Inserting`,
`Charging`, `ReleasingAction`, and `InsertingShell`. Representative sync states
include `Synced`, `CommitPending`, `Blocked`, and `ResyncRequired`.

### 5.1 Ammunition invariant

For each module using a chamber:

```text
loaded rounds = feed rounds + (chamber loaded ? 1 : 0)
```

**Updated design decision :** M9 permits a full magazine plus one
chambered round. Each H2 module's loaded-ammo value must mirror its own total;
partitioning an existing native total must never introduce a round:

```text
initial native 15 = magazine 14 + chamber 1
tactical full-magazine reload = magazine 15 + chamber 1 = loaded 16
```

The extra loaded round comes from reserve, never from changing the shared
WeaponDef capacity or adding a round during state initialization. Native HUD,
pickups, script/checkpoint reconciliation and prediction still require runtime
validation; the offline mechanics planner is not proof of those integrations.

### 5.2 Pooled-magazine semantics

**Decision:** the first implementation does not persist individual magazines.
When a magazine is removed, its remaining rounds return transactionally to H2's
reserve pool. Drawing a fresh magazine at either waist reserves up to magazine
capacity immediately. Inserting transfers those already-reserved rounds into
the weapon; cancellation refunds them exactly once. This replaces the earlier
proposal to deduct only at insertion.

This preserves total ammunition, avoids duplication through repeated ejection,
and avoids imposing magazine inventory on existing missions. Dropping the token
does not discard ammunition. Persistent magazines may be designed later as a
separate gameplay mode.

### 5.3 Empty-action policy

Empty lock is an explicit mechanical state and is never inferred solely from an
animation or from `loaded rounds == 0`.

```cpp
enum class EmptyAction
{
    LockOpen,
    StayClosed,
    OpenBolt,
    ManualCycle,
    NotApplicable,
};
```

The weapon profile determines what happens after the final accepted shot. A
weapon in `LockedOpen` remains unable to fire after magazine insertion until a
valid action-release or charging interaction completes.

Open-bolt weapons and devices must use their own readiness rule rather than a
forced `chamberLoaded` boolean. A `Spent` chamber is not ready and remains
distinct from `Empty` until the profile's extraction/ejection transition occurs.

## 6. Authoritative transitions

| Event | Preconditions | Mechanical result | Engine transaction |
|:------|:--------------|:------------------|:-------------------|
| Hand-local shot accepted | bound instance and selected module mechanically ready | Consume only that instance/module chamber; cycle and feed when possible | Commit one shot with instance, hand, module, frame, and revision identity |
| Final shot accepted | chamber loaded, feed empty | Chamber empty; apply profile empty action | Commit one original shot/ammo decrement |
| Magazine removed | removable magazine inserted | Keep chamber; detach magazine token | Move magazine rounds to reserve and mirror chamber-only loaded total |
| Magazine inserted | well/orientation/depth thresholds satisfied | Insert magazine; retain current action state | Transfer a bounded amount from reserve |
| Action released | action locked open, inserted magazine has rounds | Close action and chamber one round | No total-ammo change |
| Charging completed | profile permits and travel/release thresholds satisfied | Update action and chamber according to profile | Usually no total-ammo change |
| Shell inserted | segmented feed accepts another round | Add one magazine/tube round | Move exactly one round from reserve |
| Underbarrel round inserted | module exists, owns the interaction, and its open/empty feed accepts the round | Load only the underbarrel chamber/feed | Transfer from the underbarrel ammo binding |
| Spent case removed | module chamber is spent and action permits extraction | Chamber becomes empty | No live-ammo change |
| Fire module selected | requested module exists and H2 confirms alternate mode | Preserve both modules; route input and presentation to selected module | Commit/confirm native alternate-weapon selection |
| Support grip acquired | free hand receives a valid support-region lease | Enter two-hand constraints; mechanics unchanged | No ammo/inventory transaction |
| Support grip released | lease released, invalidated, or leaves hysteresis | Continue as one-handed; cancel lease-owned part motion safely | No ammo/inventory transaction |
| Control hand released, pistol support retained | profile permits promotion; support lease/input valid | Promote support to control; preserve mechanics and cadence; rearm trigger | No inventory/ammo mutation; atomically replace grip binding |
| Held weapon stowed | final grasp released; eligible empty slot and adapter checks pass | Free the hand; persist that instance | Commit location/empty-hand state; keep native ownership |
| Held weapon exchanged | final grasp released; occupied eligible slot and both poses valid | Swap locations; retain new gun until regrip | Commit both locations and any selection change together; no take/give |
| Release rejected | invalid slot class, obstruction or failed preflight | Retain holding until another press/release | No speculative ownership removal or world drop |
| Held weapon dropped/picked up | native inventory and world entity transition confirmed | Transfer that instance's mechanical state with the entity generation | Native drop/pickup with no duplicate item or ammo initialization |
| Weapon switched | ownership and target weapon valid | Preserve per-instance mechanics | Reconcile against H2 weapon/ammo state |
| External ammo change | script, pickup, checkpoint, or engine mutation detected | Pause interaction and rebuild safely | Treat H2 state as external authority |

Mechanical movement and gameplay commit are separate steps. A state transition
requests an engine transaction; it becomes committed only after the adapter
confirms the result. A rejected transaction leaves the state blocked or causes a
bounded reconciliation. There must never be two independent writable ammo
truths.

## 7. Engine weapon adapter

The engine adapter owns all mutation of H2 weapon gameplay state. Its
responsibilities are:

- observe current weapon ID, weapon state, weapon timer, loaded ammo, reserve
  ammo, selected alternate module, and relevant animation state;
- submit same/mixed-instance shots through the dedicated ballistic adapter,
  without native akimbo or temporary selected-weapon/player-state rewriting;
- resolve primary and underbarrel engine weapon IDs plus their independent ammo
  and clip indices without assuming that shared rendering implies shared ammo;
- validate requested transfers and shots against current H2 state;
- carry weapon-instance ID, control hand, selected module, input-frame ID, and
  expected revision through every hand-local shot or reload transaction;
- apply one atomic gameplay commit at an identified engine boundary;
- report success, rejection, or external mutation to the mechanical state
  machine;
- cancel or reconcile interaction on death, weapon removal, level transition,
  checkpoint restore, cutscene, and script-driven ammo changes;
- leave NPC weapon logic untouched.
- reject mixed dual-wield requests unless an explicit adapter capability proves
  two heterogeneous weapon transactions; never approximate them with rapid
  native selection changes or native akimbo.

`BUTTON_RELOAD` injection is permitted only as an explicit compatibility
fallback for unsupported profiles. It is not the final native reload mechanism
because the stock weapon timer and animation would remain authoritative.

**Pending:** recover and verify the exact `playerState_s` weapon fields and the
functions that accept a shot, transfer reload ammunition, and process segmented
reloads. Do not write guessed offsets. Runtime observations must identify the
target executable build and include sanity checks before any hook is armed.

## 8. Weapon presentation

The presenter consumes pose and mechanical snapshots and applies visual state
after the stock first-person pose has been evaluated.

### Arms and hands

- Apply body-space shoulder anchoring and two-bone arm IK.
- Apply wrist offsets from each held instance's handed control/support anchors.
- Blend semantic finger poses independently per hand.
- Preserve a bounded fallback pose when tracking becomes invalid.

### Activity bones

- Trigger motion follows a normalized trigger value.
- Slide/bolt firing motion follows a short procedural cycle unless directly
  manipulated.
- Directly manipulated parts project controller displacement onto a constrained
  local axis with minimum/maximum travel.
- Primary and underbarrel muzzle, aim, recoil, loading-port, and activity-bone
  bindings are selected per module without replacing the shared chassis pose.
- Dual-wield instances evaluate presentation independently; stock akimbo offsets
  never replace tracked hand transforms.
- Completion uses threshold plus hysteresis to prevent noisy repeated commits.
- Latched parts, including empty-lock slides/bolts, follow mechanical state and
  remain stable after the stock animation finishes.
- Missing or invalid bones disable only the affected interaction, not the whole
  weapon or renderer.

Sound, haptics, muzzle effects, and notetrack-derived events observe committed
transitions. They never create gameplay state by themselves.

Presentation profiles bind semantic mechanics events such as magazine release,
magazine removal, insertion contact, insertion latch, slide/bolt grab, rear-stop,
release, closure, and dry fire to existing game sound aliases where available.
The mechanics transition owns the runtime timestamp. Stock XAnim notetracks seed
the alias binding and relative ordering, but their frames do not schedule a
free-form VR interaction.

The runtime keeps only the binding identity, not decoded sound data or a cached
asset pointer. On a committed event it resolves the selected weapon and active
attachment override to an alias name, verifies that the alias is loaded, and
delegates playback to a narrow H2 audio adapter. Re-resolving after fastfile,
weapon, or attachment changes avoids retaining stale engine pointers. Alias
randomization, volume groups, DSP routing, and underlying file loading remain
owned by the game.

Desktop tooling may use local non-positional alias playback as a smoke test.
Production VR manipulation sounds should use the native first-person
player/weapon positional playback path so head motion and the weapon's tracked
location retain correct spatial behavior. The local UI playback entry is not
the production contract.

Sound, rumble, haptics, and effects remain separate outputs even when the stock
`WeaponDef` maps one notetrack key into several of them. Generic feedback names
such as `viewmodel_small`, `viewmodel_medium`, and `viewmodel_large` are not
treated as sound aliases without the corresponding map. Attachment-specific
notetrack and sound overrides are resolved before the base profile binding.
If the stock animation/event path remains active for an interaction, the VR
presentation layer suppresses its corresponding manual emission; one mechanical
commit must not produce both a native and a VR-triggered copy of the same sound.

Inspection animations do not become required VR interactions. They remain a
supplemental source for manipulation poses, incidental mechanical sounds, and
feedback variants that may be rebound to committed VR mechanics events.

## 9. Desktop testing before HMD access

The state probe and mechanics core must not require an active XR runtime or HMD.
The initial in-game capture session should record bounded, structured samples for
the following scenarios:

1. Normal firing and the final shot.
2. Reload with rounds remaining.
3. Reload from empty.
4. Reload interrupted by sprint, weapon switch, death, or script.
5. Segmented shotgun loading and firing during an interrupted load.
6. Weapon pickup, removal, checkpoint restore, and external ammo grant.
7. Entering and leaving each underbarrel mode while both modules are loaded,
   empty, and mid-interaction.
8. Underbarrel grenade firing, spent/empty behavior, insertion, closure, and
   interruption.
9. Underbarrel shotgun firing and shell-by-shell loading without primary-ammo
   mutation.
10. Explicit-weapon ballistic parameters and all selected-weapon dependencies
    listed in the independent-shot document before attempting extra native shots.
11. Independent instances with different cadence, ammo and muzzles, including
    same-tick fire, one-sided rejection and control-hand transfer.

Each sample should include:

- target executable fingerprint and current map;
- player and weapon identifiers;
- observed weapon state and weapon timer;
- loaded and reserve ammo values with their resolved indices;
- primary/alternate weapon links, selected module, and attachment overrides;
- per-instance shot ID, command/simulation time, control hand, ammo binding and
  native selected weapon before/after each adapter operation;
- user-command buttons;
- active XAnim references and relevant notetrack events;
- magazine, trigger, bolt/slide, and charging-handle bone transforms;
- monotonic sample/revision IDs and detected discontinuities.

Recommended first representatives are one closed-bolt pistol, one detachable-mag
rifle, one tube-fed shotgun, one launcher, one rifle with an underbarrel grenade
launcher, and one rifle with an underbarrel shotgun. Capture is read-only until
field offsets and transition ordering are proven.

`DesktopDebugPoseProvider` should then drive virtual hands through deterministic
paths for magazine extraction, insertion, action release, and charging. This can
validate state transitions, activity-bone constraints, cancellation, and ammo
invariants without claiming ergonomic acceptance.

Synthetic topology tests must additionally cover left-control/right-support,
right-control/left-support, support release/reacquisition, matched dual wield,
attempted mixed dual wield, simultaneous trigger edges, stow-to-free-hand reload,
and topology changes with pending leases. Architecture tests may exercise
`DualMixed` rejection before an engine adapter supports it.

## 10. HMD acceptance gate

Real hardware remains mandatory for:

- controller-to-wrist orientation and per-device grip offsets;
- shoulder, elbow, and reach comfort;
- two-hand alignment and hand-crossing behavior;
- left-control ergonomics and access to asymmetric weapon controls;
- dual-wield muzzle separation, independent recoil, simultaneous fire, and safe
  stow/reload gestures;
- interaction radii, insertion depth, orientation tolerance, and hysteresis;
- near-field depth perception and visual occlusion;
- tracking loss, recentering, runtime focus, and controller reconnect behavior;
- haptic strength and timing.

Desktop and recorded-pose tests can reject broken logic but cannot accept these
properties for production.

## 11. Safety and invariant tests

At minimum, automated mechanics tests must prove:

- Repeated magazine extraction/insertion cannot create or destroy ammunition.
- A chambered weapon without a magazine can fire exactly once.
- An empty weapon without a magazine cannot fire.
- Inserting a magazine into a locked-open weapon does not make it fire-ready.
- Releasing or charging an action never changes total loaded ammunition.
- A segmented reload transfers exactly one round per accepted insertion.
- Primary and underbarrel ammo totals remain independent across mode switches,
  firing, loading, cancellation, and checkpoint reconciliation.
- Switching modules preserves both modules' chamber/action state and cannot
  bypass an open, spent, empty, or locked action.
- An off-hand interaction cannot be stolen by an overlapping support-grip,
  magazine, grenade-belt, or other-module candidate.
- A hand-local trigger can fire only its bound instance; duplicate or simultaneous
  trigger edges cannot cross-fire or collapse into one transaction.
- Matched instances remain mechanically independent even when they share one
  `WeaponDef` and native ammo pool representation.
- Reloading, stowing, dropping, or reconciling one dual-wield instance cannot
  duplicate, reset, or consume the other instance's ammo.
- A hand cannot simultaneously retain a weapon control lease and acquire a
  magazine, shell, support, or second-weapon lease unless an explicit transition
  releases the first lease.
- Left/right control-role changes preserve authored weapon-side geometry and do
  not mirror non-mirror-safe activity bones or interaction anchors.
- Duplicate contact events, stale pose samples, and input bounce do not duplicate
  a gameplay commit.
- Weapon switch, death, checkpoint restore, and external ammo mutation produce a
  deterministic state.
- Unsupported weapons remain playable through an explicit fallback.
- Invalid profile values, missing bones, extreme controller coordinates, and
  non-finite transforms fail closed without crashing.

Property-style tests should generate long random sequences of eject, insert,
drop, switch, charge, fire, and external mutation events while checking ammo and
state invariants.

## 12. External architectural references

These references support the general interaction primitives, not H2-specific
gameplay decisions:

- Meta Hand Grab Interactions: pre-authored hand poses and wrist-relative grab
  anchors: <https://developers.meta.com/horizon/documentation/unity/unity-isdk-hand-grab-interaction/>
- Meta `HandGrabUseInteractable`: relaxed/tight use poses and progress blending:
  <https://developers.meta.com/horizon/reference/interaction/v205/class_oculus_interaction_hand_grab_hand_grab_use_interactable/>
- SteamVR Skeletal Input: finger curls, reference poses, and layering
  application-specific hand animation:
  <https://github.com/ValveSoftware/openvr/wiki/SteamVR-Skeletal-Input>
- Unreal Engine Two Bone IK: end-effector and joint-target arm solving:
  <https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-blueprint-two-bone-ik-in-unreal-engine>
