# VR cheat throwables

## Confirmed native properties

| Property | Ordinary frag | Pomegranate | Soccer ball |
| --- | --- | --- | --- |
| Weapon name | `fraggrenade` | `h2_cheatpomegrenade` | `h2_cheatfootball` |
| Direct damage field | 1 | 1 | 200 |
| Explosion radius | 256 native units | 256 | 0 |
| Inner / outer explosion damage | 200 / 50 | 200 / 50 | 0 / 0 |
| Native player fuse | 3500 ms | 3500 ms | 0 |
| Other fuse slot (AI path) | 5000 ms | 5000 ms | 0 |
| Cook enabled in definition | Yes | Yes | No |
| Nominal native launch speed | 940 units/s | 940 | 300 |
| Ammo / clip key name | `usgrenade` | `usgrenade` | `football` |
| View model bones | Body plus pin/ring | `j_gun`, `tag_clip` only | `j_gun`, `tag_clip` only |
| View model dimensions | 7.77 × 7.28 × 9.79 cm | 12.64 × 12.64 × 14.77 cm | 28.25 × 28.34 × 28.34 cm |

Definition numbers are native inputs, not promises that every target loses that
amount after script, armor, or damage rules. Both cheat weapons are inventory
type 1, weapon type 2, and offhand class 1. Registry tokens are dynamic;
production code must match definitions and assets rather than persist token IDs.

The primary and secondary offhand slots can select pomegranate and football at
the same time. Chest placement follows native slot selection; offhand class
alone does not identify the selected slot.

### Assets and visible structure

- Pomegranate held model: `h2_viewmodel_cheat_pomegrenate`; projectile:
  `h2_cheat_pomegranate`. Geometry/material inspection shows fruit and calyx,
  with all geometry weighted to `tag_clip`. No pin, ring or spoon structure was
  present. Preserve the native spelling of each asset name.
- Football held model: `h2_viewmodel_cheat_soccer_ball`; projectile:
  `h2_projectile_cheat_soccer_ball`. All visible geometry belongs to `tag_clip`.
- Both definitions' `worldModel[0]` still points to `weapon_m67_grenade`.
  The chest must select the actual variant asset explicitly, or it will show M67.
- Pomegranate reuses the ordinary `h1_wpn_grn_m67_*` idle/pullpin/throw animations.
  The flat-game pullpin gesture therefore does not prove a physical pin exists.
- Football has its own `h2_wpn_grn_cheatfootball_*` animation set, including an
  animation named `pullpin`, despite having no pin geometry or timed fuse.
- Pomegranate uses `vfx/props/h2_cheat_pomegrenate_explosion` and
  `h2_wpn_pomgrenade_exp`. It preserves frag damage/timing behavior while having
  variant appearance and effects; replacing its token with ordinary frag would
  lose those effects and native ammo semantics.
- Football's projectile has the dedicated `h2_cheat_soccer_ball` physics preset
  and `h2_projectile_cheat_soccer_ball` collision map. Its weapon-level preset
  still says `bucket_metal`; do not confuse that fallback with projectile physics.

## Native runtime behavior

The pomegranate uses the native player fuse and area-explosion path. Its native
fuse is 3500 ms and its cook state decreases while held. The football has no
timed fuse; its projectile retains dedicated physics and collision definitions.
Do not infer an explosive timer or automatic short lifetime from the model's
animation names or generic weapon-level physics field.

## Native fuse selection

The SDK labels +0x7D4 `aiFuseTime` and +0x7D8 `fuseTime`, but the native call chain
disagrees with those names. `0x14051D85D` tests the thrower's client pointer and
passes `client == nullptr` as the third argument to `0x1406A4080`. That accessor
selects +0x7D4 for false (player) and +0x7D8 for true (non-player/AI).

The player values are 3500 ms for frag and pomegranate and 1000 ms for flash.
The AI values are 5000 ms and 2000 ms respectively. `native_grenade::describe`
uses the native accessor rather than relying on SDK field labels, while the
existing VR timer start and cook rules remain in force.

## Native impact and direct-hit behavior

Frag and flash definitions specify direct damage 1. Native impact code can skip
ordinary grenade damage below `g_minGrenadeDamageSpeed` (400 units/s, about
10.2 m/s at the project world scale). VR launch velocity is derived from tracked
hand motion and capped at 15 m/s. The release-clearance trace can also remove
velocity when blocked. Preserve native hitbox contact, damage attribution,
script vetoes, and AI pain/stagger behavior. Do not substitute melee damage or
manually start an AI animation. Any VR-owned impact adaptation requires separate
native and headset validation.

