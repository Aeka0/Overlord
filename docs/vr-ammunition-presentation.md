# Magazine population and chamber cartridge presentation

Counted magazine presentation covers 29 detachable-magazine families through
41 exact receiver models and 42 reload profiles. The suppressed USP shares its
receiver recipe. P90 retains its separate full-magazine content bands. Some
camouflage variants still lack the exact source evidence listed below.
Desktop geometry and offline regressions do not establish headset acceptance.

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
| Desert Eagle | base, gold |
| SCAR-H | base receiver, including its existing support attachments |
| M4 | base receiver and its existing support attachments |
| M16 | base |
| M1911 | base |
| USP | base receiver, ordinary and suppressed profiles |
| M82 | base |
| G18 | base |
| Mini-Uzi | base |
| Vector | base, black |
| UMP | base, arctic, digital |
| FAMAS | arctic, tape |
| TAR-21 | base, digital |
| AA12 | base |
| Dragunov | base |
| WA2000 | base |
| M200 | base |
| M14/M21 | base, arctic |
| F2000 | base |

Where the source contains three usable levels, face selections retain their
original positions and stagger. M9/M93R
retain the authored partly concealed lower rounds inside the magazine; these
partial meshes are not promoted to chamber cartridges. ACR excludes the fourth
lower decorative round. FAL uses `tag_clip_02`, preserving the separate hiding
of its native animation-only spare magazine.

Models with only one or two cartridge positions use their own complete round
geometry in an authored three-round stack. Measured cartridge thickness and
the magazine column determine translation offsets. Existing two-round stagger
and column lean remain intact. Generic copies follow the native magazine axis
and measured outer body envelope. Open feed mouths use explicit per-family
placements and support geometry: a solid convex envelope alone cannot describe
their visible cavity. Any required whole-column fit preserves cartridge spacing.
Outer-envelope checks do not certify inner-wall clearance. Orientation,
scale, UVs, normals and native material identity are preserved. Caliber names do
not supply geometry or a shared pitch. These added placements still need textured
in-game inspection at the magazine mouth.

F2000's two connected strips are divided into their original per-round face
levels, using the measured strip spacing. No new vertices or caps are added;
the simplified lower source geometry stays concealed within the magazine.
P90 is not capped to three visible rounds: its existing mutually exclusive
five-round bands and empty follower continue to represent the transparent body.

Desert Eagle selects its three original 322-triangle cartridges, including
`j_bullet01` beneath `tag_bullets`. Its base and gold receivers have different
surface layouts and therefore separate immutable geometry recipes. They share
the same feed id, reload rules, contacts, sounds and knife co-grasps. Inserted,
held and dropped magazines all use the receiver-derived population state instead
of the standalone magazine's permanently baked-in rounds.

SCAR retains its complete 2,448-triangle magazine body, including the 56-triangle
magazine-owned interior structure. Its first three cartridge levels add 336,
181 and 160 triangles; the fourth 80-triangle decorative level is excluded.
The lower cartridges retain their original partially concealed geometry and
are not exposed as independent chamber or dropped-cartridge models.

M14/M21 (base/arctic) live chamber geometry now translates with the manual
action up to the existing extraction boundary. It stays independent of magazine
removal and disappears on the committed extraction. During the automatic shot
cycle it does not pretend to be the spent case. Separate immutable copies form
the magazine stack below the measured feed lips. Borrowing source geometry
does not move, hide or transfer ownership of the live chamber cartridge.

M14/M21, FAMAS and TAR-21 use a centered top cartridge with visible lower columns.
M14's 95-face magazine support and TAR-21's 134-face `j_plate` remain present in
all four states and descend below the lowest displayed cartridge. The fixed
body selection excludes those faces to prevent duplicate support geometry.
FAMAS retains its existing shallow support and places the lower pair in the
available row. TAR-21's top round is aligned to the measured magazine mouth;
its original bind-space position was below the support. These are cosmetic
population states and do not change feed timing or the ammunition ledger.

M200 (base/desert) now draws its live chamber cartridge independently of the
magazine-top cartridge, using its existing reviewed chamber endpoint and
manual-bolt displacement. Bolt lift does not spin the cartridge. The existing
in-transit feed mesh retains its route and remains the only in-transit cartridge
during feeding. The base magazine shows the remaining payload independently;
the committed feed transaction already removes that round from its count.
A chamber round remains visible when the magazine is empty/removed.
Spent-case geometry is not synthesized from a live cartridge.

## Shared implementation

- `magazine_fill.hpp` describes original face levels or a body plus three
  translated native-round instances, with exact identity, topology and bounds.
  Each family owns its
  authored resource header, with source SHA-256 provenance. Missing skins do
  not inherit the base model's indices.
