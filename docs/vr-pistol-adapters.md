# Pistol adapters

This guide describes shared pistol reload services and the authored profiles for
M9, M1911, Desert Eagle, USP, G18, M93R, TMP, and Mini Uzi. Exact native name,
effective capacity, receiver hierarchy, required bones, and rigid-part readiness
control admission. Each family keeps its own poses, magazine transforms, slide
contacts, feed rules, and sound notetrack keys.

Magazine and slide contacts use bounded profile data. A newly pressed input must
reach the admitted contact with valid orientation and travel; held poses remain
latched through the operation. The shared pistol capture policy supports knife
co-grasps while retaining each feed and control-hand lease. See
[physical melee](vr-melee.md) and [M9 physical reload](vr-m9-reload.md) for those
shared boundaries.

## Boundaries

- `viewmodel_policy.hpp` owns render visibility policy and attachment
  declarations; these are not ammunition/reload properties. `weapon_attachments.hpp`
  validates the full assembly and returns its static cosmetic mask and optional
  visible-attachment muzzle. Hidden knife props cannot supply a firing origin.
- `viewmodel_visibility.cpp` is a shared native render adapter, independent of
  reload instances and gesture logic. `hands/component.cpp` combines assembly and
  optional mechanical masks and publishes once for the exact solved bone epoch.
  No reload adapter or active reload is required to suppress an authored prop.
- The reload presenter returns only its mechanical hidden-part mask alongside
  presentation success; it cannot publish or overwrite cosmetic visibility.
  `part_rig` contains magazine/action/round roots and an optional validated internal
  bolt for a charging-handle adapter. Missing mechanical assets
  still prevent physical contact admission, not unrelated cosmetic presentation.
- Visibility counters now belong to the hands/viewmodel status, rather than the
  reload presenter. Native hooks and draw filtering are unchanged.

- Per-weapon folders (`m9/`, `m1911/`, `de50/`, `usp/`, `g18/`, `m93r/`, `tmp/`, `miniuzi/`) own their grip/equip poses,
  magazine transforms, slide grips/travel, ammo rules and sound notetrack keys.
- `physical_reload_profile.hpp` defines immutable adapter data and local-space
  geometry. `weapon_reload_profiles.hpp` provides exact native admission and
  bounded per-profile model cache slots. `physical_reload_rig.hpp` validates roots.
- `physical_reload_runtime.cpp` keeps the selected profile on each owned weapon
  instance. Interrupt/refund, external ammo grants, firing and transactions use
  that instance's rules, not a global current weapon or M9 capacity.
- `detachable_magazine.hpp` dispatches explicit closed/open-bolt feed policies
  while sharing magazine transactions. `open_bolt.hpp` has no persistent chamber
  round; its sear-ready state differs from the closed-bolt follower lock. Initial
  native admission uses the selected feed, and existing owned instances retain it.
- `physical_reload_presenter.cpp` preserves the accepted native viewmodel
  preparation/skinned-epoch attachment route. Held snapshots and dropped magazines
  retain their profile/instance; an old drop cannot use the new weapon's magazine,
  rail, clearance or exit time. Native weapon-indexed event cursors avoid replay
  when switching back. Missing assets do not substitute another pistol's magazine.
- Sound feedback resolves profile-specific notetrack keys through the current
  live WeaponDef. No raw audio, mesh, animation, parser or extraction tool is built
  into the mod. An unavailable key is silent; M9 audio is not a fallback.

## Reviewed profiles

| Profile | Native identity | Required base capacity | Receiver | Independent magazine |
|:--|:--|--:|:--|:--|
| M9 | `beretta` | 15 | `wpn_h1_pst_m9_vm` | `h2_weapon_beretta_clip` |
| M1911 | `colt45` | 7 | `h2_viewmodel_colt45_base` | `h2_weapon_colt45_clip` |
| Desert Eagle | `deserteagle` | 7 | `h2_viewmodel_desert_eagle_base` | `h2_weapon_desert_eagle_clip` |
| USP | `usp` | 12 | `h2_viewmodel_usp_base` | `h2_weapon_usp_clip` |
| USP suppressed | `usp_silencer` | 12 | same receiver + `attach_h2_silencer_02_vm` | same USP magazine |
| G18 | `glock` | 32 | `h2_viewmodel_glock_base` | exact receiver body/round subsets |
| M93R | `beretta393` | 20 | `h2_viewmodel_beretta_393_base` | exact receiver body/round subsets |
| TMP | `tmp` | 32 | `h2_viewmodel_mp9_base` | exact receiver body/three-round subsets |
| Mini Uzi | `uzi` | 32 | `h2_viewmodel_miniuzi_base` | exact receiver body/round subsets |

