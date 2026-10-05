# Magazine population and chamber cartridge presentation

The reviewed 0–3 round subset is implemented. Desktop geometry and offline
regressions do not establish admission of every loaded native asset or headset
presentation.

## Implemented scope

The following exact receivers display 0, 1, 2 or 3 magazine rounds. Counts above
three use the same three-round model, with no additional draw or vertex work.
The count is the magazine payload, excluding the chamber. A spare held magazine
uses its own payload; cosmetic drops retain the count captured at release.

| Family | Enabled receiver variants |
| --- | --- |
| AK47 | base, arctic, digital |
| TMP / MP9 | base |
| ACR | base, black, digital |
| AUG | arctic |
| MP5K | base, arctic |
| FAL | base, including its existing underbarrel assembly |
| PP2000 | base |
| L86 | base |
| M9 | existing H1-named `wpn_h1_pst_m9_vm` receiver |
| M93R | base |

These ten families / fifteen receiver variants use their original top three
positions and stagger. No extra cartridge is cloned at a guessed pitch. M9/M93R
retain the authored partly concealed lower rounds inside the magazine; these
partial meshes are not promoted to chamber cartridges. ACR excludes the fourth
lower decorative round. FAL uses `tag_clip_02`, preserving the separate hiding
of its native animation-only spare magazine.

M14/M21 (base/arctic) live chamber geometry now translates with the manual
action up to the existing extraction boundary. It stays independent of magazine
removal and disappears on the committed extraction. During the automatic shot
cycle it does not pretend to be the spent case.

M200 (base/desert) now draws its live chamber cartridge independently of the
magazine-top cartridge, using its existing reviewed chamber endpoint and
manual-bolt displacement. Bolt lift does not spin the cartridge. The existing
in-transit feed mesh retains its route and remains the only cartridge during
feeding. A chamber round remains visible when the magazine is empty/removed.
Spent-case geometry is not synthesized from a live cartridge.

## Shared implementation

- `magazine_fill.hpp` describes four cumulative immutable face selections,
  exact model identity, skeleton/surface counts and bounds. Each family owns its
  authored resource header, with source SHA-256 provenance. Missing skins do
  not inherit the base model's indices.
- `scene_models::rigid_part` reuses its existing validated multi-material
  factory for face selections across multiple rigid bones. Original vertex
  buffers/materials remain shared and untouched; each state owns only its
  indices and existing descriptor/CPU metadata. No per-frame mesh creation.
- `native_magazine_assets` prepares the complete four-state set on the existing
  main asset-owner boundary, then publishes it atomically. It validates native
  model/surface topology, bone membership, selected-face coverage and resulting
  bounds. No partial state set is published. Retirement retains the existing
  drained-unload lifetime contract.
- Inserted, held and dropped magazines use the same cached state. The inserted
  native magazine subtree is hidden only while its replacement is available;
  the rigid draw adds one magazine submission per active supported gun, rather
  than one submission per bullet. Original slide/receiver and unrelated parts
  retain their native route. M9 explicitly enables precise rigid-group hiding.
- Inserted magazines and M200 chamber rounds use distinct persistent lighting
  slots and the existing exact object/matrices/epoch placement callback. The
  callback checks weapon identity, instance, reference generation, ownership,
  payload state and freshness before applying the current native view origin.
  Holding a spare while an inserted magazine exists cannot share a slot.
- `chamber_cartridge.hpp` consumes committed ammunition state and manual motion
  only. Rendering never runs a feed transaction or changes ammunition counts.

### Support-grip transition visibility

Headset feedback identified a brief inserted-magazine disappearance when an empty
offhand acquired the foregrip on AK, FAL and ACR. The carry coordinator commits a
new support grip after the frame's reload update. Skinning stamps that newer carry
owner onto its pose, while the reload view can retain the previous support hand
until the next simulation tick. The rigid placement callback previously rejected
this difference even though the receiver and magazine were unchanged; its native
magazine subtree was already hidden for the replacement draw.

`reload_attachment_state.hpp` now supplies the same dependency checks at model
submission and native placement. Inserted magazines, chamber rounds and action
partitions follow the receiver, so support-only changes or offhand magazine-grasp
selection do not suppress them. Detached magazines still require the same hand,
grasp and visible payload. If a support hand is the weapon's sole carrier, it
continues to participate in identity validation.

