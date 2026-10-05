# Weapon interaction sound cue test build

Cues are waveform-guided candidates. First segment = removal/rear, second = insertion/forward close. Semantic boundaries still require in-game listening. No extracted audio is distributed.

## Runtime integration

`sound_reference::part` now flows from the physical reload profile through the
existing feedback queue and exact WeaponDef key lookup into native playback.
Whole recordings retain the original wrapper. Segments select a stock alias,
verify the selected loaded recording's basename, sample rate and frame count,
then use the native millisecond seek parameter for the second segment. The
first segment ends in the audio producer's PCM ring; it does not use a main
thread timer or alter a shared alias, sound asset, or decoded source buffer.
Reused loaded channels and voices reset their slice budget. Streamed/primed,
looping, randomized-start and unrecognized recordings reject slicing and log
`[VR audio] Slice rejected`, including the alias/file/format needed to diagnose
the actual asset selected on the test machine.

The native producer continues to call fill with a null decoder while buffered
PCM drains. A true fill result requests release of a live decoder exactly once;
it is not a persistent finished-state flag. The slice budget is retired on that
edge, and subsequent null-decoder calls retain the native drain path. Runtime
contract checks include the null-decoder branch and the caller's release/null
sequence. Regression covers repeated post-release callbacks, retained PCM,
natural EOF, streamed bypass and voice reuse. This corrects the observed FAL
202 ms split crash at `0x1405D56AA` from a repeated release request.

The native contract is checked before installing any audio hook. A mismatch
logs `[VR audio] Native slice contract rejected`; normal whole sounds remain
available, while segmented sounds are disabled. The initial patch was tested
on a separate device; the integrated implementation described below has been built
in both client configurations and awaits combined in-game acceptance.

An explicit `sound_reference::notetrack_weapon` can select a shared loaded
WeaponDef map after the playing weapon passes normal identity and capacity
admission. M93R's rear stop uses M9 (`beretta`) this way; all other M93R events
retain their own map. The bounded lookup neither loads assets nor caches asset
pointers. An unavailable donor logs `[VR audio] Shared notetrack source unavailable`.

## Applied profiles and acceptance

- Rear/close split: M82, M14 EBR, WA2000, AA12, ACR, FAMAS, F2000, AK47,
  M4, M16, SCAR, FAL, Mini-Uzi, P90, TMP, TAR21, Vector, AUG, RPD, M240,
  MG4 and L86. SCAR/FAL use the usual rear-then-close order. M4/M16 close
  explicitly uses the second part of the first-chamber recording.
- Rear segment only, retaining the existing whole close: USP, G18, UMP45,
  Desert Eagle (including gold).
- USP (including silenced) magazine removal/catch and insertion use the first
  and second parts of `weap_usp45_clipout_plr`, split at 527 ms in the inspected
  recording. The second part replaces the previous separate substitute
  insertion sound; the already-correct standalone slide-close sound stays intact.
- Removal segment only: M9, M93R, M82, FAMAS, F2000, Vector and AUG. Both
  dropping the magazine and taking it into the hand use this range; insertion
  retains its own binding.
- Variants inherit these settings through the existing profile copies.
- M93R rear now explicitly shares M9's whole inspect-pull recording through
  M9's native map. Its existing removal/insert/close bindings remain local.
- M1911 rear/close reuse the same whole chamber recording. M1014 rack-open
  and rack-close likewise share the existing close recording; port-load audio
  is unchanged. Neither pair requires a new asset or split.
- Desert Eagle rear/close both resolve `weap_de50_chamber_plr`. Rear retains
  its existing first-part policy; close now plays the whole stock release cue,
  as used by the native empty reload. The full and short chamber recordings
  retain guarded rear limits of 163 ms and 151 ms. The former rear inspect key
  remains superseded.

All cut points below require listening on the target build. Hold the handle at
the rear stop before returning it, then repeat with a slow manual return and
a quick release. Check both empty/loaded states and magazine catch/drop.
If an action becomes silent, retain the slice rejection log rather than
interpreting silence as an incorrect cue time. Actual alias-to-file selection
is verified at runtime, not assumed from the WeaponDef key's spelling.

PP2000's source recording is now located; see the live mapping below. Its
current rear/close bindings remain unchanged pending split implementation and
listening acceptance.

## Desert Eagle follow-up audit

HMD reports indicate overlong removal audio and missing impact in slide-close.
Read-only inspection of the running integrated build verified
the standard/gold WeaponDef maps and the loaded main aliases:

