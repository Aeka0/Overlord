# Head-mounted nightvision interaction

An empty hand can grab the stowed goggles in front of and above the headset,
using either Trigger or Grip. Hold the same button and drag down to eye level
to turn them on. While nightvision is on, grab in front of the eyes and drag up
to stow them. Either hand works; only one hand can own the goggles at a time.

The initial grasp must be a fresh press inside the corresponding volume. The
stowed volume covers 10 cm behind to 35 cm forward of the center eye, 28 cm to
either side, and 4–30 cm above it. The worn volume is 0–35 cm forward, 28 cm to
either side, and 12 cm below to 10 cm above the eye. Travel must reach 12 cm and
the destination height. These are headset-relative metres, including head
pitch/roll and the active world scale. Sampling uses the raw physical controller
grip transformed through the shared tracking frame; cosmetic wrist offsets,
glove alignment and weapon grip calibration cannot shift the equipment gesture.

The confirmed  Gulag capture contained 58 unoccupied, unsuspended
press edges. Every cosmetic wrist missed the old height gate, while physical
grips reached the forehead/top of the head. No gesture completed and no native
action-slot request was issued in that window. The geometry correction covers
those contacts. HMD testing confirmed physical switching works;
further presentation changes are tracked separately below.

Releasing the initiating button, leaving the travel envelope, tracking loss,
recenter, pause/suspension, an external nightvision change or a two-second
timeout cancels the grasp. A completed gesture cannot toggle again until a new
press. Sudden tracking displacement and large head rotations cancel it too.
The common hand arbiter excludes weapons, support grips, magazines, knives,
grenades and world interactions. The grasp retains normal tracked empty-hand
pinch/fist presentation and suppresses melee for its duration.

## Native boundary

Goggles are available only when the current player has exactly one native
nightvision action slot and the existing gameplay/weapon permission boundary
allows interaction. The captured `setactionslot` nightvision branch stores
type 3 at playerState+0x1fa0; paired native force-on/off methods establish the
active bit 0x40 at playerState+0x3c0. Byte signatures are verified at startup.

Completion requests the original `+actionslot N` / `-actionslot N` whitelist
bindings through `CL_ExecuteKey` on the main pipeline. These names are not
registered console commands. The dispatcher owns both command notification and
the slot down/up handlers; the native nightvision branch supplies its own input
bit. The shared `native_action_slots` bridge is also used by existing special
equipment. It validates the native binding cases and paired handler calls, then
rechecks slot type, player/timeline, pause,
input capture, request age and gesture authorization. It does not force a
shader, edit ammunition, replace scripts or call the force-on/off methods.
Sounds, effects and `night_vision_on/off` notifications remain native.

The second confirmed  capture showed 16 completed gestures with no
native nightvision change. All 263 registered commands were inspected and no
`+actionslot`/`-actionslot` command existed. The live binding cases instead called
the original slot handlers at `0x1403B3A70` / `0x1403B3C10`. Replacing the console
command path fixes this separate dispatch defect; do not compensate by widening
the gesture again or force-writing the goggles state.

The queued operation accepts its own previously published gesture lease if the
main pipeline runs before the simulation publishes completion. Another held
object, a changed reference, changed native state or a suspended gesture cancels
the request. No extra per-frame polling loop or render-thread gameplay writes
are introduced.

## Physical transition timing and weapon lasers

HMD testing confirmed that a held weapon restores native transition/audio and
explicitly requires the same feedback with empty hands. The capture showed M4
wear/remove states 33/34 counting down from 1500 ms, while native weapon `none`
has zero timers and no NVG animation slots. These weapon fields therefore cannot
own a physical head-equipment transition.

One device clock now starts only after the original `PM_Weapon_NightVision`
accepts a real toggle. It uses native `nightVisionFadeInOutTime` and
`nightVisionPowerOnTime`, with no weapon/model/hand identity. Power-on immediately
fades to black, switches the native vision predicate at the opaque boundary,
then performs the native-duration power-on fade. Power-down immediately enters
the native black-to-world fade. The original scene shader/effects consume the
native visibility and vision entry points; no replacement image or dummy gun is
used. Newly entered native weapon timers are capped to that same compact device
duration, on both simulation and prediction, instead of waiting for arm motion.
Stow/draw during the transition does not replace its clock.

The original `item_nightvision_on/off` aliases use their original native player
sound function. Their cue waits until the native visibility consumer has sampled
full black. Black remains through main-thread sound dispatch, and the fade back
starts at that same game-clock instant. A frame stepping over the mathematical
boundary still gets a black frame; polling the clock alone cannot release the
sound. A missing acknowledgement releases black after at most 250 ms of game
time. The original weapon event cannot play an early or duplicate electrical cue.

Equipment motion has a separate serial claim and plays at transition start.
The confirmed Gulag asset snapshot resolves local native sound slots 66/68 to
`nightvision_wear_plr_default` and `nightvision_remove_plr_default` on both M4
modes and `defaultweapon`; `none` has null slots. The main-thread fallback resolves
these shared loaded aliases directly, without selecting a weapon or retaining
asset pointers. It shares its claim with the original local entity event, so a
held weapon does not add a second copy. NPC events remain native. Replayed
predictions and superseded requests cannot play again. Command/timeline identity
fences reset/reload, while a temporary prediction rewind cannot cancel the final
frame's feedback. Native rejection starts no presentation.

`vr_nightvision_status` includes the full presentation contract, device transition
count, electrical sound count and equipment foley count. The hook fast-paths input without a fresh NVG press;
there is no background scan, and its only asynchronous work is the bounded sound
request on the existing main scheduler. Invalid duration data leaves native behavior.

Native local weapon lasers now share the Exodus scene/depth renderer. The
receiver's unique `tag_laser` is carried with the solved skeleton, verified
before and after skinning, and reconstructed using that stereo scene's model
placement. An explicit native `tag_flash` uses the muzzle instead. Native
activation, range and ordinary weapon beam RGB are retained; the Exodus
designator retains its existing red appearance. Missing/changed emitter poses
do not fall back to a head-anchored beam. NPC, flat and native script-owned
rendering remain outside this physical-weapon route.

## Verification

`vr-nightvision-tests` covers bilateral Trigger/Grip use, directional travel,
single completion, invalid/stale input, external toggles, native slot admission,
head-relative scale/roll, hand exclusivity and main/simulation publication order.
It is registered in the existing CI build. Device-clock tests cover empty-hand
startup without weapon metadata, native fade phases, rendered-black/audio
handshake, frame skips, bounded acknowledgement failure, separate foley claims,
prediction replay/rewind, reversal, reset and malformed durations. Hand-rig and
spatial-render tests cover emitter/socket identity,
skin lifetime, origin offsets, native beam color and reverse-Z occlusion.
HMD testing confirmed weapon-independent switching/transition feedback in game.
The revised blackout/audio synchronization, empty-hand equipment foley and IR
beam placement still require headset acceptance.
