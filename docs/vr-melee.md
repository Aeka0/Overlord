# Chest equipment and physical melee

Standalone ending/cheat knives use the separate
[special inventory knife binding](vr-special-knives.md). They retain ordinary
carry/drop ownership outside scripted sequences. Official Green Beret delegates
`h2_cheatcommandoknife` to this chest slot while the mode is active; see
[official cheat adaptation](vr-official-cheats.md).
Cliffhanger instead uses two Grip-operated waist picks while Green Beret is
active and hides the chest knife. Their curved head contact points, independent
leases, original-side return and story handoff are described in
[Green Beret cheat melee tools](vr-official-cheats.md).

The chest layout has three body-relative slots: knife on the left, tactical
equipment in the center, and lethal equipment on the right. Centers are 18 cm
apart, 4.5 cm forward and 35 cm below the shared estimated body anchor. After
the initial 10 cm inward adjustment, acceptance tuning moved all three slots
another 1 cm inward and 3 cm downward, followed by a further 2.5 cm inward. The anchor is
at nominal head height; it estimates equipment placement rather than tracked
hips. See [body-mounted equipment estimation](vr-body-equipment.md).
The knife has a 10 cm draw radius around its visible handle (formerly 8.5 cm).
The other two slots are display-only and do not claim hands or input.

`chest_equipment.cpp` reads H2's selected secondary/primary offhand tokens and
their native ammo counts in the server callback. Zero total ammo hides the item;
one or more rounds displays exactly one native world model at its original scale.
The source model's bounds center sits at the slot, with native +Z upright. The
model is an existing native asset with its original LODs/materials; no mesh copy,
asset registration, inventory write, selection command or throw hook is added.
Unowned tokens, invalid counts and missing/unsupported model geometry are hidden.
Snapshots expire after 150 ms and are cleared at the drained zone-unload boundary.
Current view-record body placement refines both items through the same hand
attachment service used by the stowed knife, independently of the knife lease.

Native evidence: selection code at `0x140697A88` / `0x140697A99` reads primary
offhand `playerState+0x3B4` / secondary offhand `+0x3B8`; signatures gate these
reads. Live inventory confirmed `fraggrenade` and `flash_grenade`, four clip
rounds each, using `weapon_m67_grenade` and `weapon_m84_flashbang_grenade`.
`+0x3B0` is the currently primed offhand and does not choose the chest slots.
Category selection follows these native slots, not guessed OffhandClass values.

`vr_chestEquipment` controls the two display-only slots, independently of knife
interaction. `vr_chestEquipment_status` reports selected tokens, quantities and
submission/preparation counts to `minidumps/h2-mod-vr-chest-equipment.txt`.
Automated tests cover the layout, native selector reads, zero/one/many quantities,
invalid inputs, upright placement and the retained knife lifecycle. Headset
acceptance for the added models remains pending.

## Interaction

- Squeeze an available hand near the left chest handle to draw the knife.
  Either hand can own the single knife; simultaneous draws choose the closer
  eligible hand. Existing magazine, bolt, accessory and world-use leases block
  acquisition by an occupied hand. An empty hand may also hold its trigger.
  Acquisition measures the authored palm/handle contact, including wrist offsets
  and anatomical mirroring, rather than measuring the displaced wrist joint.
  A real squeeze retains draw intent for 200 ms while the palm approaches; a
  release, hand lease, tracking/focus loss or recenter cancels it. An old held
  squeeze cannot pick up the knife by simply moving past the chest.
- Wrist orientation at acquisition selects forward or reverse grip by matching
  the stowed blade direction. The grip remains fixed until return; turning the
  wrist does not change the selected grip. Both modes preserve the same handle
  contact and use the original articulated hand pose.
  There is no rejection angle: any wrist orientation may acquire an in-range
  knife. Only the forward/reverse choice uses the orientation comparison.
- Release the owning hand's squeeze to return the knife to the chest, including
  releases away from the chest. It never creates a dropped weapon or changes
  the native firearm inventory. Focus/tracking loss cancels motion history and
  hides invalid presentation without inventing a release. Native context reset
  returns the knife, and external weapon selection wins a hand conflict.
