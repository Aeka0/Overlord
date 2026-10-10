# Underbarrel VR interaction

The underbarrel adapter adds physical firing and reload interaction for
explicitly admitted launcher and shotgun assemblies. The host firearm retains
its control hand, primary ammunition, and native firing path. The secondary
module owns its ammunition feed, mode, projectile, and moving parts. See
[native module contracts](vr-underbarrel-native-contracts.md) for identity and
native feed rules.

## Input and ownership

The control hand aims and fires the host. A support hand may retain its normal
foregrip role or acquire the admitted module's firing contact. Firing a secondary
module requires its current mode, module identity, hand lease, and valid tracking
generation. One Trigger edge produces at most one request; it cannot fire both
the host and module.

The module firing grip may be acquired with the same hand's Grip and Trigger
chord. A squeeze alone does not create a support lease. Existing support,
magazine, action, and module leases remain exclusive. Arbitration reserves a
new secondary acquisition against conflicting primary-part contacts regardless
of whether input or rendering evaluates first. A blocked edge cannot replay
later while Trigger remains held.

Releasing the host's control grip while the other hand continues holding the
module retains that module hand and its firing/support/action grasp. It carries
the weapon without firing or advancing a reload action. Regrasping the host
control rebases the existing stroke and consumes that transition sample; it
does not release the module hand or replay a Trigger press. Retention requires
the same physical weapon, assembly, tracking reference and uninterrupted module
Grip press. Releasing that hand, losing tracking/focus or changing the input
generation still ends the grasp. This policy is shared by all admitted M203,
GP-25 and underbarrel-shotgun assemblies, including retained partial action
travel on an open launcher or shotgun.

Carry-release preparation checks the released-hand mask before clearing module
state. Releasing only the host control preserves the validated module Grip;
releasing the module hand or both hands still interrupts the grasp and settles
any held round. This check runs before inventory ownership changes, as well as
the subsequent carry-only and control-regrasp checks.

| Mechanism | Registered host families | Grip during host release/regrasp | Action measurement |
| --- | --- | --- | --- |
| M203 | M4, M16, SCAR-H, ACR | Retain the same firing, support or action grasp | Shared hand-span slider |
| GP-25 | AK-47 | Retain the same support/firing grasp | No sliding or pump action |
| Underbarrel shotgun | AK-47, SCAR-H, FAL | Retain the same firing, support or pump grasp | Separate rear-hand pump frame |

Admitted cosmetic and optic variants inherit their mechanism's policy. Native
host/module identity and attachment checks still determine admission.

The body-supply modifier uses the existing belt interaction and escrow rules.
The secondary module receives the round only after the host, module, hand, and
native capacity have been validated. Reserve changes do not convert to loaded
ammunition implicitly. The primary feed remains unchanged when the module fires
or reloads.

`vr_smartAmmoSelection` is a saved toggle under Gameplay > Weapon interaction,
enabled by default, including profiles without an explicit value. On a fresh
waist Trigger press, an absent primary magazine or zero rounds across its
magazine and chamber takes priority. Otherwise the default supply is:

| Module | Prefer secondary ammunition when |
| --- | --- |
| M203 | Empty, open, and actual slider travel reaches the existing 90% opening threshold |
| GP-25 | Empty |
| Underbarrel shotgun | Fewer than three shells in its tube, excluding the chamber; four rounds total with a chambered shell |

Secondary ammunition also requires available reserves and validated module state.
Unavailable primary-feed state conservatively keeps primary priority. Holding
Grip chooses the other supply. While Trigger remains held at the waist, a fresh
Grip press or release exchanges the current item through the existing atomic
reserve transfer. Mechanical or setting changes alone never retarget held
ammunition. Turning the option off restores Trigger for primary and Grip +
Trigger for secondary, including the original directional Grip exchange.

## Physical actions

### M203

The firing hand acquires the reviewed M203 grip. A separate hand opens the
forward slider, obtains a grenade from the admitted supply point, inserts it,
and closes the action. An empty action may be opened without a shot or dry fire.
Grasping an open firing contact is valid, but open or moving state blocks
firing. A completed close retains support while Grip remains held, allowing a
new opening stroke without reacquiring the hand. The native M203 owns shot
acceptance and grenade behavior.

### Underbarrel shotgun

The adapter presents the shell feed as a per-round transaction and retains the
native four-shell capacity and eight-pellet shot definition. The pump has its
own rearward and forward stages; pump travel does not spend a second shell.
Empty and nonempty reloads retain their distinct native stages. Each shell
commit occurs once at the approved feed boundary, and the final end animation
may continue after the tube is full.

### GP-25

GP-25 retains its separate support and reload contract. It does not inherit the
M203 slider or underbarrel-shotgun pump behavior solely because its model has
similarly named grenade parts.

## Pose and presentation

Contact admission is based on the current tracking frame, calibrated controller
axes in the weapon frame, the selected hand role, and the authored host/module
geometry. Visual wrist offsets do not change physical orientation tests. A
support pose cannot steal a module firing contact, and an open action cannot
fire because a render pose appears closed.

Moving-part presentation consumes the latest admitted stroke state and current
render-frame controller pose. Support wrist, constrained hand, and moving part
use the same evaluated presentation, preventing visible stepping between server
updates. Render workers never spend ammunition, emit projectiles, or complete a
reload. Release, stale tracking, focus loss, host-generation changes, invalid
reference space, or an assembly change stops projected motion.

Muzzle candidates come from the admitted rigid barrel geometry, excluding
grenade, trigger, and sight groups. A GP-25 sight is not a barrel endpoint.
Shotgun sound feedback uses the module's native aliases; M203 and GP-25 retain
their own native notetrack mappings. Sound and haptic delivery are presentation
events and cannot authorize a shot or change ammunition.

All three mechanisms use the current visible weapon pose for muzzle origin,
firing direction, loading contacts and acquisition of a new grasp. Only an
already held shotgun pump uses the separate rear-hand frame for stroke and
positional retention. These two measurements are sampled independently, so a
pump-to-firing-grip transition cannot inherit a shot direction from the pump
frame. M203 retains its shared hand-span measurement; GP-25 has no moving action.

## Admission, state, and diagnostics

Admission checks the native host and secondary definition, alternate mode,
attachment topology, model parents, muzzle, ammo keys, capacities, and supported
feed type. Similar names or a loaded model do not admit an unreviewed assembly.
Duplicate copies of one host and concurrent hosts sharing the same secondary
loaded cell remain unsupported until native instance ownership is explicit.

All VM access and native commits stay on their validated game-thread boundary.
Input and render consumers receive bounded value snapshots. Invalid identity,
ambiguous ownership, stale input, or an out-of-range native count fails closed
and reports a reason.

`vr_underbarrel_status` reports host/module identity, mode, ammunition, held
round, action state, travel, grasp, and fault status. It does not force admission,
change ammunition, or clear a fault. The underbarrel regression suite covers
both hands, all three mechanisms, identity rejection, transaction limits,
escrow conservation, input arbitration, attachment topology, and stale-frame
handling.

## Acceptance boundaries

Headset acceptance should verify each admitted host and mode, firing and reload
with both primary hands, Grip-modifier changes, rolled weapons, partial action
travel, one-hand tracking loss, pause, focus changes, host switching, and
checkpoint restoration. Check primary ammunition while firing and reloading the
module, visible slider/pump travel, contact reach, sound, and haptics separately.
Offline tests, native signature checks, and client builds do not establish
end-to-end headset behavior.
