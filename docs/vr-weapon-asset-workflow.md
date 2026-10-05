# VR weapon asset discovery and adaptation workflow

Status: external/local-only discovery boundary established  


This workflow turns H2 weapon assets into reviewable candidates for the weapon
interaction architecture without requiring an XR runtime or HMD. Acquisition,
conversion, and review tools are engineering aids: they are not H2-Mod runtime
features and are not included in the main executable or release package.

The output is deliberately non-authoritative. Asset names, animation channels,
and notetracks can identify likely moving parts and useful hand-pose samples,
but production profiles still require visual verification, explicit overrides,
and later real-HMD ergonomic acceptance.

## 1. Tooling boundary

The previously explored in-process metadata collector is parked there as a
local reference prototype. It is deliberately not registered as a client
component: `src/client/**.cpp` is globbed into the main program, which would
otherwise turn an acquisition aid into a shipped feature.

Only reviewed runtime results may cross this boundary. Examples are compact
weapon-family profiles, verified bone bindings, constrained motion parameters,
and wrist-relative finger poses. External binaries, decoding implementations,
raw exports, source animations, and review reports do not cross it.

## 2. Acquisition and normalized evidence

Use mature external tools instead of making the Mod an asset extractor:

- Greyhound reads MW2CR models and animations and can export decoded animation
  curves through Cast or SEModel/SEAnim. Keep the chosen format behind a local
  adapter; the current acquisition set uses SEModel/SEAnim;
- x64-zt enumerates H2 zones and traces `WeaponDef`, `XModel`, and `XAnimParts`
  assets;
- Cast or SEModel/SEAnim provides the interchange layer for skeleton hierarchy,
  skinning, quaternion/translation curves, and notification tracks; choose by
  source-tool support and keep the parser behind the local adapter;
- small local adapters normalize those outputs into review evidence.

Greyhound may reject a modified game executable when its build-identification
checks no longer recognize the binary. In that case, acquire assets from a
matching unmodified official copy and record the executable/build identity.
H2-Mod must match reviewed results by stable asset name, skeleton signature,
and content evidence; it must not depend on Greyhound or its process reader.

Useful normalized evidence includes:

- view, world, hand, persistent-arm, clip, projectile, knife, and attachment
  model bindings;
- each model's bone names, parent indices, base transforms, bounds, and LOD
  summary;
- right-handed, left-handed, generic, and attachment-override animation
  bindings;
- each animation's animated-bone list, frame rate, duration, flags, and
  notetracks;
- each weapon's `notetrackSoundMap`, `notetrackRumbleMap`, attachment-specific
  notetrack overrides, resolved sound-alias names, and alias playback metadata;
- `WeaponDef` discovery hints such as clip size, ammo indices,
  `boltAction`, `segmentedReload`, `worldClipModel`, alternate weapon, and reload
  timers.

Shared model and animation assets are stored once in a top-level catalog;
per-weapon records contain bindings into that catalog. This avoids multiplying
the same hand rig or XAnim data across a large acquisition set. Each normalized
record must include source tool, source revision, game executable/zone identity,
asset name, and a content hash. Formats and local adapters are replaceable;
these provenance and identity fields are the stable contract.

## 3. Classification and confidence boundaries

The local analysis stage produces four kinds of evidence:

1. `activity_bone_candidates` identifies likely magazine/feed, trigger,
   slide/bolt/action, selector, loading-port, and ejection bones. A matching bone
   that also appears in an XAnim channel list receives higher confidence.
2. `spatial_anchor_candidates` identifies likely muzzle, aim, and weapon-mount
   tags. They are kept separate from moving parts.
3. `key_hand_pose_candidates` proposes source animation and normalized sample
   time for `primary_grip`, `support_grip`, `mag_grip`, `charging_handle`,
   `slide_release`, `shell_pinch`, and `device_grip`. A relevant notetrack wins
   over the midpoint fallback.
4. `family_candidates` combines conservative `WeaponDef` hints for detachable
   magazines, segmented loading, manual cycling, and single-round modules.
5. `audio_event_candidates` keeps animation notification keys, resolved sound
   aliases, and rumble/feedback mappings as separate fields. Inspection
   animations remain valid evidence sources even when their stock interaction
   is not reproduced in VR.

Every generated binding has `candidate_only` or `review_status: candidate`.
Confidence is ranking, not authorization. In particular:

- a bone named `j_bolt` may be cosmetic, share motion with another part, or use
  an unsuitable local axis;
- an animation's bone list proves that channels exist, not their transform
  values or whether motion is mechanically meaningful;
- a clip notetrack is a useful sample marker but its stock timestamp is not the
  future VR reload commit point;
- a notetrack name is only a lookup key until the selected `WeaponDef` sound,
  rumble, FX, and attachment-override maps are captured; generic markers such as
  `viewmodel_small`, `viewmodel_medium`, and `viewmodel_large` must not be
  silently treated as sound aliases;
- a resolved sound alias is sufficient for runtime integration. Decoded audio
  files are optional review evidence and must not become runtime dependencies;
