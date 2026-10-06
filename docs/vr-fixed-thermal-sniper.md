# Fixed thermal M82: Of Their Own Accord

Status: HMD testing confirmed the repaired scope and the anchored-plane viewing
behavior work. Headset testing accepted translation-based
aiming with either controller's squeeze button and confirmed it is sufficient
to complete mission objectives. Optional actual-reticle assistance is being
[researched separately](vr-fixed-sniper-aim-assist-research.md) as a minor enhancement.
This is the scripted
`m82_bipod_stand_thermal`, not the carried `barrett` weapon profile.

## Ownership and controls

The server observer requires the local player's mounted entity flags, the exact
turret entity and weapon definition, and that turret's local owner. It publishes
a bounded value snapshot through the existing server scheduler. Prediction must
still name the same occupied entity. Dismount, a replaced owner, checkpoint time
rollback, level shutdown and a new mount invalidate the corresponding epoch.
No mission script, native inventory, weapon definition or upstream dependency
is changed.

- Right stick forward/back supplies native `usercmd.forwardmove` for variable
  zoom at 25% input strength. Left/right does not strafe. Native zoom limits
  remain in H2.
- Left stick supplies continuous pitch/yaw at 10% of the ordinary turn speed
  (9 degrees/second at the default 90-degree/second setting), including when
  ordinary movement uses snap turning. Both axes retain their proportional
  deadzone response. The native turret still clamps the resulting angles.
- Hold either physical squeeze button to add hand-position aiming. Sideways
  and vertical motion move aim; controller rotation and forward/back motion
  have no aiming effect. Release holds the current aim, and another deliberate
  squeeze establishes a new origin without returning to an old direction.
  With both hands engaged, their valid displacements are averaged.
- HMD rotation no longer enters native aim commands or the thermal scene camera.
  The existing native scripted-camera policy retains the authored origin and
  orientation exactly, including stick/hand-position aiming and native recoil. Ordinary
  head control resumes through the existing camera handoff after dismount.
- Head rotation changes the viewer of an anchored display plane. Translation
  uses `vr_scriptedHeadScale` (the existing Scaling control), with the existing
  entry/recenter baseline and five-centimeter output bound. Neither changes the
  image camera, magnification or native firing direction.
- Either tracked Trigger fires through the native command path; right B or left Y requests
  native use/exit. Held entry/resume/recenter inputs must first return to neutral.
  Ordinary stance, locomotion, carried weapon interaction and automatic ADS do
  not consume these controls while the fixed weapon owns them.

No new polling thread, runtime input query or script VM call runs on the command
or rendering threads.

## Hand-position aiming

This is an incremental drag, not an angle-following gun or a spring-centered
velocity joystick. The command path reads already-published raw runtime grip
positions and a read-only tracking reference; it does not require a rendered
hand, a current gameplay spatial frame or a particular scope-rendering result.
Its initial head basis is held stable within the control epoch, so subsequent
head turns cannot rotate the drag axes. Each clutch captures its own origin.
The hand's reported orientation is never an aiming input.

Gain follows native `refdef.fovY` (the vertical half-FOV tangent). The default
20 cm travel corresponds to one vertical image field, even at high zoom.
Changing magnification with stationary hands cannot change aim. A 0.35 mm
spatial deadband removes small tremor without a temporal filter or trailing
motion after stopping/releasing. This deliberately avoids the optional general
hand-pose filter's lag for this precision input.

Saved advanced controls:

| Dvar | Default | Effect |
| --- | --- | --- |
| `vr_fixedSniperHandTravel` | `0.20` meters | Travel per vertical image field; larger is slower |
| `vr_fixedSniperHandDeadzone` | `0.00035` meters | Radial position-noise tolerance; zero disables it |

Entry, pause, ownership changes, recenter, stale samples, invalid FOV and tuning
changes require release/regrip. Per-hand loss of tracking or a >25 cm one-sample
tracking jump cancels only that clutch. The other valid hand can continue.
Repeated command reads of one pose never apply its movement twice. Ordinary
trigger firing and the existing stick controls can still be used while gripping.

## Scope and native thermal rendering

`native_hud_capture` reuses the existing bounded native command traversal and
GPU replay for the exact scope, thermal reticle, lens shadow and flash materials.
The shadow uses colored reverse subtraction, which cannot be flattened into a
single transparent UNORM image. Capture its positive subtractand and alpha on
a separate surface, alongside ordinary scope ink and the underlying additive
flash. The compositor follows the original order: saturating flash, clamped
`D*(1-A)-S` shadow, then the reticle and housing. Blend caches distinguish these
operations even if H2 shares their native state object. Flat scene-dependent
blur commands are excluded. Captures carry the mount epoch, reference generation,
device identity and age; both eyes use the same completed capture lease.

The fixed scope takes priority over the existing carried-optic auxiliary planner.
It requests one native narrow scene using the original vertical field of view
saved with the frontend scene publication. The horizontal field is reconstructed
for the original HUD aspect, because the native VR scene target has a different
aspect ratio. Thus native forward/back zoom changes actual scene sampling rather
than enlarging an already rendered wide image.

