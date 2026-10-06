# VR gameplay and interaction architecture

This document defines the parent architecture for native VR gameplay in
Overlord. It owns input semantics, spatial frames, player-body interpretation,
interaction arbitration, gameplay-context transitions, locomotion and stance
policy, combat policy, engine authority, failure handling, and cross-domain
testing.

The [hand interaction redesign](vr-hand-interaction-redesign.md) describes the
shared hand-composition contract. Its current runtime integration and validation
are documented in [hand interaction runtime](vr-hand-interaction-runtime.md).

Weapon-specific mechanics are defined by
[VR weapon interaction and mechanical state architecture](./vr-weapon-interaction-architecture.md).
That document is a child design: it owns weapon feed, chamber, action, magazine,
activity-bone, and weapon-family behavior, while consuming the global services
defined here.

The terms **confirmed**, **decision**, and **pending** have the same meanings in
both documents:

- **Confirmed** describes behavior or data observed in the current project,
  game, or a cited reference.
- **Decision** is the implementation direction unless later evidence requires a
  recorded revision.
- **Pending** requires runtime probing, hardware validation, or playtesting.

## Current implementation boundaries

The architecture refactor assigns shared facilities to the following owners.
The subsystem table records their current responsibilities and boundaries.

| Facility | Current owner | Boundary |
| --- | --- | --- |
| Server interaction phases | `gameplay/interaction_schedule.hpp`, `interaction_coordinator.cpp` | Named phase lists select callbacks from one checked provider inventory. Acquisition and reconciliation retain their distinct orders; lifecycle groups have explicit ownership. H2 server scheduling stays in the implementation adapter. |
| Arbitration and input history | `gameplay/hand_interaction/runtime.cpp` | Owns grants, witnessed outcomes and publication; does not schedule or call domain runtimes. |
| Carry and inventory | `gameplay/carry_interaction.hpp`, `weapon_carry_runtime.cpp` | Admits a server-owned frame and exposes carry candidates, grip commits, pickup and presentation publication. Native inventory, selection and drop transactions remain here. |
| Common hands | `gameplay/hand_service.*`, `hands/skeleton.hpp`, `hands/pose_schema.hpp`, `hands/pose_library.hpp` | Owns neutral/bilateral binding and hand presentation dispatch. Generic hand descriptors, transforms and mirroring do not require weapon admission rules. |
| Weapon pose adaptation | `gameplay/weapon_pose_library.hpp` | Adds weapon rest assembly and measured trigger-index policy to common hand bindings. |
| Hand and interaction identity | `vr/hand.hpp`, `hand_interaction/object_identity.hpp` | Hand side is independent of weapon holding. Interaction objects use provider-local values/generations, including initial world entities and head gestures without firearm tokens. |
| Native memory and renderer ABI | `common/utils/native_memory.hpp`, `vr/native_render_contract.hpp` | Bounded guarded reads and verified H2 layout constants are shared infrastructure; diagnostics consume these contracts. Memory readability does not establish asset lifetime or thread ownership. |
| Weapon catalog | `gameplay/weapon_registry.cpp`, `weapon_mechanics_profiles.cpp` | Concrete recipes are compiled in implementation files. Public headers expose immutable catalogs, capability views and queries. Native chamber-plus-one remains a separate explicit gate. |
| Profile configuration | `physical_reload_profile.hpp`, `body_supply_layout.hpp`, `families/ar.hpp` | Named initialization, common presentation/body defaults and reviewed family policies. Receiver geometry, motion travel, sound sources and admission stay explicitly authored. |
| Mechanical lifecycle | `gameplay/weapon_runtime_lifecycle.hpp` | Shared identity-preserving restore and scene publication for detachable, cylinder, tube and break-action feeds. Each family retains its mechanical rules, native comparison and ammo fields. |
| Native scene submission | `component/scene_model_record.hpp`, `scene_pose_match.hpp`, `scene_submission_pool.hpp` | Shared entry decoding, skeleton-epoch lookup and immutable submission storage. Domain predicates retain physical-instance authority. |
| Falling motion | `gameplay/falling_trajectory.hpp`, `falling_item_presenter.*` | One analytic motion definition shared by simulation and cosmetic consumers, independent of the reload-item inventory ledger. |

The coordinator consumes one admitted input/body frame. Carry state stays private
to its server adapter; the hand arbiter sees copied scene descriptors and settled
relationships. Repeated XR samples still run reconciliation without creating a
new acquisition batch. Script-only world use and vehicle control retain their
own admission paths. Native game writes remain on their verified owner thread.

The dispatch schedule uses local provider keys, never serialized hand identities
or native IDs. Binding validation rejects missing reports/collectors, callbacks
without a settlement phase, omitted lifecycle callbacks and mismatched provider
indices. No numeric ranking or disabled-rank sentinel participates in dispatch.

The admitted-batch boundary remains explicit:

1. Report settled relations, synchronize input and exchange underbarrel supply.
2. Collect domain/carry proposals and resolve them once through the hand arbiter.
3. Settle granted domain commits in their declared phase.
4. Let physical reload reconcile support-to-magazine handoffs; let underbarrel
   settle its mechanical state and support handoffs; refresh copied carry poses.
5. Let equipment consume its knife grants, then settle heartbeat, carry grips and
   world use with the resulting hand masks.
6. Publish carry/launcher feedback, report final relations and finish the batch.

Pistol support-catch and secondary-module lease rules belong to their domain
runtimes. They request ownership changes through the server-owned carry adapter;
the coordinator does not inspect magazine ownership or secondary grip internals.
Equipment receives the copied interaction frame, original input and named grip
edges instead of a positional argument list. Filtered contact input and original
squeeze retention keep their separate existing meanings.

Continuous lifecycle still runs after an admitted batch. Without one, continuous
and idle-feed lifecycle settle before vehicle/scripted-use handling. Repeated
input reports settled state through reconciliation without another acquisition.

The hand service freezes the knife presentation state before composing wrists,
fingers and empty-hand poses. Knife equipment no longer owns the global hand rig
or dispatches vehicle, grenade, special-equipment and reload-item presentation.
Its original H2 reference joint ordering and rotations are retained in
`hands/native_schema.hpp`; the knife still owns its blade, attachment and co-grasp
data.

