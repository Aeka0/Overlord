# VR weapon impact-release overview

This document describes the registered weapon profiles and runtime behavior in the current working tree, including the latest interaction refinements. These are the Mod's interaction rules. Model inspection, offline tests and headset ergonomic acceptance provide different kinds of evidence.

## Mechanisms and interaction targets

| Mechanism | Striking object | Target | Result | Current coverage |
| --- | --- | --- | --- | --- |
| AK-style magazine-latch strike | Complete body of a held spare magazine | Magazine-release latch or paddle on the weapon | The installed magazine drops; the spare remains held | AK47, M14/M21, Dragunov, FAL, M200, MP5K, UMP45, FAMAS |
| Large magazine-release button strike | Complete body of a held spare magazine | Large magazine-release button or lever on the weapon | Same magazine-release operation, with an independently authored target and direction | AUG, TAR-21, F2000 |
| Receiver bolt-release paddle slap | Palm or fingers of the free manipulating hand | Left-side bolt-release paddle | Releases follower hold-open, chambers one round and closes the bolt | M4, M16, SCAR, Vector, UMP45 |
| HK-style charging-handle slap | Palm or fingers of the free manipulating hand | Raised, manually latched charging handle | Releases the manual catch and allows the handle/bolt to return | MP5K, AUG, UMP45 |

The first two mechanisms share `magazine_latch_contact` and `knock_magazine`. A similar-looking latch or large button does not automatically enable the capability on another weapon. Receiver paddles use the separate `release_catch` operation, which cannot eject a magazine. HK slaps operate the charging handle and are included here to distinguish them from the other releases.

All directions are relative to the weapon: `+X` points toward the muzzle, `-X` toward the stock, `+Y` toward the weapon's left, `-Y` toward its right, and `+Z` upward. Changing the holding hand or rotating the weapon does not mirror the physical controls.

## Magazine-latch and large-button strikes

### All enabled weapons

The table covers 11 weapon families and 27 distinct registered reload profiles. Profile IDs identify internal configurations, not every native weapon name or attachment combination. Admitted attachment combinations inherit their corresponding family profile.

| Weapon | Target | Accepted strike direction | Contact radius / rearm separation | Registered profile IDs | Configuration |
| --- | --- | --- | --- | --- | --- |
| AK47 | Release latch behind the magazine | Forward, `(1,0,0)` | 4 / 7.5 cm | `ak47`, `ak47_arctic`, `ak47_digital`, `ak47_desert`, `ak47_woodland` | [AK configuration](../src/client/component/vr/gameplay/weapons/ak47/reload_interaction.hpp) |
| M14 EBR / M21 | Latch behind the magazine | Forward, `(1,0,0)` | 4 / 7.5 cm | `m14ebr`, `m14ebr_arctic`; native `m21` and `m14ebr_thermal` use the same family admission | [M14 configuration](../src/client/component/vr/gameplay/weapons/m14ebr/reload_profile.hpp) |
| Dragunov | Latch represented by `j_mag_release` | Forward, `(1,0,0)` | 4 / 7.5 cm | `dragunov`, `dragunov_arctic`, `dragunov_woodland` | [Dragunov configuration](../src/client/component/vr/gameplay/weapons/dragunov/reload_profile.hpp) |
| FAL | Magazine-release latch | Forward, `(1,0,0)` | 2.5 / 6 cm | `fal` | [FAL configuration](../src/client/component/vr/gameplay/weapons/fal/reload_interaction.hpp) |
| M200 / CheyTac | Release latch behind the magazine | Forward, `(1,0,0)` | 4 / 7.5 cm | `cheytac`, `cheytac_desert` | [M200 configuration](../src/client/component/vr/gameplay/weapons/cheytac/reload_profile.hpp) |
| MP5K | Magazine-release paddle | Forward, `(1,0,0)` | 4 / 7.5 cm | `mp5`, `mp5_arctic` | [MP5 configuration](../src/client/component/vr/gameplay/weapons/mp5/reload_interaction.hpp) |
| UMP45 | Paddle behind the magazine | Forward, `(1,0,0)` | 2.5 / 6 cm | `ump`, `ump_arctic`, `ump_digital` | [UMP configuration](../src/client/component/vr/gameplay/weapons/ump/reload_interaction.hpp) |
| FAMAS | `j_reload_trigger` latch ahead of the magazine | Rearward, `(-1,0,0)` | 2.5 / 6 cm | `famas`, `famas_tape`, `famas_woodland` | [FAMAS configuration](../src/client/component/vr/gameplay/weapons/famas/reload_interaction.hpp) |
| AUG | Large release behind and above the magazine | Rearward/upward, `(-0.707107,0,0.707107)` | 2.5 / 6 cm | `aug`, `aug_plain` | [AUG configuration](../src/client/component/vr/gameplay/weapons/aug/reload_interaction.hpp) |
| TAR-21 | Release lever ahead of and above the magazine | Forward/upward, `(0.707107,0,0.707107)` | 2.5 / 6 cm | `tavor`, `tavor_digital`, `tavor_woodland` | [TAR-21 configuration](../src/client/component/vr/gameplay/weapons/tavor/reload_profile.hpp) |
| F2000 | Recessed large release button ahead of the magazine | Upward, `(0,0,1)` | 2.5 / 6 cm | `fn2000` | [F2000 configuration](../src/client/component/vr/gameplay/weapons/fn2000/reload_profile.hpp) |

