# Support-grip visibility audit

. Expanded from the observed AK/FAL/ACR inserted-magazine blink to all
92 registered assemblies and 56 distinct detachable-reload definitions. Counts
come from the compiled registry, including skins and support/underbarrel variants.
This is code and deterministic regression coverage, not headset acceptance of
every weapon.

## Findings by rendering path

| Rendering path | Registered scope | Finding |
| --- | --- | --- |
| Counted inserted magazines | M9; AK base/arctic/digital; M93R; TMP; ACR base/black/digital; MP5K base/arctic; AUG arctic; FAL; PP2000; L86: 15 receiver definitions | All share the initial erroneous reload-view support comparison. The first fix already covers the complete set. |
| Independent action partitions | MP5K base/arctic; UMP base/arctic/digital; FAL; M16; F2000: 8 definitions | Their rigid bolt/handle pieces share reload attachment placement and the common native skin-record lookup. They need the same receiver ownership policy. |
| Independent live chamber rounds | M200 base/desert: 2 definitions | Same common attachment and skin-record path. M14/M21 chamber rounds instead remain on native bones. |
| Launchers | RPG, AT4, Stinger | RPG replaces the loaded rocket with an independent model and uses the shared skin-record lookup. Its own presenter checks the rear lease, not auxiliary support. AT4/Stinger keep their disposable native model path. |
| Revolver and break actions | .44 Magnum; Ranger; M79 | Independent held loader/shell models use the shared skin-record lookup. Their provider checks do not repeat the original support-equality error; loaded cylinder/barrel geometry stays on native bones. |
| Tube/fixed-drum shotguns | M1014, SPAS-12, W1200, Model 1887, Striker, including registered variants | Held/loaded shell, pump, lever and drum geometry are posed in the native skeleton. No equivalent hide-original/omit-independent-replacement sequence was found in these presenters. |
| Underbarrels | M203, GP-25, underbarrel shotgun | Tube/action and held projectile geometry are posed on native bones. No equivalent support-dependent replacement omission was found. |
| Held whole-weapon fallback | Generic carry fallback when independent native weapon objects are unavailable | A late support-only revision could reject a correctly matched queued whole-gun pose. This check is corrected too. Holstered/world-drop rendering retains its own lifetime rules. |

Unlisted native magazine bodies, exposed belts, revolver contents and ordinary
gun parts do not gain the counted-magazine replacement path just because their
weapon is registered. Missing fill variants still retain their native geometry.

## Additional scheduling window

The initial fix addressed a newly skinned carry owner compared with the previous
mechanical view. The wider audit found an earlier window in the shared render
cache: scene registration samples carry ownership, and native skinning can occur
after support is acquired or released. Exact native object/buffer/epoch data can
therefore be valid while the auxiliary support field and general grip revision
differ. The previous full-owner comparison denied that record before any
weapon-specific attachment presenter could use it.

The new regression fails under the previous cache in all eight combinations of
controlling hand, acquire/release and skin-before/state-before ordering. It checks
both early rigid-part preparation and later scene publication. Corrected records
keep the owner and pose actually consumed by native skinning; they are never
rewritten to the registration owner's state.

`weapon_render_owner.hpp` centralizes this render-only comparison. A fixed rear
carrier allows auxiliary support changes. Weapon instance, rear lease, selected
control attachment and anatomical controlling side must still agree. A weapon
carried only by support keeps strict support identity and revision checks. The
helper is shared by reload attachments, native pose binding and the whole-weapon
fallback. Detached magazine ownership/grasp/payload checks remain separate.

Firing and interaction authorization continue using their existing complete
ownership checks. Tests explicitly reject firing from a pose whose owner differs,
even when that pose can correctly render its already-consumed native geometry.
Record/camera/eye-pair identity, exact bones, freshness, asset identity, and
conflicting-epoch rejection remain in force.

## Validation

The audit covers actual support acquisition/release, both state-arrival orders,
the complete counted-magazine set, every authored rigid action partition and both
M200 chamber variants. Negative cases cover handover, foreign/recycled instances,
changed control contact, stowing, sole-support regrasp, detached grasp changes,
ammo subset changes, tracking reference resets, stale poses and native record
conflicts. Existing spatial-panel, weapon-grip, physical-reload, cylinder and
underbarrel suites validate the shared and provider paths.