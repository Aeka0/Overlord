# USP adapters: H1 hand poses, H2 native weapon

The user reports USP HMD acceptance after the one-third grasp return on
2026-09-08; keep that grasp unchanged. This report does not independently certify
every attachment/glove combination in the checklist below. Desktop read-only capture on
2026-09-08 confirmed native `usp` (weapon index 23), capacity 12, single wield,
initial ammo 12/72 and receiver `h2_viewmodel_usp_base`. Index 23 is evidence,
not a hardcoded admission key. No segmented/additive reload was set.

- H1 `h1_wpn_pst_usp_idle` frame 0 supplies rear/support wrists and fingers.
- H1 `h1_wpn_pst_usp_reload` frame 20 supplies the magazine hand.
- H1 `h1_wpn_pst_usp_inspect` frame 103 supplies the slide hand. Remove sampled
  slide displacement before storing its closed/rest wrist; render travel once.
- Rear-grasp calibration targets the foremost finger-skin edge on the preview's
  gun-local horizontal axis, NOT a wrist coordinate or offset amount. HMD feedback
  confirmed that X=-1.5 cm moved correctly but overshot. The current candidate
  returns one third of that shift (4.7484 cm forward), yielding X=+3.2484 cm and
  9.4969 cm rearward from source; keep curls/orientation and rebind contact to the rear
  slide surface. This grasp passed user HMD testing; other glove dimensions remain native.
- These exports are absolute-type hand tracks. Match H2 joints by name, omit
  H1-only finger tips (`_3`), and preserve live H2 glove finger segment lengths.
  Main H1/H2 USP weapon bind transforms agree; do not transplant H1 accessory
  or muzzle transforms. Source hashes are in the promoted pose headers.

The native attached `h2_viewmodel_knife` has a duplicate root 80 -> receiver
`tag_knife` 76. Suppression is a profile-owned cosmetic policy with exact model,
root and parent validation. It applies only when the VR USP grip is presented,
even before physical ammo ownership or independent magazine asset readiness.
The ordinary desktop weapon/knife, global melee rules, native assets and world
knife instances are untouched. `viewmodel_knife` is an offline-reviewed geometry
alias; other knife models, dual wield and unreviewed attachments fail admission.

## Explicit suppressed variant

The cross-weapon lesson and rifle extension plan are in
[weapon variants](../../../../../../../docs/vr-weapon-variants.md).

Campaign read-only capture confirmed `usp_silencer`, capacity12, single wield,
ammo12/60, with `viewhands_arctic`68 + receiver12 + `attach_h2_silencer_02_vm`2
+ knife2 bones. These indices are fixture evidence, not runtime lookup keys.
Suppressor root80 aliases receiver `tag_silencer`78; muzzle81 is
`tag_flash_silenced`, 13.34314 cm forward in the suppressor's bind frame.
Knife root82 aliases receiver `tag_knife`76. The suppressor rigid group has
2267 vertices/3338 triangles and must remain visible.

`silenced` has a separate exact native/profile identity, sharing base poses,
magazine transforms, mechanics, sound-map keys and ammo capacity. The common
attachment validator checks model/root/parent, topology and muzzle basis; the
hand rig uses the validated suppressor muzzle for its weapon pose/firing origin.
It never hides the suppressor with the knife or falls back to base admission for
an unknown assembly. This does not import H1 accessory/muzzle assets.

## Shared mechanics and verification

The live USP magazine surface combines `tag_clip` (445 vertices/543 triangles)
and `tag_bullets` (746/1153). Rigid-group visibility keeps the magazine body when
empty and combines mechanical hiding with the cosmetic knife mask. Native knife
surface is one rigid group (4608/6186), separate from the receiver's surfaces.

Independent magazine: `h2_weapon_usp_clip`. Its 445 body vertices align to the VM
within 0.000003 cm after rigid registration. The oriented well, exit rail, held
attachment frame, seating, return transition and ammo transaction logic are shared.
Native locked slide travel is 50.45 mm; manual travel 60 mm, full threshold 56 mm.
The extra overtravel is interaction tuning, awaiting HMD fit verification.

Native sound-map capture confirmed clip-out/in, `pull_slide` ->
`weap_usp45_chamber_plr`, and `weap_m9_chamber_plr` -> `weap_usp45_chamber1_plr`.
These keys resolve through USP's own WeaponDef. No sound asset is extracted or
played by a guessed alias, and a missing mapping stays silent.

HMD checklist (repeat for base and suppressed): support grip without cross-knife pose;
rear slide grasp clear of the ejection port through its entire stroke;
visible suppressor and correct muzzle origin; no attached knife in either
eye; independent/gun/dropped magazines; empty-magazine body retained; 12+1 tactical
reload and 11+1 feed from empty; B/Y priority; partial/full/repeated slide cycles;
movement, interruption and feedback. Shared intermittent M9 insertion delay is
still open. Save/load of intermediate physical states remains a separate phase.

Raw H1/H2 clips, meshes, parsers and previews stay in removable ignored local
audit storage; none is a build or runtime dependency. This folder contains only
the adapter data and behavior. See [pistol adapters](../../../../../../../docs/vr-pistol-adapters.md).
