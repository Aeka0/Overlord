# Physical grenades and cheat throwables

The grenade provider shares the chest slots, hand arbiter, native ammunition
transaction boundary, hand pose library and scene attachment pipeline. It does
not borrow the currently selected firearm or activate the native flat throw
animation. `vr_physicalGrenades` enables the interaction (default true).

## Controls and ownership

- Grip the displayed tactical or lethal chest item with either free hand.
- Release the holding grip before removing the pin to return the item to the
  chest. No ammo was spent and no world grenade is created.
- Hold the other free hand's **trigger** near the pin and pull 8 cm relative to the
  grenade. This commits one native ammo debit, removes the pin assembly and
  frees the pulling hand. Releasing the trigger too early cancels the pull.
- Touch the grenade body with the other free hand and press its side grip to
  transfer ownership. The side grip never pulls the pin. Handoff preserves the
  paid-ammo flag and cooking deadline, and commits before the original hand's
  release is processed. Handoff is forbidden throughout an unfinished pin pull;
  release the trigger to cancel it or finish extraction first. Both candidate
  admission and the state transaction enforce this restriction.
- After the pin is removed, releasing the holding grip throws/drops the grenade.
  It cannot return to the chest or refund ammo.
- Frag/pomegranate: a new B/Y press on the holding hand starts cooking. Holding the
  button before pickup, pressing before pin removal, or repeatedly pressing
  during cooking cannot pre-arm or restart it. Flash and smoke do not cook.
- Without cooking, the native fuse starts at release. Throwing a cooked frag
  preserves its remaining fuse. Holding past expiry creates an immediately
  due native grenade at the held position; native explosion, owner attribution
  and damage events handle the result. No direct health subtraction is used.

All three chest pickups use the anatomical palm and the same body-aligned box
policy. Tactical and lethal boxes are 20 cm deep, 16 cm wide and 32 cm tall;
their centers follow their item anchors. The knife box is 24 cm deep and 40 cm
tall, with its center 4 cm above the handle anchor. Widths remain symmetric with
a 2 cm gap at the default 18 cm spacing. A 200 ms fresh-grip intent is shared by
all three pickups, so early squeezes cannot favor the neighboring grenade slot.

Fuse time now uses the guarded native player accessor, giving frag/pomegranate
3500 ms and flash 1000 ms in the captured definitions. The SDK's timer field
names were reversed for this H2 path; see [native evidence](vr-cheat-throwables.md).
Smoke uses its loaded definition. Native simulation time freezes cooking during
pause. Measured hand velocity is filtered and capped at 15 m/s before tuning:
`vr_grenadeThrowSpeedScale` defaults to 2.5, and `vr_footballThrowSpeedScale`
defaults to an additional 1.5 (3.75 total for football). The resulting ceilings
at these defaults are 37.5 / 56.25 m/s, with a final native-unit safety bound.
Zero/stale motion, forced drops and cook expiry do not gain a synthetic impulse.
Spawn retries retain the already-scaled release vector and position.

Clearance still prevents releases through walls. An obstructing NPC adjusts the
spawn point outward while preserving velocity for the native hitbox/damage/pain
path. The global `g_minGrenadeDamageSpeed` is unchanged; a weak toss can still
fall below its native threshold. Status reports raw/scaled release speed,
requested/admitted native speed and NPC/world obstruction counts.

## Pomegranate and football

`h2_cheatpomegrenade` follows frag behavior, including a simulated two-hand pin
pull, transfer lock during manipulation, B/Y cooking and native explosion. The
interaction point is near the fruit crown. Its intact fruit model never gains
an invented ring, pin or spoon: the same gestures move the hand and produce native
sound/haptic feedback without pin/lever debris. Native token, shared `usgrenade`
ammo, fruit explosion FX and sound are preserved.

`h2_cheatfootball` has no pin, fuse or cook. Pickup immediately makes it throwable;
releasing Grip outside its chest slot throws it. Returning the palm to the pickup
box of its current native tactical/lethal slot and releasing Grip stows it without
spending ammo. One native ammo debit is reserved
after release preflight and retained across failed native spawn retries. An
unpaid ball is safely stowed on tracking/context loss. Native zero-fuse rigid-body
physics owns contact damage (base 200), bounce and lifetime; no blast damage or
synthetic melee event is added.

Both use their actual variant projectile model on the chest because the native
world-model field still points to M67. Football's stowed render placement is
scaled to a 10 cm diameter, including its center offset. Held and thrown geometry
and collision remain full native size. Variant hand poses come from the actual
model/animation pair; pomegranate shares the original M67 hand animation. Slot
placement follows native primary/secondary selections, not the offhand class.

