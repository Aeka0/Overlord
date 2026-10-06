# Driver weapons: Zodiac and snowmobile

This candidate adapts the active `af_chase` Zodiac and `cliffhanger` snowmobile
driver. Admission requires a living linked player, `player.vehicle` equal to the
linked parent, and the native `zodiac_player` / `snowmobile_player` animation
identity. Passenger rigs, boarding/ending cinematics, death and unlink do not
inherit the driver policy. Flat mode retains native script execution.

## Interaction

- Head rotation remains independent, while native vehicle yaw changes rotate the
  tracking-frame heading by the same relative amount. Entry, recenter, heading
  wrap and checkpoint rollback preserve the scripted camera contract. HMD angles
  are not written into driving commands. Both physical sticks supply steering/throttle:
  their vectors are added and the resultant length is capped at one before the
  shared radial deadzone mapping. Opposite inputs cancel. This also runs when a
  vehicle command branch skips normal input; right-stick view turning is suspended.
  Boarding inherits the current physical deflection, so holding forward through
  the animation starts driving as soon as the native command route permits it.
  Pause, recenter and reconnection still require both available sticks to return
  to neutral; cancelling two held sticks does not count as physical neutral.
- Translation reuses `scripted_position`, `vr_scriptedHeadScale` and its 5 cm
  displacement limit, including reference-generation changes.
- Snowmobile grips use the original animated `handle` and arctic driver's
  bilateral wrist/finger poses. Press Grip within 12 cm of the corresponding
  handle with an available hand; hold Grip to stay attached. The same arbiter
  excludes weapon/support/magazine ownership. Releasing the pistol does not
  release the other hand's handle.
- Right-hand pull or left-hand push steers right; reverse motion steers left.
  Both grips now control one virtual handle angle. Two-hand input uses the
  hand-to-hand vector projected onto the physical tracking-up plane;
  single-hand input rotates around a calibrated centre between the grips,
  using native grip spacing for its lever radius. The authored stem below and
  forward of the palms does not define this physical pivot. The plane is
  independent of vehicle roll/bounce and head look. Actual two-hand spacing is
  retained when either hand releases. The first bilateral grasp captures the
  player's actual neutral heading instead of assuming fixed tracking +Y. Later
  one/two-hand transitions retain it until both grips release. B/Y on a hand
  holding a handle explicitly recentres steering without changing HMD heading.
  Whole-body translation cancels in
  two-hand mode; radial motion does not turn a single-hand control.
  Native grip geometry is rebased into the current tracking reference, so
  vehicle translation cannot masquerade as hand movement. Steering samples the
  raw physical wrists, without head/hand presentation stabilization, then reuses
  the shared speed-adaptive rotation filter once for the common bar angle.
  The initial tuning is a 4-degree neutral zone, a 4.5-degree engagement threshold
  and 30-degree full input, with a progressive x^1.5 response outside neutral.
  Output changes at up to eight full-scale units per second and adds to stick
  steering, clamped to the native command range. Sticks retain throttle/reverse.
  Press Trigger on either hand currently holding a handle to accelerate;
  releasing it stops supplying throttle. Both triggers together still supply
  one full input. A gun/free hand's Trigger retains shooting/magazine semantics.
  Backward stick input takes priority for native braking/reverse.
  The animated handle, chassis, physics and mission scripts retain ownership.
  Physical distance and per-frame movement do not cause automatic release.
  Fresh rendered geometry is needed for acquisition, but an established grasp
  and its input do not depend on continued vehicle skin submissions.
  Grip release, stale input, tracking loss, pause, recenter and checkpoint/exit discard the affected grasp
  or calibration; reacquisition requires a new physical Grip press.
  Coincident hands or a collapsed pivot ray retain the grip, suppress the
  undefined steering angle and establish a new reference on recovery.
- Chest and abdominal equipment remain suspended with ordinary carry. The
  vehicle weapon is centered on the existing chest/body estimate. Its left side
  faces the torso, with its muzzle pointing left and 25 degrees down. The rotation
  directly reuses the Exodus designator's carry orientation. Source meshes are not mirrored.
- Grip at the chest draws with either hand. Driver weapon/chest supply contact
  tolerance is at least 20 cm, seated magazine contact tolerance is 12 cm, and
  support-hand acquisition is at least 18 cm. These larger acquisition regions
  do not enlarge the automatic quick-load dwell zone. The usual authored control/support
  poses and anatomical hand mirroring are reused. Last-hand release returns the
  gun to the chest; no native world-drop or give/take operation is performed.
- Press the holding hand's secondary button (B on the right, Y on the left) to
  release the magazine, using the same central input edge as on-foot release.
  A magazine already pinched by the opposite hand remains held; otherwise it
  leaves the feed. The same button cannot insert a magazine or operate a bolt.
  Alternatively, pinch with the opposite Trigger and pull it out along its native
  magazine-well axis. Contact boxes and pull dimensions come from the existing
  Mini Uzi/G18 reload profiles. An inserted empty magazine is still a magazine:
  it must be removed before a quick load.