- `scene_models::rigid_part` validates multi-material face ownership. Static
  selections may span rigid and skinned surfaces, as required by M4; mixed
  receiver/part boundaries reject. Instanced stacks copy only referenced
  vertices into immutable buffers and merge compatible material surfaces.
  Original assets stay untouched. Four states are prepared once, with no
  per-frame mesh creation or discovery.
- `native_magazine_assets` prepares the complete four-state set on the existing
  main asset-owner boundary, then publishes it atomically. It validates native
  model/surface topology, bone membership, selected-face coverage and resulting
  bounds. No partial state set is published. Retirement retains the existing
  drained-unload lifetime contract.
- A required receiver visibility mask is prepared for both standalone and
  counted magazines before publishing a usable asset. M4 needs this mask for
  its shared skinned surface as well as rigid-group hiding. Constructing a new
  magazine mesh does not by itself remove the original receiver geometry.
- Asset preparation separates source lookup, topology validation, bone binding,
  population construction and final publication into named operations. The
  refresh coordinator reuses complete retained sets; per-family face tables
  contain data only and do not add render-time discovery or weapon-specific
  branches to the asset service.
- Inserted, held and dropped magazines use the same cached state. The inserted
  native magazine subtree is hidden only while its replacement is available;
  the rigid draw adds one magazine submission per active supported gun, rather
  than one submission per bullet. Original slide/receiver and unrelated parts
  retain their native route. M9 and both Desert Eagle variants explicitly enable
  precise rigid-group hiding.
- Replacement visibility includes receiver-parented magazine rounds on G18 and
  WA2000, while excluding independently owned M14 chamber and M200 feeding
  geometry. TAR-21's `j_plate` remains magazine-owned while its cached position
  follows the displayed population.
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
reload snapshots arriving in either order, on both hands and all counted
magazine profiles. It also covers magazine removal, changed population, handover,
foreign lifetimes, tracking reset, stale snapshots, detached grasp changes and
sole-support carry. Headset confirmation remains separate from these checks.

Preparation is intentionally serialized with H2's existing asset owner/D3D
publication boundary; parallel asset mutation would violate that contract.
Render workers consume immutable pointers and never wait on offline discovery.

## Remaining coverage and separate mechanisms

These are explicit exclusions from this implementation, not completed features.
Existing accepted presentation remains for excluded profiles.

| Scope | Missing evidence / risk | Decision needed |
| --- | --- | --- |
| P90 | Full-magazine bands are a five-round approximation | Preserve the current transparent-magazine/follower policy; exact intermediate populations remain separate work |
| M240/MG4/RPD | Articulated external belt; some bones include links, some contain no round | Retain the existing belt/box presentation; these are not detachable box-magazine stacks |
| Tube/fixed-drum shotguns, revolver, break actions, launchers | Existing individual-shell/cylinder/chamber/rocket ownership differs from detachable magazines | Separate family-specific requests; do not impose a three-round box-magazine rule |
| Other chambers | No reviewed chamber endpoint, extraction/feed path or interior clearance for most weapons | Approve per-family geometry/animation capture before new chamber placement |
| Automatic shot-cycle cases and M200 spent cases | Final FX→XModel chain and exact case geometry remain unconfirmed | Choose native case extraction or approved per-family empty-case resource; never reuse a whole live round |
| Missing exact variants | AK desert/woodland, ACR arctic, AUG plain, M4 arctic, FAMAS woodland, TAR-21 woodland, Dragunov arctic/woodland, M200 desert | Ten profiles retain their previous presentation until their own geometry is captured; no base-skin face-index substitution |

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

The source audit compares 20 loaded candidate models' skeletons, topology and
27 selected rigid surfaces' vertices/face indices with their exports. These
witnesses agree. An offline replay constructs every enabled population through
the production rigid-part factories using all 41 exported receiver models.
An additional native CPU-geometry snapshot covers the same 41 models, including
their actual blend streams and surface descriptors; all 42 profile constructions
pass offline replay through the production factories. This does not establish
the final headset presentation or the active native visibility hook behavior.
Opaque depth previews compare the reported M14/FAMAS/TAR-21 layouts before and
after correction from above and both sides. Runtime verification must still
confirm M4 removal hides its original shared surface and that the textured
magazine mouths remain correct in both eyes.
Focused regressions cover the nested cartridge parent, distinct gold surface
layout, 0/1/2/3 population, separate spare/chamber counts, knife co-grasps, quick
reload and support transitions. Asset preparation still runs only on the native
main-thread owner and publishes a complete immutable set.

Pending in game: confirm loaded native face order/material/LOD admission for all
41 receiver variants; inspect magazine mouths with textures at counts 0/1/2/3/4;
test both hands, dual weapons, a held spare alongside an inserted magazine,
partial pull/seating, drops, checkpoints and unloading. Check M14 and M200 live
extraction/return with the last round and no magazine. No HMD success is claimed.
