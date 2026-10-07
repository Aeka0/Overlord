# Held-weapon ejection reference audit

This page compares receiver geometry and native ejection markers across 34
variants, including H2 precision-rifle definitions. These
coordinates locate native references, **not certified physical aperture centers**.
The audit includes the revolver as a comparison only; cylinder ejection remains
its separate mechanical path. M9 uses an H1 receiver and is outside this H2 export
set. The generic runtime binder can still use its loaded receiver reference.

## Findings and implemented boundary

Independent shots previously reconstructed an ejection point from a world-model
marker by aligning its muzzle with the held viewmodel. The two receiver meshes
and marker placements differ. The largest measured disagreements are WA2000,
Mini-Uzi, M82, M14 EBR and L86. A difference is not itself a measurement of either
marker's error against the real port.

Adapted weapons now publish an immutable receiver-local ejection reference from
their own validated loaded skeleton, alongside their per-instance gun/muzzle
pose. Both automatic firing and manual-bolt extraction use that coordinate
contract. Hand mirroring, suppressor length and the selected native weapon cannot
move it to another side or receiver. Unknown unadapted weapons keep their existing
fallback. Duplicate/non-receiver/invalid marker transforms are rejected.

This fixes mixing model spaces; it does not declare first-person tags physically
correct. Several markers lie on the weapon center plane, while their visible
ports are on a side or span the top. Their final origin should be calibrated
against textured closed/open receiver geometry before replacing source data.
Point-cloud side/top views alone do not resolve every window boundary. No
unverified uniform offset or automatic nearest-surface snap was applied.

M200's frozen mechanical scene and loaded bind reference agreed to about 0.01 mm
numerically, but this only validates its transform. Its live-cartridge velocity
incorrectly applied gun-local -Y in tag-local coordinates; it now follows the
tag's +X (right/up in the source receiver). Native spent-case motion is retained.

Spent-case rendering uses the selected native view-shell FX with private,
immutable world-depth descriptors. H2 maps `FX_ELEM_DRAW_WITH_VIEWMODEL` (`0x800`)
to scene depth-hack bit 1 at `0x14042EC71..0x14042EC81`, so an oriented world-space
spawn alone still draws flagged cases in front of the independently rendered
weapon and arms. The shell adapter clears that element flag throughout the
effect's impact, death, emission and runner graph. Models, materials, motion,
collision and other flags remain native; shared asset definitions are untouched.
Ordinary shots, last-shot effects, manual extraction and vehicle brass use the
same adapter. Muzzle flashes and other frontend FX retain their existing path.

Descriptors are reused across shots and retained until the native zone-unload
drain. Invalid or over-budget graphs are omitted without evicting live entries;
`world_shell_definitions` and `shell_depth_rejections` expose that admission in
native weapon FX status. Offline tests cover descriptor isolation, child graphs,
lifetime and bounded rejection. In-headset occlusion against both hands and
weapons, including map transitions, still requires runtime acceptance.

## Reference coordinates

Values are centimeters relative to the receiver gun origin: X forward, Y left,
Z up. World-reference differences below use source marker displacement after
muzzle alignment, matching the prior receiver-space bridge. Actual composite
assemblies may require additional root transforms. Missing world counterparts
remain unavailable. Source hashes and individual part coordinates are retained
in [the audit data](vr-ejection-reference-audit.json).

| Receiver | View reference X / Y / Z (cm) | World-reference separation (cm) |
| --- | --- | --- |
| `h2_viewmodel_ak47_base` | 15.00 / -1.63 / 9.93 | 4.69 |
| `h2_viewmodel_ak47_base_arctic` | 15.00 / -1.63 / 9.93 | 4.69 |
| `h2_viewmodel_ak47_base_digital` | 15.00 / -1.63 / 9.93 | 4.69 |
| `h2_viewmodel_beretta_393_base` | 2.90 / 0.06 / 8.59 | 1.24 |
| `h2_viewmodel_cheytac_base` | 18.38 / -1.68 / 10.62 | 1.84 |
| `h2_viewmodel_colt45_base` | 3.54 / 0.00 / 8.11 | 3.67 |
| `h2_viewmodel_colt_anaconda_base` | 10.62 / -0.01 / 9.39 | 1.41 |
| `h2_viewmodel_desert_eagle_base` | 2.66 / 0.00 / 8.20 | 1.89 |
| `h2_viewmodel_famas_base_arctic` | -13.15 / -1.21 / 8.45 | 0.72 |
| `h2_viewmodel_famas_base_tape` | -13.15 / -1.21 / 8.45 | 0.72 |
| `h2_viewmodel_fn_fal_base` | 19.65 / -0.94 / 9.19 | 3.12 |
| `h2_viewmodel_glock_base` | 3.28 / 0.00 / 8.72 | 3.04 |
| `h2_viewmodel_kriss_super_v_base` | 19.10 / 0.00 / 4.31 | unavailable |
| `h2_viewmodel_kriss_super_v_base_black` | 19.10 / 0.00 / 4.31 | unavailable |
| `h2_viewmodel_m14ebr_base` | 24.22 / 0.00 / 11.29 | 10.18 |
| `h2_viewmodel_m14ebr_base_arctic` | 24.22 / 0.00 / 11.29 | 10.18 |
| `h2_viewmodel_m16_base` | 11.91 / -1.29 / 10.18 | 6.99 |
| `h2_viewmodel_m4_base` | 13.43 / -1.53 / 9.49 | 2.26 |
| `h2_viewmodel_m82_base` | 18.58 / -2.87 / 12.01 | 12.38 |
| `h2_viewmodel_magpul_masada_base` | 14.42 / -1.35 / 10.55 | 4.89 |
| `h2_viewmodel_magpul_masada_base_black` | 14.42 / -1.35 / 10.55 | 4.89 |
| `h2_viewmodel_magpul_masada_base_digital` | 14.42 / -1.35 / 10.55 | 4.89 |
| `h2_viewmodel_miniuzi_base` | 1.42 / -1.94 / 9.10 | 12.55 |
| `h2_viewmodel_mp5k_base` | 14.42 / -1.07 / 13.63 | 1.14 |
| `h2_viewmodel_mp5k_base_arctic` | 14.42 / -1.07 / 13.63 | 1.14 |
| `h2_viewmodel_mp9_base` | -0.04 / 0.00 / 7.28 | 2.60 |
| `h2_viewmodel_p2000_base` | -1.40 / -1.30 / 8.63 | 2.97 |
| `h2_viewmodel_sa80_lmg_base` | -11.92 / 0.00 / 9.02 | 7.85 |
| `h2_viewmodel_steyr_base_arctic` | -13.22 / 0.00 / 9.14 | 2.17 |
| `h2_viewmodel_ump45_base` | 15.15 / -1.21 / 12.40 | 3.76 |
| `h2_viewmodel_ump45_base_arctic` | 15.15 / -1.21 / 12.40 | 3.76 |
| `h2_viewmodel_ump45_base_digital` | 15.15 / -1.21 / 12.40 | 3.76 |
| `h2_viewmodel_usp_base` | 1.84 / 0.00 / 8.10 | 4.03 |
| `h2_viewmodel_wa2000_base` | -14.66 / 0.00 / 13.08 | 18.68 |

## Follow-up calibration

Review each distinct receiver with the bolt/slide open and its textures visible;
locate the aperture center and exit normal, and ensure a cartridge-sized body
clears the window. Keep native FX direction distinct from cartridge mesh
orientation. Check both hands, rotated/inverted guns, suppressors and skin
variants. Native or world tags are comparison references only. Headset acceptance
is pending; no claim of complete geometric calibration is made by this audit.
