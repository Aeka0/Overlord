# VR weapon carry: headset test candidate

Weapon ownership and ammunition remain in the native inventory. The VR carry
state assigns each owned instance to a hand, an ordinary body slot, or the
extraction-only back overflow queue. Stowing, drawing and exchanging do not
give/take a weapon or reinitialize its ammunition.

Acquiring a different native viewmodel now uses `switchtoweaponimmediate` through
the existing isolated server script-call adapter. The native method owns the
selection resets and notifications, avoiding ordinary lower/raise animation
delays. A same-weapon grip transfer does not restart its action. Empty selection
retains the existing zero-weapon path. A live native trace showed that the method
still scheduled an 850 ms raise in both simulation and prediction. The guarded
finish-change adapter now expires only that completed physical acquisition's
raise timer, after native selection/events run and ownership is revalidated.
It leaves native state completion to the next tick and does not reset shooting
or reload timers. `vr_carry_selection_status` records the original raise durations
and roles in `minidumps/h2-mod-vr-carry-selection.txt`. Immediate arrival remains
a headset acceptance check for this candidate.

## Interaction

- Hold the side grip to carry a weapon. A deliberate release outside a slot
  requests a native physical drop. Tracking loss, menu focus and recentering
  are not release gestures.
- Each waist slot accepts a pistol or an explicitly listed compact SMG. The
  ordinary back slot accepts one weapon. Larger guns do not enter waist slots.
- Default loadout placement tries the right waist, then the left waist, then
  the back. Reconciliation preserves existing placements; explicit stowing
  continues to use the slot the player reaches.
- Waist presentation follows body yaw: muzzle down, slide/top forward, eight
  degrees of inward muzzle cant on either side. Stowed models use ordinary
  scene render flags so they share world depth rather than a foreground override.
- Back-stowed weapons are hidden and skip model preparation/submission. Their
  inventory, storage region, grip acquisition and draw order remain active.
- Waist models follow the current render body's pose, with final placement
  bound to the same native skin record and placement origin as chest equipment.
  Server updates publish inventory and local attachment geometry, not world
  positions for holsters. This avoids stepping at the lower server update rate.
- Successful pickup, drawing, stowing and exchanging provide one light 22 ms
  pulse on the interacting hand. Rejections and physical drops are silent.
  The existing `vr_weaponHapticScale` controls strength (zero disables it).
- Release inside an empty compatible slot to stow and leave that hand empty.
  Release inside an occupied compatible slot to exchange. Regrip before
  releasing the exchanged weapon again.
- An obstructed release or incompatible slot retains the final holding hand.
  Press and release again to retry. A failed release does not repeatedly retry
  while the button remains up.
- Empty-hand grip presses draw from body slots or pick up a nearby native
  world weapon. Native automatic gun pickup is suppressed while carry owns
  the interaction.
- Back draws take the ordinary slot first, then external overflow in admission
  order. An extracted weapon may enter a compatible ordinary slot; it cannot
  be pushed back into overflow. Native ownership's 15-entry array remains the
  safety bound, including non-firearm items.
- A pistol's remaining support hand becomes its controlling hand when the
  original control hand releases. For a two-hand rifle, the remaining foregrip
  hand carries without firing until the control grip is reacquired. Both-hand
  releases are resolved together, before either promotion or slot exchange.
- With only a rifle foregrip held, the opposite free hand can manipulate the
  actual charging handle and magazine or draw ammunition. Mechanical contact
  and mirroring follow that free hand; the supporting hand gains no trigger or
  rear-grip button authority. Releasing/stowing the foregrip interrupts any
  active stroke and returns hand escrow through the same transfer transaction.

## Implementation boundaries

The server scheduler is the carry-state writer. Render/input consumers read
short copied snapshots, and no publication lock is held across native calls.
Stowed parts use stable instance-slot/part lighting handles and validate the
current instance and waist location before presentation. Composite view/world
muzzle bridges and grip offsets are stored relative to the slot. At submission
they use the current render body for culling; at native preparation they use
`hands::attachments::for_record` and the shared `body_frame` conversion, exactly
as the chest attachment paths do. A missing matching hand record retains the
current render-body placement, never a server-world pose. Recenter mismatches,
stale inventory, drawn/replaced instances and invalid origins reject placement.
No added smoothing clock, render wait or native shared-model mutation is used.
`vr_carry_status` includes `holster_record_matches`, `holster_render_fallbacks`
and `holster_rejections`. HMD acceptance of the motion fix remains pending.
The selected native weapon supplies the primary viewmodel; other carried
weapons use composed native world models. This change does not complete
independent two-weapon firing, animation or simultaneous reload simulation.

The [empty-hand presentation](vr-empty-hands.md) now reuses the native character
hand model in a separate first-person object when selection is zero. It shares
arm IK with the held rig and preserves empty inventory selection. Native render
probes and offline regressions pass; headset appearance and transitions remain
acceptance checks for this candidate.

Native drop and pickup own entity creation, physics, notifications and ammo
transfer. A bounded entity-generation ledger retains mechanical state across
a VR drop/pickup; entity reuse cannot recover another weapon's state. A failed
mechanical restore retains a faulted instance instead of guessing its chamber.
Native pickup clamps the world clip to base magazine capacity. For an exact
recorded drop with an unchanged clip payload and a valid matching mechanical
profile, the adapter restores a known lost chamber bonus before restoring the
mechanical instance. Unexplained count changes remain faults. A faulted weapon
can still be released through ordinary clearance checks: outstanding hand-held
ammunition returns to reserve once, and the drop uses actual native ammunition
without serializing an untrusted chamber or cylinder state.
Checkpoint persistence of intermediate mechanical state remains a separate
feature.