Physical admission requires the exact native name, base/effective capacity,
reviewed model assembly, required bones, and rigid magazine. Animation aliases
are not native weapon identities. Use `vr_reload_interaction_status` to inspect
the native name/capacity, profile, asset readiness, and admission reason.

All these profiles author button magazine release, closed-bolt feed, last-round lock and
plus-one. A native initial total of 7 partitions into magazine 6 + chamber 1,
not a free eighth round. A full replacement magazine with a retained chamber may
reach 7 + 1; reserve/escrow conservation remains shared. Non-admitted weapons
retain native reload; explicit native chamber profiles can still provide +1.

This increment authors right rear-grip single-wield presentation. Logical tests
also exercise left/right owners, but that does not enable left-rear mesh grips.
Dual wield, unreviewed attachments, gold/silver variant admission and save/load
restoration of intermediate physical states are not claimed. Rifle candidates
are described separately in [weapon variants](vr-weapon-variants.md).

## Offline evidence

M1911:

- H2 `h2_wpn_pst_m1911_reload_empty` frame 46 supplies settled idle/support,
  frame 22 supplies the magazine grasp. The available H1 idle/reload clips are
  relative-type animations, so they are not treated as H2 absolute hand tracks.
- `h2_wpn_pst_m1911_pullout_first` frame 13 supplies the slide hand, with the
  sampled slide travel removed from wrist placement. The former 2 cm rear shift
  was present in the loaded image but did not clear the port in the reference image.
  The replacement targets the foremost finger-skin edge at preview X=+2.5 cm;
  see calibration below. Orientation/curls and mechanical travel are retained,
  with a new rear-slide contact consistent with the new wrist placement.
- VM magazine and standalone magazine topology differ (455 vs 457 body vertices).
  Bound-based translation was checked by nearest-neighbour body residual:
  maximum 0.01485 cm. No same-vertex-order assumption is made.
- Exported last-fire lock is approximately 45.17 mm. Candidate manual travel is
  55 mm, full-stroke threshold 50 mm. The extra overtravel is deliberate tuning
  for a full pull beyond lock, not a claim about the native animation maximum.
- Clip-out, clip-in and closing keys are present in native export notetracks.
  No isolated M1911 rear-pull key is claimed: rearward feedback is tactile only;
  closure uses `weap_m1911colt_chamber_plr` if the live sound map resolves it.

Desert Eagle:

- Treat `de50` and `desert_eagle` as authored asset aliases. The single-wield
  `h2_wpn_pst_de50_idle` frame 0, `reload` frame 31, and
  `first_time_pullout` frame 13 supply the candidate poses. Do not select
  `akimbo_l/r` animations merely because they share the family token.
- `tag_clip` has a non-identity bind tilt. Relative translations add the bind
  origin; animation rotations remain parent-local rotations. Multiplying the
  animated rotation by bind rotation again would double the magazine tilt.
- The rigid magazine contains extra/seam/round geometry (2180 vertices vs 674 VM
  magazine-body vertices). Body alignment residual is below 0.00010 cm.
  `rigid_in_magazine` includes inverse bind rotation, not translation alone.
- This single-bone native rigid model has baked-in round geometry. Independent
  empty drops do not yet hide those rounds individually; this is cosmetic only
  and never changes the mechanical ammunition count. The gun's separate
  `tag_bullets` subtree still follows the logical empty-magazine state.
- Contact is transformed into an oriented well frame. Alignment compares the
  magazine against the seated magazine axis, not against an assumed gun +Z.