- With the gun held, use the free hand's Trigger at the chest or either waist
  holster to draw a new 32-round driver magazine. Keep Trigger held and insert
  it into the empty magazine well. The adapter reuses the physical reload mouth
  sweep, alignment, withdrawal and tracking-jump checks. A spare already held
  keeps its payload when B/Y releases the old magazine. Loaded and empty removed
  magazines can also be inserted; occupied wells cannot accept a second magazine.
- Hold the gun hand at either configured waist holster or at the chest for one
  continuous second. The shared quick-reload dwell applies, including slot,
  ownership, recenter and tracking-continuity resets. This driver path is always
  enabled, independently of `vr_quickReload`. It loads the native 32-round vehicle
  supply. A detached magazine held in the other hand has a separate rendering
  instance; it does not block the new magazine.
- No chamber or bolt-action state is created for the driver gun. Loading makes
  it immediately usable. There is no empty-slide lock or open-bolt return pose.
  Quick-loading audio and localized text reuse the existing sound alias and text
  renderer; the caption is bound to the same scene record as the holding hand.
  Outside loading, that caption displays the remaining native rounds out of 32.
  Driver ammunition digits use the game's loaded `fonts/bank.ttf` (BankGothic
  Md BT), registered privately from its TTF bytes and held through draw/unload
  with a resource lease. No font file or machine font installation is required.
  Localized loading text retains the existing Unicode caption path.
- Accepted native shots publish through the shared weapon-feedback queue for
  holding-hand haptics. Empty/absent magazines produce one dry-fire response per
  Trigger press. Neither path changes native ammunition or adds automatic loading.

At the final Zodiac waterfall, the native `player_jumping_over_waterfall` flag
and actual linked boat/blend-target identity transfer ownership to the cinematic
policy before dismount. Driver gun/hands retire; the native worldbody arms remain.
The ending retains free head rotation and the shared linked-head translation
scale/clamp through the boat-to-blend-target camera handoff. A stale flag alone
cannot suppress hands on foot or on an unrelated linked entity.

The driver interaction is coordinated by the existing server hand arbiter. It
does not project a temporary weapon into ordinary inventory, borrow the on-foot
reserve pool, run a second polling thread, or change native driving flags.

## Native script boundary

The native shooting loop remains the owner of shot pacing, animation notetrack
sound and mission restrictions. Complete pullout/putaway waittillmatch statements
are bypassed only in the VR player driver scope; native attach/detach work and
completion notifications remain. Each bounded statement contract includes both
string pushes, the vehicle local and all wait/cleanup opcodes. Short Trigger
presses are retained for up to 150 ms and acknowledged once by an accepted
ballistic call; stow, tracking discontinuity, magazine removal and loading cancel
pending input. This removes animation latency without bypassing native shot rate. Its shoot-button query is scoped to the current
VR driver and the actual holding Trigger. Native `magicbullet` remains the only
ballistic call. Its origin comes from the tracked model's `tag_flash`.

Vehicle aim assistance is independent of `vr_aimAssistStrength`. The native
enemy candidate source, Zodiac 1300-unit / 20-degree yaw / 15-degree pitch limits,
and snowmobile 750-unit / 0.94 forward-dot limits are extracted into the vehicle
adapter and evaluated relative to the controller muzzle. Native target queries
and collision traces are reused. Zodiac helicopter overrides, destructible
targets and rider handling remain on the mission route. The Zodiac target
structure is supplied before its original shot function, so its supplementary
damage/death actions address the selected target instead of an old head target.
This is a muzzle-relative adaptation of the native policy, not an invocation of
the optional general VR aim-assist setting.

Native script helper execution runs at the existing server scheduler after
`G_RunFrame`, only with the native script error depth at its idle value (-1).
The shooting builtin consumes a prepared, ownership/reference-bound target
snapshot; it does not recursively call `VM_Execute`. Missing/stale preparation
blocks firing. Animation-only queries after stow retain an origin-only struct.

The two original script ammunition fields (`B016` and `BFD3`) remain the loaded
round authority. A successful shot consumes once at the ballistic boundary.
Unique, bounded H2 instruction contracts bypass the original delayed decrement,
automatic reload branch and post-wait refill. Thus removal/reload cannot race a
late native subtraction or inherit an automatic refill after checkpoint resume.
A rejected shot skips the rest of that native shot function, including extra
script damage. Other actors, functions and flat gameplay keep their native path.
Bindings are removed/rebuilt with script lifecycle; no script assets on disk or
upstream dependencies are modified.

