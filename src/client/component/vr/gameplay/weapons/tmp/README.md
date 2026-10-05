# TMP foregrip / rear charging-handle adapter

Offline single-wield candidate, 2026-09-09. Both hands determine aim with the front
grip engaged; the right hand remains the position/roll and firing owner. Native
automatic fire is retained. The user confirmed M4-style empty lock with both
button release and rear-handle cycling.

The verified native identity is `tmp`, base capacity 32, with receiver
`h2_viewmodel_mp9_base`. The exported `mp9` animation/model name is not a native
WeaponDef alias. A read-only live check corrected the old 15-round assumption
that blocked physical reload. Native name/capacity, full assembly,
inventory ownership and prepared assets must agree before physical admission.
Unreviewed suppressor/optic variants and akimbo do not inherit this base profile.

## Interaction

The front grip uses shared two-hand acquisition and release: fresh squeeze within
10cm, 22cm breakaway, 100ms blend, and release below 8cm hand separation. Releasing
support restores rear-hand aim; a free hand moving to reload does not steer it.

B/Y ejects the magazine, with a valid bolt-lock release taking priority. The free
hand's trigger draws a waist magazine, inserts it into the pistol-style grip well,
or grabs the rear charging handle. A support lease excludes part acquisition;
leaving support while pinching requires release/repress before grabbing a part.

The internal bolt holds open on empty. The handle returns fully forward and does
not reciprocate with shots. With a loaded magazine, either B/Y or a full handle
cycle feeds one round. An inserted empty follower prevents closing; removing it
permits no-magazine closure. Inserting into a closed empty chamber still needs a
full handle cycle. Tactical reload supports 32+1 with shared escrow
conservation. Repeated full cycles extract once per cycle; partial pulls do not.

## Offline evidence

`h2_wpn_pst_mp9_idle` frame 0 supplies both wrists and thirty parent-local finger
rotations. The replacement magazine grip is `reload` frame 30; the frame-10
clip-out event occurs before a usable replacement-magazine hold. The well is at
gun-local cm `(-0.86818, -0.04497, -7.08179)`, near the grip mouth rather than the
extended baseplate at Z=-15.886cm. Shared pistol capture uses radius 3.5cm, 6cm
above/below the mouth and 85-degree tolerance.

The left-hand handle source is `pullout_first` frame 17, normalized back to the
forward handle position. A further 3.534286cm rearward adjustment targets the
front finger edge at gun X=-7.8cm, with nearest handle contact 1.727mm. Contact
bounds cover the rear crosspiece, not the forward rod. HMD acceptance is pending.

`j_reload` and `j_bolt` are separate direct children of `j_gun`. The native handle
peaks at 87.294mm while the bolt reaches 43.645mm. `bolt_curve` records their
separate pullout channels; a shared optional `charging_handle_bolt` follower
interpolates these positions with a bounded curve. Its empty-lock floor is
independent of handle return. Native accepted-shot bolt animation is preserved;
the follower never writes ammunition. The manual handle range is 88mm with a
79mm full-stroke threshold. Other adapters have no follower unless they opt in.

Receiver SHA256:
`19f45105a81c07afec24ed3508a30ac1a957d7a40a43991d8e0990d45a761785`.
Animation hashes are beside the poses. The fourteen-bone receiver has three
separate magazine-child round roots. Surface 1 contains body 882 vertices / 974
triangles and three rounds of 207 vertices / 234 triangles each. No mixed weights
or crossing triangles occur. The shared subset service prepares body-only and
one/two/three-round variants from the exact receiver. The standalone world clip
differs (1,049 vertices), so it is not substituted. Surface 3's bolt and handle
groups are unaffected by magazine visibility.

The exported keys `weap_mp9_clipout_plr`, `weap_mp9_clipin_plr` and
`weap_mp9_chamber_plr` resolve through the selected WeaponDef. The compound chamber
key plays once on closing; rear-stop feedback is tactile, and shot audio stays native.

## Verification boundary

Client compilation and twelve VR test executables passed, including exact rig and
round-mask contracts, rejected follower aliases/parents/curves, full/partial handle
cycles, independent empty lock, both release paths, support exclusion, native shot
replay and shared WARP rigid-subset checks. Previous M4/AK permutations still pass.
Actual game admission, audio, both-eye presentation and HMD ergonomics remain
unverified. No deployment or hardware session was performed. The existing bounded
retained asset cache is shared; exhausted native asset generations close admission
and can require restarting the game rather than recycling in-flight descriptors.