- Exported settled last-fire lock is approximately 76.34 mm. Candidate manual
  travel is 87 mm, full threshold 82 mm. Comfort and release feel are HMD-pending.
- Semantic keys include `weap_de50_clipout_plr`, `weap_de50_clipin_plr`,
  `wpn_h1_deserteagle_ins_pull`, and `weap_de50_chamber_plr`.

USP:

- H1 absolute idle frame 0, reload frame 20 and inspect frame 103 supply wrist
  and finger poses without H2's crossed knife hand. Retarget by bone name and
  preserve H2 glove segment lengths; H1-only `_3` tip channels are not imported.
- Native desktop evidence: `usp`, base capacity12, weapon23, ammo12/72, no dual,
  segmented or additive reload. The DObj contains 68 hand + 12 receiver + 2 knife
  bones. Knife root80 aliases receiver `tag_knife`76. No live memory was written,
  game code called, process suspended or test candidate hot-loaded for capture.
- An explicit hidden-attachment policy validates the exact model/root/parent and
  hides the knife's own rigid group. Unknown attachments are not admitted.
  No raw mesh mutation, global knife hide or melee damage/ownership change.
  Cosmetic suppression is independent of ammo ownership/magazine asset readiness,
  but missing magazine assets still prevent physical interaction admission.
- Native magazine surface shares clip/round groups (445/543 and 746/1153
  vertices/triangles). It uses the same granular visibility as M1911 so empty
  rounds cannot cull the magazine body. The separate knife is 4608/6186.
- Native lock is50.45mm, manual60mm/full56mm. Rigid magazine body alignment has
  maximum residual below0.000003cm. Grip comfort/tolerances await HMD testing.
- Native sound-map keys for out/in and `pull_slide` resolve to USP sounds.
  The apparently cross-family `weap_m9_chamber_plr` actually resolves to
  `weap_usp45_chamber1_plr` in USP's own map; no borrowed M9 mapping is used.
- Campaign capture confirmed a distinct exact native name `usp_silencer`12,
  sharing the receiver/bones and sound map. Its 84-bone assembly has a visible
  suppressor root80 ->78, `tag_flash_silenced`81, knife root82 ->76. The generic
  validator admits only reviewed attachment topology and muzzle orientation.
  Both USP variants share pose/mechanics data but keep separate native identities
  so a mismatched scene cannot take over a different weapon's ammunition.

### Rear slide grasps

The accepted finger-front targets are +2.5 cm for M1911 and -1.5 cm for USP.
These are preview horizontal
X-axis coordinates in centimetres of the foremost finger edge, NOT a wrist
position, contact centre or the caption's `source - N cm` amount. With the
reference `viewhands_us_army` glove and native finger lengths, skinning gives:

| Weapon | Source finger-front X (cm) | Requested X (cm) | Rear shift from source (cm) | New wrist X (cm) |
|:--|--:|--:|--:|--:|
| M1911 | 13.8734 | 2.5 | 11.3734 | -14.7251 |
| USP, both variants | 12.7453 | -1.5 | 14.2453 | -18.0125 |

Only whole-grasp X placement changes; curls/orientation are retained. Rebind
contact-in-wrist to an actual rear-slide surface point (nearest reference finger
skin is within 0.83 mm/1.40 mm respectively) instead of translating the old
contact behind the gun. This is a vertex-based proximity check, not proof of
collision-free skin or HMD fit. Existing acquisition volumes/tolerances and
mechanical strokes are unchanged. CPU tests cover rest and locked-slide contact.
Other hand meshes keep their own segment lengths; visual clearance through the
full stroke remains to be tested. Previews and geometry stay local-only.

Final M1911 and USP finger-front targets are +2.5 cm and -1.5 cm respectively. The profiles keep separate rear-slide contact points while retaining their authored curl and orientation. Reference finger/slide proximity is below 1.42 mm for the reviewed poses; this does not establish dynamic clearance or headset comfort.

## Verification and next HMD pass

### Fast magazine approach correction

The common gesture controller discards the untrusted sweep and rebases its
history when a discontinuity ends outside. It only requires exit/reentry when
the discontinuity lands inside the neighbourhood. No capture radius, alignment,
cooldown, native write or rendering path is changed. Spawn-at-contact and
rejected-transaction safeguards remain. Regression covers all authored
profiles, both rear hands, spare-before-eject, and shot/eject/fetch sequences.