Catalog capacity is explicit and checked against the compiled registration
count. Capability-view overflow or invalid registration rejects the whole view.
Native ammunition readers copy fields under their memory guard and then pass
copied scalar values to `weapon_native_traits.hpp` admission. Cylinder runtime
and presentation use registered profile/asset recipes instead of Magnum-specific
constants. Shared M4/M16 policy remains opt-in; borrowing a real M9 sound for M93R
continues to identify that original asset source.

Scene submission pools keep native lighting storage stable and freeze each
submission's payload. The existing 150 ms freshness limit and reuse guard are
named separately; neither proves GPU completion. Metadata retirement is tied to
the drained native asset-unload boundary. Mechanical ownership and current item
revision checks remain in the reload-item presenter, while native-record and
skeleton identity checks precede attachment placement.

The public catalog headers now transitively include 6 repository files / 637
lines, down from approximately 390 / 19,000 in the initial review. This is a
source include-graph measurement, excluding SDK and standard-library headers,
not a runtime performance claim. The shared-PCH workaround is limited to the two
engine-independent catalog implementation files.

Validation: the RelWithDebInfo client compiles and links. Targeted offline
regressions cover catalog admission, mechanical feeds, hand arbitration,
empty-hand poses, input, vehicle/underbarrel/nightvision controls and scene
submission/transfer contracts. Two assertions also fail in isolated builds of
the original HEAD code: thermal ADS return timing and the Mini Uzi legacy fixed
support-position flag. They are recorded as baseline failures, with no tuning
change in this refactor. This does not establish in-game or HMD acceptance.

## 1. Problem statement

H2 was designed around a flat-screen player model:

- camera direction, aim direction, and weapon direction are normally coupled;
- button state drives complete weapon and item animations;
- ADS changes camera presentation and weapon accuracy as one operation;
- hidden spread, view kick, stance bonuses, and aim assistance compensate for
  mouse or gamepad input;
- the player is represented by one movement capsule and one view origin;
- weapon selection, offhand use, melee, and stance are command actions rather
  than spatial interactions.

Six-degree-of-freedom head and hand tracking invalidates those assumptions. The
head may look away from the weapon, either hand may interact with an object, the
weapon may be obstructed while the camera is clear, and physical posture may not
match the native gameplay stance. Replacing individual buttons or animations
without a shared gameplay model would create inconsistent authority and repeated
interaction systems.

**Decision:** Overlord will add a global VR gameplay layer that translates one
coherent tracking/action frame into validated semantic intentions. Domain
systems such as weapons, offhand items, melee, UI, and locomotion consume those
intentions and request native engine transactions. H2 remains authoritative for
inventory, damage, mission scripts, checkpoints, player movement state, and
accepted gameplay outcomes.

## 2. Weapon architecture integration audit

The weapon document was audited against this global model and the current
repository. Its mechanical direction is retained.

### 2.1 Accepted decisions

The following weapon decisions are compatible with the global architecture:

- original first-person animation timing is not gameplay authority;
- feed, chamber, action, interaction, and synchronization are separate state
  concerns rather than one reload enum;
- H2 ammo mutation occurs only through a verified engine adapter;
- physical transitions request transactions and do not commit gameplay merely
  because a pose or animation threshold was crossed;
- pooled magazine semantics are the initial compatibility mode;
- unsupported weapons use an explicit fallback instead of partially active
  mechanics;
- controller-independent synthetic and recorded inputs are required for
  deterministic tests;
- weapon support begins with read-only state provenance and verified executable
  identity.

The referenced `CallOfDuty4_VR` implementation was also verified locally. It has
the documented `Ready`, `HoldingLoaded`, `Ejected`, and `HoldingFresh` stages,
opens a 400 ms insertion window, and maps that window back to the original reload
button. It remains useful as evidence and as an interaction reference, not as
the authority model for this project.

### 2.2 Ownership corrections

Where the two documents overlap, this document has precedence for the following
concerns:

| Concern | Global owner | Weapon-domain responsibility |
|:--------|:-------------|:-----------------------------|
| Runtime input sampling | Input frame service | Read one published frame; never poll XR directly |
| Reference spaces and recentering | Spatial frame service | Reject or rebase stale interaction leases |
| Torso, shoulders, arms, and generic hands | Body/hand service | Supply weapon anchors and requested hand poses |
| Hand occupancy and candidate selection | Interaction arbiter | Expose weapon and part interactables |
| Menu, gameplay, vehicle, cinematic, death, and focus state | Gameplay context service | Suspend, cancel, fallback, or reconcile as instructed |
| Shot direction, ADS, spread, recoil, and aim assistance policy | Combat policy | Supply calibrated muzzle, sight, grip, and obstruction geometry |
| Ammo, damage, ownership, scripts, and checkpoint results | H2 engine adapters | Submit idempotent weapon transactions and consume results |
| Feed, chamber, action, magazines, and activity bones | Weapon mechanics | Full ownership within committed native constraints |

The `IVrPoseProvider::sample` sketch in the weapon document is therefore a
conceptual source, not the final consumer API. Sampling must occur once at a
defined frame boundary and publish one immutable `VrInputFrame`. Multiple domain
systems must not independently call a provider and observe different action
edges or poses.

The body/arm solver and semantic hand-pose library are reusable global services.
The weapon domain may contribute wrist anchors, support-grip constraints, and
finger-pose identifiers, but it must not create a second torso or arm model.

### 2.3 Required weapon integration contracts

Weapon implementation must consume or provide these additional contracts:

- every gameplay request carries a monotonic transaction ID, input-frame ID,
  context generation, weapon revision, and expected native revision;
- a speculative mechanical transition is not authoritative until the engine
  adapter accepts the transaction;
- a rejected or duplicated transaction is idempotently ignored or enters a
  bounded reconciliation path;
- each supported weapon supplies, directly or through a companion combat
  profile, primary grip, support region, muzzle axis, sight axis, obstruction
  bounds, handedness constraints, and two-hand policy;
- direct-part interactions use per-hand leases so holding the weapon, holding a
  magazine, and manipulating an action do not collapse into one global reload
  state;
- context loss, focus loss, death, script-driven weapon replacement, and
  reference-space changes invalidate pending commits before new input is armed;
- weapon presentation consumes the same committed mechanics snapshot as sound,
  haptics, and gameplay feedback.