- Swing the held blade through an NPC. Empty-hand strikes require a closed
  squeeze; held firearms can strike with their receiver/stock/barrel span.
  A two-handed firearm has one melee source at its controlling hand.

### Knife and pistol magazine co-grasp ( candidate)

USP (including its already admitted suppressor), M9, M1911 and Desert Eagle
explicitly allow the knife hand to pinch a replacement magazine from the waist
with Trigger while Grip continues holding the knife. Both reload orders remain
valid: eject then fetch, or fetch then eject. Insertion, ammunition escrow and
rear-hand magazine/slide-release buttons retain the shared pistol mechanics.
Holding Trigger before entering the supply does not acquire a magazine.
This exception does not enable support grips, world use, other equipment, or
other weapon families while holding the knife. The slide co-grasp below extends
the same Trigger/Grip separation to physical chambering.

The co-grasp uses `h2_wpn_pst_usp_tactical_reload` frame 15: its articulated left
fingers and receiver `tag_knife`/`tag_clip` transforms, relative to the wrist.
The sampled receiver knife tag is distinct from the hand's `tag_knife_attach`;
using that socket alone would rotate the actual common knife incorrectly.
M9 and M1911 retain their magazine registration relative to the ordinary USP
grasp, transferred into this native co-grasp. Following HMD feedback, Desert
Eagle instead shares the USP co-grasp rail/roll directly, with a common lower-body
grip point 1 cm above each magazine base. Its distinct bare-hand rotation is no
longer transferred into the knife grasp. These fits are not native animation
samples for those three pistols.
All source exports remain external; only compact transforms and source hashes
are promoted. Both hands use the shared anatomical mirroring and live glove
segment lengths. Forward/reverse flips the blade around the existing handle
pivot without changing the magazine transform or fingers.

The renderer samples the reload state once for the wrist solve, raw insertion
geometry and magazine presentation. The equipment pass preserves that solved
wrist/finger pose, and the knife attachment mode travels with the exact skinned
view record. An accepted magazine grasp keeps its pose until its lease ends:
releasing Grip returns only the knife, without moving/cancelling the Trigger-held
magazine; releasing Trigger leaves a still-Gripped knife in hand. Knife and
magazine co-grasp permits physical knife strikes. Entering/leaving the co-grasp
resets swing history; pending magazine settlement still suspends strikes.
Focus/tracking loss,
recenter, selection changes and rejected ammo writes use the existing cancellation
and neutral-rearm boundaries.

`vr_reload_interaction_status` includes `scene_knife`, `knife_magazine_capable`
and `knife_magazine_grasp`. Offline coverage includes both hands/grips, both
reload orders, separate Grip/Trigger releases, empty reload with rear-button
chambering, interrupted escrow, duplicate input, mirrored contact geometry and
unsupported profiles. Headset testing confirms the co-grasp interaction works. The
subsequent height, DE50 pinch/magazine-angle and insertion-tolerance refinements
still require HMD fit confirmation in both knife directions.

### Knife and pistol slide co-grasp

The same four families also allow a fresh Trigger pinch over the slide while
Grip holds the knife. The source is the USP **pickup** animation,
`h2_wpn_pst_usp_tactical_pullout_first`: `pull_slide` occurs at frame 13; frame
17 supplies the closed fingers, wrist-relative receiver knife attachment and
slide-relative hand. Its existing slide travel is removed before authoring the
rest pose. The empty-reload animation's slide-release button gesture is not used.
M9, M1911 and Desert Eagle translate this grasp by each slide's rear edge and
upper body height, with acquisition contacts rebound to actual slide vertices
inside the existing capture bounds. The follow-up HMD refinement raises all four
knife slide wrists 1 cm along gun-local +Z and rebinds their contacts to the real
slide surface. DE50 additionally blends thumb/index/middle 25% toward the more
open pickup-frame-13 fingers, adding about 4 mm of thumb/index distal separation
on the reference glove. Ring/pinky knife support, wrist orientation, knife grip
direction and live bone lengths stay intact. Ordinary bare-hand pistol grasps
and stroke tolerances are unchanged.

