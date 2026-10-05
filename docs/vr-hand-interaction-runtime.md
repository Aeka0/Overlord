# Unified hand interaction and empty-hand poses

This guide describes the current unified hand-interaction implementation and
empty-hand poses. The shared coordinator owns input intents and hand-session
composition; see [hand-interaction architecture](vr-hand-interaction-redesign.md)
for the design contract.

## Implemented responsibilities

| Entry point | Responsibility |
| --- | --- |
| `gameplay/hand_interaction/core.hpp` | Unified button history, target identity, grasp sessions, shared-grasp permissions, bounded candidate ranking, and rejection records |
| `interaction_schedule.hpp`, `interaction_coordinator.cpp` | Named phase order and one checked callback inventory; the sole H2 server adapter sequences fresh, suspended and repeated-input paths |
| `hand_interaction/runtime.cpp` | Current-frame authorization, confirmation of actual transaction outcomes and session publication; no domain dispatch |
| `weapon_carry_runtime.cpp`, `carry_interaction.hpp` | Native inventory/carry adapter, tracked-frame admission, carry candidates, grip/pickup commits and presentation publication |
| `hand_service.*` | Common hand binding and presentation dispatch, with one knife-state snapshot for wrist and finger composition |
| `physical_reload_contact_sample.hpp`, `hand_interaction/mechanical_contacts.hpp` | Recompute reload contact from immutable bindings and current tracking, without relying on the previous render input to decide a current button press |
| `hand_interaction/access.hpp` | Explicitly grants part or ammunition operations and passes centrally confirmed press/release events, preventing a rejected action from becoming another action |
| `hand_interaction/constraints.hpp` | Shared constraints for M203 auxiliary orientation and one-dimensional slide travel |
| `hand_interaction/pose_plan.hpp` | Presentation owner, composed pose, and support-aim capability for each hand; empty-hand presentation follows the entity relationship |
| `empty_hand_pose.hpp` | Finger poses without an item, time-based smoothing, and continuity after releasing an entity |

Existing ammunition conservation, native compare-and-write, mechanical action cores, weapon fire cadence, model bindings, and hand IK remain. Forced native inventory changes can still rebuild holding relationships; new controller acquisitions must pass unified authorization. The core adds no second writable ammunition ledger.

The four mechanical runtimes no longer register separate grasp ticks. Production calls use central button intents; local gating for the original controller remains for isolated mechanical tests and protection during continuous operations.

Post-commit support handoffs are owned by their domains: physical reload handles
the supported-pistol magazine catch, and underbarrel handles secondary-module
grasp/release. They commit through the carry adapter after arbitration; they do
not grant a hand by querying another interaction provider. Equipment settles its
knife grants from a typed frame/input/edge boundary. The coordinator sequences
these stages and applies shared exclusion masks without reading domain internals.