Only the current driver's attached native hand/gun models are hidden. Vehicle
geometry and other vehicles remain native. Runtime rigid model descriptors are
registered through `scene_models`; source model geometry/materials stay intact.
Each frontend submission retains its own immutable state and lighting handles
in bounded storage. Backend placement resolves the entire gun once per scene
record and submission, instead of mixing a newer server snapshot into individual
parts. Frontend culling placement also uses the latest matching solved gun root.
Rigid vertices remain in source bind space. Parent-local equip poses are resolved
through the model hierarchy, and each part uses `desired * inverse(source_bind)`.
Gun muzzle/brass markers use the same resolved hierarchy. Native script FX
requests enter a bounded queue, then emit through the existing oriented native
FX wrapper on the scene frontend after a fresh holding-wrist solve. Ownership,
tracking reference, input sequence and age prevent old shots crossing a stow,
hand change or recenter. This avoids spawning at the older server gun position.
Native FX still own particle lifetime and shell physics; placement at speed must
be checked in-headset. This fixes applying
bone translations twice and separating the stock, sights and receiver.
The driver hands-only solver ignores the suspended on-foot firearm owner while
preserving that owner for exit. A stowed gun retains its fresh body placement if
its hand skin record is unavailable, matching ordinary chest equipment. Native
attached hands are not masked before the replacement hand binding is ready.

## Verification and remaining acceptance

`vr_vehicle_status` reports driver identity/epoch, script and asset admission,
hand binding, magazine state, rounds, firing and quick-loading state, plus queued,
emitted and rejected frontend FX requests, held handle mask, physical steering,
raw/filtered handle angles, input sequence, plane axis, pivot and both raw wrists
in tracking-reference metres.
It writes
`minidumps/overlord-vehicle.txt`.

Enable **VR Settings > Debug > Vehicle steering recording** before launch to
collect this history (`vr_debugVehicle`, default off).
`vr_vehicle_steering_trace` saves the most recent 512 unique driver input samples
to `minidumps/overlord-steering.csv`. The bounded in-memory history retains the
last driving samples across pause/console entry; normal frames perform no file
I/O. It records both physical wrists, the shared axis/pivot, grasp mask and
raw/filtered angles, physical steering/throttle output, Grip/Trigger masks and
tracked-hand mask. A new driver epoch clears it.

The implementation was checked against a read-only capture of the currently
running flat Zodiac driver. Player weapon selection was zero and
native weapon-disable bit `0x80` was set; its linked vehicle DObj contained the
Zodiac, TF141 hands and Mini Uzi. The current bytecode's reload/decrement
statements match the bounded contract. Snowmobile bytecode comes from the earlier
Cliffhanger capture, not a new live snowmobile run.

`vr-vehicle-tests` covers driver classification, mirrored chest axes, body/world
scale, both waist zones and chest, native aim limits, quick-load duration and
cancellation, bounded head translation, and rejected/ambiguous/truncated script
contracts. It accepts optional captured-bytecode slices for native verification.

HMD acceptance is still required for steering while looking sideways/backwards,
both-eye placement at speed, both drawing hands, magazine contact/pulling,
quick-loading with the quick-reload setting disabled, dry firing, stow/redraw, native
aimed shots and mission progression, pause/recenter, death/checkpoint restart and
exit restoration. The development run does not establish those hardware results.

The  read-only paused Cliffhanger witness confirmed the driver DObj's
`vehicle_snowmobile_player` / `viewhands_player_arctic_wind` assembly, its animated
`handle`, both wrist/finger chains and the loaded BankGothic asset/family. The
steering regression uses the actual captured grip geometry to cover equal
one/two-hand angular response under different vehicle attitudes and world scales,
whole-body translation rejection, neutral jitter, grasp-spacing changes,
handoff/return-to-centre, large held displacement, degenerate geometry and
tracking loss. HMD acceptance remains necessary for grip placement while
moving, one-hand firing/reloading, physical/stick input mixing and displayed font.

The later firing investigation captured two different native crash sites: a
null `sv_running` pointer in client input and an invalid FX light-grid pointer.
Neither dump alone identifies the original corrupting write. The assembly
transform defect is confirmed from the existing rigid-part contract; target
preparation was also moved out of the nested script callback. Crash-ring events
`vehicle_aim_prepare`, `vehicle_shot` and `vehicle_fx` now record entry/completion
stages without file I/O or runtime locks. Headset testing confirmed firing
no longer crashed after the callback parameter correction below. A later read-only
live capture reported 64 accepted shots, zero suppressed shots, zero aim errors
and zero remaining rounds. That capture establishes an exhausted magazine, but
does not establish the visual/haptic result of every shot.


### Confirmed callback parameter corruption ( follow-up)

The repeated firing crash was traced to the replacement A8AF entry, before the
ballistic callback. The old generated function began with OP_clearparams (4D).
Disassembly of H2's actual VM shows that opcode releases values until type 7
(SCRIPT_CODEPOS), whereas a newly called function has type 8 (SCRIPT_PRECODEPOS)
under its arguments. Bypassing the three native parameter bindings and their
OP_checkclearparams (32) therefore crossed the current frame and released caller
references. This explains unrelated subsequent failures in input, lighting and
render-resource access; a null-pointer guard at those sites would hide damage.

The adapter now validates and executes the original A8AF prologue unchanged,
then redirects only its body to a four-byte builtin-return continuation. The
zero-argument shoot query also retains its original checkclearparams. Regression
coverage includes 0-3 supplied parameters, caller-reference preservation, exact
native prefix validation and rejection of changed/truncated prefixes. The
corrected model assembly and server-side target preparation remain in place.
