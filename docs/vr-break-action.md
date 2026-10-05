# Ranger and M79 break actions

Status: implemented with offline mechanics, source-pose and native-boundary
regression coverage. Headset fit, gesture thresholds and in-game asset admission
remain to be accepted. The previously tested knife/pistol refinements are accepted
by prior headset acceptance; that scope does not cover newly admitted weapons.

The first deployment failed during component startup with `zone-unload observer
limit`: the added presenter was the ninth observer in an eight-entry table.
The correction uses a shared startup-only callback registry for zone unload,
model submission and placement. Registration can grow, while runtime traversal
remains read-only/allocation-free and retains callback order and deduplication.
The rigid-part suite covers the ninth registration, growth to 64 callbacks,
duplicate/null registration and repeated ordered dispatch. This regression does
not itself establish a successful in-game boot.

The first live interaction pass identified two native integration gaps. Ranger's
actual H2 fire-mode value is 6 with a 10 ms interval, previously rejected by the
independent clock's mode 0–5 / minimum 20 ms contract. It now admits that native
double-barrel timing while retaining one shot per fresh Trigger edge.

M79's post-shot opening failure was traced to cached ammunition observation:
the native fire/consume hooks ran, but snapshots outside the scheduler's server
callback scope returned the ledger's old count, reporting 1-to-1 while native
consumption had changed the clip to 0. The missing settlement caused an
unexplained count fault and blocked opening. Dropping/reacquiring reimported the
empty clip, explaining the native recovery path. Consumption snapshots now
read the actual native player state through a read-only, identity-bound helper;
projected instance, ownership and transfer checks remain enforced. Native write
authority is unchanged. The earlier delayed-projectile call-site hypothesis and
its special return-address exception were removed. Shot settlement still requires
the existing fire scope. A regression covers a stale ledger outside scheduler
scope, actual 1-to-0 consumption, and subsequent opening/reloading without dropping
the weapon. In-game confirmation of this correction remains pending.

Native launcher input is withheld while a physical breech is open or needs
neutral rearming; closed empty-chamber input retains the native dry click.

## Controls and chamber rules

- Press the holding hand's B/Y reload/release button to unlock and fold the
  barrel open. Firing stops immediately. Opening takes 0.25 seconds; its fully
  open boundary automatically ejects spent cases once.
- Ranger has two independently recorded chambers. A live round in the other
  barrel remains in place when opening after one shot. M79 has one chamber.
- Pinch Trigger at either waist supply to take one cartridge. Bring its tip
  into an empty open chamber while holding Trigger. Ranger chooses the closest
  eligible empty chamber, never overwrites the other round, and may be loaded
  one cartridge at a time. The transferred cartridge leaves the hand immediately.
- Release Trigger after insertion. Either make a deliberate upward wrist flick
  to swing the barrel closed, or pinch Trigger over the foreend and lift the
  barrel back along its hinge. The auxiliary-hand method follows actual hand
  displacement and can be stopped/regripped partway through.
- B/Y does not toggle an already open gun closed. Empty/partially loaded guns may
  still be closed deliberately. Open, opening, partially folded and closing guns
  cannot fire; holding the firing Trigger throughout closing cannot queue a shot.
  Release it to neutral before firing again.

The barrel and cartridge use the same auxiliary Trigger, with exclusive leases.
A normal supporting Grip must be released before loading or pinching the barrel.
Ranger, like M79, uses both hands to aim while the supporting Grip is held.
Ranger may also be stowed in either waist slot and drawn with either hand;
this size-policy exception does not change its two-hand aiming or admit M79.
Its single cartridge uses the established SPAS-12 single-shell pinch, registered
by the actual shell bounds centers, instead of the native Ranger animation's
two-cartridge palm carry. Source cartridge geometry and loading mouths remain
weapon-specific.
The selected M79 retains the existing native projectile launch, trajectory,
damage and debit path. Independent nonselected launcher firing is not added.
Ranger uses the existing independently held bullet-weapon path. The native akimbo
inventory split remains a separate carry policy; no alternate double-shot binding
or shared two-gun reload transaction is introduced here.

## Shared implementation

`break_action_feed.hpp` is the bounded one/two-chamber planner. Live and spent
masks are disjoint. Every accepted shot moves one live chamber to spent; opening
clears only the spent mask. Initial native counts cannot reveal whether an absent
round was fired or never loaded, so missing rounds import conservatively as spent
until the first opening. No ammunition is manufactured by that visual assumption.
There is no detachable magazine, tube reserve, or chamber-plus-one bonus.

