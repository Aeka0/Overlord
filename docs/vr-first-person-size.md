# First-person size

`vr_firstPersonScale` controls the size of independent first-person hands and
personal weapons, including stored native weapon skeletons and their detachable
reload/equipment parts. The launcher exposes it under Calibration as Hands and
weapon size, from 20% to 100%. The default is the original size, 100%.

The parameter is a direct multiplier: `0.8` means 80%, `0.2` means 20%. It is
independent of `vr_worldScale`. Native world actors, fixed weapons, vehicle mounts
and world-anchored mission equipment retain their world dimensions.

Size is selected at the first admitted hand rig and retained for the process.
Restart the game after changing it. Status reads do not select or freeze it.
`vr_hands_status` includes `model_scale` and the mesh packet/cache statistics;
the normal `vr_diagnose` package includes that evidence.

## Shared geometry

`spatial_math::anchor` carries an optional uniform scale, defaulting to one.
Composition and inversion include that scale. Authored weapon/part positions
remain in their original model coordinates; an inverse weapon transform maps
physical tracking back into those coordinates for contact and stroke tests.
Grip, magazine mouth, bolt, shell and shield geometry use the same transform as
their presentation. Native firing uses the committed, resized muzzle bone.

Tracked wrist and shoulder positions retain their physical coordinates. Arm IK
retains anatomical reach. A newly evaluated pose sizes finger/segment geometry
around its own joints and weapon geometry around the receiver, once before the
ordinary solvers. Finger rest poses and authored equip offsets use the selected
size. Sleeve deformation pivots retain their axial placement along the physical
limb while their radial offsets resize. A rejected solve restores its uncommitted
source pose.

Native dual-quaternion skinning cannot represent uniform scaling by changing a
bone quaternion alone. The existing skin packet boundary therefore substitutes
immutable copies of packed vertices and matching inverse bind translations.
Rigid packets receive the inverse bind before their native matrix builder and
the vertex surface after the existing visibility pass; skinned packets receive
both before CPU/GPU job publication. Normals, UVs, skin weights,
material identity and source assets are retained. Triangle-mask variants share
their resized vertex buffers. Caches retire only after the native asset drain.

An object/matrix/epoch witness selects the size for each native skin job.
Foreign and later uncommitted poses retain native size. The default 100% path
does not create replacement buffers. Packed raw/structured vertex views retain
their original byte offsets. Subdivision/blend-shape surface layouts are not
admitted by this adapter; rejection is visible in the mesh statistics.

Continuously posed private equipment uses the generic skeletal pose observer to
publish its committed size. Rigid detachable parts retain scale in their native
placements, local-to-world contacts, rail departure and free-flight pivots.

## Validation and acceptance

Focused tests cover 20%, 80% and 100% wrist/grip/muzzle relationships, unchanged
arm reach, transactional rollback, magazine/well coincidence, scaled contact
boxes and shield traces. WARP readback covers raw and structured subviews,
immutable source assets, vertex-buffer sharing and object epoch isolation.
Launcher tests cover percentage conversion and independent saved world size.

These are engineering checks. Headset acceptance still needs empty/held hands,
two-hand aiming, both physical hands, detached magazines, action manipulation,
optics, independent weapons and mission transitions at the selected sizes.
The setting provides a way to compare player preferences; it does not establish
the cause of earlier large-arm reports or prove device-specific behavior.
