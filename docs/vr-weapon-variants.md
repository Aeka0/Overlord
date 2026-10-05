# Weapon variants and attachment composition

This guide records weapon-family variant and attachment composition. Each
weapon profile remains the authority for its admitted assemblies and validation
scope; one accepted variant does not establish family-wide headset support.

[L86](../src/client/component/vr/gameplay/weapons/l86/README.md) and
[FAMAS](../src/client/component/vr/gameplay/weapons/famas/README.md) now have
closed-bolt physical adapters: two-hand aiming, chamber plus-one, physical-only
magazine removal, last-round hold-open and button/manual return. L86 uses fitted
left manipulation poses because the native clips use the right hand. Both FAMAS
receiver appearances are registered. Native capacity/variant and HMD confirmation
remain pending; no spare-magazine strike or HK handle catch is enabled.

PP2000 has a [physical adapter](../src/client/component/vr/gameplay/weapons/pp2000/README.md)
with two-hand aiming, button magazine release, chamber plus-one and no automatic
hold-open. Empty replacement needs a top-handle cycle. Source geometry and
offline interactions are validated; live native variant/HMD confirmation is pending.

FAL now has a [physical adapter](../src/client/component/vr/gameplay/weapons/fal/README.md)
for ordinary and underbarrel-shotgun rifle grips: physical magazine pull, spare
magazine latch strike, last-round hold-open and button/manual-handle return.
The button cannot release its magazine. Offline source geometry is authored;
live native variant capture and HMD acceptance are still pending.


## Vector implementation checkpoint, 

The [Vector candidate](../src/client/component/vr/gameplay/weapons/vector/README.md)
adds base/black receivers with one native integral-foregrip pose and two-hand
aiming. Optic/suppressor variants reuse the shared attachment resolver and retain
the same support position. M4-style closed-bolt transactions provide magazine
ejection, empty lock and button/manual release. The folding side handle uses
native reload geometry and the shared finite return transition; the separate
internal bolt retains an authored empty-lock pose based on native firing travel.
Magazine body and round rendering use exact receiver subsets.

The 336 synthetic assembly combinations and twelve VR regressions passed, as did
client compilation. No extra game capture or deployment was performed. Native
admission, folding-handle ergonomics and both-eye presentation remain HMD pending.

## ACR implementation checkpoint, 

The [ACR candidate](../src/client/component/vr/gameplay/weapons/acr/README.md)
reuses M4-style magazine release, empty lock and button/manual-handle chambering.
Base/black/digital receivers each select bare-handguard or M203 support while
retaining exact magazine appearance. The side handle has its own native travel
and converted left-overhand grasp. ACR-native base hands are retained; the M203
support now uses a read-only capture of native `h2_wpn_asl_masada_gl_idle`, with
the complete left palm/finger chain. This phase adds no grenade operations.

The shared attachment resolver and bounded native-family matcher are reused;
native instance names, primary feed and complete asset/rig admission remain
separate. ACR's two magazine-child round roots use exact receiver subsets.
The observed `masada_digital_grenadier_eotech` / 30 revealed the missing
`attach_h2_m203_vm_digital` contract, which had rejected grip and reload together.
Base and digital M203 models now share the same validated launcher role. The
3,024 synthetic assembly combinations are regression coverage, not observed game
variants. HMD acceptance of the corrected admission, support and handle remains pending.

## AK implementation checkpoint, 

The AK candidate uses three support configurations: bare, GP-25 and underbarrel
shotgun, across the base/arctic/digital receiver skins. It reuses the shared
rifle attachment contracts and validation now also used by M4. Underbarrels only
select support contact; no switching, firing or loading of alternate feeds is
added. Native instances remain separately pinned, while scene identity selects
the exact magazine camouflage.