Weapon/instance identity, rear-hand lease, tracking reference, payload subset,
active/fault status and the 150 ms freshness limit remain checked. Placement still
requires the exact native object, matrices and skin epoch, with the current native
view origin; there is no previous-frame or latest-pose fallback. The subsequent
[cross-weapon audit](vr-support-render-audit.md) also corrects support-only
differences at scene registration/skin binding and the held-weapon fallback.
Scene identity, exact native bone matching and conflicting-epoch rejection remain.

Regression reproduces actual carry support acquisition/release with the skin and
reload snapshots arriving in either order, on both hands and all fifteen counted
magazine variants. It also covers magazine removal, changed population, handover,
foreign lifetimes, tracking reset, stale snapshots, detached grasp changes and
sole-support carry. Weapon-grip, physical-reload and spatial-panel suites and the
Debug client build pass. Confirmation of the visible fix in the headset is pending.

Preparation is intentionally serialized with H2's existing asset owner/D3D
publication boundary; parallel asset mutation would violate that contract.
Render workers consume immutable pointers and never wait on offline discovery.

## Deferred decisions and risks

These are explicit exclusions from this implementation, not completed features.
Existing accepted presentation remains for excluded profiles.

| Scope | Missing evidence / risk | Decision needed |
| --- | --- | --- |
| M4/M16, M1911, USP, M82 | Only one/two independent source round positions are established; a third needs authored placement and clearance | Approve deriving a three-round stack from measured magazine geometry, then review 0–3 states |
| Desert Eagle | Two shared rounds plus a separately animated child need phase/ownership review before selecting three persistent positions | Confirm which meshes belong to magazine vs reload/chamber animation |
| G18, Mini-Uzi, Vector, UMP, FAMAS, TAR-21, AA12, Dragunov, WA2000, M200 magazine | A usable single-round shape does not establish three correct magazine positions | Author per-model pitch/stagger/tilt and retain follower geometry; do not scale by nominal caliber |
| M14 magazine | Existing round belongs to the chamber; taking it for the magazine would erase independent chamber ownership | Author a separate magazine-stack source/placement |
| SCAR | Partial lower geometry requires grouping by actual round, rather than connected-component count | Review complete per-level face sets or approve replacing partial rounds |
| F2000 | Multi-round connected strips have open/nonmanifold boundaries | Choose face-level repair versus a different approved single-round source |
| P90 | Transparent magazine, follower and native five-round visual bands; three visible rounds would misrepresent the rest | Decide between preserving full magazine bands and authoring exact low-count states |
| M240/MG4/RPD | Articulated external belt; some bones include links, some contain no round | Decide whether the three-round cap applies only inside the box; preserve exposed belt semantics |
| Tube/fixed-drum shotguns, revolver, break actions, launchers | Existing individual-shell/cylinder/chamber/rocket ownership differs from detachable magazines | Separate family-specific requests; do not impose a three-round box-magazine rule |
| Other chambers | No reviewed chamber endpoint, extraction/feed path or interior clearance for most weapons | Approve per-family geometry/animation capture before new chamber placement |
| Automatic shot-cycle cases and M200 spent cases | Final FX→XModel chain and exact case geometry remain unconfirmed | Choose native case extraction or approved per-family empty-case resource; never reuse a whole live round |
| Missing exports | AK desert/woodland, ACR arctic, AUG plain and other audit gaps | Capture exact variants before promoting their face recipes |

No change to caliber, ammunition ledger, feed timing, gameplay mechanics,
production extraction tools or upstream code is included.

## Validation and remaining acceptance

Desktop coverage includes four-state geometry counts, cumulative face membership,
zero/negative/extreme counts, chamber exclusion from magazine count, simultaneous
spare payload, exact skin selection, immutable D3D WARP multi-bone face subsets,
source preservation and rejection of foreign-bone faces. Chamber tests cover
empty/removed magazines, manual translation without bolt rotation, extraction,
in-transit feed, spent cases, invalid inputs and repeated rendering without
ammunition mutation. Existing weapon-grip, physical-reload, pistol-profile and
rigid-part suites are required alongside the client build.

Completed for this change: Debug client build; weapon-grip, physical-reload,
pistol-profile and rigid-part/WARP suites (all pass); Debug EXE/PDB production
feature audit (all twelve checks pass); whitespace and UTF-8/path checks.
Optimized client builds and game/HMD execution were not run for this change.

Pending in game: confirm loaded native face order/material/LOD admission for all
fifteen variants; inspect magazine mouths with textures at counts 0/1/2/3/4;
test both hands, dual weapons, a held spare alongside an inserted magazine,
partial pull/seating, drops, checkpoints and unloading. Check M14 and M200 live
extraction/return with the last round and no magazine. No HMD success is claimed.