Slide contact takes precedence over an overlapping waist supply, as with normal
physical reload. A held magazine or seating lease remains exclusive: release
Trigger after insertion, then pinch the slide. Partial/full stroke, live-round
extraction, empty chamber feeding, follower lock and repeated cycles reuse the
ordinary slide controller. Grip release returns the knife while the Trigger-held
slide retains its selected co-grasp; Trigger release returns the slide without
returning a still-Gripped knife. Forward/reverse uses the same hand/slide pose.

Both the ordinary/knife pose set and its style index latch at acquisition.
Returning the knife or turning the wrist cannot switch a held style. The solved
view record carries an explicit grip/magazine/slide knife attachment mode, so
the knife follows the exact constrained wrist and fingers. Physical knife
strikes are suspended during slide manipulation.
The status command additionally reports `knife_slide_capable` and
`knife_slide_grasp`. Offline tests cover both hands/grips, independent releases,
partial/full/repeated strokes, fresh-pinching after insertion, invalid pose IDs,
native-write rejection, interruptions, and mirrored contact at rest/lock/full pull.
Headset testing confirms operation; the fit refinements still need HMD confirmation.

The shared pistol-well insertion policy now accepts up to 95 degrees of magazine
rail mismatch (previously 85), with 9 cm above/inside the mouth (previously 6),
6 cm below and unchanged 3.5 cm radius. This applies to bare and knife hands and
other profiles already using the shared pistol-well policy. Rifle-specific
tolerances, seated magazine transforms, escrow and fresh-contact guards remain
unchanged. Fully reversed magazines are still rejected.

All strike types share a 400 ms player cooldown. A used contact must also
be left before another strike. Current motion thresholds are:

| Tool | Minimum contact speed | Useful travel |
| --- | --- | --- |
| Knife | 1.0 m/s | 4 cm |
| Fist | 2.8 m/s | 8 cm |
| Firearm | 3.5 m/s | 12 cm |
| Shield | 3.0 m/s | 10 cm |

Blunt strikes measure speed and travel separately for each of the nine contact
points. A fast muzzle cannot arm a slow stock or receiver. Travel is the point's
displacement during its above-threshold stroke; reversing direction starts a
new stroke, so short repeated oscillations cannot accumulate into an attack.
These stricter blunt defaults are an offline-tested candidate pending HMD tuning.
The blade thresholds and stroke policy are unchanged.

Blunt tracking validation follows the holding wrist in the same calibrated body
frame: translation above 16 m/s or rotation above 35 rad/s invalidates the sample.
It no longer rejects the whole weapon for a contact point exceeding 12 m/s or
45 cm per sample. Accepted contacts retain their entire previous-to-current
native sweep, including a target crossed between samples. History older than
120 ms, initial samples, recentering, producer continuity changes, changed
ownership/grip revision, and malformed poses cannot create attacks. Player
translation and snap turning alone do not arm a strike. The fixed nine-point
trace budget, native obstruction checks, damage and shared cooldown stay intact.

## Native boundary and presentation

The adapter sweeps bounded points along the blade or firearm through native
locational traces, using the same surface/model query and priority map as stock
melee. Targets must be living native actors with a valid current entity
generation, or the explicitly admitted [trainer knife targets](vr-trainer.md).
Those damage-enabled script models retain native target scoring and do not
emit actor blood. A separate player-to-contact clearance check prevents striking
through an obstruction. The shared gate commits before entering damage/script
callbacks, preventing same-frame and alternating-hand duplicates.

Native melee damage comes from the loaded weapon accessor. The witnessed stock
knife value is 200; empty-hand and firearm strikes use approximately one third
(67 for that value). While official Ragdoll Impact is active, all player melee
tools instead use the current tactical knife's native damage; disabling it
restores the ordinary policy on the next hit without modifying weapon assets.
The complete native `G_Damage` call uses `MOD_MELEE`, the
actual contact point, direction and hit part. Native actor pain, damage policy,
invulnerability, effects, death and mission scripts remain authoritative. The
adapter does not subtract health or force AI animation state. Scripted actors
may therefore reject damage or suppress a pain animation as in ordinary play.

