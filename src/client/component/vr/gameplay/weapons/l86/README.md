# L86 physical interaction

The adapter uses two-hand aiming and a closed-bolt feed with a chambered round,
last-round hold-open and rapid button release. Magazine removal requires a
physical downward pull. The button never ejects the drum. Replacing an empty
drum retains the lock until a button release or full charging-handle cycle;
tactical replacement preserves the chamber. AK magazine strikes and HK manual
handle latching are disabled.

## Source and presentation

The exported receiver is `h2_viewmodel_sa80_lmg_base`, 16 bones, SHA256
`980ea68a1ec2101c5762b447dff15274ed16ff37241f2b8df95d2fc894117d3c`.
Its `tag_clip` drum has 6,474 vertices / 9,778 triangles; child `j_bullets`
has 741 vertices / 864 triangles. Both are rigid groups, kept independent of
the bipod, receiver and sight. The existing receiver-subset service preserves
the exact magazine geometry and materials; no world-magazine substitute is used.

`h2_wpn_lmg_sa80_idle` supplies the two wrists and all 36 finger/palm rotations.
The native reload and initial charging animations use the right hand. Physical
interaction instead uses an actual left AUG reload-frame-49 grasp fitted to the
reflected native drum grasp and moved 3 cm outward for clearance. Charging uses
the same shared index-side/pinky-side hooks as M14, registered to the outer cap
of L86's real right-side handle. Both hands have explicit fits for both facings;
the right wrist does not simply reflect across the receiver. The shared finger
chains include the relaxed pulling finger and the corrected resting index.
Pose headers record source hashes; wrist/contact placement is receiver-specific.
The reflection supplies a placement target; right-hand finger rotations are never
assigned to left joints. Offline contact projections guide this candidate but do
not establish collision-free fingers or HMD comfort.

The right-side `j_reload` travels 85.46 mm in `first_time_pullout`; the adapter
uses an 86 mm stroke and an 80 mm extraction threshold. It does not move in
native fire or empty-additive animations. It therefore returns independently
of the requested mechanical empty lock. Native right-hand equip action motion
is suppressed using the shared equip-only policy. No new bipod interaction is added.

The rear magazine mouth is at gun-local Z = 0 cm, aligned with the lower
receiver edge. The shared capture tolerances and interruption guards apply.
Own source notetrack keys provide removal/insertion and one compound chamber
sound on completion. Native shot audio remains native.

## Admission and verification

The candidate native family is `sa80`, with a 100-round base drum. Native
name/capacity and HMD fit still require live confirmation. Runtime admission
requires the exact complete scene, matching base capacity, primary mode and
verified native ammo cells before any ammunition writes.

Compatible common optic, suppressor and heartbeat attachments use the shared
parent/cardinality resolver. Unreviewed grip-changing assemblies are rejected.
CPU tests cover assembly permutations, hand/part binding, physical removal,
tactical 100+1, empty replacement and both return methods, repeated rear-stop
contact, failed compares and interrupted held-magazine accounting.
