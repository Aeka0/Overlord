# Hand alignment

Hand placement uses one explicit grip-local wrist point. Position calibration
does not change controller aim calibration, native empty-hand orientation,
authored fingers, or the wrist-to-weapon transform. OpenXR grip and aim retain
their distinct roles; the input filter applies the same rigid correction to
both. Gameplay consumers use the published frame rather than querying the SDK.

## Coordinate contract

`hands/position_offset.hpp` constructs the control target as:

```
wrist_position = grip_position - scene_view_offset
               + grip_rotation * wrist_offset_in_native_units
control_rotation = aim_rotation
```

The inward/back/up settings describe a point in the grip frame. The inward
sign mirrors between the physical hands. Conversion uses the current world
scale; head looking and aim-angle adjustment cannot translate this point.
The numeric defaults and presets are starting values, not measured anatomy.

For an empty hand, presentation applies the admitted native model's existing
neutral wrist basis to the control rotation. For an occupied hand, the existing
weapon pose supplies the authored wrist-to-weapon relationship. The arm solver
then targets that wrist through the existing native presentation path. Position
calibration must not replace either native orientation basis.

## Source references

[REFramework's OpenXR input](https://github.com/praydog/REFramework/blob/d1461375aee4ec3f313170f8eaad12064eb542d9/src/mods/vr/runtimes/OpenXR.hpp#L214)
binds its controller pose to grip. Its
[RE7/RE8 script](https://github.com/praydog/REFramework/blob/d1461375aee4ec3f313170f8eaad12064eb542d9/scripts/re8_vr.lua#L20)
provides explicit position and rotation offsets. Its
[hand IK path](https://github.com/praydog/REFramework/blob/d1461375aee4ec3f313170f8eaad12064eb542d9/src/mods/vr/games/RE8VR.cpp#L304)
rotates the position offset with the base controller rotation, applies rotation
compensation separately, and retains the original animation's relative hand
poses when holding a weapon with both hands. These are useful transform and
ownership references; their RE-specific numbers and bone axes are not H2
calibration values.

[HIGGS](https://github.com/adamhynek/higgs/blob/93bf67b1bc4c4a11a20ccaef0d5012781d0d7eee/src/hand.cpp#L4429)
retains transforms between native hand and weapon nodes. It starts from
SkyrimVR's existing VR hand presentation, so it does not establish H2's physical
controller-to-wrist offset.

[QuakeVR](https://github.com/vittorioromeo/quakevr/blob/920fc87fdcd5f59992ba9ed0b91ce8f95c10a961/Quake/view.cpp#L1436)
uses explicit weapon hand-anchor vertices and offsets for its authored hand
visuals. These anchors cannot be inferred from an unrelated model's bone names.

## Rejected palm trial

The removed model-palm trial treated the midpoint of the wrist and proximal
knuckles as an OpenXR palm surface, and inferred axes from the thumb and
knuckle plane. Neither assumption was an admitted H2 asset contract. It also
replaced the native empty-hand orientation basis. Headset feedback rejected
both the position and angle results.

The associated SDK palm bindings, model publication, trial controls and tests
have been removed. A saved `vr_modelHandAlignment` value cannot select that path
in the repaired client. Controller-derived SDK joints remain simulated hand
references, not independent measurements of the user's wrist.

## Acceptance

Engineering checks validate the grip-local position calculation, current world
scale and scene rebase, separate aim orientation, and native skeletal/weapon
relationships. They do not establish that a numeric wrist offset matches the
user's anatomy. A test that constructs a palm estimate and verifies that same
estimate is aligned establishes only mathematical consistency.

An alignment repair requires headset acceptance against the previous baseline:

- Previously accepted empty-hand and weapon angles must remain correct.
- Stationary placement and placement while rotating the wrist must improve
  without worsening the rotation centre.
- Left/right hands, empty/held hands, native weapon grips and the planar HUD
  pointer must retain their accepted behavior.

Build and deployment success are engineering results. Without an independent
physical reference or accepted headset observation, do not report automatic
anatomical calibration or declare the remaining position offset fixed.

## Menu-ray reference and wrist pivot

The planar OpenXR menu intersects the complete `runtime_aim` ray: its SDK
position and minus-Z direction. Agreement with its direction alone establishes
parallel lines, not coincident lines. A weapon comparison must use the same
input sequence, tracking reference, camera sample and committed skeletal pose.
Record both the angular difference and the perpendicular muzzle-to-ray offset.

For a model rotation `R`, native wrist point `w`, native muzzle point `m`, and
reference ray `o + t*d`, the wrist positions that place the muzzle on that ray
satisfy `wrist(t) = o + t*d + R*(w-m)`. The ray leaves longitudinal placement
undetermined. Forcing coincidence can also move the wrist differently for each
weapon, so this equation is a diagnostic constraint, not permission to replace
the anatomical wrist anchor or the authored hand-to-weapon relation.

The internal control pivot and rendered wrist joint must coincide. Separately,
the controller-to-wrist mapping must place that joint at the physical rotation
centre. Agreement between two consumers of the same guessed point proves only
the first condition. The shared input stabilization origin is another part of
this chain that must be distinguished from the raw physical tracking reference.

Nominal firing geometry comes from the solved muzzle. Actual shots additionally
pass through native spread, optional aim assistance and other existing firing
rules. A no-firing pose capture cannot establish final ballistic acceptance.
Keep a geometry repair in the model/hand transform chain; independently bending
bullets toward a menu pointer would conceal a remaining visual mismatch.
