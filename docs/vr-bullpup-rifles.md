# Bullpup rifle physical interaction

This page covers the TAR-21 and FN2000 adapters. Runtime profiles contain only
reviewed constants; exported assets are authoring evidence, not build
dependencies.

| Weapon | Native identity / capacity | Magazine | Empty action |
| --- | --- | --- | --- |
| TAR-21 | `tavor` family / 30 | Physical extraction or forward/upward spare-magazine release strike | Follower lock; release control or charging-handle cycle |
| FN2000 | `fn2000` family / 30 | Physical extraction or upward spare-magazine release strike | Closed empty chamber; charging-handle cycle required |

Both use the existing physical-pull interaction, preserving the chamber during
tactical 30+1 reloads. A partial pull leaves the magazine seated. A full pull
transfers its actual ammunition to the hand; returning a partially used magazine
does not refill it. Both left-side charging handles return independently of
firing, and hand mirroring preserves their actual side on the receiver.
The release control does not eject either magazine. Weapon ownership, dual-hand
arbitration, ammunition commits, independent part rendering and HUD remain shared
with existing weapons. The preceding grip-priority change remains included.

TAR's hold-open is mechanical state: its exported receiver has a handle but no
independently skinned internal bolt. The handle therefore remains forward during
follower lock. FN2000 does not acquire an automatic follower lock; its empty
source animation likewise leaves the handle forward. Its release control cannot
chamber a round after inserting a magazine into the empty closed action.

Manufacturer references used to distinguish manual controls from game animation:
[IWI Tavor SAR manual](https://iwi.us/wp-content/uploads/2024/01/IWI-Tavor-SAR-manual.pdf),
[IWI SAR/X95 control comparison](https://iwi.us/blog/experts-corner-tavor-x95-vs-tavor-sar/),
and [FN FS2000 manual, sections 4.2–4.5](https://fnamerica.wpenginepowered.com/wp-content/uploads/2016/10/OM_FS2000_0608.pdf).
These civilian-family references inform the interaction policy; they do not
identify native game models or provide any runtime pose coordinates. This
adapter covers extraction, insertion and straight charging-handle cycles; the
FS2000 manual's separate raised handle notch is not an authored gesture here.

## Asset contracts

TAR accepts `h2_viewmodel_tavor_base` and `h2_viewmodel_tavor_base_digital`, each
with 17 bones. The two models have identical bone binds and magazine, follower
and cartridge vertices. Their independent part recipes retain the actual skin's
materials. The specific `attach_h2_tavor_scope_vm` MARS optic is admitted at
`tag_tavor_scope`, alongside reviewed common optics, sensor and silencer
contracts. Competing optics are rejected. TAR's `tag_thermal` is not silently
treated as another receiver's `tag_thermal_scope` mount.

FN2000 accepts `h2_viewmodel_fn2000_base`, with 21 bones, and common attachments
whose exact roots/parent tags match. Unreviewed underbarrels or legacy receivers
cannot inherit a bare grip. Captured native instances include `fn2000`,
`tavor_reflex`, `tavor_acog`, `tavor_mars`, `tavor_digital_acog` and
`tavor_digital_eotech`, all capacity 30. Native capacity, instance, mode and full
scene checks remain mandatory before any ammunition write.

| Source | TAR-21 | FN2000 |
| --- | --- | --- |
| Idle wrist/finger pose | `h2_wpn_asl_tavor_idle`, frame 0 | `h2_wpn_asl_fn2000_idle`, frame 0 |
| Magazine grasp | `reload`, frame 43 | `reload`, frame 15 |
| Action grasp | `pullout_first`, frame 13 | `reload_empty`, frame 87 |
| Action travel | 113.74 mm | 137.24 mm |
| Magazine bone | `tag_clip` | `tag_clip` |
| Action bone | `j_reload` | `j_reload` |
| Ammunition bone | `j_bullet` | `j_bullets` |
| Permanent magazine child | `j_plate` | None |

Generated headers and source-derived test skeletons retain SHA-256 provenance.
Side/top mesh contact review gives nearest hand-to-handle distances 0.37/1.08 mm
and hand-to-magazine distances 0.30/0.34 mm for TAR/FN2000 respectively. These
are offline geometry checks, not headset ergonomic acceptance. Reviewed magazine
mouth heights are gun-local -2 cm and 0 cm respectively.

## Permanent magazine geometry

TAR's magazine surface contains 3,337 body triangles, 134 follower triangles and
569 cartridge triangles, without crossing triangles between their bone groups.
The follower must remain in an empty detached magazine and must not be classified
as ammunition. `reload_profile::magazine_body_bones` adds bounded permanent
magazine groups to every immutable subset, before optional cartridge groups.
The shared recipe retains existing round-indexed subsets and standalone cartridge
selection for other weapons. Rig admission requires permanent groups to be
direct magazine children, disjoint from ammunition/action bones; missing,
duplicate, oversized and incorrectly parented recipes fail closed.

FN2000's body and ammunition account for 3,528 and 1,628 triangles respectively.
Both weapons reuse receiver materials and the existing world-depth part renderer.

## Validation and remaining acceptance

Three suites pass: `vr-weapon-grip-tests`, `vr-physical-reload-tests`, and
`vr-rigid-part-tests` (including WARP rendering checks). The new assembly tests
cover 608 skin/optic/sensor/silencer/order/glove combinations. Shared reload
tests cover both hands, partial and full extraction, magazine return, tactical
reloads, accepted last shots, each weapon's empty policy, repeated handle pulls,
rejected native commits and interrupted manipulation. Magazine recipe tests
also verify that all previously registered profiles retain their subset ordering.
The Debug x64 client compiles successfully.

Headset testing remains: rear magazine reach and grasp, insertion angle,
left-side charging handle, TAR MARS and digital variants, TAR empty release,
FN2000 empty reload, and handover with a detached magazine. Confirm the TAR
follower remains visible in an empty magazine in the hand and on the ground.