The authored native holding hand is right for all supported source animations.
The opposite hand uses anatomical hand mirroring and a 180-degree rotation
around the grenade's local vertical axis, preserving its upright direction and
grip origin. Pin geometry and interaction points follow the same transform.
Grip/pin finger poses come from the native idle and pull-pin animations and
close over 120 ms. Pin contact uses the visible pin geometry and the anatomical
palm-to-pinch reach, rather than a distance from the controller wrist alone.
The acquisition radius is 10 cm. During extraction the hand and ring follow a
fixed local +Y slide with an 8 cm stroke; lateral/backward movement cannot
increase extraction progress. Hand IK reuses the weapon-part constraint, and
visible pin travel comes from the current solved render record rather than the
lower-frequency server snapshot. Physical input remains unconstrained for
simulation and breakaway checks. Hand-closing interpolation uses pose timestamps.

## Native boundary and lifecycle

`native_grenade.cpp` validates the stock projectile entry, its caller's six
arguments, and the native fuse scheduling site. It submits the actual weapon,
position, velocity and remaining fuse to `0x1404CF130`. The native engine owns
projectile trajectory, collision, AI response, grenade-fire script notification,
explosion effects and damage. It is called once per accepted release, on the
server owner. No per-eye gameplay calls occur.

The central arbiter prevents a throwable grip/pin pull from overlapping a gun,
knife, magazine or another mechanical interaction. Accepted hand ownership is
reported back after the transaction. Ammo is compared and debited at successful
pin removal through `native_ammunition::commit_carried`; football reserves its
debit at release preflight. Acquisition and safe return never debit/refund.
No unbounded projectile or event queue is added.

On focus/tracking loss, disabled interaction, script takeover or forced firearm
selection, safe grenades return and committed grenades drop with zero synthetic
velocity. Already cooking grenades retain their deadline. Failed native spawn
retains the committed state for retry and never resets the timer or refunds it.
Zone unload / native player replacement retires the state with the replaced
inventory. Pin movement is measured relative to the held grenade, so moving both
hands together does not remove the pin.

## Rendering

Original source models: `viewmodel_m67`, `viewmodel_m84`,
`viewmodel_ussmokegrenade`. Subset vertices, including rigid groups, remain in
common model bind space. Every piece therefore uses the same model-to-body
transform; reapplying an individual bone bind displaces M67's pin/ring.
M84 is a skinned surface: body/lever and pin assemblies form separate closed
triangle partitions in bind space. The reusable `scene_models::rigid_part`
partition builder preserves material/vertex buffers and creates private index
buffers; mixed partition triangles and malformed influences reject creation.
No shared model, skin, material or weapon definition is modified.

Held items replace their chest depiction. Render placement follows the exact
solved hand record and model origin for each eye. Pin pieces move during pulling
and detach after commitment, remaining visible for a brief 450 ms cosmetic fall.
The cosmetic pin never causes damage or consumes inventory. The native missile
owns the thrown grenade model.
Assets are resolved by name, not persisted pointers or session-local tokens.

Successful pin extraction and actual native release queue their original weapon
notetrack sounds (`*_pin_plr` / `*_fire_plr`) on the main audio owner. Resolution
uses the grenade definition, independently of the selected firearm. The queue
is bounded, expires stale requests and clears at zone unload. It never invents
an alias if the current definition lacks a unique matching native key. Starting
cook replays the native pin sound once, along with haptic confirmation. Repeated
B/Y presses neither replay the sound nor restart the fuse. No synthetic ticking
sound is added.

The M67 spoon shares the body bone but is a disconnected 540-triangle island.
Its topology is checked against the loaded native surface (2525 vertices, 3020
triangles and a full index-stream hash) before immutable body/spoon index subsets
are created. Cooking hides the attached spoon and shows it springing out and
rotating away as cosmetic debris. The original model and vertex buffer remain
untouched; topology mismatch rejects the asset instead of cutting arbitrary faces.

## Validation and remaining acceptance

`vr_grenade_status` reports native readiness, model readiness by type, acquisition,
pin, return, throw/cook/handoff counters, native spawn/audio failures and current slot state.
It also writes `minidumps/h2-mod-vr-grenades.txt` without triggering an action.

Automated coverage: safe return, failed debit, pin commitment, frag-only cooking,
repeated B/Y, remaining fuse, held expiry, stale/teleported throw motion, opposite
hand rotation, arbiter exclusivity, and GPU skinned prop partition construction.
Native signatures and live frag/M84 geometry were inspected read-only.

Headset acceptance remains required for grip alignment, ring reach, throw feel,
native AI/mission behavior and in-hand explosion. Smoke definitions/models depend
on the loaded mission; this work does not give the player unavailable equipment.