These corrections do not change the weapon document's ammunition invariant or
weapon-family plan. They define the parent services that make those mechanics
safe to integrate with the rest of VR gameplay.

## 3. Current project facts

- The current runtime publishes HMD/view data but has no production controller
  Action pipeline for either OpenXR or OpenVR.
- `head_pose_bridge` performs recentering, world-scale conversion, and tracking
  to H2 coordinate conversion for the HMD only.
- `game::usercmd_s` exposes buttons, view angles, weapon, offhand, and movement
  axes, so a compatibility command adapter is possible.
- The declared `game::playerState_s` exposes movement flags, origin, velocity,
  view height, and delta angles, but most weapon and inventory state remains
  undeclared padding.
- Existing movement code already mediates sprint, stance flags, movement input,
  and action blocking, but is driven by flat-screen command semantics.
- `WeaponDef` exposes models, animation references, weapon family hints, ADS,
  spread, recoil, clip/ammo indices, and reload timing. These are useful inputs,
  not sufficient proof of runtime state or transaction boundaries.
- The current project is focused on the local campaign player. Network
  replication and VR behavior for NPCs are not first-implementation goals.

## 4. Authority model

The design separates observation, intention, settlement, and presentation.

| State | Authoritative owner | Notes |
|:------|:--------------------|:------|
| Runtime poses and raw action values | Active XR/OpenVR backend | Sampled once and normalized before publication |
| Semantic action edges | Global input frame service | Derived once per frame; consumers cannot recreate edges |
| Calibrated body frame and hand occupancy | Global VR gameplay layer | Rebuilt from native player state plus tracking |
| Native player capsule, stance, inventory, ownership, ammo, damage, and scripts | H2 engine | Changed only through verified adapters or native commands |
| Pending interaction intention | Domain interaction system | Speculative and cancelable |
| Committed weapon mechanical partition | Weapon mechanics plus accepted engine transaction | Must reconcile to H2's normalized ammo state |
| Independent weapon trigger, cadence and burst progress | VR weapon simulation | Per instance/module; independent of native akimbo; ammo and damage commit through the engine adapter |
| Offhand fuse, count, projectile, and damage result | H2 engine | VR supplies selection, hold, and bounded release motion |
| Melee damage and target result | H2 engine | VR supplies a validated physical swing observation |
| Hand, item, weapon-part, sound, effect, and haptic presentation | VR presenters | Derived from committed state; never creates gameplay state |

There must be no second writable copy of native inventory, ammo, damage, or
mission state. A VR object may visually represent native state, but its creation
or destruction does not itself mutate that state.

## 5. Canonical input frame

All consumers read a single runtime-independent frame.

```cpp
enum class TrackingQuality
{
    Unavailable,
    OrientationOnly,
    PositionValid,
    Tracked,
};

struct SampledPose
{
    RigidTransform transform;
    Vec3 linearVelocity;
    Vec3 angularVelocity;
    TrackingQuality quality;
    std::uint64_t sampleTime;
};

struct HandFrame
{
    SampledPose grip;
    SampledPose aim;
    std::optional<SampledPose> palm;
    SemanticHandInput input;
};

struct VrInputFrame
{
    SampledPose head;
    HandFrame left;
    HandFrame right;
    GameplayActions actions;
    RuntimeCapabilities capabilities;
    RuntimeFocus focus;
    std::uint64_t frameId;
    std::uint64_t sampleTime;
    std::uint64_t predictedDisplayTime;
    std::uint64_t referenceSpaceGeneration;
};
```

The exact C++ representation may change, but these invariants do not:

- head, hand, action, and focus values in one frame are temporally coherent;
- each pose has explicit validity and freshness rather than relying on a default
  transform;
- action `current`, `pressed`, `released`, and active/bound state are generated
  exactly once;
- hand-bound destructive actions retain `HandId` and independent left/right
  edges. Input publication never collapses simultaneous triggers or grips into a
  single global `fire`/`grab` edge before the active wield/interaction topology
  resolves them;
- consumers can detect duplicate, skipped, stale, and reference-space-incompatible
  frames;
- non-finite transforms, implausible velocities, and invalid quaternions are
  rejected before publication;
- controller models and runtime-specific handles never cross this boundary.

OpenXR implementations should use semantic actions and suggested bindings per
interaction profile. OpenVR implementations should use SteamVR Input Action
Sets and an action manifest. SteamVR requires an absolute manifest path at
runtime; the path must be resolved from the installed executable or manifest
location and must never be a development-machine absolute path in source or
configuration.

Hand tracking and skeletal data are optional capabilities layered on top of the
controller pose/action baseline. Gameplay support must not require a particular
controller model, finger sensor, or vendor extension.

## 6. Spatial frames and generations

The global layer defines these spaces:

1. **Runtime tracking space**: OpenXR/OpenVR coordinates and runtime origin.
2. **Calibrated player space**: tracking space after recentering, handedness, and
   user height calibration.
3. **H2 game space**: native player origin and yaw in H2 coordinates and units.
4. **Body space**: damped torso and shoulder frame used by arms and body slots.
5. **Item-local space**: authored weapon, device, magazine, and interaction
   anchors.
6. **Render prediction space**: optional later pose used only for visual
   prediction or late presentation.

**Decision:** gameplay and collision use the canonical input frame, never an
unrecorded late-latched render pose. Rendering may apply a newer visual-only
correction, but that correction cannot change which shot, grab, or melee swing
was accepted.

World scale and tracking-to-H2 axis conversion are centralized. Domains must not
repeat coordinate conversion. Every calibrated frame carries a
`referenceSpaceGeneration`. Recenter, tracking-origin replacement, map load, or
world-scale changes increment that generation. Pending spatial interactions
from an older generation are canceled or explicitly rebased; they are never
silently continued with mixed coordinates.

## 7. Gameplay contexts and action sets

The active H2 state is normalized into a global context:

```cpp
enum class VrGameplayContext
{
    Frontend,
    Loading,
    Gameplay,
    Paused,
    Menu,
    Cinematic,
    Vehicle,
    Turret,
    Dead,
    Spectator,
};
```

The first implementation may collapse unsupported contexts, but transitions
must be explicit and generation-counted.