AK, M14, Dragunov, M200 and MP5K use the 4 cm contact allowance from `rocking_magazine`. FAL and the five newly enabled families use 2.5 cm. Sharing a detector does not imply identical contact dimensions across weapons.

### Operation and effects

1. While the old magazine is still installed, acquire a spare with the manipulating hand and keep Trigger held to retain it.
2. Move the complete spare-magazine body outside the latch's rearm range, then strike the target in the weapon's configured direction. Its middle, sides and either end can produce the contact.
3. On success, the installed magazine detaches. The spare remains held, and the chambered round and action state are preserved. Rounds in the old magazine follow the existing dropped-item/discard accounting policy; the impact detector does not directly refill or erase the ammunition inventory.
4. Withdraw the spare from the insertion region before inserting it. Successful latch release starts a 300 ms insertion guard and requires withdrawal, preventing one contact from both ejecting and inserting a magazine.

Directly grasping and pulling out the magazine remains available. A latch strike does not require the installed magazine to be empty and does not automatically release follower hold-open. The transaction rejects an action that is currently held open by a hand (`held_open`).

### Geometry, thresholds and false-trigger prevention

| Property | Current implementation |
| --- | --- |
| Accepted striking object | Magazine held by the current manipulating hand; a bare-hand slap does not use this magazine-release path |
| Contact surfaces | One conservative oriented box encloses each complete magazine body and its permanent components; ammunition is excluded. Curves, hollows and small surface details are simplified within that enclosure |
| Latch position | Authored physical release location in weapon-local coordinates; it does not flip when hands are exchanged |
| Minimum directed speed | At least 0.15 m/s projected along the configured strike direction |
| Minimum directed travel | At least 1.2 cm of net approach along that direction |
| Direction test | Displacement projection and approach-side checks, without a separate fixed angle cone; oblique strikes must still satisfy the directed components |
| Sample continuity | Sample interval greater than zero and no longer than 150 ms, swept-motion bound no greater than 25 cm, and unchanged contact-box definitions |
| Rearming | The complete body must leave the separation distance in the weapon table before another approach is admitted |
| One approach | At most one transaction attempt; a failed native write consumes the approach and cannot retry continuously in place |

The detector follows the same material point on the magazine across successive poses. Switching the nearest corner cannot manufacture speed or travel. Spawned overlaps, reverse passes, slow pressure, tracking jumps and duplicate samples do not produce a valid strike.

This mechanism uses magazine motion relative to the weapon. It does not apply the world-space wrist-speed test used by bare-hand slaps below. Maintain the two mechanisms' thresholds and motion evidence separately.

## Receiver bolt-release paddle slaps

### All enabled weapons

There are 5 weapon families and 9 distinct registered reload profiles. Every target remains on the physical left side of the weapon and accepts an inward slap along weapon-local `-Y`.

| Weapon | Model target | Registered profile IDs | Configuration |
| --- | --- | --- | --- |
| M4 | Upper paddle of `j_clip_release`, identified as the bolt release | `m4`, `m4_arctic` | [M4 configuration](../src/client/component/vr/gameplay/weapons/m4/reload_interaction.hpp) |
| M16 | Upper paddle of `j_bolt_catch` | `m16` | [M16 configuration](../src/client/component/vr/gameplay/weapons/m16/reload_profile.hpp) |
| SCAR-H | Upper left receiver paddle, within the `j_gun` mesh | `scar` | [SCAR configuration](../src/client/component/vr/gameplay/weapons/scar/reload_interaction.hpp) |
| KRISS Vector | Centre of the complete exposed left `j_switch` paddle | `vector`, `vector_black` | [Vector configuration](../src/client/component/vr/gameplay/weapons/vector/reload_interaction.hpp) |
| UMP45 | Left receiver paddle behind the magazine | `ump`, `ump_arctic`, `ump_digital` | [UMP configuration](../src/client/component/vr/gameplay/weapons/ump/reload_interaction.hpp) |

