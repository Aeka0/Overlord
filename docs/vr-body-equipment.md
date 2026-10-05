# Body-mounted equipment estimation

The chest, waist weapon holsters and back holster share an estimated body frame.
The camera and controller transforms remain actual tracking transforms. Arm IK
and waist ammunition supply retain their existing rules; this estimate controls
equipment storage, including knife pickup and holster hit volumes.

`body_pose.hpp` owns a bounded, allocation-free estimator. The head-pose bridge
updates it once per distinct tracking timestamp and publishes the result in the
same spatial frame as the center-eye camera. Consumers do not run independent
filters or infer a body again from a later head sample.

## Initial policy

- Preserve an equipment anchor at the neutral head-equivalent height. Existing
  chest/waist/back vertical offsets remain meaningful; no floor-height or player
  height assumption is added.
- Compensate the motion of an eye point 8 cm ahead of a neck pivot. The initial
  eye offset is the reference, so enabling/recentering does not introduce an
  extra backward shift in addition to the explicitly requested chest adjustment.
- Leave 7 cm of horizontal head/neck translation tolerance. Beyond that, follow
  with a 120 ms time constant and at most 15 cm of horizontal separation. Small
  leaning does not translate all slots; room-scale walking eventually follows.
- Follow physical crouch height with an 80 ms time constant. Equipment remains
  upright and does not inherit head pitch or roll.
- Allow 35 degrees of head yaw relative to the estimated body. Beyond it, follow
  the excess with a 180 ms time constant and a maximum 180 degrees/second.
  Duplicate eye/camera evaluations cannot advance this filter.
- Filter only recenter-relative tracking movement. Native locomotion, teleport,
  snap turn and native crouch move the world base immediately; they are not
  delayed by the body estimator.
- Recenter/reference changes, tracking gaps over 150 ms, time reversal, large
  tracking jumps over 75 cm, scene/view reset and invalid data reset history.

This cannot distinguish a sustained head turn from a torso turn, or infer actual
hip position from only head/controller tracking. A large head turn can therefore
gradually turn the estimated body, and a large lean can move its position. The
current implementation intentionally does not infer torso yaw from hands, since
aiming, crossing arms and manipulating equipment can give misleading directions.
These are first-pass headset tuning values, not full-body tracking.

## Consumers and coherence

`body_slots_frame` selects the shared estimate, with an explicit head-frame
fallback for callers without an estimate. `locate_chest` and `locate_holsters`
apply the same policy to rendering and interaction. Holstered weapon rotation
uses the estimated body axes as well as its estimated slot center.

The hand attachment record retains this estimate in the exact solved object's
model-origin space. Chest model preparation rebases that same record into the
current native placement origin. It must not rebuild chest placement from the
record's raw head pose, which would make the rendered model disagree with the
knife's estimated pickup location. `vr_hands_status` reports the estimated
equipment-body position and forward vector alongside the existing head data.

## Shadow casting

Holstered weapons and player equipment use `scene_models::no_cast_shadow`
(`0x40`) on each native scene submission. This covers chest knives/grenades,
abdominal equipment, the UAV/AGM notebook, mission detonators, waist pickaxes and
signal flares. Equipment keeps this policy while held, so drawing or returning
an item cannot introduce a detached shadow. World depth, received lighting,
materials and mechanical/bone poses remain native. Dropped weapons, thrown
grenades, loose debris and placed equipment retain their existing scene rules.

The H2 native shadow-view initializer at `0x140727CB2` uses the rejection mask
`0x01000040`. DObj and rigid-model visibility at `0x140723475` / `0x1407237AC`
and shadow admission at `0x140785232` / `0x140785290` consume the same mask.
Bit 6 supplies casting suppression independently of the first-person bit 24.
The notebook combines it with its required skeletal-path flag `0x2000`;
generic skeletal submissions otherwise retain their caller-selected flags.

## Validation

Pure regressions cover small sustained glances, neck-pivot movement, short leans,
larger room-scale translation, physical crouching, rate-limited large turns,
yaw wrapping, duplicate camera queries, world-base movement/turning, recenter,
tracking gaps/jumps and invalid poses. Layout tests verify that the central chest
anchor is 4.5 cm forward and 35 cm down while both waist centers use the same estimated body
position, and that the visible waist center remains inside its interaction volume.

Headset acceptance is still required: turn the head without turning the torso,
look down, lean, walk/turn, crouch and recenter. Inspect chest distance and both
waist slots, then draw/return the knife and stow/draw a weapon at the visible
slots. A side glance should not orbit the waist weapons around the head.