- `weap_de50_clipout_plr` resolves to
  `h1_foley/wpfoly_de50_reload_clipout_v1`: 48000 Hz, stereo, 53546 frames
  (1115.54 ms), SHA-256
  `b5fde10549a5d9a34c23313b69fd87efa16b6e31d88093e7d00833486c5ffa1e`.
  Removal currently plays this whole recording; it has no removal split.
  After the initial activity decays around 300 ms, further transients occur
  around 605, 950 and 1106 ms. A candidate 326 ms end preserves the initial
  group and excludes these later events (centered 2 ms RMS: -57.87 dBFS).
- `weap_de50_chamber_plr` resolves to the full 27852-frame recording already
  listed below; its encoded hash matches. The existing 163 ms split assigns
  the strong 90–125 ms activity to rear, leaving the later ~182 ms transient
  for close. A candidate 83 ms split retains both groups on close, but is
  not a silent gap (centered 2 ms RMS: -26.71 dBFS). Waveform amplitude alone
  cannot establish which movement each transient represents; compare the
  candidate rear/close pair by listening before adopting it.

Both aliases name generic secondary foley layers. This audit extracted only
the primary recordings and did not trace a new player-triggered call. Candidate
previews are local analysis artifacts. The 326 ms removal proposal remains
unapplied. The existing 151 ms short-recording cue is unchanged; it is not the
primary alias selected by these weapon maps.

The slide uses the original native release cue; the runtime
animation evidence supersedes the proposed 83 ms close split:
`h2_wpn_pst_de50_reload_empty` emits `weap_de50_chamber_plr` at frame 44
(30 fps). The exported `j_bolt` is already rearward through frame 47 and
returns forward over frames 48–50, with no preceding rearward rack. Both live
WeaponDef maps resolve that key to the full chamber recording. Close therefore
now retains the complete native sound, with no seek offset or slice window.
The ordinary and gold variants share this profile. Rear behavior is unchanged.

## PP2000 chamber source investigation

Read-only inspection of the running integrated build confirmed
that both `pp2000` and `pp2000_silencer` map `weap_pp2000_chamber_plr` to the
same alias. That loaded alias contains one variant and resolves to
`h1_foley/wpfoly_ak74u_reload_chamber_v4`, not a recording named after PP2000.
The file is present: 48000 Hz, stereo, 31658 frames (659.54 ms), 60811 encoded
FLAC bytes, SHA-256
`57f56f578df2aee593f789eba60950439c3976833a512be56bddddebd7345502`.
The alias also names `h2_wpn_foley_reload_chamber_plr` as its secondary layer;
there is no chained alias.

The waveform has two distinct activity groups separated by a low-energy gap.
A candidate split at 159 ms has a centered 2 ms RMS of -85.54 dBFS, before
the second group's onset around 164 ms. This is a waveform candidate, not an
accepted in-game cue. The current profile plays the whole chamber sound on
`action_close` and has no `action_rear` key. No playback hooks or forced sound
calls were needed to locate the asset; this inspection does not claim an
observed player-triggered call. No extracted audio is distributed.

## RPG support-grip feedback

RPG support acquisition and release are separate confirmed carry transitions,
independent of loaded/reserve counts. Both use `weap_rpg_lift_plr`, with two
non-overlapping windows from the captured stock
`h1_foley/wpfoly_rpg_reload_lift_v1` (48000 Hz, stereo, 40969 frames):

| Transition | Source window | Duration | Boundary RMS, dBFS |
| --- | --- | --- | --- |
| Support acquired | 108–167 ms | 59 ms | −61.84 / −58.16 |
| Support released | 443–498 ms | 55 ms | −60.14 / −55.91 |

These quieter transients avoid the louder middle mechanical hits. Native
millisecond seek plus the existing one-shot PCM budget bounds both ends; no
extracted audio is distributed. Explicit windows must match the cue's recording
identity and format, and cannot be combined with another first/second policy.
Normal insertion retains its own whole `weap_rpg_insert_plr` feedback. Twist is
not a support-grip cue. Holding, reconnecting, changing control hands, dropping
or switching weapons does not manufacture additional support transition audio.

The spurious empty-grip reload sequence was traced to prediction-side native
reload admission using a render scene invalidated by a support revision change.
Live evidence showed eight captured state 0 -> 9 / animation 20 starts, while
the authoritative server remained idle. Launcher feed admission now uses the
validated instance/assembly binding plus current physical ownership, independent
of the latest rendered pose/revision. Existing native reload gates therefore
remain closed through support acquisition/release without globally muting the
aliases or touching NPC sound paths. New windows still require headset listening.