| Context | Hands and UI | Locomotion | Weapons and items |
|:--------|:-------------|:-----------|:------------------|
| Gameplay | Direct interaction | Enabled by policy | Native physical or explicit fallback |
| Menu/frontend | UI ray/direct selection | Disabled | Fire and gameplay grabs disarmed |
| Paused | UI only | Disabled | Held presentation may remain; commits suspended |
| Cinematic/loading | Cosmetic or hidden | Native/script owned | Pending interactions canceled; script authority |
| Vehicle/turret | Context profile | Vehicle-specific | Dedicated profile or command fallback |
| Dead/spectator | Release/cosmetic | Native state | Fire, grabs, and pending commits canceled |
| Runtime focus lost | Frozen bounded presentation | No new input | All held actions released and rearm required |

Separate action sets should cover at least gameplay, menu, vehicle/turret, and
debug contexts. Domain code consumes semantic actions such as `fire`, `grab`,
`use`, `move`, `turn`, `jump`, `sprint`, `stance`, `offhand`, `weapon_select`,
and `recenter`; it does not know that an action came from an A button, trigger,
trackpad, or hand gesture.

`fire` and `grab` are hand-addressed semantic actions even when a single-weapon
compatibility adapter later maps them to one native command bit. A control-hand
binding determines which held weapon instance receives a fire edge. With matched
or mixed dual wield, left and right edges remain separate through validation and
transaction submission. No domain may infer a firing side from call order.

On focus or context restoration, destructive actions require a neutral-release
cycle before they can produce a new pressed edge. A trigger held while the
runtime was unfocused must not fire on return.

## 8. Body model, collision, and physical posture

The player has related but distinct roots:

- the **native root** is H2's authoritative movement capsule origin and yaw;
- the **tracking root** maps calibrated room movement into H2 space;
- the **body root** estimates torso orientation and anchors shoulders, body
  slots, and holsters;
- the **head pose** moves inside a bounded physical envelope around the native
  root.

The torso must not copy instantaneous HMD pitch, roll, or every yaw movement.
Body yaw is inferred from locomotion facing, recent head yaw, hand geometry, and
turn actions with damping and configurable recenter behavior. Exact weighting is
pending hardware testing.

Horizontal room-scale lean does not automatically move the native capsule.
Head, hands, held items, and muzzle positions are checked against world and
player bounds. The implementation must prevent or visibly resolve:

- viewing or firing through a wall while the native root remains outside;
- reaching through a door or collision plane to activate an object;
- unlimited room-scale displacement away from the movement capsule;
- a weapon snapping through geometry because only the HMD was collision-tested;
- a forced native stance transition placing the capsule in solid space.

Resolution may use constrained pose projection, weapon lowering, collision
feedback, limited fade, or a recenter prompt. It must not silently teleport the
native player or generate non-finite transforms.

Standing and seated height are calibrated explicitly. Physical head height is
an observation, not by itself an authoritative crouch or prone state.

## 9. Global interaction arbiter

All direct interactions use one hand-occupancy and candidate-selection service.

Representative hand occupancy states are:

- `Free`
- `PrimaryGrip`
- `SupportGrip`
- `HoldingItem`
- `ManipulatingPart`
- `UiInteraction`
- `Unavailable`

An interactable exposes one or more authored anchors, allowed hands, required
actions, positional and angular tolerances, priority, context mask, and an
interaction policy. Candidate scoring combines distance, orientation,
reachability, occupancy, context, and continuity. A grab edge resolves at most
one candidate.

Hover and visual attraction never commit gameplay. Selection creates an
interaction lease containing:

```cpp
struct InteractionLease
{
    InteractionId id;
    HandId hand;
    InteractableId target;
    std::uint64_t inputFrameId;
    std::uint64_t contextGeneration;
    std::uint64_t referenceSpaceGeneration;
    std::uint64_t targetRevision;
};
```

Leases are invalidated by stale tracking, incompatible context changes, target
replacement, ownership loss, reference-space changes, or explicit release. A
lease may control presentation continuously while producing zero or more
idempotent gameplay transactions.

Each hand can hold at most one exclusive control/direct-manipulation lease. A
two-hand weapon uses one control lease plus a support lease that references the
same weapon instance. Dual wield uses two control leases referencing two weapon
instances; neither hand is then available for a magazine, shell, support, or
generic object until its control lease is released through a confirmed stow,
drop, or other explicit transition.

The arbiter owns generic body slots. A slot is a torso-relative affordance that
projects confirmed inventory ownership; it is not an independent inventory
container. Drawing or stowing an owned weapon changes its location and hand
binding. The single-weapon adapter also requests native selection when drawing;
future independent dual wield must not equate every held instance with H2's one
selected weapon. Acquisition/drop changes ownership only through a verified
engine transaction. A preview may be shown while pending, but the committed
location remains unchanged until the required adapter operation succeeds.
Scripted selection always wins and causes a bounded reconciliation.

Left-handed, right-handed, one-controller, seated, and reduced-reach profiles
must use the same semantic contracts. They may change slot layout, dominant
hand, interaction assistance, or fallback actions without changing engine
authority.

### 9.1 Carry capacity and weapon locations

Carry capacity follows occupied locations and explicit weapon eligibility,
within verified native inventory limits. Runtime carry/drop, empty-hand
behavior, and independent firing are implemented by the relevant adapters;
their scenario-specific acceptance remains documented with those features.

| Location | Capacity | Admission |
|:---------|:---------|:----------|
| Left/right hand | One instance per hand | Valid grip/hand profile |
| Left/right waist | One instance per slot | Reviewed pistol/compact-SMG carry profile |
| Normal back slot | One instance | Reviewed back-carry profile; long guns use this body slot |
| Back overflow queue | Bounded by confirmed native inventory | Exceptional external grants only; extraction-only |

Five distinct weapons fit the ordinary positions only when their profiles allow
that arrangement. Two hands gripping one assembly count once; an installed
underbarrel remains part of its assembly. Do not replace the old three-weapon
constant with a new global five-weapon constant. Compact-SMG admission is an
explicit profile decision, including relevant variants, not all native SMGs.
Offhand consumables and scripted/mounted contexts retain separate domain rules.

All carried weapons, including overflow entries, remain natively owned. VR
stores instance locations and hand leases, not a second inventory. Stow, draw,
exchange and hand transfer preserve instance identity, mechanical state and
native ownership; do not implement them with take/give or ammo initialization.
A weapon has one committed location. Pickup/drop alone transfers between the
native inventory and a world entity through a verified adapter. Runtime empty
hands are valid while other weapons remain owned: no automatic equip follows
stow/drop, and ordinary external grants must not seize an occupied or empty hand.
Mission-forced equipment remains an explicit context reconciliation.