- a filename-only sound tree can confirm that a candidate underlying sound name
  exists and can accelerate review, but it cannot prove alias membership or a
  `WeaponDef`/attachment mapping;
- finger channels identify a possible pose source, but the rotations still need
  extraction into an independent wrist-relative pose asset;
- missing names disable only that candidate path and should be handled by an
  explicit per-weapon override or fallback.

## 4. Review and adaptation checklist

When expanding a proven mechanical family, existing decoded models/animations
can supply the review and synthetic tests without repeating in-game acquisition.
The AK candidate follows this path: offline meshes, parent graphs, curves and
notetracks supply explicit overrides; established native transaction, visibility
and scene-model services retain their checked admission boundaries. New native
layout/behavior assumptions still require evidence. Record in-game presentation
and HMD ergonomics as pending; an offline candidate is not runtime acceptance.

For each representative weapon:

1. Open the first-person model in the existing XModel viewer and verify every
   candidate bone visually. Record the actual constrained local axis, travel
   limits, rest/latched values, and parent relationship.
2. Preview the candidate fire, reload, empty reload, and rechamber animations.
   Verify that the reported bone is genuinely animated and identify useful
   notetrack boundaries.
3. Preview inspection animations as a supplemental evidence source. Record
   stable hand poses, manipulation phases, and sound/feedback notification keys
   even when no stock inspection interaction will ship in VR.
4. Resolve relevant notification keys through `WeaponDef::notetrackSoundMap`
   and `notetrackRumbleMap`, including active attachment overrides. Verify that
   each resolved alias exists by asking the game audio system to play it; export
   decoded audio only when offline listening or waveform comparison is useful.
   The VR interaction owns event timing; stock animation frames are reference
   evidence rather than runtime scheduling authority.
5. Sample candidate hand animations at the proposed normalized times. Store only
   finger-joint rotations relative to the selected wrist/interaction anchor;
   do not preserve the stock arm root or stock animation timing.
6. Create an explicit weapon-family profile override. Auto-discovery may prefill
   fields, but it must not directly enable an interaction.
7. Exercise the profile with desktop synthetic/recorded hand paths. Validate
   missing-bone handling, axis projection, limits, hysteresis, cancellation, and
   mechanical/ammo invariants.
8. Mark ergonomics pending until the HMD is available. Controller grip offsets,
   reach, two-hand comfort, interaction radii, insertion tolerances, occlusion,
   and haptics cannot pass on desktop evidence.

The first review batch should cover one closed-bolt pistol, one detachable-mag
rifle/SMG, one tube-fed shotgun, one manually cycled weapon, one launcher, and
one composite weapon for each underbarrel feed style. Analyze unusual devices
after those families establish the common profile vocabulary.

## 5. Acquisition coverage

For accessory families, use the
[variant collection and test matrix](vr-weapon-variants.md#6-collection-and-test-matrix).
Capture exact native identity and full attachment/parent topology alongside the
shared receiver. Track loaded-scene coverage separately from export coverage;
neither a museum weapon nor one decoded asset folder proves every campaign
variant is represented. Compare normalized snapshots and promote only reviewed
recipes, not every possible combination of recognized model names.

Start with Greyhound and its supported decoded interchange format on a
representative weapon set because curves close the largest evidence gap: actual
per-bone motion and finger-joint rotations. The current batch uses
SEModel/SEAnim. Use x64-zt `verifyzone`, `dumpzone`, `dumpasset`, and `dumpmap`
for zone-complete enumeration and `WeaponDef` dependency tracing.

No single source is automatically authoritative. Cross-check asset names,
skeleton signatures, animation bindings, and hashes between independent dumps
or game versions. Record disagreements in the review report instead of adding a
permanent extraction path to the Mod.

If a missing fact can only be observed in the running game, prefer an
out-of-process local probe. A temporary in-process experiment must remain a
local build overlay, must never be registered in the default client build, and
must be removable without touching runtime source.

Sound acquisition follows a narrower rule than model and animation acquisition.
The production Mod stores semantic-event bindings and stable alias names, then
asks H2's existing sound system to resolve and play those aliases. It does not
decode, copy, or package the underlying audio. A local alias-preview command may
use the known non-positional UI playback entry to validate names without an HMD;
the production adapter must use the native player/weapon positional path once
that call contract is verified.

## 6. Acceptance states

Use these states in the review queue:

| State | Meaning |
|:--|:--|
| `captured` | Raw live asset metadata exists. |
| `candidate` | Analyzer found likely bindings or pose samples. |
| `desktop_verified` | Visual inspection and synthetic interaction tests pass. |
| `hmd_pending` | Desktop work is complete; ergonomics remain untested. |
| `hmd_verified` | Device-specific comfort and interaction acceptance pass. |
| `fallback` | Required assets or engine transaction support are unavailable. |

No weapon advances from `candidate` directly to `hmd_verified`. Evidence and
explicit overrides should remain traceable so later asset or executable changes
can invalidate only affected profiles.

This workflow supplies the asset-discovery portion of
[VR weapon interaction and mechanical state architecture](./vr-weapon-interaction-architecture.md).
It does not replace the runtime state probe or engine transaction work described
there.