Knife contacts also follow the native melee feedback sequence, which is separate
from `G_Damage`. The server creates `EV_MELEE_HIT` (0x3c) at the actual contact
with the native attacker, target, weapon, knife/player flags and stance flag.
The event allocator retains its own trajectory, entity number and lifetime.
After accepted player-to-actor damage, the equipment owner queues one native
knife blood FX request with the actual knife root, hand, lease revision and
tracking reference. The client resolves the common knife's `tag_knife_fx` bind
pose and emits the original `flesh_impact_knife` effect at that world socket using
the existing engine oriented one-shot API. Native blood/alternate-melee controls
remain in force, including the original no-blood FX variant. Asset resolution
and emission run on the main client owner; the bounded queue expires at 250 ms
and clears at zone unload. Released/reacquired knife leases are rejected.

The old player `EV_MELEE_BLOOD` path is intentionally not also sent for physical
knives: its client handler reselects the current firearm and bolts FX onto the
flat viewmodel's `tag_knife_fx`. That made VR knife feedback depend on the other
hand's weapon (observed working with USP45 only). Shield events retain their
existing path. Knife impact audio still uses the normal `EV_MELEE_HIT` event.
Feedback is emitted once inside the committed physical strike, never per eye. A
valid but invulnerable contact can produce the stock impact event; it cannot
produce the damage-dependent screen blood. Blunt strikes retain their separate
impact feedback and do not pretend to be knife contacts. No stock forward
auto-targeting, lunge or scripted knife animation is started by this adapter.

The knife reuses the common `h2_viewmodel_knife` mesh referenced by normal P90,
USP, AK and M9 weapon definitions at `WeaponDef+0x488`. It does not use the
ending mission's commando knife or its standalone weapon definition. Damage
uses a loaded normal weapon that references the same common knife. The left-hand
pose from `h2_wpn_knife_melee_slice`, frame 8, mirrored anatomically for the
right hand, supplies the grip. This is the common H2 knife animation, including
its attachment orientation and finger pose. The authored knife points toward
the pinky (reverse grip); forward grip points toward the thumb. The forward
half-turn uses knife-local Z, preserving cutting-edge facing; the former Y
half-turn also reversed the edge. This applies before anatomical mirroring to
ordinary, magazine and slide co-grasps. The flip pivot
projects the middle/ring knuckle midpoint onto the handle axis, at -1.6953 cm
in knife X. The earlier -5 cm point was near the thumb and displaced the handle
on flipping. Both modes use the same palm contact for acquisition, physical
collision and exact-record rendering. No extracted models or textures ship
with the mod. Its immutable
rigid subset selects only `tag_knife`, which owns all 4,608 vertices and 6,186
triangles. `tag_knife_fx` is an empty effect socket, not a required mesh group.
Chest visibility depends on the mesh and body pose; hand binding and the damage
definition gate drawing, and definition lookup retries independently of mesh
creation. The subset is registered through the shared model-identity bridge and freed
only at the drained native asset-unload boundary.

Held placement binds to the exact native skinned wrist and view record,
including a hands-only object with native selection zero. A shared hand
attachment cache handles this without inventing a firearm muzzle identity.
Record reuse invalidates previous bindings before skinning; object, matrix,
epoch, wrist contents, reference and equipment lease must match. Stowed
admission uses the current render body's pose; final placement uses the same
record's head/body as the hands. It no longer follows the server body mailbox.
Held placement also uses the mirror basis from that exact solved rig, not a
later global hand-binding publication. Both stowed and held presentation use
ordinary world depth, with no first-person depth override.

Confirmed firearm hits reuse the native `melee_hit` alias, which selects
`h1_weapons/melee/melee_hit01/02`. The weapon-specific `h2_*_melee_hit` aliases
actually select swing sounds and are not used as an impact substitute. A copied
event enters the existing bounded feedback queue only after accepted damage;
the main sound owner checks held identity, tracking freshness, pause and the
existing `vr_weaponInteractionSound` option. Misses, persistent overlap and
cooldown-rejected contacts cannot queue this sound.

