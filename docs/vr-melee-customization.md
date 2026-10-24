# Chest melee weapon selection

Status: native FF build/readback and CPU coverage; fresh-process/HMD acceptance pending.

The launcher Personalization page sits between Gameplay and Cheats. Its
`vr_meleeWeapon` choice owns only the chest melee slot:

The melee component registers this saved native enum during `post_unpack`,
before profile execution. The launcher also reads the engine's hashed form
(`0x2BA94576`), including earlier profiles containing `baton` under that name.

| Saved value | Display name (zh-CN) | Behavior |
| --- | --- | --- |
| `default` | 默认（跟随游戏） | Ghost Dance normally; Green Beret may supply the existing bayonet/waist picks. |
| `ghost` | 鬼舞 | Force the common H2 knife. |
| `baton` | 甩棍 | Forward grip, collapsed in slot, extends after drawing, fixed 120 base damage. |
| `karambit` | 爪刀 | Reverse grip only. |
| `ending` | 终局之刃 | Clean H2 ending knife, ordinary chest-equipment lifecycle. |
| `bloody` | 血腥之刃 | Bloody H2 ending knife, ordinary chest-equipment lifecycle. |

Every named choice overrides Green Beret's chest/waist melee substitution.
Other Green Beret inventory/ammunition rules remain native. The setting does
not affect mission-owned climbing tools. Endgame's admitted scripted sequence
retains its independent native models, choreography and input ownership; this
chest-equipment owner is suspended for that sequence. SAS knife is not admitted.

Changing the choice returns the current chest tool and invalidates its lease,
motion history, acquisition intent and pending feedback. A held Grip cannot
draw the new choice without a new press. Existing hand arbitration, the single
knife lease, opposite-hand drawing and Grip-release return are retained.

## Presentation and contacts

The selected tool supplies its source wrist-relative attachment and finger
shape. H1 right-handed poses are anatomically mirrored, then retargeted through
the existing offline source/target bind-basis method to H2's finger schema.
H2 glove lengths and physical controller/wrist tracking remain authoritative.
Existing magazine/slide co-grasps retain their own hand authority and map the
selected object through the same handle frame. Forward/reverse changes keep
the handle pivot fixed; the baton and karambit constrain the choice explicitly.

The baton uses the existing retained `scene_models::skeletal_model` service.
Its eleven bones retain the source rods, switch, ring and blended strap skin.
The source first-raise segment (frames 22–45 at 30 FPS) drives deployment;
original camera and stock arm displacement are not replayed in VR. Stored
equipment always uses the collapsed pose. The hand keeps the corresponding
source grip while the player controls its world motion.
Its final root follows a committed native hand solve with the actual tool
hand visible, matching equipment lease/tracking reference and unchanged
DObj/epoch/wrist buffers. Scene submission precedes viewmodel skinning and must
not depend on a completed skin record. The snapshot carries its own render origin; a later
hidden-hand solve or a different camera origin cannot move the baton. Missing
held poses omit that submission, while native skinning retains strap blending.

Deployment cannot arm a melee sweep: motion history is reset until it settles.
Subsequent nine-point traces follow the actual extended rod tip and retain
native obstruction checks, target generation, contact withdrawal and cooldown.
The baton passes exactly 120 base damage into native G_Damage, including when
Ragdoll Impact/EXO is enabled. Native armor, invulnerability and mission damage
callbacks still decide accepted damage. Other tools retain the native policy.

The baton queues independent `vr_melee_baton_hit` / `vr_melee_baton_extend`
one-shots through the existing positional sound owner. Their source recordings
are H1 baton stab variants and first-raise extension. Its native knife-contact
audio event and `flesh_impact_knife` particle/screen-blood path are excluded, so
it does not duplicate the ordinary knife sound or paint screen blood. Other
knife feedback remains native at the selected model's real FX socket. Queues
are bounded and reject stale hand leases, tracking references and zone epochs.

## Dedicated asset zone

`data/zone/h2_vr_melee.ff` is loaded with common client resources. It contains
five models, 54 source animation curves, the H2 material/shader providers,
self-contained native BC texture mip chains and two baton sound aliases.
No additional native inventory WeaponDefs or item grants are introduced.
The raw exports remain private; ordinary builds and packaging consume the FF.

Optional rebuild tools accept explicit input roots:

- `tools/prepare_melee_assets.py`: bounded H1-to-H2 XModel conversion, shared
  upstream XSurface/XAnim records, H2 native material/state providers and exact
  CASC image slices. H2-only model fields come from a native H2 knife template.
- `tools/casc_slice.cpp`: optional offline CascLib reader, compiled against the
  existing `casclib.lib`/`zlib.lib`; never linked into the client.
- `tools/prepare_melee_audio.py`: selected source recordings re-encoded as
  verified 1024-sample-block FLAC; H2 alias templates retain one-shot starts.
- `tools/generate_melee_poses.py`: existing external guarded sampler/retargeter,
  explicit source roots, compact generated H2 attachment/finger/bone data.
- `tools/build_melee_zone.py --game-root GAME --tool ZONETOOL --assets SOURCES`:
  isolated compile, native verify, dump and readback before promoting the FF.

The texture port preserves native compressed formats and packed specular alpha;
H1 materials use the reviewed H2 knife shader provider with a neutral occlusion
map. No H1 runtime addresses, ABI, damage events or inventory formats are reused.

`vr_melee_status` reports saved choice, resolved variant/model, grip, damage,
lease state and baton sound counters. HMD acceptance must check both hands,
each admitted grip, source finger fit, co-grasps, slot clearance, baton strap and
deployment, hit/extension audio, no screen blood, pause/recenter, option changes,
Green Beret on/off, checkpoint/zone transitions and Endgame exclusion.

For reports of silence across the entire game, enable **Debug > Audio output
diagnostics** (`vr_debugAudioOutput`) in the launcher and restart the game.
It defaults off; disabled processes install no audio diagnostic hooks, make no
diagnostic COM queries and write no audio diagnostic file. When enabled,
`vr_status` includes the native
WASAPI output lifecycle snapshot; the same snapshot is written as
`overlord-audio.txt` in the diagnostic output directory. It records endpoint
identity/state, native start result, buffer/padding query results and the game's
own session volume/mute/PID. These are observations, not proof of audibility;
no device, volume, or session preference is changed.
