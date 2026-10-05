# .44 Magnum / Colt Anaconda adapter

Status (2026-09-08): first candidate implemented, compiled and covered by offline
tests. Native contracts captured read-only in a non-VR session. Deployed for HMD
testing on 2026-09-08; not HMD-accepted. This dedicated folder owns weapon-specific properties, part
bindings, poses, motion ranges and feedback keys. No extracted assets or parser code belong
here. See [revolver interaction](../../../../../../../docs/vr-revolver-interaction.md)
and [shared disposal policy](../../../../../../../docs/vr-ammunition-disposition.md).

Verified first-person model: `h2_viewmodel_colt_anaconda_base`.
Export/animation aliases include `colt_anaconda` and `coltanaconda`; these do not
by themselves prove the native WeaponDef identifier. Live capture separately
confirms native `coltanaconda`, capacity 6, with 68 hand + 25 receiver bones.
`profile.hpp` admits that exact binding through `cylinder_profile`; it has no
detachable-magazine reload descriptor and no closed-bolt-plus-one registration.

Opening itself does not clear contents. The confirmed gravity rule is fully open
AND opening faces world-down: an already-down gun clears only upon full opening,
with no turn-away/re-entry requirement. Retain both logical and visible contents
until then; an upward/neutral opening never clears unconditionally. Downward
orientation rejects loader transfer; upward orientation permits an otherwise
eligible transfer into a genuinely empty cylinder.

Reviewed LOD0 geometry:

- `j_speed_loader`: independent loader, 2,443 vertices / 3,024 triangles.
- `j_bullet01..06`: separate case bodies, 321 vertices each.
- `j_bullet_tip01..06`: separate projectile tips, 223 vertices each.
- `j_cylinder_rot` / `j_cylinder`: swing/support and cylinder parts. Rest/open
  transforms are authored from the export; HMD clearance still needs calibration.
- `j_cylinder_ammo` and `j_speed_loader` are siblings below `j_gun`; attachment
  to cylinder or off hand must be explicit, not assumed from bone hierarchy.
- All exported triangles belong to one rigid bone group, without mixed weights
  or cross-group triangles. Several groups share a surface; whole-surface hides
  are unsafe. The candidate creates independent immutable loader/case/tip model
  subsets through `scene_rigid_part`; no separately exported loader XModel is
  required. The first HMD run found a native resource-index crash on falling
  rounds. Each subset now registers its real loaded source identity through the
  shared scene boundary, retaining separate geometry. WARP/native-index/leaf-ABI
  regression tests pass; corrected native HMD rendering still needs acceptance.

Inspected animations: `h2_wpn_pst_colt_anaconda_idle`, `reload`, `inspect`, `fire`,
`empty_fire`. Reload has 93 frames at 30fps. Source feedback notetracks include
`weap_coltanaconda_lift_plr`, `weap_coltanaconda_clipout_plr`,
`coltanaconda_shelleject`, `weap_coltanaconda_clipin_plr`, and
`weap_coltanaconda_chamber_plr`. The fire clip also contains a Desert-Eagle-named
key; do not infer sound semantics from that filename/key. Resolve each event
through the live weapon's mapping. The captured map verifies clipout, clipin and
chamber keys used by the candidate. `coltanaconda_shelleject` has no verified
entry. A subsequent loaded native sound-pool audit verified the shared alias
`shell_eject_pistol` with 16 variations. The adapter selects it explicitly as an
alias, once per successful gravity clear, at the cylinder origin. Notetrack
lookup and direct alias lookup are distinct typed references; a missing key
never silently becomes an alias. This is ejection feedback, not a simulation of
surface-specific ground impacts. Native API playback and HMD audibility require
separate verification. Original frame numbers are sources for poses and feedback
discovery, never gameplay reload timers.

`poses.hpp` contains derived numeric poses: idle frame 0, held-loader reload frame
43 and open-cylinder reload frame 52, plus equip-rest groups and relative round
anchors. Export centimetres are converted once to native inches. Source hashes
are retained in the header; the runtime does not read exported models/animations.

Local separation audit inspected six related models and five animation exports.
Its report, source hashes and color-coded bind-geometry preview are kept under
ignored local asset-audit storage, not referenced by a build or shipped renderer.

Shared implementation: `cylinder_feed` owns transactional live/case/loader state;
`cylinder_gesture` owns geometric/input gates and mechanical travel;
`cylinder_runtime` owns native admission/commits; `cylinder_presenter` owns part
binding, visibility and exact-frame independent objects. Both feed families use
the common native boundary, ammunition policy primitives and feedback service.

The default candidate keeps used empty loaders held, supports partial supply,
clears only when fully open and currently downward, and requires a fresh trigger
after twist closure. First native import treats missing rounds as spent cases;
gravity-clear once to establish known-empty contents. Runtime weapon ownership
is still the existing right-rear stage, although core tests cover either hand.
The revised closure gate uses a 220 ms signed-motion window, allowing modest
swing and acceleration/deceleration instead of requiring every frame to be a
pure fast roll. See the linked interaction document for thresholds and guards.

Use `vr_cylinder_status` for copied state/presentation diagnostics;
`vr_cylinderReload` defaults to 1 only after VR admission. See the linked
interaction document for current tuning and the HMD acceptance matrix. Native
subset rendering, contact/roll comfort and simultaneous object visibility remain
unaccepted. Intermediate save/load restoration is not implemented. The shared
`vr_discardAmmoPenalty` toggle defaults off; when enabled, discarded loader
payloads and cleared live rounds are lost. Releasing a held loader at the waist
recovers its remaining rounds, and forced cleanup is exempt.