Draw/load/discard/refund use the existing shared reserve/disposition and native
compared-write boundaries. Failed writes preserve the committed state. Duplicate
input is ignored; a failed draw/open consumes its button edge, while failed
insertion requires fresh withdrawal/contact. Focus/tracking loss, recenter,
ownership changes and scripted contexts cancel manipulation without silently
closing the breech or losing a held cartridge. Native world transfers retain
chamber masks and hinge state, while clearing old input leases and event payloads.

The gesture controller measures the wrist flick from consecutive tracked aim
rotations, projected on the controller's pitch axis. It reuses the revolver's
bounded signed-motion window with an explicit axis, keeping the revolver's
default roll policy unchanged. Initial tuning is 6 rad/s current pitch speed,
0.35 rad total useful angle, 0.20 rad traveled above the speed gate, a 0.22-second
window and 60 rad/s tracking-jump rejection. Translation, artificial turn/head
transforms and animation do not contribute. These thresholds require HMD tuning.

`break_action_runtime.cpp` adapts that planner to owned instances and the existing
server/shot/reload hooks. Profiles are discovered through the single weapon
registry. Exact name/capacity and the matching presented recipe are mandatory:
`ranger` capacity 2 and `m79` capacity 1. M79's name/capacity/projectile type were
previously witnessed in a read-only native descriptor capture. The catalog label
"Thumper" is not its native WeaponDef name. The stock no-partial reload flag does
not reject these explicit physical chamber recipes; no unknown native family is
given that exception.

## Source assets and presentation

| Family | Native receiver | Barrel root | Cartridge/case roots | Source frames |
| --- | --- | --- | --- | --- |
| Ranger | `h2_viewmodel_sawed_off_double_barrel_base` (11 bones) | `j_reload` | `j_le_bullet`, `j_ri_bullet` | idle 0, shared SPAS-12 frame 6 shell pinch, reload 20 hinge |
| M79 | `h2_viewmodel_m79_base` (17 bones) | `j_front_end_reload` | `j_grenade_round`, `j_brass_round` | idle 0, reload 25 shell grip, reload 15 hinge |

The per-family pose headers retain source SHA-256 provenance and native-unit
calibration. M79's idle animation parks an invisible live round offscreen, so its
seated chamber is authored from the receiver bind instead of copying that hidden
track. Ranger rounds are barrel children; M79 rounds are siblings and explicitly
follow the authored barrel transform. M79's strap joints interpolate their native
closed/open poses with the hinge. Actual foreend vertices nearest the native hand
skin supply grab contacts, rather than a controller-origin region.

Held cartridges and expelled cases use immutable rigid subsets of the loaded
native receiver, registered with the shared model-identity bridge. M79 uses its
separate brass-only case; Ranger reuses its native shell geometry for spent cases.
No models, textures, sounds, export parsers or external asset paths are packaged.
Asset caches are bounded and freed only after the drained native unload boundary.
Held cartridge placement matches the exact skinned object/matrix/epoch and view
record, including instance, ownership, reference and freshness checks. Dropped
cases have bounded cosmetic lifetimes; their rendering never mutates ammunition.

## Diagnostics and acceptance

`vr_breakActionReload` enables the family. `vr_break_action_status` prints its
current state and saves `minidumps/h2-mod-vr-break-action.txt`, including native
name/capacity, live/spent masks, hinge amount, hand lease and cartridge contacts.
Sound notetrack keys come from the respective original reload animations and are
resolved through that weapon's current native sound map. Missing keys stay silent.

Offline tests cover exact profile/rig admission, both hands, one/two chambers,
partial loads, automatic spent-only ejection, no repeated ejection, manual and
inertial closure, rejected native writes, neutral rearm, interruptions and 12,000
randomized mechanical requests with ammunition conservation checked throughout.
Existing revolver, magazine, hand, carry/registry, input and diagnostic regressions
remain required. Offline source previews establish candidate geometry, not HMD fit.

In-game acceptance should test Ranger with zero/one/two rounds and M79 with zero/
one, both reload orders, supply reach, each Ranger chamber, both hands, both closing
methods, partial manual closure/regrip, normal handling without false closure,
focus/recenter, holster/drop/pickup, and checkpoint/level changes. Check case count,
hand/cartridge alignment, sounds and M79 projectile origin. Do not extend an
accepted individual operation to untested variants or combinations.