After integrating the shared-source fixes and RPG support windows, the physical
reload (including ring/slice/profile and shared-source checks), controller input
and quick reload suites completed with zero failures. Debug and RelWithDebInfo
clients compiled and passed executable/symbol pairing and feature checks.
Regression assertions keep M93R's shared source separate from RPG's own-source
windows. This integrated build has not yet been deployed or game-tested.

## Recording cues

| Recording | Rate | Frames | Split ms | SHA-256 |
|---|---:|---:|---:|---|
| h1_foley/wpfoly_ak47_reload_chamber_v4.flac | 48000 | 44307 | 175 | 4be5eb7f4351c612acad95b6c87141c112de6d3ca52090e0e470741138eba907 |
| h1_foley/wpfoly_beretta9mm_reload_clipout_v2.flac | 48000 | 65321 | 495 | 3ef9760b5997ca8211a90ddf644c6f5e0690e9d9e139e6e36569d3f99597d382 |
| h1_foley/wpfoly_de50_reload_chamber_v1.flac | 48000 | 27852 | 163 | 721b846e89f9433734633caefdb076bcbe57dbe9224d2b84abe0a7a2da037955 |
| h1_foley/wpfoly_de50_reload_chamber_v1_short.flac | 48000 | 27300 | 151 | 0fb5fa5a74743aa3cf61c992ea31eafe8dfc5056582d52664bedc4e5c04aefa3 |
| h1_foley/wpfoly_m14_reload_chamber_v1.flac | 48000 | 36575 | 182 | b324a25c5a143469a8ba24be5083f7b227deb1846ddd696c3d60b9e491553263 |
| h1_foley/wpfoly_m4_reload_chamber.flac | 48000 | 25474 | 137 | c45f753524f4b101f34a90572db56c7978fc495925f83df0ea114ef7e7f134a2 |
| h1_foley/wpfoly_m82_reload_chamber_v1.flac | 48000 | 41847 | 209 | 4bb891e027a47e3f7ede6f281576a8e52d97d1be628530b4725e6cb3675b4899 |
| h1_foley/wpfoly_m82_reload_clipout_v1.flac | 48000 | 79344 | 345 | 59aba1a2de20aa2d0143f99d35ab56c3a9c5981cff16514e8c0dcec700d96c64 |
| h1_foley/wpfoly_miniuzi_reload_chamber_v1.flac | 48000 | 29561 | 124 | c59e987e0e535697402c3defe88ae35f742cc6cd025f53f3670c3efd9d78c97f |
| h1_foley/wpfoly_p90_reload_chamber_v1.flac | 48000 | 30880 | 105 | b5817a69d5e115740d23cd26b51d061090532db0c865a7e3c753c4fdc4a624c1 |
| h1_foley/wpfoly_rpd_reload_chamber_v1.flac | 48000 | 55032 | 204 | a89442950c9e025c3efd2f23f4d9c8cb9e7f48164f7e14ed58470f0955f126f2 |
| h1_foley/wpfoly_rpd_reload_chamber_v1_quick.flac | 48000 | 55032 | 144 | f433468c231ee5a970840ea95b047a5271886274d17b33d5b9874e61527a36b5 |
| h1_foley/wpfoly_usp_reload_chamber_v1.flac | 48000 | 25087 | 199 | 00ef72c3de2f3508886c6d010c835210a67ac4f720048270ccb44f5e981e89ec |
| h1_foley/wpfoly_usp_reload_clipout_v2.flac | 48000 | 67733 | 527 | 353051868d75e6ee8bb619d9925acbaac984b7a07c4c2ee98efff0cf540a0c48 |
| h2_weapons/aa12/wpn_h2_aa12_foley_chamber_01.flac | 48000 | 26364 | 275 | f77dbfaeb01656329a38eb566efa1b1c6b9dced08e3c1cb482b5aa3a16ab07f5 |
| h2_weapons/aug/wpn_h2_aug_foley_chamber_01.flac | 48000 | 30863 | 255 | d69be1a177e12d989fb517cf8b54bd6ef160a90d61be932579548ed66f3dbcec |
| h2_weapons/aug/wpn_h2_aug_foley_clipout_01.flac | 48000 | 77285 | 424 | e959febd6b03f5c1e6d3abc49a61a87c8c1ac975c98be63f8e7f8f41c6d8811d |
| h2_weapons/fal/wpn_h2_fal_foley_chamber_01.flac | 48000 | 29020 | 202 | 8a6b6068e7dc70c538bb68aef733d34c8f7eb552930a9fe1b13a526c2181aa80 |
| h2_weapons/famas/wpn_h2_famas_foley_chamber_01.flac | 48000 | 59588 | 176 | 919961ed62cebaa07c8d2fc322da3efa0c7df37db2db8235027ded20261bdade |
| h2_weapons/famas/wpn_h2_famas_foley_clipout_01.flac | 48000 | 72006 | 392 | 747f10709fa4b46e7ae89bbb5d2b312748bd93d4472564b2b5972b75e5e37347 |
| h2_weapons/fn2000/wpn_h2_fn2000_foley_chamber_01.flac | 48000 | 32464 | 150 | 5f211b08355baefb7d4988a9fc6cf4995408e0f665f8bde197c1290a88a1f01e |
| h2_weapons/fn2000/wpn_h2_fn2000_foley_clipout_01.flac | 48000 | 74330 | 211 | ce1ffe47ee99132c417372a6a7fbd77c95c8af18b6a4d01834c66981f09327e0 |
| h2_weapons/g18/wpn_h2_g18_foley_chamber_pullout_01.flac | 48000 | 18745 | 97 | 65a673eab54bef20b8597b2b070a56f266928111af6f4bb6800814269e497d9b |
| h2_weapons/kriss/wpn_h2_kriss_foley_chamber_01.flac | 48000 | 56086 | 161 | a2a313b0f4e026245d38860af96c319506180cf8112d5b245421accbd5fc48dc |
| h2_weapons/kriss/wpn_h2_kriss_foley_clipout_01.flac | 48000 | 54429 | 590 | 93bfdc62884e465cd83644dce3818f7f82f7d43efc49e7a517e6ba4dbcee2aa7 |
| h2_weapons/masada/wpn_h2_masada_foley_chamber_01.flac | 48000 | 54188 | 142 | 0b72fb4b3bbdd5d20a28c94dae296c1736241f6b5563e67b714b4071cfe1a581 |
| h2_weapons/mg4/wpn_h2_mg4_foley_chamber_01.flac | 48000 | 98029 | 269 | b6bca959c26c510e14618394716d62ecbe1aed5c1a7e3576ebe7a41e3518af11 |
| h2_weapons/sa80/wpn_h2_sa80_foley_chamber_01.flac | 48000 | 37676 | 219 | 52e1a7eb9ecf053c6c3da31c2eb38226db054dd4b1db0e196631c4d2193d5c37 |
| h2_weapons/scar_h/wpn_h2_scar_h_foley_chamber_01.flac | 48000 | 27730 | 205 | 3de6d1d6e094baaed75c7909431f84b0af093016728908edff32485959d6e07c |
| h2_weapons/tavor/wpn_h2_tavor_foley_chamber_01.flac | 48000 | 43876 | 108 | b323f526cac0c004e40437aa9a8090ef37d5b7a01ac376d60d5d5de133f06821 |
| h2_weapons/tmp/wpn_h2_tmp_foley_chamber_01.flac | 48000 | 34180 | 149 | 3109e07fbb4885a7ac6f905fb5773c4d915140d59432e4ea4ddc871f8c9ff021 |
| h2_weapons/ump45/wpn_h2_ump45_foley_chamber_01.flac | 48000 | 44026 | 173 | c08121ded364d7729138821992fac706b8cfef58209f675ee7147a60c8bd6623 |
| h2_weapons/wa2000/wpn_h2_wa2000_foley_chamber_01.flac | 48000 | 28950 | 225 | d7d17d146c96f3cecefefed1b4d0f9c1aeaf5fe9550dd12c318be1ad66353454 |
| mw3/foley/wpfoly_ak47_reload_chamber_v4.flac | 44100 | 32002 | 160 | be7bc3a6ceef6b2168cb0568648b9aba4c0804ce9816fe22db4f4c326237bdc4 |
| mw3/foley/wpfoly_mp9_reload_chamber_v1.flac | 44100 | 40878 | 195 | fe44ce9d6c3fd0a15dad3b00808d7a840d8fc78bbbb73dd4c9e99cb50d8b5dc0 |
| mw3/foley/wpfoly_ump45_reload_chamber_v1.flac | 44100 | 34523 | 180 | 50ec3f4c1340c663e484a02ecd7ac3ffed707f7211e41751a6b350ca6f8ff68a |
