# M4 mechanical and variant capture

Status (2026-09-08): read-only native capture in a non-VR session, plus offline
model/animation analysis. No M4 reload implementation, deployment or new HMD
acceptance in this capture. Preserve the existing `foregrip` aiming profile and
its coordinated hand poses; this is additional mechanical evidence.

## Captured assembly

Native name `m4_silencer`, base capacity 30, non-segmented/non-additive reload.
The observed local token was 32; it is evidence, not a production lookup key.

| Model | Bones | Role |
| --- | ---: | --- |
| `viewhands_us_army` | 68 | Hands; not weapon identity |
| `h2_viewmodel_m4_base` | 24 | Receiver |
| `attach_h2_mp5k_foregrip_vm` | 1 | Existing authored support grip |
| `attach_h2_m4_cover_vm` | 1 | Cover |
| `attach_h2_silencer_01_vm` | 2 | Suppressor |

Receiver-local `tag_foregrip`, `tag_cover` and `tag_silencer` resolve the three
attachment root aliases. Native `tag_sight_off` was hidden; retain the engine's
sight selection instead of resetting all visibility bits. Do not hard-code the
captured global bone indices when changing hand models or attachments.

## Independent-object sight visibility, 2026-09-13

Read-only Team Player sample: `m4m203_eotech`, native token 64, with M203,
PEQ6 and `attach_h2_eotech_2_vm`. Its `WeaponDef.hideTags` contains
`tag_sight_on`. Native DObj hides that upright sight (receiver-local bone 19)
while retaining `tag_sight_off` (local 18, folded). The independent VR DObj had
an empty native hide mask, exposing both sight states alongside the EOTech.

Independent objects now bind their own definition's 32-entry hide-tag table to
their actual assembled bone order and apply it under the existing DObj lock
before skinning. The verified native visibility getter consumes this mask.
Binding is refreshed on object recreation or weapon identity change. It neither
copies the selected weapon's mask onto other held instances nor modifies shared
models. No-optic variants retain their own native choice. Existing mechanical
part filtering and arm omission remain separate layers.

Client Debug build and weapon-grip tests pass, including native-mask reproduction,
different glove bone counts, optic-to-iron changes and independent held variants.
This candidate is local only; the user requested no deployment or restart while
collecting further Team Player issues. HMD acceptance remains pending.

The current weapon has no M203. Its receiver still contains `tag_m203`, and its
native sound map contains M203/selector keys. Neither proves an attachment is
present or authorizes an underbarrel firing module. Actual assembly and native
mode/ammo contracts must agree. See [variant composition](../../../../../../../docs/vr-weapon-variants.md).

## Parts: observed evidence versus inferred roles

Export coordinates are centimetres in the receiver frame. These are discovery
bindings, not calibrated VR contact anchors or accepted interaction roles.

| Bone | Geometry / animation evidence | Role to verify visually |
| --- | --- | --- |
| `tag_clip` | 2,758 rigid vertices; moves in tactical/empty reload; child `j_bullet` | Detachable magazine |
| `j_reload` | 718 rigid vertices at rear/top; translates in first-pullout | Charging handle |
| `j_reload_trigger` | 168 rigid vertices; child of `j_reload`; rotates in first-pullout | Charging-handle latch |
| `j_clip_release` | 215 rigid vertices; rotates during empty reload with a chamber-close event | Bolt catch/release candidate; do not infer magazine-release behavior from its name |
| `j_flip` | 495 rigid vertices beside receiver; rotates during inspection | Dust-cover candidate |

No separately identified bolt-carrier bone was established in this audit.
The charging handle and feed/bolt readiness must be separate concepts: do not
keep the handle rearward merely because the feed is locked open, or reuse the
pistol slide's locked-travel setting for both. The exact handle, latch and
release interaction design remains a separate implementation decision.

An independent, single-bone `h2_weapon_m4_clip` is already loaded. Its exported
mesh has 2,573 vertices and no mixed weights; this is the preferred detached-item
candidate, subject to body alignment and native validation. Its bind frame is
not the same as the receiver's `tag_clip` frame; do not assume zero registration
offset. Conversely, the receiver has six mixed-weight vertices and a native
surface with flags 6: it cannot be treated as the all-rigid Magnum receiver or
sent unchanged through the new rigid-subset factory. Never hide a whole shared
surface to remove only the magazine or a moving part.

Receiver SHA-256:
`90307e116e650ae82a652c24bac94b8ef9fcec4af19f9b097e82a3c56fd3ff44`.
Independent magazine SHA-256:
`fada1bbba58d53932bf44b9cd852c36374ef3870c19ca3b2e6f89afc1de8d0e4`.

## Animation and sound sources

Clips use the prefix `h2_wpn_asl_m4a1_`; frame numbers below are zero-based export
indices at 30 fps, not runtime deadlines.

