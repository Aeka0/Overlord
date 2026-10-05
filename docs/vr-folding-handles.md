# Folding charging-handle ends and acquisition fixes

This guide covers charging-handle acquisition for AK, SCAR, and ACR, plus folding
ends for F2000 (`fn2000` in code) and PP2000.

## Acquisition boundaries

- AK: Reuse `part_capture_halfspace`. The raw wrist must be outside the right receiver wall, at a gun-local Y no greater than the charging-handle capture box's right boundary. Apply this physical boundary to both hands without mirroring it. The 60° contact aid must not cross it and steal magazine interactions.
- SCAR: Add pose-level palm semantics. Determine facing from the calibrated physical controller palm relative to gun up. Only an unambiguously upward palm selects the upward grasp; a downward or nearly vertical palm uses the native downward grasp. Decide before the animated wrist offset. Keep the selected pose latched while held instead of selecting again when the wrist turns.
- ACR: The left palm-up pose retains the normal index/middle-finger charging grip; palm-down uses a separate pinky hook. Only prevent palm-down from incorrectly selecting the index/middle-finger pose. The right hand retains both existing poses, selected by wrist facing.

## Folding structure

| Weapon | Folding node | Deployment | Evidence |
| --- | --- | --- | --- |
| Vector | Existing `j_reload` root | 90° to the left | Keep the existing implementation |
| F2000 | Separate end mesh within `j_reload` | 90° to the left; either hand operates the same left-side end | Source animation only translates; folded pose authored from the visible hinge |
| PP2000 | `j_reload_end`, child of `j_reload` | Leftward for the left hand, rightward for the right hand, about 99° | End deployment in native first_time_pullout frame 15 |

Choose and latch the PP2000 deployment side from the physical hand on acquisition. After release, retract it through the existing 75 ms visual return. A different hand, instance, tracking reference, or holding-hand identity cannot inherit the previous grasp's deployment side. Visual return does not delay mechanical lockup, ejection, or feeding.

The PP2000 long rod retains its original linear travel and native firing reciprocation; only its end child bone's rotation is overridden. Bone admission checks that the end is a direct child of the long rod and verifies its rest transform. A missing or incorrect hierarchy must not fall back to rotating the entire guide rod.

The F2000 end shares a bone with the long guide rod, so Vector's root rotation cannot be copied. The end is a separate 1,126-triangle component; the guide rod, connector, and pivot pin remain in the original linear motion. Vertex and triangle counts for three face ranges across nine material surfaces are stored in the weapon's `folding_handle.hpp`, together with the source hash and geometric bounds.

## Runtime asset boundary

Reuse `scene_models::rigid_part` and add face-range slicing across material surfaces while retaining the existing single-surface interface. The main asset thread creates immutable index slices that reference the original material and vertex buffers. It does not modify the source XModel, add a source bone, or write exported files.

Publish the F2000 slices only after validating model identity, bones, surface counts, face ranges, and end bounds. Hide the original complete charging-handle group only after both guide rod and end have been prepared and their native asset identities registered. Admission to the physical profile also requires this display contract. If preparation fails, retain the original model and report the reason under `partition_asset(fn2000)` in `vr_reload_interaction_status`. The shared `native_partition_assets` provider also prepares the [bolt surface partitions](vr-bolt-presentation.md); the folding pivot and motion remain F2000-specific.

Both rigid parts use the same model object, matrix cache, bone generation, and current native scene origin as the gun and hand. Resolve their poses at the rigid-part packing boundary so stereo eyes and multithreaded rendering do not follow different frames. Keep descriptors until native asset unload/render drain; do not allocate per frame or replace source geometry at runtime.

## Validation scope

Regressions cover the AK right-side boundary and both hand contacts; SCAR selection before animated wrist offsets and pose retention; the disallowed ACR left-hand selection; PP2000 deployment/retraction on both sides, identity changes, and child binding; and F2000 pivots and cross-material slice bounds. Retain Vector, hand-interaction, and reload regressions.

Offline previews check separation of the F2000 guide rod and end and both PP2000
deployment poses. F2000's nearest index-tip contact is about 0.47 mm; PP2000's
reviewed contacts are 0.69–1.43 mm. A single closest vertex does not establish
a correct grip, and these measurements do not replace headset acceptance.

## Additional acquisition acceptance

The F2000 charging-handle capture radius is 14 cm. Its bounds extend 1 cm
outward (gun-local +Y) and downward (−Z). The folding end, hinge, and left/right
grasp poses use the existing authored transforms. Candidate arbitration and
actual acquisition share the same bounds.

## F2000 grasp feedback

The subsequent grasp correction adds palm-down and palm-up styles for each hand.
The native left index/middle fit moves inward onto the deployed end; the added
left palm-up fit uses the shared pinky hook. Both right fits reuse the accepted
left-side-handle index/pinky family, keeping the wrist behind the handle and
outside the receiver instead of turning it toward the muzzle.

The ACR left-hand palm-up profile uses the index/middle-finger wrist orientation
with hooks aligned to the charging handle. Translate the wrist only, and derive
the middle finger's bending plane from anatomical bind coordinates. Retain the
independent right-hand fits and left pinky pose. Apply the palm threshold only
during left-hand acquisition; do not reselect while held. All four skins share
the same facing rule for presented and raw controller poses.