## Diagnostics and acceptance

`vr_closeNpcCollision` reduces the player's horizontal query radius against
native actors from the usual 15 units to 9. NPC bounds and player vertical bounds
remain native. The shared PM trace boundary uses matching prediction/server
handlers. Only an actual actor obstruction triggers two supplemental sweeps:
original player bounds against the environment, and reduced bounds against the
original mask. The earlier blocking result wins. Walls, floors and ceilings keep
their original clearance, NPCs retain a positive collision core, and invalid or
unexpected trace results retain the original answer. Narrow/point bounds,
synthetic path-node moves, non-player and scripted weapon-control contexts keep
native behavior. This changes native movement clearance, not room-scale head or
hand clipping. `vr_npc_collision_status` reports client/server adjustments and
environment blocks; its output is also included in `vr_melee_status`.

The eye-to-muzzle firing sweep excludes the native actor collision contents
for controller, independent and underbarrel shots. A muzzle inside an NPC no
longer rejects the shot; the projectile's normal target trace remains native.
World cover and the separate shield check remain in the firing gate.

###  live NPC collision follow-up

HMD testing confirmed the muzzle change, but joystick movement still rebounds at
an NPC. Read-only sampling found a player/NPC horizontal center distance of
about 29.8 units at rest, matching the original 15 + 15 bounds rather than the
intended 9 + 15. The adapter was installed and produced prediction/server
adjustments, so its three PM dispatcher call sites are not the final authority.
Tracing the native handlers without changing results found hundreds of direct
actor hits from `PM_GroundTrace` at return address `0x140689bcf`, with player
bounds 15 x 15, mask `0x281c011` and NPC contents `0x4000`.

`vr_physicalMelee` enables this interaction alongside physical carry.
`vr_melee_status` prints and saves `minidumps/h2-mod-vr-melee.txt`: native asset
readiness, holder/grip, draw/return counts, contacts, damage attempts/results,
last target/damage, and exact-record presentation counters, including chest
preparation, grab availability/pending masks and both palm distances. This command does
not cause a strike.

Its per-hand `motion` rows include the tool (`kind`: fist 0, firearm 1, knife 2,
shield 3), input sequence, maximum contact speed, maximum useful travel, and a
bit mask of individually armed points. `rejected` is 0 for no rejection, 1 for
malformed input, 2 for a history boundary, or 3 for a tracking jump;
`tracking_rejections` is cumulative. Sequence zero marks a suspended hand;
other fields may retain its previous observation. These rows are sampled
diagnostics, not a motion recording or proof of a native damage result.

`native_knife_hit_events` counts native contact event submissions. The legacy
`native_knife_blood_events` counter remains zero for the physical knife route.
`knife_fx_ready`, `queued`, `emitted`, `discarded` and `missing_native_asset`
describe its independent FX requests. Emitted counts native API admission,
not proof that particles survived native budgets or appeared in the headset.
The fix still requires acceptance with USP45, other weapons, and an empty
opposite hand, including release/reacquisition and pause/scene transitions.

Offline tests cover layout separation, single ownership, release/reacquisition,
the source-witnessed palm contact and thumb/pinky orientation in both hands
(including nonidentity anatomical bases), native event payload boundaries and
preservation of allocator-owned fields, damage scaling, shared cooldown/contact withdrawal,
tracking jumps, stale samples and body translation/turning. Hand-rig and
physical-reload regressions also pass. Native functions, loaded model geometry
and damage values were read from a flat-game session; no gameplay
mutation or damage was injected into that session.

Headset acceptance remains required: inspect both grips in both hands, draw
while holding another weapon, return away from the chest, hit/miss/obstruction,
blunt stagger and damage, rapid alternate hands, pause/recenter, checkpoint and
level changes. Native visual effects and AI reactions have not been certified
by the offline tests.