| Clip | Frames | Relevant notetracks |
| --- | ---: | --- |
| `reload` | 62 | 9 clipout; 31 clipin |
| `reload_empty` | 71 | 8 clipout; 32 clipin; 50 chamber_close |
| `pullout_first` | 38 | 5 first_chamber; handle/latch motion source |
| `inspect` | 131 | 3 look_01; 48 look_02; 92 rest |
| `gl_reload` | 62 | 7 clipout; 31 clipin |
| `gl_reload_empty` | 71 | 8 clipout; 32 clipin; 50 chamber_close |

Idle, GL idle, fire and fire-empty were also inspected. The GL clips provide
offline comparison only; their presence is not live GL-variant acceptance.
Inspection animates even muzzle/foregrip/silencer tags. Normalize through the
current gun frame and select a deliberate pose; do not adopt an arbitrary
inspection frame as a stationary grip or muzzle reference.

The captured WeaponDef resolves these reload keys to same-named aliases:

- `weap_m4carbine_clipout_plr`
- `weap_m4carbine_clipin_plr`
- `weap_m4carbine_chamber_close_plr`
- `weap_m4carbine_first_chamber_plr`

It also maps `wpn_h1_m4_ins_look_01`, `wpn_h1_m4_ins_look_02` and
`wpn_h1_m4_ins_rest`. The AK-named chamber key and M203/selector keys in the same
map are not a reason to trigger those sounds for every M4 operation. Future VR
feedback should use the existing native map/playback service once the semantic
mechanical transition commits. No extracted audio is needed.

## Native ammunition transitions

Both branches were captured while running the unchanged deployed executable,
using read-only process access at approximately 20 ms polling intervals. Values
below are first observed samples, not exact native scheduling guarantees.

| Branch | Counts | Native animation / state | Reload entry timer / delay | At ammunition transfer |
| --- | --- | --- | --- | --- |
| Tactical | 30/629 -> shot 29/629 -> 30/628 | 20 / 9 | 1,988 / 1,358 ms | Still 20 / 9, timer 588 ms, delay 0 |
| Empty automatic | 30/628 -> 0/628 -> 30/598 | 21 / 9 | 2,340 / 1,380 ms | Still 21 / 9, timer 943 ms, delay 0 |

Ordinary shots were animation/state 2/6, the last shot 3/6; both reloads returned
to 0/0. Transfer appeared about 1.36 s and 1.39 s after the respective first
reload observations. Do not turn these values into VR insertion cooldowns:
ammo transfer, mechanical readiness, hand animation and sound notetracks are
distinct boundaries. Future physical loading must not wait for native reload
timers or cosmetic completion.

## Evidence and remaining scope

Removable local records under ignored asset-audit storage:
`m4-live-20260908.json`, `m4-mechanical-20260908.json`,
`m4-native-reload-20260908.json`, `m4-native-empty-reload-20260908.json`.
They contain full assembly, source hashes, channel samples, sound mappings and
native transitions; raw reports and parser tools are not build dependencies.

Remaining: canonical magazine/body registration, authored contact volumes and
part-grasp poses, visual confirmation of inferred roles, exact native bindings
for other variants, muzzle/attachment validation, and HMD interaction acceptance.
The current sample does not cover a launcher, other optics, other gloves or
attachment-mode switching.

## 2026-09-09 M203 and foregrip comparison

This follow-up is read-only data collection, not a physical-reload rollout.
The earlier scope statements above describe the 2026-09-08 sample only.
The same running game was sampled first with M203 and then the suppressed
vertical foregrip. No code was injected, no runtime memory was written and the
game was neither stopped nor launched for this collection.

| Observed identity | Actual attachments | Native idle |
| --- | --- | --- |
| `m4_grenadier_airport`, rifle mode | `attach_h2_m4_cover_vm_icon`, `attach_h2_m203_vm` | `h2_wpn_asl_m4a1_gl_idle` |
| Same weapon, launcher mode | Same assembly | `h2_wpn_asl_m4a1_gl_grenade_idle` |
| `m4_silencer` | `attach_h2_mp5k_foregrip_vm`, `attach_h2_m4_cover_vm`, `attach_h2_silencer_01_vm` | `h2_wpn_asl_m4a1_idle` |

All three use `viewhands_us_army` and `h2_viewmodel_m4_base`. The airport cover
is a distinct model, not an automatically accepted spelling of the prior cover.
Its root aliases receiver-local `tag_cover`; M203 aliases `tag_m203`. The live
duplicate pairs establish those parents despite attachment-parent bytes of255.
The M203 assembly has100 total bones versus96 in the suppressed foregrip.
Receiver-local `tag_sight_off` remains hidden. Do not identify weapon parts by
these particular global indices, glove name, clip size or animation prefix.

### Alternate feed and timing

