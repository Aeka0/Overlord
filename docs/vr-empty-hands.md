# Native empty hands: headset test candidate

Empty carry now has its own first-person hand presentation. Stowing or dropping
the final selected gun can leave the native weapon selection at `0` while both
arms follow the controllers. The renderer reuses the current character's native
hand XModel, materials, sleeves and gloves. It does not create a weapon instance.

## Asset and pose policy

The native character selector chooses the hand model independently of the gun.
The captured scene uses `viewhands_us_army`, with 68 bones including both arms
and fingers; that asset name and bone indices are not fixed in the implementation.
Semantic shoulder, elbow, wrist, view and weapon-reference tags define the rig.

The inspected ending knives, bayonet and default weapon contain weapon geometry
only and depend on this separate character hand model. They are not substitutes
for an empty-hand asset. No exported assets or weapon animation trees are shipped
for empty hands.

A native DObj without an animation tree produces collapsed joint translations.
The empty-hand path therefore uses the model's complete bind pose as its source,
rebases it at the native view anchor, and runs the shared transactional arm IK.
Each wrist has an anatomical orientation from the current skin. Finger transforms
retain that model's relaxed bind shape; dynamic grip-button finger curls are not
part of this change. Firearm attachment and muzzle rules remain in the held path.

## Native lifecycle

`empty_hands_native.cpp` submits a dedicated hands-only DObj after the native
first-person weapon pass, only when both native selection and carry ownership
are empty. It uses the existing controller/spatial snapshots and requires fresh,
focused tracking for both hands. Native death, spectator and cinematic suppression
also apply. A pose is submitted only after the IK hook accepts the complete native
skeleton epoch; failed tracking never displays a collapsed or partially solved rig.

The adapter uses native DObj construction, frame skeleton allocation, locking,
viewmodel submission and destruction. It owns object storage, borrows the current
character model, and allocates no client weapon handle or inventory slot. The
steady-state object is reused. Model changes drain pending rendering before
rebuilding; a pre-unload observer releases references at the engine's asset barrier.
Resource generations prevent native epoch-cache reuse across object lifetimes.

Drawing a gun suppresses this standalone object and resumes the existing held
viewmodel. A free support hand alongside a selected gun continues through the
existing held rig. Independent two-gun firing and simultaneous weapon animation
remain separate work.

Free wrists now use the same current-model neutral basis in held and standalone
rigs. A shield or other weapon in the opposite hand cannot supply their wrist
rotation; individual hand input still controls finger gestures. The common
equipment/empty-hand boundary respects held-weapon, knife, reload-part and world
interaction pose ownership before applying this neutral wrist orientation.

## Verification

The skinned visibility hook is required in every build. `prepare_hands`
prepares both arm subsets through `prepare_skinned_part`; without the hook,
that call fails, `prepared_hands` stays empty and independent held objects
cannot become active. The old Debug-only install gate made optimized clients
fall back to native first-person rendering, including foreground depth and
native equip/ADS presentation. This gate has been removed. Independent hands,
held guns and separate magazines keep the same scene-depth behavior in Debug
and optimized builds; build mode is not a feature switch.

The Debug x64 client builds. Hand-rig and hand-pose tests pass, including a
hands-only semantic fixture, independent wrist positions/orientations, retained
finger shape, untouched weapon-reference tags, rejected hidden receivers and
transactional rejection of invalid targets. Weapon-grip, physical-reload and
cylinder regression suites also pass.

A bounded live probe created, calculated and destroyed a native hands-only DObj
on the first-person frontend boundary. A second probe seeded native bind matrices
and submitted it for four frames: four native skin calls each returned four
surfaces, and weapon selection remained `0`. These probes establish the native
resource/render path; they do not replace headset acceptance of the compiled
controller-driven feature.

Headset checks:

1. Stow and drop the final gun. Both arms should remain visible, with no automatic
   equip, knife, muzzle flash or invisible weapon attack.
2. Move and rotate each empty hand, cross hands, turn and recenter. Check wrist
   orientation, arm reach, finger shape and both eyes.
3. Draw into either hand, stow again and pick up a dropped weapon. Check for
   duplicate arms and switching flashes.
4. Load a checkpoint while empty, then change level or character. Check that the
   correct sleeves/gloves return without stale assets.
5. Pause and lose/recover controller tracking. Check that the arms resume cleanly
   and that recovery causes no drop or pickup gesture.

`vr_independentHands` controls both held and empty-hand IK. `vr_hands_status`
writes `minidumps/h2-mod-vr-hands-latest.txt`; its `empty_hands` line records hook
installation, object builds/releases, accepted submissions and rejection state.
