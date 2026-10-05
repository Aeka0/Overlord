# G18 physical interaction candidate

2026-09-09: authored from existing offline H2 exports using the shared pistol
reload controller. The user confirmed gripping works. A read-only live WeaponDef
check corrected base capacity to 32; physical reload and sound still need HMD
acceptance with the corrected build.

## Behavior

Single-wield G18 uses the same button-release, insertion and slide interaction
as the existing pistols. The rear hand owns aim/fire; support squeeze holds the
front hand without steering aim. Offhand trigger grabs a waist magazine or the
rear of the slide. Native automatic fire and its cadence remain engine-owned.

B/Y ejects the magazine. With an empty follower holding the slide open, ejection
takes priority; after a loaded replacement is inserted, B/Y releases the slide
and chambers one round. Releasing a locked slide with no magazine closes it
without feeding. Inserting into an already closed, empty chamber still requires
a complete physical rack. Partial strokes cannot extract/feed, and a full live
extraction spends one chambered round under the shared inventory policy.

The verified native identity/base capacity is `glock` / 32, with plus-one,
last-round lock and button release. Runtime checks the actual WeaponDef capacity,
complete scene, idle entry, unique ammo cells and rendering readiness before
admission. Initial 32 partitions into 31+1; tactical replacement may reach 32+1.
The earlier 33-round base assumption prevented physical admission; it is not an
extended-capacity opt-in. Aliases such as `g18`, akimbo,
suppressed/optic variants and altered capacities are not admitted by prefix.

The model name is `h2_viewmodel_glock_base`; ten receiver bones include `j_bolt`,
`tag_clip` and `j_bullet`. Unlike the prior pistols, the bullet is a direct child
of `j_gun`. An explicit profile flag permits that reviewed sibling topology;
the rig captures its bind offset relative to the magazine and presentation moves
it with the magazine. Other profiles still require magazine-child round bones.
Missing, duplicate, aliased or reparented mechanical roles reject the binding.

## Reviewed assets and poses

Receiver SHA-256:
`2ed3651182a36b5c17a27d15b4080f0ea2dfdf149c2b0a9b1e06348fb02bad4f`.

- `h2_wpn_pst_glock_idle`, frame 0: both wrists and thirty finger rotations.
- `h2_wpn_pst_glock_reload`, frame 10: long-magazine hand and fifteen fingers.
- `h2_wpn_pst_glock_first_time_pullout`, frame 10: rear overhand slide grasp.
  A 0.9965 cm rearward adjustment places the reference glove's foremost finger
  skin at gun X=3 cm. Nearest reviewed slide surface is about 2.50 mm away.
  This is an offline candidate, not a change to accepted M1911/USP grasp tuning.

Hashes accompany the compact authored poses. Receiver translation is additive;
rotation channels replace parent-local rotation rather than multiplying the bind
tilt again. Position conversion from export cm to native units happens once.
The live glove keeps its own finger segment lengths.

Settled `last_fire` slide travel is 46.018 mm; first-pull peak is 50.655 mm.
Candidate manual travel is 55 mm, with a full-stroke threshold of 51 mm. The
magazine's insertion axis follows its approximately 21-degree bind tilt. Its
well is at the grip mouth (gun Z about -5.45 cm), not the extended magazine's
baseplate (about -15.93 cm). Capture uses the common pistol tolerances.

The independent `h2_weapon_glock_clip` has a different, simplified mesh. A
translation fit differs by up to 5.40 cm, and a tested rigid fit still differs
by up to 4.28 cm. Independent magazines therefore reuse exact receiver geometry
through the same immutable rigid-subset service as AK. G18 needs two subsets:
empty body, and body plus its single visible round. Positive ammunition counts
all select the latter; AK retains its zero-to-three visible-round variants.

Receiver surface 0 contains frame 2,826 vertices/2,825 triangles, round 103/112,
and magazine 486/455. All are rigid and no triangles cross groups. Surface 1 is
the slide. Granular rigid-group visibility keeps the shared frame visible when
the round or magazine is hidden. Original assets, GPU vertices and materials
are retained; independent subsets own their indices and descriptors. Queued
rendering lifetime and bounded cache exhaustion follow the shared service.

Feedback uses exported keys `weap_glock_clipout_plr`, `weap_glock_clipin_plr`
and `weap_glock_chamber_plr`, resolved through the actual selected WeaponDef.
A full manual rear stroke also plays `weap_glock_first_lift_chamber_plr`, verified
live as `h2_wpn_g18_foley_plr_chamber_pullout`. The accepted extraction event owns
this cue: a short pull, rear-stop jitter or repeated input cannot replay it;
another full cycle can. Lifecycle cleanup stays silent and automatic firing
audio remains native.

## Validation

The shared pistol tests now include G18: exact identity, finger binding, tilted
insertion, full magazine clearance, moving-slide contact, both logical owners,
both magazine orders, empty/tactical reload and partial/repeated strokes.
`g18_profile_tests.hpp` also checks the exported sibling-round topology, atomic
rejection, round-to-magazine transform and selective shared-frame visibility.
Closed-bolt coverage includes 32-round firing to last-round lock and plus-one
accounting. Logical left-owner tests do not enable a left-rear or akimbo mesh.

HMD checks still needed: two-hand comfort, long-magazine grasp/reach, slide
clearance, capture feel, held/dropped alignment, near-field visibility and
audio/haptics. Offline assets and CPU/WARP tests cannot accept those properties.
