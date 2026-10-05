# Interaction diagnostic views

## Cover push judgement view

Available in Debug and optimized builds: enable **VR Settings > Debug > Feed
cover geometry**, save and restart. The saved `vr_coverPushDebug` selection
defaults off; console edits apply on the next launch. Unselected channels do
not allocate their history or register a stereo drawing callback.
Pickup/body overlays below remain Debug-only; reload/HK geometry is available
through the launcher's separate startup selections.

- Cyan rectangle: authoritative base capture rectangle, including its length
  and width acceptance margins, rotating with the lid.
- Green rounded boundary: contact entry plane, 1.2 cm above the skin, expanded
  by the hand-directed capsule (6 cm radius plus 1.5 cm toward fingers/wrist).
  It rotates with the projected hand axis and matches capsule/rectangle overlap.
- Yellow rounded boundary: exterior arming plane, 2.5 cm above the skin, with
  the same footprint tolerance. Cyan shows the base capture rectangle inside it.
- Green arrow: required push direction from outside toward the skin.
- White capsule, cross and arrow: palm footprint up to 15 cm long and 12 cm wide, its raw anatomical
  centre and physical controller palm normal. The arrow turns magenta only when
  the relaxed facing check fails (normal dot >= 0.2).
- Blue cross: displayed anatomical palm. Orange cross: last simulation sample.
- First text row: actual simulation push stage/rejection; second: current
  tracking/input/occupancy gate. The rows can refer to different input sequences.
  `SETTLING` means a short slow-push run-out; `COASTING` means full-close inertia.

The view reuses the existing stereo line renderer, black label outlines, bounded
line batches, skeleton epoch/reference/age checks, and left-eye frozen pair.
It neither presses buttons nor alters collision or mechanical state. Geometric
facing and patch calculations are shared with the push controller.

`vr_coverPush_status` prints the latest state and renderer counters.
`vr_coverPush_dump` saves up to 4096 enabled-view samples to
`h2-mod-vr-cover-push.txt` in the game working directory. It includes raw/displayed
palm positions and longitudinal axis, local skin gap and facing, input activity, occupancy, simulation
sequence, cover amount and rejection reason. The history is bounded; dump after
the agreed reproduction window. Toggling the view clears its history. The dump
copies history to the heap under a short lock, then formats/writes outside it.

## World and body views

Available in Debug builds. Toggle either view independently in the console:

```text
vr_interactionDebug 1
vr_bodySlotsDebug 1
```

Use `0` to disable a view. Toggles are not saved across game restarts. These
overlays reuse stereo line rendering and native world labels, with black outlines
for bright scenery. Both eyes share one frozen sample. Stale samples and tracking
reference changes suppress the view; the diagnostics never perform pickups.

## World pickup

The white ray and cone are the actual empty-hand query. The cyan sphere at its
origin shows the 12 cm contact tolerance. Each oriented box uses that particular
gun's composed native model dimensions or a script model's server DObj bounds,
with its current entity transform. Contact
means the hand sphere intersects the model box, not a fixed sphere moved to the
gun. Purple crosses show the old native collision centres for comparison.

Box colors and labels:

- Green / SELECTED: final target of that hand's query, with its contact point.
- Cyan / CANDIDATE: admissible, but another object wins direction/distance priority.
- Red / BLOCKED: failed head or hand visibility.
- Yellow / RANGE/AIM: model geometry, distance or aiming test rejected it.

Labels identify left/right query and native entity number. At most eight
candidates per hand are displayed. The source is the native admitted candidate
list; an object excluded by native eligibility before our query is not falsely
shown as a geometric rejection. Querying itself retains native eligibility,
orientation, notification and final pickup behavior.

## Body storage and ammunition supply

LEFT WAIST, RIGHT WAIST and BACK show rounded cross sections of the same body-aligned
volumes used by the carry arbiter. All three slots fit the bounded line batch.
Cyan means empty; purple means occupied; green means a sampled hand is inside.
The number in brackets is the stored native weapon token (zero means empty).
Green indicates contact, not permission to store an incompatible weapon.

AMMO SUPPLY shows the two rounded replenishment areas from the active
mechanical weapon profile. Yellow means outside; green means the actual
manipulating wrist is inside. Its white cross is the same raw wrist used in the
contact calculation. The shared volume construction also drives simulation.
Waist and supply cores extend 22 cm backward, 20 cm outward and 24 cm downward
from their authored anchors, retaining the original radius and upward supply
reach. The back core extends 16 cm farther backward, 30 cm to either side,
30 cm downward and 12 cm upward. These are directional extensions, so ordinary
forward hand movement does not gain extra inventory reach. Overlapping holsters
resolve by distance to their original anchors; weapon eligibility still applies.
Supply areas appear when a matching weapon presentation and free manipulation
hand are active, including foregrip-only carry. This is a replenishment volume,
separate from waist weapon storage. The launcher's **Magazine well geometry**
shows the insertion well; **Handle slap geometry** shows authored HK slap contacts.

`vr_interaction_debug_status` prints and saves the sampled candidate verdicts,
model/native centres, script-model status, visibility trace point, separate
head/hand visibility verdicts, slot occupancy, timestamps and renderer counters to
`minidumps/h2-mod-vr-interaction-debug.txt`. This distinguishes absent native
candidates, stale input, geometric rejection and occlusion while reproducing.
Visibility is `not-tested`, `blocked` or `clear`. Diagnostic queries test both
ends even when the first is blocked; ordinary queries retain short-circuiting.

Headset acceptance: compare pistol and rifle boxes with visible models; aim near
their rear/front ends; cross overlapping targets; inspect waist/back reach and
ammo supply with either hand; repeat while carrying a rifle only by its foregrip.
Check both-eye alignment and bright/dark scenery. Offline tests cover translated
and rotated short-weapon bounds, model-size-dependent contact, strict range after
coarse padding, malformed transforms, supply dimensions, and stereo line clipping.