This dedicated scope view is presented on a plane two meters ahead of the head
at entry, subtending 60 degrees horizontally and retaining the native HUD aspect.
Its tracking-space anchor stays fixed while either the head or native aim turns.
Both eyes receive the same optical image through their individual perspective
and eye origin. Recenter/remount reanchors the plane in front of the current
head without changing native aim. Rotation, roll and scaled translation affect
only the viewer-to-plane transform. Looking partly away clips the projected
quad normally; looking behind it shows the black exterior. The GPU interpolates
texture UVs perspectively, keeping reticle and scene landmarks on the same plane.

This reuses the existing auxiliary resource, history, upload and target-routing
machinery and still adds only one native scene pass while mounted. No extra
scene rendering or readback was added for head movement. Ordinary optics retain
their existing rendering path.

Because the fixed scope covers the entire submitted world image, frontend
visibility now collects the optical scene using its native near plane and HUD
aspect, including the displaced source eye. It does not collect invisible
wide-angle static models behind the opaque scope canvas. The auxiliary clip
projection and its inverse retain that native near plane without replaying
the jitter finalizer. Native surface capacities are unchanged.

During entry, the script temporarily sets a 0.01-unit near plane. The ordinary
VR near plane now has an IPD-relative lower bound (half eye separation / 8),
about four physical millimeters for a typical headset. Both render eyes and
their culling envelope use this same bound; the old near-plane singularity
could reject the stereo publication entirely. Normal close planes remain intact.

The private scope record preserves native thermal activation (bit 0 at `+0x204`)
and sets the native full-thermal bit 1. Signature-checked native PostFX then uses
the thermal color transform, heat rendering, grain and scanlines without the
flat scope stencil or the subsequent ordinary-color redraw. It never activates
thermal on an ordinary record and never changes either world eye's record or
the natural desktop tail. No grayscale replacement shader is used.

The compositor maps that encoded scene and the native premultiplied HUD through
one canvas and converts to linear output once. The whole canvas exterior is
black. Missing, stale, mismatched or failed scope resources produce black for
that scope frame, rather than showing ordinary scene pixels through a missing
mask. Native narrative fades/titles remain later composition layers.

## Evidence and validation

The current-game read-only witness confirmed mounted flags `0x3002`, no carried
weapon, and the exact M82 definition. Loaded script 45551 enables forced thermal,
calls `useby`, sets `ui_barret`, observes normalized movement for the zoom hint,
and restores native state after `turret_deactivate`. Captured LUI commands were
op 17: scope, lens shadow and thermal reticle had flags 0; the flat blur commands
had flags 64. Native narrow-view tangents and PostFX route `(5,5,4,0)` were captured
from the same process. These CPU witnesses establish the source chain, not final
headset pixels.

Offline coverage:

- Controller tests: exact admission, all stick signs, simultaneous zoom/aim,
  neutral rearming, missing/invalid axes, tracking/focus loss, recenter and remount.
- Actual head-pose bridge: head movement leaves mounted command angles, scene
  origin and scene orientation unchanged; native aim changes still appear;
  ordinary head control resumes after exit.
- WARP composition: source pixels, original opaque scope ink, black exterior,
  invalid canvas rejection; auxiliary tests preserve ordinary records and reset
  history across a thermal mode change.
- Anchored plane and WARP: finite stereo disparity, authored aspect, rotated
  perspective, texture landmarks staying on the plane, scaled/capped leaning,
  partial clipping, looking away, recenter and remount.
- Existing controller, stereo probe, optic render and spatial panel regressions.

Diagnostics: `vr_fixedSniper_status` reports ownership/input requests;
`vr_fixedSniper_renderStatus` reports planned scopes, composed eyes and missing
pairs. Existing `vr_hud_capture_status` identifies unsupported native blend/depth
state. The render-status command also saves `minidumps/overlord-fixed-sniper.txt`.
Counters are requests/compositions, not confirmation of hits or heat pixels.
The input-status command also saves `minidumps/overlord-fixed-sniper-input.txt`,
including active hand mask, applied displacement count and current hand tuning.

The failed build's live counters showed zero planned auxiliary views, zero
composed scope eyes and repeated unsupported blends. A bounded D3D state read
identified the lens shadow's exact `SRC_ALPHA / INV_SRC_ALPHA / REV_SUBTRACT`
state. Thus the third scene pass had not yet run and was not the source of the
reported static-model warnings.

One saved stall dump verified a native static-model worker waiting on the console
mutex, while the console input thread held it inside synchronous terminal output;
main and renderer threads waited for native jobs. Console messages now enter a
separate bounded output queue. The output owner performs terminal I/O without
holding the producer queue lock, repeated messages coalesce, and overflow is
counted and reported. Shutdown cancels blocked terminal I/O before joining its
owners. A blocked terminal therefore cannot block a renderer warning producer.

The repaired thermal scope, anchored viewing plane and hand translation have
user-confirmed operation, including completing mission targets with hand input.
Regression coverage includes both clutches,
handoff, averaging, zoom-invariant response, no stationary/rotational drift,
release/regrip, tracking discontinuities and a native-owned camera with tracking
coordinates still available. Detailed performance, other devices and long
sessions have not been separately accepted.