### 9.2 Grip release, stow, exchange and hand transfer

Normal holding requires the side grip button. One valid release edge requests
one transition, after resolving any remaining grasp of the same weapon:

| Release target | Result after validation/settlement |
|:---------------|:-----------------------------------|
| Eligible empty body slot | Stow; that hand becomes empty |
| Eligible occupied body slot | Atomically exchange the two instances |
| Body slot with wrong weapon class or blocked placement | Reject; retain the original held weapon |
| No body-slot target, valid world placement | Native drop; that hand becomes empty |
| No body-slot target, invalid world placement | Reject; retain the original held weapon |

Resolve one body-slot target with bounded overlap arbitration/hysteresis. A
rejected stow never falls through to world drop. World obstruction checks must
distinguish solid level geometry from intended torso/holster overlap. Validate
the destination and exchange draw geometry before publishing either location.
Concurrent requests use expected instance/slot revisions: two hands cannot
take the same instance or overwrite the same slot.

Rejected release retains attachment until a new press/release cycle. Movement
into a valid location while the button stays up must not auto-retry. Successful
exchange puts the newly held weapon into the same retained-until-regrip state:
the consumed release cannot drop it or exchange it again. Repress resumes normal
holding; the next release creates a new request. Clear old fire intent and
require neutral/fresh trigger input after control or instance changes. Tracking
loss, focus loss and pause invalidate input; they are never deliberate drops.

Releasing only support leaves the control hand holding the weapon. For a
handguard-supported long gun, releasing control while the other hand maintains
a valid grasp leaves a non-firing carry grasp; a valid control grip is required
to fire again. For pistols and other profiles whose control hand has exclusive
pose authority, the profile's handover rule is automatic: if control
releases and support remains gripped, that hand becomes the new control hand,
using its authored control pose and trigger route. This preserves the same
weapon/mechanics; it is not a drop/pickup. Rebase the remaining grip at the
transfer, invalidate stale muzzle/leases and clear inherited trigger intent.
No handover is inferred from tracking loss. Evaluate both hands from one input
frame; if both release together, produce one final-release request, not a
transient handover. Profiles own handover geometry; the shared arbiter owns
leases and transition order. Unsupported authored handover is reported, not
silently replaced with a foregrip pose. These are runtime roles, not permanent
left/right dominance.

Physical reload requires a free hand. At full ordinary occupancy, confirmed
world placement/drop frees it; a separately designed assistance mode is optional.
Weapon holsters and the existing waist-magazine interaction share hand/candidate
arbitration. A single action cannot acquire both a gun and a magazine.

### 9.3 Back overflow and world lifecycle

The overflow queue is separate from the normal back slot despite sharing a
reach area. An empty hand draws the normal slot first, then queue entries in a
stable order when the normal slot is empty. One accepted grab extracts one
instance; remove it from the queue only when the hand transfer commits. Another
draw requires a free hand and a new grab. Stow/exchange targets only the normal
back slot, never the queue. HMD testing confirmed that an extracted weapon may
enter any eligible ordinary slot; only reinsertion into overflow is forbidden.

Admit exceptional grants from confirmed native ownership changes, preserving
existing placements. Do not re-enqueue held, pending-transfer or extracted
instances by rescanning for weapons without a body slot. Queue reconciliation
removes deleted instances, rejects stale generations and cannot create weapons
or exceed native storage. Normal pickups do not gain unlimited overflow storage.
Unplaceable new external grants may enter overflow regardless of gun class.

Drop must confirm native inventory removal and the resulting world entity
before publishing an empty hand. Preserve the instance's mechanical state across
that entity's lifetime and restore it only on confirmed pickup of that entity.
Match entity generation as well as identity to prevent state leaking to reused
entity numbers or another gun of the same type. Native automatic pickup,
replacement/selection, ground collision, despawn and checkpoint behavior require
verification. Cosmetic dropped-magazine models do not implement weapon drops.
Failure before a committed transfer retains holding; a partial native result
requires reconciliation against actual inventory/entity state, never a fabricated
second copy. Live entities must not lose mechanical records to cosmetic-cache
eviction. Checkpoint persistence remains a separately verified capability.

## 10. Combat policy

### 10.1 Aim and shot origin

Head view, weapon aim, and movement facing are independent values. A weapon shot
request records the held weapon-instance ID, originating hand, selected fire
module, calibrated muzzle transform, sight state, input-frame ID, and weapon
revision. The engine adapter validates readiness and commits the native shot at
a verified boundary.

Independent dual wield uses the VR-owned per-instance shot simulation described
in [independent weapon firing](vr-independent-weapon-fire.md). Native akimbo,
dual-wield attachments and rapid native selection changes cannot implement it.
The engine remains the inventory/ammo and ballistic/damage authority through
verified adapters; no selected-weapon or akimbo timer is shared by the two VR
instances. A declaration of this architecture is not proof of native execution.

For native VR-supported firearms:

- shot direction follows the calibrated bore/muzzle axis, not HMD forward;
- near-field collision starts at the physical muzzle;
- weapon geometry or a bounded muzzle sweep detects obstruction before firing;
- a muzzle behind collision cannot damage a target merely because the HMD has a
  clear line;
- engine-specific camera-origin or bullet-trace behavior must be probed before
  replacement; no offsets are guessed from other Call of Duty versions.

If a weapon or scripted context cannot support a safe native shot path, it uses
an explicit command fallback and presents that limitation consistently.

### 10.2 ADS and sights

ADS is divided into independent concepts:

- **sight alignment**: geometric relation among dominant eye, rear sight, front
  sight, and weapon axis;
- **weapon readiness**: whether current mechanics and posture permit accurate
  fire;
- **native ADS compatibility**: optional engine state used for weapon mode,
  scripts, reticles, or timing;
- **optic rendering**: magnified or special-scope presentation.

Sight alignment uses profile thresholds, dwell, and enter/exit hysteresis. It
must not move, rotate, or forcibly zoom the HMD camera. Native ADS state may be
driven internally where required, but its camera FOV, blur, viewmodel centering,
and view-kick behavior are separately suppressed or redirected.

