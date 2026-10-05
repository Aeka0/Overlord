# SCAR-H support variants

The 19-bone receiver supports bare, shotgun, M203 and vertical foregrip
assemblies. Selection uses the shared attachment role/topology validator. A
foregrip, shotgun and launcher are mutually exclusive; optics and suppressors
retain the shared reviewed contracts. Unknown attachments remain excluded.

The native `scar_h_fgrip` definition expands to `h2_viewmodel_scar_h_base` and
the one-bone `attach_h2_scar_foregrip_vm`. Its root aliases the receiver's
`tag_foregrip`. The captured native DObj is retained as a regression fixture.

`foregrip_poses.hpp` contains the left wrist and all 18 finger/palm/webbing
rotations from `h2_wpn_asl_scar_h_fgrip_idle`, frame zero. The read-only animation
witness has 23 identity rotations, 48 static four-component rotations and 55
static translations. Quaternion shorts divide by 32768 and are normalized;
native translations remain in native units. Hand channels are parent-local,
while the receiver subtree applies additive animation to its own model bind.
The emitted hand anchor is relative to the resulting `j_gun` transform.

Only the support contact and hand pose change. The ordinary right-hand grip,
free-hand reference and shared SCAR rifle reload mechanics remain intact.
Native fgrip equip clips join the normal equip suppression family; reload and
fire animation ownership are unchanged. Pose contact and comfort still require
headset acceptance independently of the captured-rig and combination tests.
