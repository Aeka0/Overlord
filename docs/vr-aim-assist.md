# VR aim assistance: gameplay eligibility

Audited. Assistance selects one native target point for an
otherwise admitted bullet shot. It never changes the tracked gun pose,
creates extra bullets, overrides damage, or grants permission to harm a mission
character. `vr_aimAssistStrength=100` sets a 10-degree muzzle cone; it is not an
instruction to target every visible character or guarantee a hit.

## Native team semantics

Read-only inspection of the running museum build located `getaiarray` at
`0x140505360`. Its team-parameter parser (`0x1404FCF30`) and team-name resolver
(`0x140515F10`) establish the following mapping:

| Team | Native value | Included in `bad_guys` |
| --- | --- | --- |
| `axis` | 1 | Yes |
| `allies` | 2 | No |
| `team3` | 3 | Yes |
| `neutral` | 4 | No |

`bad_guys` is mask `0x0A`; `all` is `0x1E`. This is a fixed native team set,
not a general query for characters that may legally be killed in the current
mission. The native actor iterator examines 64 actor slots, but their entity
IDs belong to the separate 4000-slot H2 entity table. The old VR limit of 2048
could reject valid high-numbered actors; the corrected limit matches H2.

Native candidate admission is retained, including its actor-state exclusions.
VR then checks entity validity, life, muzzle angle/range, civilian identity and
visibility. `getshootatpos` supplies the aim point. Nothing caches a target
across shots, deaths, entity reuse or checkpoint rollback.

## Museum display versus combat

The museum is internally `ending`. Loaded `maps/char_museum_code::_id_C67A`
sets display actors to noncombat, sets health to one, and changes `axis` actors
to `neutral`. Allied display actors keep their team. This explains why static
displays are absent from the normal enemy set even at strength 100.

The panic-button path restores combat behavior and health, then `_id_C123`
changes participants to `axis` and assigns the player as their favorite enemy.
They can then enter normal assistance selection. The museum also sets
`level.friendlyfiredisabled=1`; that suppresses a penalty and does not change
the native team mask or turn a display into an assistance target.

The initial test used static exhibits and live dvar inspection confirmed 100.
Their exclusion explained that result with the original enemy-only policy.

The subsequent user-requested practice exception admits neutral AI only when
`mapname` is exactly `ending` or `museum`. Native `getaiarray` ORs its team
arguments, so one `getaiarray("bad_guys", "neutral")` call supplies the combined
set (`0x1A`). Allied AI stay excluded. Enemies and neutral displays compete in
the same angular selection; neither team has artificial priority. The setting,
angle, distance, life, civilian and obstruction checks still apply. A single
shot commits at most one correction after all queries succeed.

The map is read per shot and never latched across level changes or saves.
Missing/unknown map state and similar names such as `ending_extra` do not
admit neutral AI. Other missions, including Favela after its protected phase,
retain enemy-only admission. The exception does not alter native team values,
mission scripts, damage or friendly-fire rules. It applies to both shot owners.
Headset acceptance of the practice exception remains pending.

## Civilians and mission protection

| Situation | Evidence | VR policy |
| --- | --- | --- |
| Allied actor | Native team mask | Excluded before VR scoring |
| Neutral actor | Native team mask; requested museum practice exception | Admitted only in exact `ending`/`museum` maps |
| Civilian identity on a hostile team | Native friendly-fire script classifies civilian identity separately from team | Excluded from assistance |
| Favela runner during capture | Mission-specific damage-location failure logic | Keep admitted manual aim during the existing protected phase |
| Unalerted hostile guard | `noncombat` describes alert state, not civilian identity | No blanket alert-state exclusion |
| Other scripted protected actor | No universal kill-permission API established by this audit | Requires evidence from that mission; do not infer permission from team alone |

`maps/_friendlyfire::friendly_fire_think` checks same-team damage separately
from civilian identity. Its civilian classification recognizes `type ==
"civilian"`, a class name containing `civilian`, and the explicit
`upperdeck_canned_deaths_drone` target name. Normal actor spawning starts this
friendly-fire watcher; scripted drones have a separate registration route.
Penalty behavior may then depend on per-level callbacks, participation points,
`no_friendly_fire_penalty`, or airport-specific conditions such as
`loc_airport_fail_civilian`.

VR reuses those civilian identity checks after enemy admission, independently
of penalty switches. This is the mod's conservative assistance policy; the
friendly-fire script is evidence for civilian identity, not proof that every
native aim-assistance system implements the same filter. Field reads run only
for candidates that can improve angular selection. A query failure preserves
the original shot. A rejected civilian does not prevent selection of another
eligible visible enemy. Manual fire continues through native damage rules.

The retained Favela capture gate is necessary even for a hostile runner:
`maps/favela::_id_ABD6` monitors damage and `_id_ABC3` treats lower/upper torso,
neck, helmet and head hits as fatal objective errors. Grenade hits have a
separate failure branch. A successful allowed hit follows `_id_D2F7` into the
capture/interrogation flow; it must not be redirected to a generic torso point.
VR therefore retains manual muzzle aim throughout the existing opening phase.
`torture_sequence_done` restores ordinary assistance after interrogation.
Native later start points (`soccer`, `hilltop`, `trailer1` through `trailer3`,
`end`) bypass that opening gate. Missing phase state fails closed and a loaded
checkpoint re-evaluates current native state.

Favela registers a civilian chatter callback for neutral spawners and contains
explicit civilian fleeing/spawn sequences. This supports neutral civilians in
that mission, but does not establish the team of every civilian in every map.
The independent civilian identity check avoids relying on that assumption.

`ignoreme`, `ignoreall`, invulnerability and `noncombat` are not treated as
universal protection rules: mission scripts also use them for staging and
stealth. Native `enableaimassist`/`disableaimassist` methods were inspected;
they toggle entity-state flag `0x4000` after specific entity-type checks. Their
use on scripted models/vehicles does not establish a universal AI eligibility
test, so this change does not impose that flag on all actors.

## Diagnostics and verification

Projected native fire and independent instance fire both call the shared
adapter. The previous projected-only counters were incomplete after the
independent-fire integration; accounting now belongs to the adapter.
`vr_fire_status` and `vr_dual_fire_status` report both routes, native candidates,
whether neutral admission was enabled for the last evaluation,
protected candidates, visibility traces, selected entity and correction angle.
The counters describe evaluations, not confirmed damage or hits.

`vr-aim-assist-tests` validates geometry and entity limits.
`python tests/vr/aim_assist_adapter_tests.py` reuses the earlier local native
adapter fixture as a repository test. It compiles the actual adapter body
against deterministic query/trace endpoints and covers both shot routes,
native team filtering, museum/ending neutral admission and map transitions,
allied exclusion, all three civilian markers, fallback to another enemy,
Favela start points and checkpoint rollback, cover, and failure atomicity.
The mocked team set follows the separately audited native mask; it does not
execute the original engine's team parser.

Combat headset acceptance and a complete audit of all mission-specific
protected actors remain outside the verified scope. No civilian was shot, no
mission event was forced and no live game code was patched for this audit.
