# HEI Round

Status: implementation candidate; native gameplay, visual size, sustained-fire
performance and HMD acceptance remain pending.

Cheats > Fun features > **High-explosive ammo** (高爆弹药 in Simplified Chinese;
`vr_heiRound`, default off) adds a weak
explosion, a short original-game fireball and positional explosion sound at an ordinary player bullet's
first eligible native contact. Existing bullet damage, flesh effects, decals,
material effects, ammunition and mission firing notifications remain native.
Each ordinary discharged round has at most one addition across penetration;
a missed segment does not prevent a later segment from supplying the contact.
Either independently held firearm uses its actual shot identity.

While HEI is enabled, all primary shotguns (AA-12, M1014, SPAS-12, Striker,
Ranger and Model 1887, including variants), admitted underbarrel shotguns,
M200/CheyTac, WA2000 and ordinary carried Barrett/M82 instead launch **one real
underbarrel grenade per shot**. They consume one round/shell from their original
feed and retain their own magazine, pump, lever, bolt and reload behavior.
They do not also emit the original bullet/pellets or the weak HEI impact blast.
With HEI off they resume their original firing delivery immediately. The
mission-owned fixed thermal sniper remains on its separate native story route.
Carried Barrett identification uses its native `barrett` family, shared with
the reload family matcher; `m82` is the exported model/animation family.
NPCs, vehicle/projectile weapons, already explosive bullets, rejected shots,
sky/no-impact surfaces and the local player's shield do not create HEI blasts.
The existing fallback/legacy firing route is outside this initial integration.

The first native trace is observed after the tracked shield's collision
correction. It supplies a copied point/normal; no second forward ray guesses
where the bullet hit. All original pellets and penetration processing finish
before the addition. The scope is closed before native radial damage can run
script callbacks. Player identity, command time, ammunition timeline and zone
generation must still match. Failed or changed-context shots are not retried.

The trace ABI and hook are centralized in `native_bullet_trace`; the physical
shield is its single collision interceptor and HEI/debug diagnostics are bounded
observers. Registration is idempotent and callbacks run without a gameplay lock.
This preserves native shield-part filtering around the original trace and
ensures observers see the corrected contact.

## Tuning and native damage

The damage/radius options below apply to the ordinary additive bullet mode.
Converted shotgun/sniper shots retain the selected native grenade's full damage,
trajectory, arming distance, collision and explosion resources instead.

- `vr_heiRoundDamage`: 20 additional center damage by default; 0..100. Zero
  retains the visual/audio feedback and disables additional radial damage.
- `vr_heiRoundRadius`: 0.75 meters by default; 0.1..2 meters. The runtime converts
  meters through the shared world scale. Damage falls to zero at the radius.
- `vr_hei_round_status`: requested state, native readiness, admitted shots,
  contacts, blast requests, FX requests, discards and queue overflows. These
  counters do not prove visible particles or accepted damage. `sound_ready`,
  `sound_requests` and `sound_rejections` report the verified positional wrapper
  and accepted/rejected audio requests; they do not prove audible playback.

The complete native `G_RadiusDamage` call contract and default cone/trace flags
come from H2's `radiusdamage` builtin at `0x1404FBB10`, which calls
`0x1404BE5B0`. The added damage keeps the real player attacker, actual weapon,
MOD_EXPLOSIVE and native collision/occlusion, immunity and script handling. It
does not directly subtract health, mutate WeaponDef, create a second bullet or
emit the builtin's separate EV_RADIUS_DAMAGE event. Native friendly/self damage
and mission consequences remain applicable.

## Native grenade delivery

`native_launcher_fire` centralizes the same class-6 fire-packet primitive already
used by physical M203/GP25 modules. The conversion prepares this packet before
debit, then preserves the source feed's compared ammunition write and mechanical
settlement. Primary conversions retain one `weapon_fired` notification with the
source firearm's full name; underbarrel conversions retain their existing module
transaction. Only delivery changes to a validated native underbarrel definition at the
real tracked muzzle. No inventory grant, selected-weapon swap, shared-definition
edit or second ammunition debit is performed. Infinite ammunition preserves the
original source feed's ready state through the existing sustained-shot policy.

Native definitions are resolved by reviewed M203/GP25 names in the current asset
database. A resident but unregistered definition may be registered through the
original name resolver. No absent map resource is fabricated or implicitly
loaded. Missing resources reject before debit/notification with a console reason;
the feature does not silently fire ordinary bullets. Positive lookup results are
revalidated, missing lookups are rate limited, and unload/timeline changes reset
the cache. `grenade_requests`, `grenade_spawns`, `grenade_rejections` and
`grenade_reason` are included in `vr_hei_round_status` (reason 1: unavailable
resource, 2: projectile budget). They are not hit/damage acceptance counters.

At most 64 conversion projectiles may remain active. On-demand, generation-checked
metadata releases slots when the native entity is freed/reused. The feature does
not delete a projectile, extend its life or scan all entities each frame. Budget
exhaustion rejects before ammunition debit. Both hands share this limit.

## Additive visuals, audio and resources

The main-thread consumer chooses one of the original common resources
`fx/impacts/small_explosion_a`, `_b` and `_c`. Each is a 125 ms fireball; no
original impact selector or effect is replaced. A half-native-unit normal
offset avoids placing the added effect inside the impacted surface. Ordinary
flesh effects remain, with the added fireball at that same contact.

The same accepted contact also plays the original `bullet_explode_default`
sound alias through the verified positional client wrapper. H2's impact-sound
registration at `0x14039E3D2` constructs this default before its surface variants.
The current non-default alias is resolved on the main thread without retaining
asset pointers. Native variant selection, mixer levels and distance falloff
remain authoritative. Missing FX and missing sound are independent; neither
retries or accumulates delayed playback. Converted grenades never enter this
queue and retain their own native explosion audio.

The value-only queue is bounded to 128 entries. Old events, pause/menu events,
backward time and zone/shutdown generations are discarded. Overflow may omit
the cosmetic addition; it never replays a shot or changes damage budgeting.
FX resolves by name against resident, non-default assets in the current zone
and uses the existing native oriented one-shot queue. No captured pointer,
private FX definition, extracted asset or level-specific package is retained.

Read-only investigation witnessed these resources and their inspected FX,
material/image/technique dependencies in dcburning and a freshly started trainer
process. Their inspected origins are common engine/game zones. This supports
reuse without loading dcburning, but does not establish all-map, checkpoint,
rendering or gameplay acceptance.

## Validation boundary

Focused regressions cover one addition across pellets/penetration, a later
valid pellet after a miss, malformed geometry, sky/local-shield rejection,
the existing fire/debit/notify transaction, shield/weapon interactions and the
default-off launcher Bool. Native-name cases include carried `barrett` variants
and reject the fixed thermal route. `python tests/vr/hei_feedback_adapter_tests.py`
exercises the production consumer and audio helper with engine endpoints replaced:
world position, single playback, independent resources, stale/pause/zone rejection
and current-alias validation. Runtime acceptance must separately check NPC and
world contacts, damage/occlusion and attribution, both hands, shotgun/automatic
fire, native story permissions, pause/checkpoint/zone changes, positional audio
balance and HMD visuals.