The module boundaries are documented in the
[current architecture boundaries](vr-gameplay-interaction-architecture.md#current-implementation-boundaries).

## Interaction behavior

### Drawing ammunition at the waist

An empty hand holds Grip and newly presses Trigger to select the current weapon's underbarrel ammunition. Selection does not depend on candidate enumeration order or the most recent render frame. Missing secondary ammunition or a rejected transaction does not draw a primary magazine instead. Walking into the waist area with Trigger already held does not create a new draw.

A scene-script notification without a target does not establish physical hand occupancy. Real scene targets still occupy the hand and retain native use restrictions; a short press continues to produce paired activate/release notifications.

### M203 and GP-25

The M203 front support position and chamber tube use one session. Holding it preserves support aiming; an empty or unlocked chamber allows slide movement, and closing the chamber keeps the grasp. An empty chamber does not require a preliminary dry trigger pull.

The M203 solver uses raw two-hand directions and spacing, the bound axis, and acquisition-origin compensation. It does not read the hand already moved by this frame's IK. Whole-weapon rotation and constant-radius turning do not create slide motion. Degenerate geometry and tracking jumps reject mechanical advancement.

The rear firing grip retains its own orientation and range requirements. GP-25 continues firing through its ordinary support grip. The underbarrel shotgun retains a separate tolerance for holding during pumping; its initial acquisition and firing ranges are not widened.

M203/GP-25 in-hand projectile transforms now include the actual mesh child node and use the corresponding source-frame hand shape: frame 38 for M203 and frame 24 for GP-25. Source code has no runtime dependency on the export directory. Measurement regressions verify mesh centers; actual fit still needs headset inspection.

### Tactical knife and shared grasps

Verified knife + magazine/slide combinations are admitted through explicit recipes. Grip maintains the knife and Trigger maintains the pinch; releasing either ends only its own relationship. If the magazine remains held after returning the knife, it retains this composition's wrist reference and hand shape without waiting for a render republication.

A hand currently reloading pauses knife-blade melee and rebuilds motion history when melee resumes. Empty-fist melee permission is separate from knife acquisition, preserving the existing Grip melee semantics.

## Quest 3 default empty-hand poses

| Current buttons | Hand shape |
| --- | --- |
| Grip and Trigger released | Natural open hand |
| Grip only | Pointing, with index finger extended and other fingers curled |
| Trigger only | Thumb/index pinch, with other fingers relaxed |
| Grip + Trigger | Fist |

Empty-hand gestures use the digital Grip/Trigger buttons. Quest/Touch also publishes optional capacitive trigger contact for [held-weapon trigger discipline](vr-trigger-discipline.md); contact does not change these four empty-hand gestures.

Fingers interpolate continuously with a 45 ms half-life, reaching about 90% of the change between stable empty-hand states in 150 ms. Reverse transitions continue from current progress; stereo eyes or repeated draws of the same input do not advance the transition twice. Tracking interruption and recentering recalibrate animation time, preventing an invisible interval from completing the transition instantly.

Entity poses always take precedence. The controller records the final finger pose of the actually visible hand and transitions from it after entity release; hidden dual-wield DObj arms do not contribute. Default gestures do not change wrist facing, grasp state, input consumption, or ammunition, and do not depend on the `vr_physicalMelee` switch. When physical carry is disabled, native rendering of primary/secondary hands is still protected by the entity occupancy mask.

## Lifecycle and first acquisition

- On duplicate XR frames, the server only performs native coordination; it neither clears input history nor acquires again.
- Pause or invalid tracking still runs necessary mechanical settlement. If a refund compare-and-write fails, the ledger is retained while other instances continue processing.
- Script takeover preserves existing feed/ammunition custody and clears transient actions; on recovery, it rearms from the new context.
- A level timeline change invalidates old identities before processing, preventing refunds into new instances. Disabling carry/VR performs native normalization.
- After initializing a mechanical instance for the first time, contact is recomputed with the same frozen input frame and real generation. Candidate previews reuse native definition, identity, and capacity validation; arbitrary generation 0 is not admitted.
- If a release and a new press occur within one sample, end the old pinch first. A final `down=true` must not hide the release.

## Diagnostics

`vr_hand_interaction_status` prints current sessions and the most recent candidate decision and writes `minidumps/h2-mod-vr-hand-interaction.txt`. Records include physical hand, input event, weapon-instance generation, binding, purpose, distance, rejection reason, and blocking session. Existing domain status commands continue to provide mechanical/ammunition details.

Candidate authorization is reserved only within the current batch. If the actual provider does not enter the corresponding state, mark a domain rejection rather than publishing a ghost grasp to rendering. The core holds eight sessions and 64 candidates; overflow rejects new requests while preserving existing relationships.

## Validation and acceptance scope

Regression covers ten independent groups: unified interaction, empty-hand poses, underbarrel, physical reload, revolver, weapon grip, controller input, melee, hand poses, and hand skeleton, plus a full client build.

Added coverage includes randomized candidate order, same-hand composition and independent release, no replay of rejected input, first admission, rejected refund retry, current-frame contact rebuild, separation of M203 rotation and slide motion, source-mesh in-hand position, and empty-hand pose changes/entity-release transitions at 60/72/90/120/144 Hz.

Pure state tests were not treated as headset acceptance. The next pass needs physical confirmation of:

1. First ammunition draw on the M4, airport M4, and SCAR M203; common grip, aiming, and sliding when loaded/empty/open; and continued grasp after reloading.
2. Full GP-25 and underbarrel shotgun cycles, and actual projectile fit in both hands.
3. Forward/reverse knife grip with supported pistol magazines/slides, both release orders, pause/cutscene/weapon changes.
4. All four Quest 3 empty-hand poses, rapid alternation, entity release, hidden dual-wield hands, and tracking recovery.

Gesture recognition, more shared-grasp recipes, and tactical-equipment fuses/throwing remain later features. This pass provides an integration model without inventing those native behaviors.