Magnified optics require an independent optic-rendering design. Until that path
exists, affected weapons use a deliberate non-magnified or compatibility
fallback rather than distorting the HMD view.

### 10.3 Spread, stability, and recoil

**Decision:** flat-screen hip-fire spread is not directly preserved for
native-aimed weapons.

The initial implementation is the saved `vr_disableHipFireSpread` command
(default `1`). Accepted controller-muzzle bullet shots use the native weapon's
attachment/alternate-aware ADS baseline instead of its stance/hip-fire/bloom
cone. `0` restores native spread on the next shot. This preserves intrinsic
dispersion and shotgun pellet generation; it does not implement the remaining
recoil, stability or readiness policies below. See the
[native firing boundary](vr-controller-interaction.md#holding-authority-and-firing)
for hook guards, diagnostics and hardware verification status.

The initial policy separates:

- intrinsic ballistic dispersion defined by weapon/projectile type;
- shotgun pellet patterns or weapon-specific intentional spread;
- visible physical instability represented by muzzle movement;
- recoil impulses represented by weapon rotation/translation and haptics;
- gameplay readiness penalties from sprinting, jumping, unsupported firing, or
  awkward posture.

Standing, crouching, prone, movement, and ADS should not secretly redirect a
well-aligned barrel through a large random screen-space cone. Those states may
change visible sway, recoil magnitude, recovery, weapon-ready delay, support-hand
benefit, and intrinsic dispersion within profile limits. Exact formulas and
exceptions require weapon-family playtests.

Native view kick must not rotate the tracked head camera. Recoil is applied to
the weapon presentation and subsequent muzzle direction, with committed-shot
haptics. Stock recoil, sound, notetracks, and effects may be reused only after
their authority and camera side effects are separated.

Aim assist and crosshair assistance are disabled by default for native muzzle
aim. The saved `vr_aimAssistStrength` setting implements explicitly disclosed
bullet-direction assistance: 0 is off, 100 permits 10 degrees from the barrel,
with a linear mapping. It uses the native enemy AI set and target points in a
bounded, per-shot 3D cone and cover check, preserving the native shot pipeline.
The launcher and console share the setting. See the
[firing boundary](vr-controller-interaction.md#holding-authority-and-firing)
for range, scope and diagnostics. Other accessibility options may provide a
laser, projected reticle or increased interaction tolerances. Assistance must
be visible or disclosed and must not silently disagree with the muzzle.

### 10.4 Weapon-domain boundary

The weapon child architecture owns feed, chamber, action, magazines, weapon-part
manipulation, and weapon-family profiles. The global combat layer consumes:

- committed readiness and weapon revision;
- calibrated muzzle and sight geometry;
- dominant/support hand relationship;
- obstruction bounds;
- accepted shot, reload, cycle, and switch results.

It supplies context, canonical poses/actions, body transforms, hand leases,
native player state, and combat policy. Neither layer may mutate the other's
state directly.

## 11. Offhand, tactical items, use, and melee

### 11.1 Offhand and tactical items

Body slots expose native offhand categories and counts. Grabbing a projected
item requests native offhand selection or preparation. The VR layer controls
held presentation and collects a bounded release-motion history; H2 remains
authoritative for inventory count, fuse, projectile creation, damage, and
scripted restrictions.

Release velocity uses timestamped valid samples, outlier rejection, bounded
linear/angular speed, and a minimum history window. A single tracking jump must
not launch an item across the map. Focus loss, death, context replacement, or
invalid tracking cancels or safely resolves the token without duplicating an
item.

Weapon-domain magazines are not generic offhand items. They retain the pooled
transaction semantics defined by the weapon document.

### 11.2 Generic use

Native use prompts may be activated through direct hand proximity, a bounded
hand ray, or an accessibility action. Direct use requires line-of-sight and
reach checks from the calibrated player envelope. Unsupported scripted prompts
fall back to the native use command.

### 11.3 Melee

Melee is based on a swept hand/weapon volume over coherent input frames, not a
single velocity threshold. Validation considers:

- tracked sample freshness and continuity;
- minimum useful path length and directional intent;
- bounded linear and angular velocity;
- active weapon or hand melee profile;
- native cooldown, current context, and player state;
- obstruction between the native player envelope and the target;
- one accepted hit per swing/contact policy.

H2 remains authoritative for target validation, hit result, damage, effects, and
mission logic. Oscillation, input bounce, repeated overlap frames, and tracking
teleports cannot create repeated hits.

## 12. Locomotion, turning, sprint, and stance

The global input layer produces normalized movement and turn intentions. A
locomotion policy selects head-relative, hand-relative, or body-relative movement
without changing the semantic action binding. Smooth and snap turning are
presentation/input policies that ultimately request native yaw changes.

H2 remains authoritative for collision, velocity, ladders, mantle, gravity,
ground state, sprint, crouch, and prone. Existing movement extensions should be
adapted through a VR movement adapter rather than bypassed by a second movement
implementation.

### 12.1 Sprint and weapon readiness

Sprint remains a native movement state. VR presentation lowers or repositions
the weapon and may require a bounded ready transition before firing. Sprint does
not need a hidden random spread increase if the weapon is visibly unready or
unstable.

### 12.2 Crouch

Physical crouch recognition uses calibrated standing/seated height, hysteresis,
minimum dwell, and explicit enablement. It produces a crouch request; the native
stance changes only after H2 confirms capsule clearance and state validity.

Picking up an item, leaning, briefly ducking the head, or losing positional
tracking must not accidentally toggle crouch. An explicit action remains
available for seated players and accessibility profiles.

### 12.3 Prone

Prone requires explicit intent and native validation. Physical height may be an
additional confirmation but is not sufficient by itself. The system must not
force a room-scale player prone because the HMD was set down or tracking origin
changed.

### 12.4 Accuracy effects

Native stance may still influence AI visibility, collision, movement, scripted
checks, and weapon-ready policy. For native muzzle aiming, stance accuracy
benefits are expressed primarily through visible stability, recoil recovery,
support-hand effectiveness, and weapon-family intrinsic dispersion rather than
an unrelated screen-space spread multiplier.

## 13. Presentation, UI, feedback, and accessibility

Presentation consumes immutable committed snapshots. It may predict or smooth
visual transforms but cannot manufacture gameplay edges.

- arms and hands use the global body/IK and semantic pose services;
- domain presenters provide item-local anchors and activity transforms;
- sound, effects, and haptics observe accepted transactions;
- a rejected interaction may provide a distinct bounded feedback event but
  never the success feedback;
- runtime focus, tracking quality, blocked muzzle, invalid target, and fallback
  mode must be diagnosable without relying on a desktop-only console.

Menu contexts use UI actions and hand rays/direct interaction without leaving
fire or grab actions armed. HUD information such as ammo and inventory remains a
projection of native state. Wrist, weapon-mounted, or world-space HUD designs
must support both light and dark environments, high DPI, and adjustable scale.

Accessibility is part of the architecture rather than an afterthought. Profiles
may configure dominant hand, one-handed operation, seated height, reduced reach,
grab toggle/hold, physical crouch, turn mode, movement reference, interaction
assistance, reload fallback, laser/reticle assistance, and haptic strength.

## 14. Transactions and engine adapters

All gameplay mutation follows an intention/settlement protocol:

```cpp
struct GameplayTransaction
{
    TransactionId id;
    Domain domain;
    Intent payload;
    std::uint64_t inputFrameId;
    std::uint64_t contextGeneration;
    std::uint64_t referenceSpaceGeneration;
    std::uint64_t expectedNativeRevision;
    std::uint64_t expectedDomainRevision;
};

enum class TransactionResult
{
    Accepted,
    Rejected,
    Duplicate,
    ContextChanged,
    ExternalMutation,
    Unsupported,
};
```

Adapters execute only on verified engine boundaries and on the appropriate
engine thread. Replaying the same transaction ID cannot apply the mutation
twice. A result publishes a new native observation and domain revision; domain
presentation then converges on that committed snapshot.

Command-bit injection is a supported compatibility adapter for actions already
modeled safely by H2. It is not automatically the final adapter for physical
mechanics whose commit timing would remain controlled by a stock animation.

Every hook or direct field access requires:

- executable fingerprint and target validation;
- read-only evidence of the field or transition meaning;
- range and consistency checks;
- a fail-closed path that preserves original gameplay;
- bounded diagnostics identifying the rejected contract;
- tests for interruption, duplicate calls, and external mutation.

## 15. Failure and reconciliation policy

### Tracking or controller loss

- stop generating new destructive action edges;
- release held trigger/use action values at the semantic boundary;
- suspend spatial commits and invalidate direct manipulation leases;
- keep a bounded cosmetic fallback pose where appropriate;
- preserve native inventory ownership;
- require neutral input before rearming after reconnect.

Loss of a support hand may degrade to one-handed weapon handling. Loss of the
primary hand freezes or safely lowers native weapon interaction until fallback
input is explicitly armed.

### Recenter or origin change

- increment `referenceSpaceGeneration`;
- invalidate hover and pending spatial transactions;
- rebuild body and slot transforms;
- rebase only interactions with an explicit safe policy;
- never compute velocities across the discontinuity.

### Native external mutation

Death, checkpoint restore, script grant/removal, forced weapon switch, cutscene,
map transition, and native ammo changes invalidate affected expected revisions.
Pending transactions are rejected, ephemeral tokens are removed or rebuilt, and
the domain reconciles from a fresh native observation.

### Invalid or extreme data

Non-finite transforms, stale samples, impossible velocities, missing profile
anchors, invalid bones, unsupported weapon states, excessive candidate counts,
and over-capacity event queues fail closed. No failure may block the renderer,
game thread, or runtime worker while waiting for another subsystem.

## 16. Threading and performance contract

The runtime/backend samples and publishes a fixed-size immutable input frame.
The gameplay thread consumes the newest complete frame and produces bounded
intentions and transactions. The renderer consumes presentation snapshots. XR
calls, engine mutation, and rendering remain on their required owner threads.

Required properties:

- no consumer calls into an XR backend for a fresher pose;
- no renderer/gameplay/runtime circular waits;
- no unbounded per-frame allocation or candidate search;
- fixed or capped transaction, haptic, trace, and diagnostic queues;
- stale frames and full queues have explicit drop/coalescing policies;
- expensive model/profile discovery occurs at load or cache construction, not
  in per-frame interaction loops;
- haptic output is queued by semantic event and coalesced before backend calls;
- synthetic and recorded providers exercise the same public interfaces as
  hardware providers.

The common per-frame path should be proportional to the fixed action set, two
hands, currently active leases, and nearby bounded candidates rather than total
weapons, map entities, or historical samples.

## 17. Testing and acceptance

### 17.1 Deterministic automated tests

Automated tests must cover:

- action-edge generation across duplicate, skipped, and stale frames;
- context and focus transitions with held destructive actions;
- recenter and reference-generation discontinuities;
- body-frame reconstruction with extreme but finite poses;
- candidate arbitration, hand crossing, simultaneous grabs, and lease expiry;
- release rejection/regrip, occupied-slot exchange without release replay,
  pistol handover versus long-gun carry, and simultaneous final-hand release;
- stable extraction-only overflow, ordinary-slot storage after extraction,
  grant/removal races and empty hands with nonempty native inventory;
- idempotent accepted, rejected, duplicate, and externally invalidated
  transactions;
- weapon ammunition/mechanical invariants from the child architecture;
- offhand release-history filtering and velocity bounds;
- melee sweep deduplication and obstruction;
- seated/standing calibration and crouch/prone hysteresis;
- script-forced selection, death, checkpoint, and map-transition reconciliation;
- unsupported domain/profile fallbacks;
- randomized long event sequences with invariants checked after every commit.

### 17.2 In-game read-only probes

Before gameplay writes, capture bounded evidence for:

- native context and focus transitions;
- current command, player movement, weapon, offhand, stance, and script state;
- exact shot, switch, offhand, melee, use, and stance commit boundaries;
- camera, ADS, spread, recoil, and aim-assist call chains;
- interruption by sprint, mantle, ladder, vehicle, turret, cinematic, death, and
  checkpoint restore.

Probes record executable identity, monotonic IDs, thread identity, input frame,
native values, and detected discontinuities. They must not mutate gameplay.

### 17.3 HMD acceptance gates

Hardware validation remains mandatory for:

- grip/aim offsets and left/right controller profiles;
- torso inference, shoulder comfort, reach, and hand crossing;
- body-slot placement and seated/reduced-reach profiles;
- sight alignment thresholds and optic usability;
- weapon obstruction, near-field occlusion, and room-scale collision feedback;
- recoil, sway, throwing, melee, and haptic tuning;
- physical crouch/prone intent and false-positive resistance;
- focus loss, tracking loss, reconnect, and recenter behavior;
- UI readability across headset resolution, high DPI, and light/dark scenes.

Desktop and recorded tests may reject broken behavior but cannot accept headset
ergonomics.

## 18. Implementation phases

### Phase 0: global contracts and provenance

- Freeze `VrInputFrame`, spatial generations, gameplay contexts, transaction
  envelopes, authority boundaries, and diagnostic schemas.
- Build read-only probes for native context, input commands, player movement,
  weapon state, ADS/spread/recoil, offhand, melee, and stance transitions.
- Record representative gameplay, interruption, and script-driven scenarios.

Exit condition: required engine observations and transaction boundaries are
repeatable for the supported executable without gameplay writes.

### Phase 1: input, context, and replay

- Implement OpenXR semantic actions and controller spaces.
- Implement SteamVR Input actions and executable-relative manifest resolution.
- Publish coherent immutable input frames with validity, velocity, focus, and
  reference-space generation.
- Implement desktop synthetic and recorded providers against the same frame
  contract.
- Add context action-set switching and neutral rearm behavior.

Exit condition: hardware and replay providers produce equivalent semantic
frames, and context/focus tests cannot generate accidental destructive edges.

### Phase 2: body, hands, collision, and generic interaction

- Implement calibrated player/body frames, torso inference, global arm IK, and
  semantic hand poses.
- Implement hand occupancy, bounded candidate arbitration, interaction leases,
  and torso-relative body slots.
- Add head/hand/item reach and obstruction diagnostics before gameplay mutation.

Exit condition: synthetic and HMD input can select, hold, release, cancel, and
recenter generic interactables deterministically without engine inventory writes.

### Phase 3: first weapon and combat vertical slice

- Follow the weapon child architecture's evidence, mechanics, and transaction
  phases for one closed-bolt pistol.
- Add calibrated muzzle aim, obstruction, geometric sight state, redirected
  recoil, and initial spread policy.
- Keep unsupported weapons on an explicit native command fallback.

Exit condition: one weapon fires and reloads through accepted native commits;
the shot follows the physical muzzle; HMD orientation is never driven by recoil;
tracking/context failure cannot duplicate ammo or fire.

### Phase 4: two-hand weapons and movement integration

- Add rifle/SMG support grip and two-hand solving.
- Integrate movement direction, turning, sprint readiness, and explicit native
  stance requests with existing movement code.
- Validate physical crouch as an optional intent source.

Exit condition: one- and two-handed representative weapons remain stable across
movement, sprint, stance, recenter, and support-hand loss.

### Phase 5: offhand, melee, use, and context expansion

- Add body-slot offhand projection, priming, bounded throw motion, and native
  projectile commits.
- Add swept melee intents and native hit commits.
- Add direct/ray use with native fallback.
- Add explicit vehicle, turret, cinematic, death, and checkpoint policies.

Exit condition: domain interactions share one arbiter and context model, recover
from interruptions, and preserve native inventory/damage authority.

### Phase 6: weapon families, optics, UI, and accessibility

- Expand weapon families according to the child architecture.
- Complete left-control, two-hand support and profile-controlled handover.
- Implement independent same/mixed-weapon firing through the dedicated shot
  adapter; native akimbo is not a prerequisite or implementation backend.
- Validate sections 9.1-9.3: typed waist/back slots, release/exchange/regrip,
  extraction-only overflow, empty hands and native world placement/pickup.
- Implement magnified/special optic presentation.
- Replace incompatible flat HUD elements with tested VR projections where
  needed.
- Complete left-handed, one-controller, seated, reduced-reach, interaction
  assistance, and comfort profiles.

Each feature requires deterministic tests, read-only or transactional engine
evidence, and a recorded HMD acceptance result.

## 19. Open decisions

The following remain unresolved until evidence or playtesting exists:

- body-yaw inference and physical-to-native root recenter policy;
- default movement reference, turn modes, and comfort settings;
- default physical crouch behavior and prone confirmation;
- exact weapon-family intrinsic dispersion and stability formulas;
- aim-assistance accessibility levels and competitive implications;
- body-slot geometry, dominant-hand switching, and one-controller mappings;
  carry/release/overflow rules are decided in sections 9.1-9.3;
- native inventory limits, empty-hand admission, world placement/drop and
  recovery, and checkpoint/scripted-loadout reconciliation;
- muzzle/body collision presentation when constrained poses diverge from tracked
  controllers;
- exact engine hooks for muzzle-directed shots, recoil redirection, offhand
  release, melee, and stance transactions;
- which vehicles, turrets, scripted weapons, and cinematics receive native VR
  interaction versus command fallback;
- whether any later realism mode adds persistent magazines, plus-one chambers,
  dropped-item persistence, or inventory-format changes;
- multiplayer replication and remote-avatar representation, which are outside
  the initial local campaign scope.

Changes to decisions must record evidence, affected invariants, migration impact,
and child-domain consequences rather than silently changing behavior.

## 20. External architectural references

These references support input and interaction primitives. They do not replace
H2-specific runtime evidence or the authority decisions in this document.

- OpenXR Action Sets: context-dependent semantic input:
  <https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrActionSet.html>
- OpenXR suggested interaction-profile bindings:
  <https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrSuggestInteractionProfileBindings.html>
- SteamVR Input: action manifests, action sets, edge state, pose actions, and
  haptics: <https://github.com/ValveSoftware/openvr/wiki/SteamVR-Input>
- Meta Hand Grab Interactions: interactors, authored wrist-relative poses,
  candidate scoring, constrained transforms, and controller-driven hands:
  <https://developers.meta.com/horizon/documentation/unity/unity-isdk-hand-grab-interaction/>
- SteamVR Skeletal Input: optional controller skeletal pose layering:
  <https://github.com/ValveSoftware/openvr/wiki/SteamVR-Skeletal-Input>
- Unreal Engine Two Bone IK: end-effector and joint-target arm solving:
  <https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-blueprint-two-bone-ik-in-unreal-engine>