### Required state and result

The action must be follower-locked open (`locked_open`). A magazine containing ammunition must be fully inserted, and the hand must have released its magazine grasp. The manipulating hand must be free: it cannot still hold the foregrip, a magazine, the charging handle or a knife. Open-hand slaps need no button input. An already formed Grip+Trigger fist is also accepted; Trigger-only pinch and a new Trigger press remain excluded so that part acquisition retains its authority.

Slap the side paddle inward with the free hand; no controller release-button press is required. On success, `release_catch` feeds one round from the magazine, closes the bolt and emits the existing action-close feedback. It cannot fall through to magazine ejection. An absent or empty magazine, a closed action, a manually latched action or an action still held open by a hand is rejected.

UMP's upper charging handle must be lowered. If it has been raised and manually latched, use the downward HK slap described below; the side paddle cannot replace that operation. UMP controller release buttons remain disabled.

Vector's contact target is centered on the full exposed paddle rather than its
foremost tip. Both receiver skins retain the shared palm/fist sweep, inward
direction, 2.5 cm target radius, and existing follower-lock transaction.

### Shared thresholds

| Property | Current value |
| --- | --- |
| Contact radius / rearm separation | 2.5 / 7 cm |
| Minimum directed approach travel | 2.5 cm |
| Minimum contact-motion speed | 0.5 m/s |
| World-space wrist speed | 0.5-8 m/s |
| Approach direction | Inward along `-Y`, with a direction cosine of at least 0.5: a 60-degree half-angle cone |
| Hand contacts | Original 21 palm/finger points plus a solid palm envelope sampled on a fixed 6 x 6 x 4 grid; includes the wrist heel, thumb pad, knuckle roots and both fist edges |
| Sample continuity | No more than 150 ms between samples; step allowance is `max(25 cm, 8 m/s * sample interval)`, with the independent world-space wrist-speed check |

Contacts come from the current raw wrist and admitted glove shape. Snapped render IK cannot create a slap, and moving the weapon into a stationary hand does not qualify. A failed native write consumes the approach; move away before trying again. Both hands use the same rules, and the control stays on the same physical side of the weapon.

The palm envelope comes from the glove's wrist and five finger roots, with bounded skin allowances. Every interior point is within 24 mm of a grid sample, below the unchanged 25 mm target radius. It extends only 8 mm behind the wrist and does not include the forearm. All palm and finger samples share one approach history, preventing another sample from retrying a failed write without separation. HK handle slaps keep their original 21-point geometry.

## Related mechanism: downward HK charging-handle slap

| Property | Current implementation |
| --- | --- |
| Weapons and profiles | MP5K: `mp5`, `mp5_arctic`; AUG: `aug`, `aug_plain`; UMP45: `ump`, `ump_arctic`, `ump_digital` |
| Required state | Handle raised and manually caught in `latched_open`; manipulating hand has no part grasp, does not hold support, and is not pressing Trigger |
| Target / direction | Actual raised charging handle, struck downward along weapon-local `-Z` |
| Contact radius / rearm separation | 6 / 10 cm |
| Minimum approach travel / speed | 2.5 cm / 0.5 m/s |
| Direction range | Direction cosine at least 0.35, approximately a 69.5-degree half-angle |
| Sampling and wrist speed | Shared hand-sweep detector and 0.5-8 m/s world-space wrist-speed limits used by receiver paddles |
| Result | Releases the manual catch; the return chambers a round when feeding is possible, without creating ammunition for an absent or empty magazine |

An HK slap can release a manually latched action without a magazine or with an empty magazine. A receiver-paddle slap requires a loaded magazine and automatic follower hold-open. UMP's two detectors retain separate approach histories; a side slap cannot be treated as a downward slap.

## Implementation entry points and maintenance

