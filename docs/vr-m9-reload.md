# M9 physical reload

The physical M9 adapter owns magazine, chamber, slide, and held-ammunition
transactions for the admitted base `beretta` assembly. Native fire acceptance
remains authoritative. Unsupported names, capacities, attachments, modes, or
instances stay on their existing native path.

## Ammunition and fallback

The base M9 has a 15-round magazine capacity plus one chambered round. A native
loaded count of 15 is represented as 14 in the magazine and one in the chamber;
enabling the feature, changing weapons, or restoring a checkpoint never creates
a round. A full physical magazine with a chambered round may contain 16 total.

`vr_physicalReload` controls the physical adapter. When physical reload is
disabled or the assembly is not admitted, the existing native reload path and
`vr_closedBoltChamber` policy remain responsible for reload behavior. The
chamber-plus-one policy is an automatic-feed representation, not physical slide
control and not permission to fire during reload.

The server thread commits ammunition changes. Held magazines reserve rounds
against their owning weapon instance; successful insertion, extraction, cancel,
switch, and cleanup each settle the reservation at most once. Drawn rounds are
not duplicated by repeated input or native callbacks. A live cartridge removed
by a complete slide cycle is spent; it is not refunded as reserve ammunition.

## Magazine interaction

- A free hand reaches the left or right waist area and presses Trigger to take a
  magazine. Support, magazine, and slide leases cannot belong to one hand at
  the same time.
- Waist capture extends above the original reach while preserving its lower and
  horizontal boundary. Seated and standing reach use the same validated volume.
- A held magazine inserts when its top contacts the well with valid orientation
  and entry. The presentation seats only the magazine and loading hand; it does
  not move the rear hand, camera, weapon root, or native reload timeline.
- The contact volume provides acquisition tolerance below the physical mouth.
  A magazine already overlapping because it spawned, teleported, or followed a
  rejected native write must leave the contact neighborhood and re-enter before
  retrying. Ordinary top-to-well contact does not require a separate below-only
  approach history or dwell timer.
- After insertion, a held off-trigger grip may remain on the seated magazine.
  Release, separation, tracking/focus loss, or a weapon switch ends the visual
  lease without undoing a committed insertion.
- A held spare draws rounds immediately. Cancel refunds exactly the rounds still
  owned by that held magazine. Ejected magazine rounds return to reserve; a
  cartridge extracted from the chamber does not.

Dropped magazines are temporary cosmetic presentation. This adapter does not
provide persistent world inventory, recovery, or ground pickup.

## Slide and controls

| Input or state | Result |
| --- | --- |
| B/Y with a locked-open slide and a nonempty magazine | Release the slide. |
| B/Y with no magazine | Release the slide without creating a chambered round. |
| B/Y with an empty inserted magazine | Eject the magazine; the follower keeps the slide locked. The same press does not also release the slide. |
| A/X | Retain its existing HUD function. |
| Complete rearward slide stroke | Extract the chambered round once, if present. Holding the slide rearward does not extract repeatedly. |
| Forward return while continuously gripped, or Trigger release | Complete the stroke and feed one round if available. |
| Empty inserted follower | Stop at its locked-open travel; presentation and simulation share this boundary. |
| Short or reversed partial stroke | Do not extract or feed. |

An empty closed-bolt magazine does not chamber a round on insertion. Complete a
physical slide stroke or release an existing engaged lock with a loaded
magazine. If the slide was already closed, B/Y cannot release a lock that is no
longer engaged. Rear/forward jitter cannot repeat transactions. A physically
held slide blocks firing and B/Y manipulation.

The native M9 profile includes original and overhand slide grasps. A new press
selects a grasp from its own contact and wrist orientation; the selected pose
stays fixed for the held lease. Wrist rotation cannot switch poses mid-stroke or
move the slide axis. Release and a fresh in-region press can select the other
grasp.

## Ownership and lifecycle

Magazine escrow is refunded against the old weapon/instance before a weapon
switch or rear-hand reassignment. Magazine, chamber, and lock state are tracked
per owned instance; a loaded-count total alone cannot recreate an intermediate
mechanical state.

Focus or tracking loss, recentering, stale input, and instance replacement cancel
held visual leases and settle escrow once. An incomplete slide stroke leaves
mechanical ammunition unchanged; a completed stroke remains committed. Checkpoint
restore establishes a new instance generation and cannot replay old controller
edges or refund old escrow into restored native counts. Save/load restoration of
intermediate physical states is outside the current contract.

Disabling physical reload exits the admitted physical instance and returns
still-owned escrow against current native counts after the server processes the
transition. Stock reload is then restored. An unexplained native loaded-count
change blocks firing/reloading until the physical instance exits or a new level
establishes fresh state.

## Diagnostics and acceptance

`vr_reload_interaction_status` reports admission, magazine escrow, slide stage,
held-grip pose, contact eligibility, native transaction results, and lifecycle
rejections. `vr_chamber_status` reports magazine/chamber partition and separate
server/prediction observations. Diagnostic counters help isolate faults; they do
not prove visible alignment or headset acceptance.

Offline checks should cover both hands, each slide grasp, waist draw, empty and
partial magazines, insertion at valid and invalid angles, full and short slide
cycles, follower lock, held-grip stability, cancellation, weapon switching,
tracking/focus loss, native rejection, and ammunition conservation. In the
headset, additionally verify both draw/eject orderings, locomotion with a held
magazine, contact near the well, continuous slide cycles, wrist comfort, both
eyes, and a non-M9 weapon. Mid-reload save/load is not an acceptance scenario.