World firearm touch is guarded both at the public item-touch entry and at the
native scavenging function. The latter can drain the world gun's reserve payload
and loaded clip, then return permission to delete it. VR blocks that consumption
unless the source entity, entity generation and weapon token match the exact
admitted grip transaction; another pickup nested inside it gains no authority.
Non-firearm ammo items, other actors and flat gameplay retain native behavior.
Both native entries are byte-checked before hooks are installed.

Equal-definition primary bullet weapons now have independent physical clips and
can enter separate grips through the native pickup adapter. Auxiliary feeds,
native akimbo and clip aliases remain excluded from duplicate admission. Native
and headset acceptance, and persistence of multiple copies in checkpoint saves,
remain pending; see the [instance migration](vr-weapon-instances-todo.md).

Mechanical scene admission is independent of free-hand manipulation permission.
A second occupied hand blocks part gestures but still admits the new gun's
physical feed and preserves its empty/chamber state. Mapped physical feeds also
reject stock reload while scene admission is pending; independent firing waits
for their mechanical instance. This closes the observed mixed-weapon path that
allowed a left-held Desert Eagle to enter native reload on both simulation and
prediction and refill from 0 to 7.

World-model composite containers can have zero bounds. Their bounded child
assembly supplies clearance geometry and body-slot rendering. Viewmodel and
world-model origins differ; their muzzle anchors align the physical drop with
the held gun. Unknown or malformed geometry rejects release.

Hand mirroring includes anatomical bone-axis conversion. Control/support
grips, magazine handling and slide/handle grasps share the adapter. A
side-mounted handle's **physical contact stays fixed**; only the hand posture
is reflected about that contact. The weapon, magazine feed geometry, cylinder
swing, travel axes and mechanical parts are not reflected.

Native akimbo inventory is not admitted as independent instances. Disabling
`vr_physicalCarry` returns control to the existing equipment path.

## Verification and headset checklist

The native server probe verified Glock stow/draw (`9 -> 0 -> 9`), creation and
ground settling of a dropped weapon, and pickup with ammunition remaining
`32/288`. With UMP and AK already owned, the same native pickup admitted Glock
as a third firearm without replacing either existing gun. The scoped pickup
limit bypass does not modify the global native inventory rules.

Engine-independent tests cover simultaneous releases, promotion/carry-only
ownership, incompatible/rejected releases, atomic exchanges, overflow order,
reconciliation, tracking/focus generations, mirror contact invariance and
adversarial transaction sequences. Grip/profile, controller-input, physical
reload, cylinder and pistol-profile regression suites passed during development.

No live controller poses were available during the native probe. This is a
headset test candidate; the following remain acceptance checks:

1. Draw and stow with either hand; check empty-hand visuals and that no body
   weapon automatically equips.
2. Drop in clear space and near walls. Check release position, orientation,
   velocity, collision and pickup reach; repeat a rejected release by regripping.
3. Exchange both waist slots and the back slot; reject a long gun at the waist.
4. Hold two different guns and stow/drop either one. Check both hand poses and
   the secondary world's model placement. Do not use this as a dual-fire test.
5. Transfer a pistol to its support hand; carry a rifle by its foregrip; release
   both hands together. Check that carry-only cannot fire.
6. With the control grip in either hand, manipulate a magazine, a pistol slide,
   and a side-mounted charging handle. Check that the latter stays on its real
   side and that a hand already holding another weapon cannot manipulate parts.
7. Repeat stow/drop/pickup with a loaded chamber, removed magazine, locked/open
   action and revolver cases. These mechanical-state cases need live verification
   beyond the native ammunition-count probe.
8. Pause, lose controller tracking and recenter while holding a gun; confirm
   that none causes a physical drop or unintended firing.

`vr_carry_status` writes `minidumps/h2-mod-vr-carry.txt` in the game directory.
It also retains the latest sixteen release transactions with input age, native
transaction time, weapon/hand, outcome and diagnostic detail. Pose admission is
per hand: losing the opposite controller cannot swallow a valid hand's release.
A pose-only gap preserves a real button-release counter until that hand has
valid drop geometry. It delivers once; tracking recovery alone never creates
a release. A later press, input-generation change, recenter or gameplay/focus
suspension cancels that pending release. Resuming with a held button does not
acquire anything, but its next real release remains usable. Collision/holster
rejections still require a fresh press and release to retry.
The latest 32 input transitions also report valid-pose/down masks, accepted
press/release masks, deferred releases, resumed-held baselines and each
button's generation/press/release counters. These distinguish swallowed input
from clearance, incompatible holster, mechanical preparation or native-drop
rejection. These input changes cover carry/support ownership; mechanical
handle and magazine gestures keep their own existing admission rules.
Empty-hand pickup now uses the directional native interaction query described
in [World interaction](vr-world-interaction.md), with extended reach and world
`Hold Grip` labels, instead of the original 23 cm proximity pickup.
`vr_hands_status` writes `minidumps/h2-mod-vr-hands-latest.txt`. Slot dimensions
are saved meter-based dvars: `vr_holsterWaistWidth`, `vr_holsterWaistDown`,
`vr_holsterBackDistance`, `vr_holsterBackDown`, `vr_holsterWaistRadius`, and
`vr_holsterBackRadius`.
