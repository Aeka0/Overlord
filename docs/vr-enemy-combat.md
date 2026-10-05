# Enemy close combat in VR

Settings → Gameplay → Enemy close combat contains:

| Setting | Console variable | Default | Range |
| --- | --- | --- | --- |
| Enemy melee damage multiplier | `vr_enemyMeleeDamageScale` | 0.5 | 0.1–2.0, step 0.1 |
| Prevent dog knockdowns | `vr_disableDogPounce` | On | On/off |

Both settings are saved through the shared launcher schema and native saved
dvars. Profiles without these entries receive the defaults. Changes made through
the console affect subsequent attacks; launcher changes apply on the next launch.
Setting the multiplier to 1 restores the original incoming melee damage.

## Native behavior

The existing `notifies` component remains the sole owner of `G_Damage`
(`0x1404BD2E0`). A reusable native damage filter runs after Lua damage callbacks,
before the original engine calculation. The VR policy requires a player target,
an AI actor attacker on a different nonzero sentient team, and `MOD_MELEE` (8)
or `MOD_MELEE_ALT` (9). This includes hostile dog bites and neutral actors that
actually attack the player in the museum. Bullets, explosions, environmental
damage, friendly teams, player attacks and NPC-vs-NPC damage are unchanged.

The filter scales incoming damage once. Positive fractional results round to the
nearest integer, with a minimum of one and an overflow guard. Native difficulty,
invulnerability, shields, health application and notifications still determine
the outcome. `player_meleeDamageMultiplier` remains owned by the original game
scripts; the VR setting is an additional relative multiplier.

`animscripts/dog/dog_combat::meleebiteattackplayer` chooses ordinary bites or a
knockdown through `dog_cant_kill_in_one_hit`. The adapter verifies that function's
bytecode and conditionally redirects its first field read to the existing native
`return 1`, after parameter validation. Ordinary bites keep the original attack
animation, notetracks, hit detection, damage, miss recovery and AI loop. The
knockdown branch does not start its player-view/QTE sequence. No dog or player
script fields are overwritten. Disabling the setting restores the original
decision immediately; VR off also preserves original behavior.

The guard is rebound on level start, retained across checkpoints that keep their
scripts, and cleared before script release. It governs new attack decisions; an
already active or saved knockdown sequence is not forcibly unwound.

## Verification

`vr_enemy_combat_status` reports damage-layout and dog-script binding, current
settings, filtered-hit count and last raw damage before/after scaling, plus the
number of forced bite selections. `dog_bound=0` can mean dog scripts are absent;
a present but changed script logs a signature rejection instead of guessing.

Offline coverage checks damage-kind and ownership exclusions, multiplier bounds
and overflow, dog bytecode mutations, saved configuration, and Chinese/English
launcher controls. In-game acceptance still needs ordinary enemy melee and dog
encounters at multiple difficulties, the pounce switch off/on, checkpoint reload,
and a VR-off comparison. Builds and policy tests do not establish headset comfort
or the number of hits required to kill a player with regeneration and shields.
