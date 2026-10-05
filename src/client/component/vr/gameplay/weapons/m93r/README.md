# M93R foregrip / slide adapter

Single-wield candidate, updated from headset feedback on 2026-09-09. The rear
hand determines position, aim and firing, including while the front grip is held.
The support hand follows its authored foregrip contact without steering the gun.
Native burst timing is retained. This adapter changes physical handling, not the
engine's fire mode or its accepted-shot authority.

The candidate native identity is `beretta393`, base capacity 20, with exact
receiver `h2_viewmodel_beretta_393_base`. Animation/model aliases are not native
WeaponDef evidence. No new in-game capture was made: actual inventory identity,
capacity, complete assembly and prepared native assets must agree at admission.
Akimbo and unreviewed attachments do not inherit this base adapter.

## Interaction

- New support squeeze near the front grip acquires support without changing aim. A held
  squeeze moved into range does not auto-grab. Shared 10cm acquisition, 22cm
  breakaway, 100ms blend and 8cm minimum hand separation apply.
- B/Y releases the magazine, or releases a valid slide lock first. An inserted
  empty follower prevents closing; with the magazine removed, closing is allowed.
- The free-hand trigger takes a waist magazine, inserts it through the grip mouth
  or grasps the rear slide. Support and part manipulation are mutually exclusive.
- Empty lock supports button release or a full manual slide cycle. Inserting a
  magazine into a closed empty chamber still requires a full cycle. Partial pulls
  cannot extract; repeated full cycles each extract one live chambered round.
- Twenty rounds plus a retained chamber are supported without creating ammunition.
  The native initial total of twenty partitions into nineteen plus one.

## Offline evidence

`poses.hpp` records `h2_wpn_pst_beretta393_idle` frame 0: two gun-local wrists,
thirty parent-local finger rotations and parent-local equip parts. The two nested
front-grip hinges use the unfolded idle pose; the second hinge must not receive
its gun-local translation as a parent-local value. Native stock geometry remains.

The magazine grasp uses `h2_wpn_pst_beretta393_reload` frame 24. Frame 6 is the
release phase, not the stable replacement-magazine hold. The tilted magazine's
well is at gun-local cm `(-0.62145, 0.05430, -5.03111)`, above its protruding
baseplate. It uses the shared pistol capture volume: radius 3.5cm, 6cm above and
below the mouth, 85-degree alignment tolerance. Drop travel clears this same rail.

M93R's native first-equip animation flips the foregrip; its empty reload uses
the release control. Neither supplies a manual slide grasp. The existing
`h2_wpn_pst_m9_pullout_first` frame 13 hand is reused and fitted to this receiver:
rearward adjustment 6.697078cm, foremost finger skin at gun X=3cm, nearest reviewed
rear-slide contact 0.206mm. Glove bind lengths are retained. The 75mm manual range
and 67mm extraction threshold are authored candidates based on the native peak
slide excursion of 73.92mm; empty-reload lock supplies 46.164mm. These numbers
need HMD acceptance and are not a measured M93R manual-pull animation.

Receiver SHA256:
`5b45c88a2a6826a27aeb4d08dc656cb931fb10023b83baa2fb68a01a7d4507c5`.
Other source hashes are beside the authored data. Surface 5 has body 450 vertices /
564 triangles and `j_bullets` 445 vertices / 618 triangles, all rigid and separate.
The independent world clip matches the body closely but has no round group. The
shared exact-receiver subset service supplies both empty and loaded magazines,
retaining native materials and the corresponding empty-round visibility.

Reload notetracks actually use `weap_m9_clipout_plr`, `weap_m9_clipin_plr` and
`weap_m9_chamber_plr`; these are resolved in the selected native WeaponDef, not
borrowed audio aliases. Following game listening, the previously silent rear
stop explicitly shares M9's `wpn_h1_m9_ins_pull` key through the loaded
`beretta` WeaponDef map. M93R identity and ammunition admission remain intact;
the other events still use M93R's own map. A missing loaded donor is reported,
not replaced by a guessed alias. This sound adjustment awaits target-machine
compilation and listening.

## Verification boundary

Client compilation and twelve VR regression executables passed. Tests cover
exported topology, nested hinge coordinates, selective masks, foregrip aiming,
capture/exit geometry, full/partial cycles, both logical owners, input interruption,
twenty-round burst accounting, short final burst and prediction replay.
Runtime identity/asset admission, native audio lookup, stereo appearance and HMD
reach remain unverified. No deployment or hardware session was performed.
