# FAMAS physical interaction

Two-hand aiming accompanies a closed-bolt feed, chamber plus-one, last-round
hold-open and rapid button release. A physical downward pull removes the
magazine; the button only releases an eligible locked action. Empty replacement
requires button release or a full manual cycle. A tactical replacement retains
the chambered round. The adapter does not enable AK strikes or HK handle latching.

## Source and presentation

Two exported receiver appearances have identical 20-bone geometry contracts:

| Receiver | SHA256 |
| --- | --- |
| `h2_viewmodel_famas_base_arctic` | `991c921bb8913c95b1337b19f695be00078699b840b68e69439d19290034c3e2` |
| `h2_viewmodel_famas_base_tape` | `a578aa8073b779c8c648d249c267b98c71e4092d05ce4e243bd4128292062f36` |

Each appearance retains its own receiver-subset identity and material source.
`tag_clip` has 796 vertices / 874 triangles; its child `j_bullet` has 452 vertices
/ 490 triangles. No mixed-weight vertices are present. The magazine, cartridge,
trigger and action remain separate even where material surfaces are shared.

`h2_wpn_asl_famas_idle` supplies both wrists and 36 finger/palm rotations.
The magazine uses the native left reload grasp at frame 46. The top action uses the native
left grasp from `reload_empty` frame 78, translated back with its action to the
closed rest. This avoids the right-hand `pullout_first` source. Headers record
the sampled animation hashes and convert export centimetres to native units once.

The magazine mouth uses the rear receiver's lower edge, gun-local Z = 0.7 cm.
`j_bolt` travels 82.55 mm during native reload and fire. The adapter preserves
native firing motion with an 83 mm manual stroke, 78 mm full-stroke threshold
and an authored 75 mm retained lock. The retained pose implements the requested
empty lock; it is not claimed to originate in native `empty_additive`.

Source clip-out/clip-in keys provide magazine sounds. The compound chamber key
plays once on action completion, and shot audio stays native. Equip substitution
is limited to the family's equip clips, preserving reload/fire event routing.

## Admission and verification

The candidate native family is `famas`, with 30-round base capacity. Matching
scene appearance, native primary mode, exact owned instance and unique ammo
cells remain mandatory. Live WeaponDef and HMD confirmation are pending.

Reviewed common optics, suppressor, heartbeat and laser mounts retain the same
support grip. Underbarrel mounting tags alone do not admit an unreviewed support
pose or secondary feed. Both receiver appearances have independent registry entries.

CPU coverage includes full attachment/hand permutations, malformed topology,
mouth clearance, physical pulling, tactical 30+1, follower hold-open, button/manual
return, short strokes, one extraction per full cycle and interruption accounting.