The physical latch supports direct grab/pull and a held-spare strike that releases
the old magazine without losing the spare. AK has no B/Y release or last-round
lock; an empty reload requires a full bolt stroke. The left hand has index-side
and little-finger-side poses at the actual right-side charging tab. A fresh grab
chooses by wrist orientation and retains that choice until release; the right
rear grip remains anchored. Primary equip suppression restores receiver rest
poses so native right-hand cocking cannot leave residual part motion.
Contact comes from raw tracked
hand/magazine geometry, with separated approach, direction, speed and continuity
checks. The receiver mesh supplies independent magazine subsets because the
world clip differs in shape. See the [AK candidate](../src/client/component/vr/gameplay/weapons/ak47/README.md)
for provenance, operations, tests and pending runtime/HMD verification.

This increment reused existing offline exports and verified shared native
boundaries; it did not require another game capture. Its 8,064 synthetic assembly
permutations are code coverage, not native observations. Export review and runtime
admission cannot replace the later in-game presentation and ergonomic check.

## M4 implementation checkpoint, 

MW2CR M4 has exactly two underbarrel configurations: vertical
foregrip and M203. Other native variants combine those with suppressors, optics
and camouflage. No bare-handguard or additional underbarrel pose is invented.
Existing M4 primary interaction/hand data is reused directly, selecting only the
support contact from the actual underbarrel. This supersedes the earlier
foregrip-only admission and repeated per-native-profile approach described below.

The candidate has one shared physical reload definition, two immutable grip
profiles, explicit attachment/root/cardinality contracts and pinned exact native
instance identities. `m4`/`m4_...` and `m4m203`/`m4m203_...` recognition proposes the family but cannot write
ammo without the matching complete scene, capacity/mode, unique native cells,
prepared skinned-magazine visibility and independent-magazine asset. No new
mode switching or grenade firing/loading is included. Grenade ammo is not admitted
to the rifle state machine. The native alternate-mode exclusion remains in place.

The M4 resolver covers exported cover/silencer, ACOG/EOTech/red-dot/thermal and
reviewed camouflage aliases, plus compatible native sensor/laser prop roots.
Only support grip differs. Free-hand/physical-operation wrist orientation remains
shared, so underbarrel replacement does not rotate the copied waist magazine.
Native sound names and inventory states are not merged by family/profile pointer.
4032 synthetic combinations cover role selection, optional attachments, ordering
and glove labels; this is resolver coverage, not4032 observed game variants.

The broader common assembly architecture below remains a plan; no dynamic plugin,
runtime extraction or mutable descriptor cache was introduced. See the
[current M4 candidate](../src/client/component/vr/gameplay/weapons/m4/README.md)
for operational and rendering limits. Earlier sections retain the source trail
and design constraints, not a claim that their old M4 blockers are still unchanged.

