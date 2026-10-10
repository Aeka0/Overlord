# Independent Javelin display
The physical CLU eyepiece experiment was rejected in headset testing: operation
was difficult and the lock query did not coincide with its displayed center.
That renderer, aperture geometry and eye-box restriction have been removed.

## Controls and ownership

With the Javelin in the right control hand, bring the actual CLU rear aperture
within 14 cm of the head-center/eye region while looking through its rear side.
Moving beyond 20 cm exits; the separate thresholds prevent boundary chatter.
Correct orientation alone never enters. No ADS assistance translates or rotates
the gun. Button entry/exit is temporarily commented out, with its experiment
retained for later comparison. A trigger held on entry must release before firing.
Native lock, ammunition insertion and cooldown remain authoritative.
A left-hand-only carry cannot operate the display.

AT4, Stinger and Javelin now use fixed roles throughout physical inventory:
right hand controls, left hand supports. A left pickup or holster draw starts
support-only. Releasing the right hand never promotes the left into a firing
hand. The right hand can subsequently acquire the real control grip. RPG retains
equal left/right control ownership.

`vr_javelinDisplay 0` disables the independent display. This replaces the old
experimental `vr_javelinScreen` setting; its saved value no longer selects an
eyepiece path. Existing grip and native reload remain separate. The speculative
reserve refill has been removed; native stock and mission grants own ammunition.

## One aiming camera

After the normal head-pose bridge has published the tracking/hand frame, the
camera owner may select a snapshot of the held Javelin's muzzle origin and axes.
Only the native scene/HUD camera changes. HMD tracking and hand-to-world space
keep their original reference, so turning the head is a viewing action rather
than a second aim source. Native world-target HUD projection consequently sees
the same camera basis used to collect and render the scene.

The launcher target-rectangle adapter reads that selected snapshot while the
display owns aim. It does not independently resample a later hand pose. Native
target eligibility, lock progress, sounds, firing and reload timing remain in H2.

The full-ADS query in the native CLU gate (ScriptFile 44133, function BCBD) now
uses VR near-eye intent once the native clip is loaded and the weapon is ready
with no remaining weapon timer. The native `playerads` method still executes;
only that validated call's temporary float result is replaced. This removes the
extra ADS-blend wait after reload without starting lock at the earlier ammo-add
event. Moving away returns false and lets the original script clear its lock.
The separate partial-ADS pickup gate, all other callers and nonlocal actors keep
native results. Script pointers are rebound at level start and cleared before
shutdown. The original two-second lock plus finalization delay is unchanged.

The auxiliary scene uses the scene publication's native center origin, removing
the source eye's IPD displacement from both the leading camera and model-relative
eye offset. The narrowed view remains inside the existing conservative stereo
culling volume. Scene, native HUD and lock projection therefore share a center
and basis. No scene texture is taken from the desktop or another old frame.

The existing fixed-M82 screen compositor presents the image on an independent
plane two meters ahead at entry, with a 60-degree horizontal display field and
the native 1280x720 design aspect. The per-eye capture texture is not its display
aspect: the observed 1639x1351 resource previously squeezed the authored HUD.
The native 4:3-horizontal ADS FOV is converted to a vertical tangent and held
stable through reload; the scene/HUD camera uses that tangent with the same 16:9
aspect as the display. The head views the plane through each eye's projection.
Looking away clips the plane;
its exterior is black. Fixed scripted scopes retain priority over carried displays.

## Native HUD and lifetime

Native `JavelinActive` normally requires ADS fraction exactly 1. That delayed
HUD entry after the VR scene was already visible. Only its UIIntWatch call at
`0x140356205` now reads the admitted Javelin near-eye selection. Other callers
of the native getter and `Game.GetJavelinActive` retain their native behavior.
The separate scoped CLU lock gate is described above. The original HUD factory,
timer and lamp callbacks still draw the interface; no duplicate HUD is created.

Process data from the original aimed and acquisition states established the
native materials and actual draw-time blend states. Central lock-box ink uses
ONE/ONE addition, backgrounds use alpha blending, and status lamps use opaque
replacement. The shared capture keeps those operations and draw order; opaque
quads replay native RGB followed by coverage-only geometry, restoring native
shader/class instances and render state afterward.

The central reticle, lock box, lamps and target markers remain native HUD output.
The physical-eyepiece mask, lens shadow, distortion and grain are not part of the
independent display. Extracted Lua and CPU witnesses remain local analysis files;
no extracted native script is deployed and no screenshot was used for collection.

Scene rendering no longer depends on a HUD lease: native ADS/reload transitions
may remove the HUD, but the live camera continues. A loaded-state HUD publication
gap may retain matching ink for at most 100 ms. An empty clip clears that cache,
so old lock indicators are not held across a reload. Missing scene resources
still fail closed; missing HUD alone does not black out the scene.

Display epochs are carried with scene and HUD publications in addition to weapon
lifetime, grip revision, device, tracking reference and capture age. Entry/exit
cannot reuse an earlier session's target frame.

## Verification

Regression covers input entry/exit and rearming, fixed right-hand carry rules,
RPG handedness, real Javelin rig admission, matching screen/lock rays under head
and gun disagreement, center-camera model rebasing, invalid records, and existing
WARP fixed-screen composition. Headset readability and live target alignment
still require user acceptance; mathematical and GPU fixtures do not establish it.

`vr_javelinDisplay_status` reports mode, trigger arming, camera epoch/origin/axis,
planned views and composed eyes. It writes `diagnose/overlord-javelin-display.txt`.
The same section is included automatically in `vr_status`, its core summary,
and `vr_diagnose`. No extra player commands are required. It records cached input
and camera ownership, HUD rejection/retention, auxiliary request identity, crop,
per-eye composition gates and compositor stage/HRESULT when available. Missing
optional Javelin ink is reported separately from a failed scene/display frame.