The main PS weapon token stays the rifle token in launcher mode. In this capture
PS+0x3c0 bit0x4000 identifies the alternate mode, consistent with the existing
native-ammunition adapter's deliberate alternate-mode rejection. The explicit
WeaponDef link is `m4_grenadier_airport` -> `m203_m4_airport`, capacity30 ->1.
The launcher definition has no reverse alt link. Do not invent a symmetric graph.

The airport rifle uses clip key57/reserve key32; its launcher uses key58 for
both. `m4_silencer` uses key32 for both. These local key values demonstrate
separate clips and potentially shared rifle reserve pools; they are observations,
not authored production identities. Compare the full native ammo-key contract.

The retained launcher cycle shows 1/10 ->0/10 ->1/9 while rifle ammo stays30/630.
The shot was action3/state6; native loader action20/state9 had first-observed
timer2578ms/delay1478ms and transferred the round while still reloading. Its
remaining timer then read1079ms. Mode return is action26/state2; the0x4000 bit
clears while `gl_grenade_2_bullet` is still playing, before `gl_idle` settles.
The opposite mode switch is also observed. Neither cosmetic animation state nor
the unchanged main token alone is sufficient to choose a feed.

The airport rifle tactical sample shows27/182 ->30/179, action20/state9;
first-observed timer2002ms/delay1372ms, transfer with625ms remaining. Initial
static observation was28/182 before a further shot. Report actual counts, not
the nominal requested number of shots. These are sampled observations, never
physical insertion cooldowns. The foregrip empty branch is re-captured separately;
the earlier nonempty/empty foregrip record remains available for comparison.

The new foregrip window records two complete empty transfers,0/538 ->30/508 and
0/508 ->30/478, action21/state9; first entry timer2360ms/delay1400ms, first transfer
timer951ms/delay0. It does NOT contain a new tactical action20 transfer: retain
the 2026-09-08 nonempty foregrip sample rather than synthesizing one from the
user's reported two-shot burst or counts observed in separate windows.

### M203 parts and source poses

The seven-bone M203 contains a static root, `j_m203_button`, `j_slider_m203`,
`j_trigger_m203`, parent `j_grenade_m203`, and separate `j_grenade_main` /
`j_grenade_shell`. All exported M203 vertices are rigid and each triangle belongs
to one bone. Native surfaces match the rigid grouping: body/button/slider/trigger
share one surface; the projectile and shell share another but separate groups
(1,292 and968 triangles). This supports a future independent spent shell without
requiring extraction of a new combined magazine-like mesh. Rendering is still
to be implemented; names alone do not validate physical button semantics.

The sampled slider moves approximately9.17cm forward along gun-local X. Native
`gl_grenade_reload` export has79 frames at30fps, with open/load/close sound
notetracks at12/39/57. Both main and alternate WeaponDefs map the corresponding
`weap_m203_chamber_open_plr`, `weap_m203_load_plr` and
`weap_m203_chamber_close_plr` keys to native aliases. Source frame labels are
pose/event evidence, not runtime deadlines; no audio extraction is needed.

Offline pose witnesses preserve both wrists and all finger rotations for
standard idle, GL rifle idle, GL launcher idle, magazine insertion, first-pullout
and selected launcher loading frames. The rear wrist stays effectively identical
between idle modes; the support wrist and finger pose differ substantially.
The native first-pullout operates the charging handle with the RIGHT hand in
both rifle variants: never use the sampled left support wrist as its grasp.
No retargeted/calibrated VR contact pose is accepted by this capture.

### Shared skinned receiver: data contract now observed

Both assemblies return identical receiver bind data, surface metadata and
skinned blend/index bytes. Surface3 has4,159 vertices /5,900 triangles, flags6:
4,153 one-weight vertices and6 two-weight vertices. The latter all bind gun plus
dust cover, NOT magazine. Native bone offsets are64-byte multiples within the
receiver's24-bone range; blend stream length and triangle indices are bounded
and validated against their counts. Export and native classification agree on
3,910 magazine-only triangles,1,990 others, and no magazine-crossing triangle.
An additional rigid surface contains304 magazine vertices /428 triangles.

This makes a narrow magazine triangle mask plausible without discarding receiver
geometry, but the existing rigid-group-only renderer still cannot consume the
skinned surface as such. Preserve original skinning, source assets and immutable
buffer lifetimes; validate actual draw-packet integration before enabling M4
physical reload. Independent magazine registration remains a separate task.

Local removable evidence: `m4-m203-*-20260909.json`,
`m4-foregrip-*-20260909.json`, `m4-m203-mechanical-20260909.json`,
`m4-m203-poses-20260909.json`. An interrupted/network-delayed first launcher
window contains idle only and is retained as such; its successful retry contains
the shot and reload. Empty windows are not relabeled as completed action samples.