| Responsibility | Entry point |
| --- | --- |
| Registered weapons and reload capabilities | [weapon_registry.hpp](../src/client/component/vr/gameplay/weapon_registry.hpp), [weapon_reload_profiles.hpp](../src/client/component/vr/gameplay/weapon_reload_profiles.hpp) |
| Magazine-strike opt-in, direction, allowances, speed and travel | `manual_magazine` in each weapon's `reload_interaction.hpp` or `reload_profile.hpp`; shared definitions in [magazine_manipulation.hpp](../src/client/component/vr/gameplay/magazine_manipulation.hpp) |
| Complete magazine collision box and latch position | Each weapon's `magazine_collision.hpp` and `magazine_contact_profile` in `reload_poses.hpp`; [physical_reload_profile.hpp](../src/client/component/vr/gameplay/physical_reload_profile.hpp) performs coordinate conversion |
| Solid magazine sweep | [swept_box_contact.hpp](../src/client/component/vr/gameplay/swept_box_contact.hpp) |
| Receiver palm envelope and bounded contact grid | [palm_contact.hpp](../src/client/component/vr/gameplay/palm_contact.hpp) |
| Receiver-paddle configuration | [receiver_bolt_release.hpp](../src/client/component/vr/gameplay/receiver_bolt_release.hpp) and each weapon's `receiver_release` |
| Bare-hand slaps and HK catch | [handle_catch.hpp](../src/client/component/vr/gameplay/handle_catch.hpp), [swept_impact.hpp](../src/client/component/vr/gameplay/swept_impact.hpp) |
| Current input, grasp state and operation eligibility | [physical_reload_contact_sample.hpp](../src/client/component/vr/gameplay/physical_reload_contact_sample.hpp), [physical_reload_gesture.hpp](../src/client/component/vr/gameplay/physical_reload_gesture.hpp) |
| Atomic ammunition transactions | `knock_magazine`, `release_catch` and manual charging-handle operations in [detachable_magazine.hpp](../src/client/component/vr/gameplay/detachable_magazine.hpp) |

Before adding a weapon, verify its registration, actual release location, strike direction, end-contact boxes, mechanical transaction and tests, then update this document. Bone names or similar appearance alone are insufficient; M4's `j_clip_release`, for example, identifies a bolt-release target here. `spare_strike` defaults to `true`, so an inventory must inspect the final aggregate-initialized value. Searching only for an explicit `true` would miss FAL.

AA-12, L86, M82, WA2000 and P90 currently do not enable spare-magazine latch strikes. Receiver-paddle slaps on M4/M16/SCAR/Vector do not imply magazine-strike release. Machine-gun cover closure, tube feeds, pump actions and break actions retain their separate mechanisms and are not registered as latch/button release capabilities in this table.

## Audit and acceptance

Enumerating and deduplicating the current registered configurations confirms 27 magazine-strike profiles, 9 receiver-paddle profiles and 7 HK-slap profiles. These groups overlap and must not be added together as a count of distinct weapons. The receiver palm-coverage refinement preserves these coverage counts.

Relevant regression entry points: [magazine sweeps](../tests/vr/magazine_box_tests.hpp), [AK](../tests/vr/ak_reload_tests.hpp), [FAL](../tests/vr/fal_reload_tests.hpp), [precision rifles](../tests/vr/precision_reload_tests.hpp), [M200/MP5K and UMP side release](../tests/vr/comfort_interaction_tests.hpp), [new magazine-release refinements](../tests/vr/weapon_polish_tests.hpp), [receiver releases](../tests/vr/receiver_bolt_release_tests.hpp), [palm coverage](../tests/vr/palm_contact_tests.hpp), [receiver palm/fist trajectories](../tests/vr/receiver_palm_tests.hpp), [HK catches](../tests/vr/handle_catch_tests.hpp), and [slap sampling timing](../tests/vr/slap_timing_tests.hpp). These links identify existing coverage; this overview does not establish headset acceptance.

The whole-body refactor adds [complete magazine coverage tests](../tests/vr/magazine_body_tests.hpp) across all 27 registered strike profiles and [Vector paddle-edge regressions](../tests/vr/vector_receiver_release_tests.hpp) using native surface coordinates. [Spare-magazine latch contact](vr-magazine-latch-contact.md) records the approximation and bounded sweep implementation.

Headset acceptance should check each weapon with both hands: target alignment, ease of the required motion, unintended release during reloading, old/spare magazine presentation and ammunition accounting, and clear separation between UMP magazine release, side-paddle bolt release and downward HK slap. Source audits, offline model checks and automated tests do not replace that practical interaction review.

Historical evidence and detailed model records: [receiver bolt releases](vr-receiver-bolt-release.md), [MP5/AUG/UMP](vr-manual-handle-catch.md), [Dragunov](vr-dragunov.md), [M14/M82/WA2000](vr-precision-rifles.md), [TAR-21/F2000](vr-bullpup-rifles.md), and [the latest interaction refinements](vr-weapon-interaction-refinement.md). Keep this overview synchronized when impact-release coverage or parameters change.
