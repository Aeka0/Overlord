# M4 family: foregrip and M203 support variants

The 2026-09-27 Gulag capture adds `h2_viewmodel_m4_base_arctic` with M203,
laser and arctic reflex sight (`m4m203_reflex_arctic`). Its receiver bind poses
and skinned magazine partition match the base. A separate arctic reload recipe
prepares visibility on that exact receiver while sharing the authored hand
poses, mechanics and native independent magazine. See the
[capture and validation notes](../../../../../../../docs/vr-gulag-weapons.md).

Runtime physical-reload candidate, 2026-09-09; HMD acceptance remains pending.
All admitted M4 combinations share one physical definition and rear-hand pose.
Exactly one foregrip or M203 determines support contact. Suppressors, the reviewed
optic/camouflage aliases, covers, laser and heartbeat props retain native geometry.
No mode-switch input or grenade firing/loading control is added in this version.

The 2026-09-08 non-VR capture confirms a native `m4_silencer` with this foregrip,
cover and suppressor. [Mechanical capture notes](asset-notes.md) record parts,
reload branches and sound mappings without changing the existing grip tuning.
That capture is not an additional HMD acceptance pass.

`mechanics.hpp`, `reload_interaction.hpp` and `feedback.hpp` author shared
thirty-round closed-bolt rules, a non-reciprocating charging-handle policy and
captured native reload keys. `reload_profile.hpp` registers the common definition.
Synthetic interaction tests cover 30+1, B/Y empty-follower priority, no-magazine
release, insertion into an empty closed chamber and repeated held handle cycles.
The common presenter excludes charging handles from native firing recoil, while
existing pistol slide behavior is unchanged.

Native family recognition only selects candidate data: a complete M4 scene,
validated attachment topology, thirty-round base capacity, unique native inventory
and ammo cells, idle entry, prepared visibility and independent magazine must all
agree before admission. Each instance pins its exact native name; different native
weapons do not share mutable ammunition. Sounds resolve through that actual weapon.

The 2026-09-10 live `m4m203_eotech` capture (30-round rifle, separate
`m203_m4_eotech` one-round launcher) exposed a missing native family stem:
the grenadier scene and mechanical geometry were valid, but `m4`/`m4_...`
recognition rejected the rifle name. Admission now also recognizes bounded
`m4m203`/`m4m203_...` names using the same physical definition. Launcher names,
incorrect capacity and mismatched scenes remain excluded. The captured receiver
retains the verified 3,910 magazine-only triangles with no crossing triangles.

The 2026-09-09 read-only capture validates the shared surface's native
blend/index partition against the export: 3,910 magazine-only triangles out of
5,900, with no magazine-crossing triangles. M203 and suppressed-foregrip receivers
have identical captured skin metadata/bytes. The new visibility path prepares
immutable retained-triangle index buffers on the main asset owner. It replaces
only the matching packet's surface pointer before native skin jobs are published;
original vertices, weights, materials, source assets and other triangles stay intact.
Rigid groups use the existing filtering path. Missing contracts leave admission
closed, and no in-flight metadata/cache entries are evicted.
The M203 assembly, separate feed identity and mode transitions are also captured
in [variant evidence](asset-notes.md#2026-09-09-m203-and-foregrip-comparison).
That collection itself did not enable interaction; the implementation follows it.

User-confirmed current scope: the rifle feed of both underbarrel configurations,
B/Y magazine release with valid bolt-lock release taking priority; off-hand
trigger for waist magazine, insertion and rear charging handle; spring return
independent of bolt lock. M203 control/mode switching remains separate. Keyboard R
and automatic rifle reload are blocked only after physical admission; original
non-VR behavior and the unimplemented native alternate feed are left alone.

`profile.hpp` registers `h2_viewmodel_m4_base` with exactly one
`attach_h2_mp5k_foregrip_vm` or `attach_h2_m203_vm`. Duplicate/conflicting or
unknown weapon-owned attachments reject the profile. The free/reloading hand
retains the standard anatomical basis; only engaged M203 support changes pose.
The suppressor supplies its validated `tag_flash_silenced`, not the receiver tip.
Thermal/sensor display behavior itself is not implemented by accepting its model.

`reload_poses.hpp` reuses native reload frame31 for the magazine and mirrors the
RIGHT charging-hand source at pullout_first frame16 to the physical left hand.
The independent native `h2_weapon_m4_clip` uses a rigid registration with p95
distance0.2125cm and maximum1.9847cm: close but NOT an identical viewmodel mesh.
The offline side/top preview verifies rear handle contact and places the actual
receiver-mouth candidate around gun-local Z0.5cm. Magazine capture is radius4.5cm,
above/below6cm and85 degrees. Full exit follows that mouth, not the capture extent.
Independent-magazine hand/seat appearance and handle comfort still need HMD checks.

`poses.hpp` contains two gun-local wrists and thirty parent-local finger
rotations from `h2_wpn_asl_m4a1_idle.seanim`, frame 0. SHA-256:
`0cb3a6e3ca29fe772f2d872c6ef3017de473a7de7661c6f27aa65b1baaa6f388`.
Source hand hierarchy: `viewhands_us_army`; receiver model SHA-256:
`90307e116e650ae82a652c24bac94b8ef9fcec4af19f9b097e82a3c56fd3ff44`.
Compose the hand hierarchy to `tag_weapon`, then compose the receiver's additive
`j_gun` translation (-1.84404075, 0, -3.26643991) export centimetres before
inverting the gun transform. Divide positions by 2.54 exactly once. This root
offset is nonzero on M4; reusing tag_weapon as the gun origin misplaces both
wrists. Live glove lengths are retained. No raw assets or extraction tools are
runtime/build dependencies.

The right hand owns firing and is the position/roll reference. A new left squeeze
within 10 cm of the foregrip acquires two-hand aiming; holding while entering
does not grab. Acquisition blends over 100 ms. Retention measures distance to
the two-hand solved anchor (22 cm breakaway), so turning the rear wrist alone
does not spuriously release support. Hands closer than 8 cm release the lease
before their baseline becomes ambiguous. Logical release is immediate; the
last relative support rotation returns to rear-only aim over 100 ms without
following the now-free hand. Recenter, ownership/assembly changes and stale
input clear the lease and require release/repress.

All of this uses shared `grip_presenter` / `support_grip` / `hand_pose_library`.
M9 remains `rear_hand`. M4 native firing remains active; admitted physical
instances own magazine/chamber state and handle presentation. Confirmed native shots use the shared
controller feedback service; native shot audio plays once through the engine.

Check `vr_hands_status`: profile=m4, variant=foregrip/grenadier, support=0 when acquired,
blend converging to 1. In the HMD check iron/optic sight alignment, aim above and
below, rear wrist roll, support release while firing/reloading, switch/recenter,
controller occlusion and both-eye consistency. Offline geometry tests do not
establish hardware acceptance. `vr_reload_interaction_status` reports admission
and physical state. Test B/Y eject/valid-lock-release, take-before-eject, immediate
post-shot reload,30+1, repeated held full/partial handle strokes, empty/no-magazine
closure, seated insertion and switch/tracking-loss cancellation. Check both eyes
and locomotion with a held magazine. Intermediate save restoration remains deferred.