Insertion still requires valid contact within the 2.5 cm radius. This retry
policy does not replace headset checks for alignment and reach.

### Native HUD plus-one boundary

The native HUD pip table is sized to magazine capacity, while a chambered
round can make the displayed loaded count one greater. The original HUD review identifies a matching
boundary: only capacity pips exist while the event handler indexes loaded ammo,
which can now be capacity+1. The original gameplay Lua adapter saturates that
one graphical lookup to the last real pip, without appending a child, changing
table length, ammo values, numeric text or layout. It only adapts the single-hand
ammo graphic instance; new/cached graphics are handled after native refresh.
Other missing indices, foreign metatables and unrelated UI remain untouched.
No global ammo query overrides, broad error catches or extracted Lua are shipped.
CPU Lua tests cover plus-one/normal/empty, weapon changes, cached graphics and
feature toggles. Native HKS/error cessation still requires HMD/runtime retest.

### M1911 shared-surface visibility

The native DObj hide mask is empty; the MOD hides global bone 77 (`tag_bullets`)
when magazine rounds are zero. A chambered round may remain after the magazine
payload reaches zero and must remain visible.

LOD0 `m/mtl_wpn_h1_pst_m1911_body` is one rigid XSurface containing five groups:
magazine (420 triangles), frame (3472), rounds (360), trigger (188), hammer (474).
Its part mask includes all five bones. The native surface cull tests ANY overlap
with the hide mask, which explains why the frame disappears while the separate
slide and grip surfaces remain. No-magazine hiding has the same shared-surface
hazard, so it is covered by the same fix rather than a last-round exception.

M1911 opts into `part_visibility::rigid_groups`. The reusable visibility module
keeps the existing object/matrices/epoch contract, leaves native hide bits and
assets untouched, and changes only the renderer-owned rigid packet's surface
pointer to an immutable metadata variant. Hidden groups have zero triangles;
group count, vertex counts, offsets, transforms, materials and GPU buffers stay
unchanged. The original rigid builder and draw consumer were checked in live
code: each rigid group's triangle count feeds DrawIndexed. M9 and Desert Eagle
remain on their reviewed surface-mask route.

The cache is bounded to 64 immutable variants, never recycles in-flight entries,
and includes source metadata/group contents in its key to detect asset reuse.
Unsupported skinned/subdivided/range layouts or exhausted cache keep the original
surface visible and increment a rejection counter; they never hide the frame or
alter native assets. Status exposes readiness, variants, filtered and rejected
counts. This is not a claim that arbitrary skinned mixed surfaces are supported.

Retest M1911 with chamber-only, completely empty, ejected and newly seated
magazines, including both eyes and locomotion; frame, trigger and hammer must
remain visible and only the relevant rounds/magazine should disappear. Confirm
the shifted grasp and repeated slide cycles, then quickly regress M9/Desert Eagle.

### Shared regression checklist

CPU coverage includes exact native/profile matching and rejection, finger/part
binding, malformed rig rejection, world-rotation invariant tilted-well contact,
full magazine clearance, slide contact selection, unchanged M9 exit geometry,
seven/twelve-round feed rules, no fabricated ammunition, both logical rear owners,
new-before-old magazine order, immediate eligible insertion, partial/full/repeated
slide strokes and old-instance escrow refunds. These controlled tests cannot
reproduce or dismiss the outstanding in-game random insertion delay.

On restoration, verify M9 first; then each base new pistol:

1. Admission, independent hands/support and unchanged weapon aim/muzzle.
2. Both magazine orders; gun/held/dropped magazines simultaneously, switching
   while holding or dropping, moving and turning the player.
3. Empty follower vs no magazine, release priority, chamber-only fire,
   loaded magazine + empty closed chamber, full/partial/continuous slide cycles.
4. Sound/haptic lookup, stock-ammo pickups, tracking loss and interruption.
5. Fit/tolerances and the known delayed insertion against each actual visible
   model. Do not close the M9 issue based on a passing new weapon alone.