This records the suppressed-USP admission lesson and next implementation
increments. It refines the assembly/module design in
[weapon interaction architecture](vr-weapon-interaction-architecture.md#44-weapon-assembly-and-module-profiles),
not a second interaction, input or ammo authority. Acquisition follows the
[local-only asset workflow](vr-weapon-asset-workflow.md).

HMD reports indicate USP HMD acceptance after the one-third grasp return. Preserve
that pose; this acceptance does not independently certify every glove or
attachment combination. Deployment/CPU tests likewise do not imply family-wide
acceptance or acceptance of other attachment combinations.

## 1. What the USP exposed

| Concern | Ordinary USP evidence | Suppressed campaign USP evidence | Consequence |
|:--|:--|:--|:--|
| Native identity / capacity | `usp` / 12 | `usp_silencer` / 12 | Shared receiver and capacity do not identify a native weapon. |
| Receiver | `h2_viewmodel_usp_base`, 12 bones | Same | Share reviewed receiver mechanics, not native inventory identity. |
| Captured assembly | 68 hand + 12 receiver + 2 knife bones | 68 hand + 12 receiver + 2 suppressor + 2 knife bones | Full assembly and resolved parents matter; indices shift. |
| Attached knife | Root80 aliases receiver76 | Root82 aliases receiver76 | Hide the proven knife/alias subtree, not a fixed range after the receiver. |
| Suppressor | Absent | `attach_h2_silencer_02_vm`, root80 aliases receiver78 | Validate and preserve this visible attachment. |
| Muzzle | Receiver `tag_flash` | Attachment `tag_flash_silenced`81 | Recognizing a model is insufficient: resolve its output anchor too. |
| Captured hand model | `viewhands_us_army` | `viewhands_arctic` | Hand skin is not weapon identity or a reason to share global indices. |

Numbers above are fixture evidence, never production lookup keys. Two separate
admission gates originally failed: exact native-name binding and full-assembly
profile selection. Fixing either alone would leave physical interaction
unavailable. The attachment muzzle must also be checked after admission works.

The deployed candidate uses separate exact native/profile identities while
sharing base USP poses, magazine geometry, mechanics and semantic sound keys.
Common attachment validation checks model/root/receiver-parent, bounded topology
and visible-muzzle basis. The knife mask does not hide the suppressor. CPU tests
cover both captured assemblies and malformed/ambiguous attachment data.

Source trail: [assembly selection](../src/client/component/vr/gameplay/weapon_profiles.hpp),
[attachment validation](../src/client/component/vr/gameplay/weapon_attachments.hpp),
[USP recipes](../src/client/component/vr/gameplay/weapons/usp/profile.hpp), and
[captured-layout regression fixtures](../tests/vr/pistol_profile_tests.cpp).
Raw capture/deployment evidence stays in the ignored local USP engineering note;
the table and synthetic fixtures are the portable evidence retained here.

Notetrack keys are not sound aliases: USP's `weap_m9_chamber_plr` resolves to a
USP sound through its own map. The two observed variants share that map, but
this does not prove that every rifle accessory shares base sound/FX or animation
overrides. The current sound adapter reads the verified base map; general
attachment-override resolution is still pending.

Successful engineering path: verify native identity and live assembly read-only;
reconstruct duplicate-root ownership; compare offline geometry/poses; author
explicit contracts; add captured-layout and rejection tests; build; backup and
hash-check deployment; then obtain variant-specific HMD acceptance. The nearby
crash is separately unresolved, not evidence of a variant bug.

## 2. Current limits before broad rifle expansion

The [M4 adapter](../src/client/component/vr/gameplay/weapons/m4/README.md)
confirmed `m4_silencer` with the authored foregrip, cover and suppressor. Its
sound map includes M203 keys despite no launcher being attached. Receiver tags,
available clips and sound keys are discovery hints, not assembly admission.
Its mixed-weight receiver also differs from the rigid Magnum: reuse contracts
and services, not an unverified rendering assumption across weapon families.

- Assembly dispatch now uses the shared [weapon registry](vr-weapon-registration.md).
  Family selectors retain their reviewed attachment contracts. M4 requires
  exactly one foregrip or M203, with shared topology, cardinality, and muzzle
  validation. Independent sight anchors remain separate work.
- `profile` combines receiver, hand pose, aiming, equip behavior and reload
  binding. `reload_profile` combines exact native identity with common mechanics
  and geometry. Its small copied USP variant is deliberate, but cloning whole
  profiles for every rifle combination is not the scaling strategy.
- Native/scene agreement currently uses immutable profile pointers. Composed
  definitions cannot be temporary objects passed to existing consumers: storage,
  identity and lifetime must be designed together.
- `assembly_key` hashes model names in order, including hands. Native pose/skin
  epoch guards exist separately. That name hash is neither a topology signature
  nor a native instance ID; do not silently expand its meaning.
- Firing currently publishes one muzzle frame and the hand rig requires a single
  forward receiver muzzle. Underbarrel support is not implemented by merely
  allowing another attachment model.

These are source-verified limits, not claims that all listed attachments are
malfunctioning. This planning change does not alter deployed behavior.

## 3. Decision: compose reviewed data, not every combination's full profile

| Layer | Owns | Must not imply |
|:--|:--|:--|
| Native binding | Exact native identity, verified mode/flags, base/effective capacity, live ammo/engine binding | A model-name prefix authorizes ammo writes |
| Weapon family/chassis | Receiver contract, mechanical family, base anchors, authored handed poses | All guns with equal capacity share mechanics |
| Attachment definition | Exact model aliases, parent contract, required/provided capabilities, local anchors | One shared model fits every receiver transform |
| Reviewed assembly recipe | Required/allowed/forbidden combinations and per-chassis overrides | Every product of known attachments is supported |
| Resolved runtime assembly | Immutable validated bone bindings, chosen capabilities/modules, definition revision | Frame poses or mutable ammunition belong in definition data |

```text
exact native binding + family + reviewed attachment recipe
                          |
           validated live model/parent graph
                          |
             immutable resolved assembly
              /           |            \
       grip/part poses  module bindings  render/audio/FX bindings
                          |
           existing native transaction authority
```

Use compiled typed descriptors first. Do not add a runtime extractor, plugin
system, arbitrary scripting/JSON override language or deep inheritance tree.
Shared data lives once; each weapon retains its own anchors, poses and exceptions.

Proposed organization, not new empty folders to create now:

```text
vr/gameplay/
  weapon_native_binding.hpp       exact native-to-family/module admission
  weapon_assembly.hpp             immutable resolved definition and identity
  weapon_assembly_resolver.hpp    bounded engine-independent composition
  weapon_attachments.hpp          reuse/extend current graph validation
  weapons/
    attachments/                 genuinely shared reviewed attachment data
    usp/                         shared data + two exact native recipes
    m4/                          shared data + recipes + local fit overrides
    ak/                          only when that family is authored
```

These names are proposals, not current interfaces. As composition grows, keep
model-role/anchor contracts outside render-visibility-only policy. The visibility
backend consumes resolved masks/draw rules, not attachment names or ammo state.

## 4. Attachment effects and composition rules

| Class | Potential effects to review | Boundary |
|:--|:--|:--|
| Suppressor / muzzle device | Firing origin, muzzle FX, obstruction extent, sound overrides | No automatic new feed/chamber; not merely passive visibility |
| Optic / sight | Sight/aim reference, body, occlusion, support-space interference | Not the bullet muzzle; no implicit HUD relocation |
| Foregrip / handguard | Support wrist/fingers, acquisition region, two-hand baseline, nearby part clearance | Not rear-grip ownership or a new ammo pool |
| Magazine / feed accessory | Capacity, mesh, well/exit geometry and feed rules if actually changed | Never infer capacity from visual resemblance |
| Cover / cosmetic prop | Visibility, rigid groups and ownership | Passive only after other effects are reviewed |
| Underbarrel launcher / shotgun | Own muzzle, feed/action, ammo binding, selector and support grip | Composite fire module, not a passive accessory |

1. Recipes list required/optional/excluded reviewed attachments with cardinality
   and dependencies. Missing required or unknown weapon-owned attachments reject
   the affected contract. Exact reviewed aliases are allowed; prefixes and parsed
   name suffixes remain discovery hints only.
2. Attachments can have several effects. A launcher may provide a support region
   and a firing module; a single classification enum must not discard either.
3. Nonconflicting effects compose independent of discovery order. Two providers
   for one primary muzzle or support region are a conflict, not last-writer-wins.
   A reviewed recipe may explicitly select/replace a provider; arbitrary priority
   numbers must not conceal an ambiguous combination.
4. Keep firing origin, muzzle FX, sight/aim, HUD attachment and obstruction bounds
   distinct even if some share a tag initially. Preserve the accepted frame-aligned
   HUD/independent-part rendering paths when changing an attachment anchor.
5. Resolve anchors in their owning model/module space through native duplicate
   aliases and attachment parents. Handle non-identity gun roots and nested
   mounts explicitly, not an assumed direct receiver parent. The USP validator
   currently supports its direct-parent case, not arbitrary nesting. Check the
   final muzzle basis in the chassis frame as well as its local attachment frame.
6. Measured fit overrides belong to the chassis+attachment pair. Shared mesh or
   animation names do not prove equal wrist transforms, clearance or mechanical
   travel. State coordinate system, units and whether calibration targets wrist,
   contact centre or finger edge; never use an ambiguous offset label.
7. Feedback uses the selected native binding and confirmed mechanical event.
   Capture effective attachment/mode sound, FX and animation overrides before
   adding exceptions. Unresolved overrides are reported, not replaced with an
   unrelated gun's sound. Raw audio stays outside the Mod.

Mounted attachments remain static initially. Physically attaching/removing
accessories is separate from supporting native campaign variants and scripted
weapon replacement.

## 5. Identity, rebinding and safe capability boundaries

Keep four generations/keys separate:

- Native weapon instance: inventory ownership, native weapon/mode binding,
  transaction revision and module state. Two weapons sharing assets never share
  mutable ammunition merely because they use the same recipe.
- Reviewed recipe: family, native binding, attachment roles and authored revision.
  Canonicalize roles for comparison, preserving duplicate counts and parent
  relations. Hash lookup verifies equality rather than trusting collisions.
- Resolved rig generation: model ranges, bone topology, duplicate aliases,
  relevant bind data and asset lifetime. Changed rigs invalidate indices even
  when names match. A different glove retargets hands, not the retained chamber.
- Frame publication: retain existing object/matrices/epoch, consumed-skin,
  stereo-pair, owner and tracking-reference checks. Definition caching is not
  permission to reuse a previous-frame transform.

Publish a complete immutable assembly at a valid engine boundary, never a
half-updated grip/muzzle/reload combination. Transactions require matching
instance/definition revisions. Render workers consume bounded snapshots without
ammo writes or waiting for extraction/analysis. Rig rebinding cancels stale part
and support interactions through the existing interrupt/refund path before
rearming; it must not reset valid chamber state. Mode switching preserves the
shared chassis and independent primary/underbarrel states.

Fallback is explicit, not unsafe independently toggled bits:

- Before physical admission, unsupported assemblies retain known generic/native
  behavior; do not enable guessed anchors or VR ammo ownership.
- An already-owned physical instance cannot simply hand ammo back to stock
  reload after a contract disappears. Preserve its state, invalidate stale
  gestures and reconcile/refund through the verified native boundary. Failed
  refunds retain escrow and block the transition instead of creating rounds.
- Capabilities fail independently only when independence is proven. Missing
  optional cosmetic/audio data need not erase primary mechanics. An unsupported
  underbarrel may use module-local native fallback only after mode, ammo and
  input isolation are verified, as required by the parent architecture. That
  isolated composite fallback is not implemented now.

Incomplete scene loading is not proof an asset does not exist. Retry only at
bounded new asset/rig/context generations; rejection caching must not permanently
poison a variant after its first incomplete frame. Cache shared magazine assets
by verified asset/geometry identity, not by native recipe count, without merging
instance state. Cache exhaustion must reject safely rather than evict in-flight
definitions or renderer metadata.

## 6. Collection and test matrix

Reuse local probes/parsers; add a normalized variant comparison report, not a
collector in the shipped client. Capture build and map/zone identity, native name
and effective flags, capacity/ammo bindings, complete ordered model layout,
resolved parents/duplicates, relevant bind anchors, asset/skeleton hashes,
LOD/rigid-group ownership and effective animation/notetrack/sound/FX bindings.
A coverage manifest distinguishes observed, exported, not loaded, missing and
unreviewed assets.

Group by receiver plus attachment roles/topology, then compare changed fields.
Export presence is not proof of a native recipe; a loaded-scene dump is not a
game-wide inventory. Unknown identities enter a review queue, not automatic
registration. Parallelize bounded offline parsing/hash/mesh work on immutable
captures, never unsynchronized live engine reads. Raw tools/reports remain
removable ignored local data, without machine paths in shipped descriptors.

Record acceptance per variant and capability using the existing captured,
candidate, desktop_verified, hmd_pending, hmd_verified and fallback states;
store CPU-test results separately as evidence. A museum/base test cannot accept
every campaign variant.

Test priority:

1. Enumerate every shipped recipe in pure tests: native/scene agreement, required
   and extra attachments, cardinality, conflicts, topology/ranges, aliases,
   nonfinite transforms, wrong muzzle axes and incomplete loading.
2. Reorder model descriptors with correctly remapped bone indices; change hands;
   reuse names with changed topology. Preserve recipe meaning where appropriate,
   but rebuild rig bindings and never reuse stale bone indices.
3. Test each effect alone and relevant pairs (optic+grip, suppressor+grip,
   suppressor+optic), plus high-risk full recipes. Pairwise ergonomic tests do
   not authorize unreviewed combinations or replace resolver coverage for every
   shipped recipe.
4. Exercise weapon/script/mode changes with held magazines or actions, last-round
   and empty states, external ammo grants, tracking loss, recenter and loading.
   Check stale commits, double refunds and cross-module ammo writes are rejected.
5. HMD: actual muzzle, support and sights; action/ejection-port access through
   full stroke; both eyes; hand movement and locomotion; no HUD/part drift.
   Include different glove sets and actual campaign variants. Keep accepted
   M9/Desert Eagle behavior as regression baselines.

## 7. Bounded implementation order

### A. Record and confirm the baseline

This plan and the USP evidence are the first deliverable. Complete HMD acceptance
of the deployed suppressed USP/rear grasps separately. Preserve the open
insertion-delay and crash issues; variant expansion does not fix them.

### B. First code increment: composition without new weapon behavior

Separate exact native binding from shared mechanical/pose data; introduce stable
immutable resolved-assembly storage. Re-express USP's two bindings and existing
M4 foregrip assembly as typed recipes. Preserve guards, frame publication,
buttons and instance/refund behavior. No new physical rifle reload or automatic
attachment inference in this refactor.

When preserving M4's current passive list, label that evidence as legacy
name-only admission; do not silently upgrade it to validated muzzle/sight
capabilities or use it to authorize new combinations. Those checks belong to C.

Exit criteria: existing CPU suites plus resolver/native-identity/rebind fixtures
pass; current recipes retain behavior; diagnostics identify native binding,
family, attachments, chosen anchors, readiness and exact rejected contract.
Formatting and file I/O stay outside pose/native locks with bounded logging.

### C. M4 first, then a second rifle family

M4 already has reviewed grip data and live attachment topology. Capture available
native recipes spanning standard handguard, foregrip, suppressor, optics and
representative combinations; measure their existence/names rather than inventing
them. Replace passive muzzle-device admission with explicit anchor validation.
Add rifle-specific action, support and magazine data for reviewed configurations.
Use an AK-family adapter next to prove the common composition does not assume
M4 tags, origins, hand poses or magazine-release mechanics.

### D. Composite modules after native routing evidence

Capture primary/alternate identity, confirmed mode transitions, ammo ownership,
reload interception and both firing origins. Implement the existing primary+
underbarrel design with independent mechanical state, not one global muzzle/ammo
object or a respawned rifle on mode switch. Physical accessory swapping remains
separate.

No exhaustive full-profile cloning, speculative engine hooks or unrelated
runtime cleanup is required. Each increment has reviewable data, deterministic
tests, safe deployment and an HMD gate.

## Manual handle catch adapters

MP5K, AUG and UMP45 now use the shared manual receiver catch and physical
magazine pull. See [coverage, native evidence and acceptance](vr-manual-handle-catch.md)
for the six reviewed receiver recipes, AUG foregrip/optic contracts and
multi-material magazine rendering. HMD interaction acceptance remains pending.