### Throw-speed policy

- Proposed initial general gain: **2.5x**, applied to frag, flash, smoke,
  pomegranate and football from the central physical-throw boundary.
- Proposed football-only additional gain: **1.5x**, multiplying the general
  gain for a nominal total of **3.75x**. Match the actual native football profile,
  not whichever firearm happens to be selected.
- Expose the two gains independently as saved tuning values, with finite bounded
  inputs, so range can be adjusted from headset tests without changing physics
  constants or modifying shared WeaponDef assets.
- Compute/filter raw tracked velocity first, then apply gain exactly once at
  release. Retain the existing raw 15 m/s sanity cap; raise the final ceiling
  with the gains (37.5 m/s and 56.25 m/s at these initial defaults) instead of
  accidentally clipping both back to 15 m/s. Native finite/range validation
  remains the final admission guard.
- Zero motion stays zero. Safe return, tracking/focus-loss drops and in-hand
  cook expiry do not receive an artificial forward impulse. Failed native spawn
  retries reuse the already calibrated release vector rather than multiplying
  it again. Pin/spoon cosmetic debris is unaffected.
- Add raw-speed, calibrated-speed and final native-speed diagnostics. Distinguish
  genuinely slow motion from a clearance check that zeroed the velocity. A
  higher gain cannot repair an NPC-obstruction path that clears velocity.
- Validate short/long throws, faster football hits, bounce/CCD behavior, nearby
  NPCs/walls, repeated retries and pathological tracking samples. Increased
  speed can increase range nonlinearly; these defaults still require headset
  tuning and do not assert a measured final throw distance.

The gains are now implemented as `vr_grenadeThrowSpeedScale` and
`vr_footballThrowSpeedScale`; raw, scaled and native admitted velocities are
reported by `vr_grenade_status`. Runtime native-unit validation remains in place.

## Proposed implementation sequence

1. **Native throw contracts, speed and impact parity.** Add bounded diagnostics for
   release velocity, obstruction entity/type, native projectile identity and
   direct damage admission. Apply the central 2.5x gain and additional 1.5x
   football gain described above. Correct the player-fuse accessor. Handle nearby NPC
   obstruction without silently discarding intended throw velocity, while
   retaining world-geometry clearance. Validate direct hit, bounce, explosion,
   invulnerable actors and stationary contact independently.
2. **Separate behavior from visuals.** Replace frag-only assumptions with a
   throwable profile: visual assets, arming interaction, timed/impact behavior,
   optional fuse/cook, ammo debit boundary and chest scale. Centralize profile
   counts instead of extending scattered three-entry arrays. Allow zero fuse
   only for verified untimed profiles. Reuse `spawn_projectile` and the original
   weapon token so football retains its native rigid-body path.
3. **Whole-object geometry and chest presentation.** Both variants use their
   complete native meshes, bypassing pin/lever partition requirements. Use the
   actual variant projectile/held asset for the chest. Target a football chest
   diameter of 10 cm (about 0.35 scale), derived from bounds. Scale only the
   stowed render placement and its center offset; restore full size on pickup
   and preserve original projectile/collision size. Re-author hand alignment
   from each actual model/animation rather than reusing M67 offsets unchanged.
4. **Explicit preparation for pinless objects (proposed controls).** Side grip
   takes/transfers the item. A holding-hand trigger press marks it ready to throw;
   release before readiness returns it to the chest, release after readiness
   throws it. Football has no cook and debits at accepted native throw.
   Pomegranate retains explosive preparation, B/Y cook and native self-explosion
   semantics, with no invisible ring or flying metal spoon. This is a proposed
   VR interaction choice, not a discovered native mechanism.
5. **Regression and headset acceptance.** Frag/flash/smoke behavior, safe return,
   transfer before release, no handoff during active pin manipulation, one debit,
   shared frag/pomegranate ammo, football's distinct ammo, pause/recenter/load,
   no cooked-state inheritance, and no repeated cook cue. Test football on NPCs,
   walls and floors at several speeds and distances, including bouncing and many
   retained balls. Use bounded ownership metadata and native unload lifecycle.
   Verify chest spacing, scaling transition and full-size release obstruction.
