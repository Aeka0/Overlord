# Mounted weapons: Team Player and Of Their Own Accord

## Blackhawk minigun: Of Their Own Accord

The `weapon_blackhawk_minigun` vehicle now shares the mounted interaction,
authored wrist calibration, stable arm IK and completed-camera render refresh
with Team Player. A complete Grip press/release takes over the native hands;
then Grip near the matching handle holds it, release frees that hand, and only
a gripping hand's rearmed Trigger may fire. Physical hand motion and the right
stick aim the gun independently of head look. Existing native ADS input may
pre-spin the gun while a handle is held; free hands cannot supply that input.

`mounted_turret_profile.hpp` identifies exact weapon/model assemblies. The
Blackhawk has the exterior, low interior and US Army hand submodels (55+47+68
bones in the current asset). The hand offset is resolved from the assembly,
not assumed to follow the first model. Its `tag_turret` / `tag_barrel` joints
drive yaw/pitch; `tag_turret_base` supplies the stable vehicle reference. Native
rotor, barrel, interior and ammo-box transforms survive. The Suburban shield
material filter and fixed `j_cover` policy remain exclusive to that profile.

Blackhawk uses native vehicle ownership (`e_flags & 0x100000`) and pose type 9,
separate from the ordinary turret's `0x3000` / type 8. Its native controller
receives a private pose with only the compact yaw/pitch fields replaced. The
server adapter supplies aim only around the original vehicle target and slew
calculations, restoring player view angles immediately afterward. The original
`turret_fire -> fireweapon` script, collision trace, rotation limits, sounds,
barrel spin, damage and mission notifications remain authoritative. Limits are
read from the current VehicleDef; the observed Blackhawk has left/right spans
50/30 degrees, up/down spans 12/40 degrees and a 160-degree/second native slew
rate. Native usercmd and script attack-history fields all reject unauthorized
fire. This also works in the native vehicle command branch that skips normal
on-foot input.

The input controller retains the requested local aim separately from the native
solved angle published for rendering. Native vehicle aim traces from the eye,
then converges the barrel toward that target; feeding its output back into the
next input request accumulates the convergence correction. The adapter keeps
the requested local aim separate from the native convergence/slew result and
publishes solved angles only for presentation. This prevents native convergence
from accumulating into subsequent input targets.
`vr_turret_status` reports `requested_pitch` / `requested_yaw` alongside the
actual `pitch` / `yaw`. The native trace, limits, slew and firing calls remain
unchanged. Post-fix headset validation is pending.

The current default helicopter camera mode skips `CG_VehicleView`. Camera
geometry is therefore published at the existing final native-camera boundary,
before HMD composition, rather than an optional vehicle-camera branch. Native
eye placement supplies translation: Blackhawk `tag_player` is the player base
and the engine adds rotated view height. For VR comfort, that height is lowered
to place the eye 35 cm above the neutral `tag_barrel` height. The offset is
computed from the model's bind geometry and current world scale, along the
stable vehicle up axis; gun pitch, yaw and barrel animation do not bob the eye.
The observed 79 cm clearance becomes 35 cm (about 44 cm lower). HMD movement
is composed afterward through the existing tracking frame. This applies only
while operating the Blackhawk; authored boarding/impact cameras retain their
own policy. The shared vehicle-yaw camera policy carries
flight heading while permitting independent head rotation. Camera ownership
uses a new instance on mount, checkpoint rollback or re-entry.

Boarding remains authored until native `useby` and the hand attachment complete.
The first missile impact exits native ownership and swaps to the separate
`h2_vehicle_blackhawk_minigun_viewhands` dummy. The adapter yields without
modifying that cinematic skeleton; return to the live gun requires fresh hand
takeover and grip/fire rearming. Final impact, unlink and death also invalidate
ownership. Server-only tag queries cannot seed the client presentation cache.

### Skin-consumer refresh

The existing `SkinSceneDObj` observer now calls `mounted::prepare_skin` before
any skin work is published. The native scene preparer already released the DObj
lock at `0x14075ED12`; the new adapter explicitly reacquires it, requires the
supplied matrix pointer to still match the DObj, refreshes from the retained
native baseline and releases the lock before forwarding native skinning.
It reuses the current camera/input cache key and does not accumulate IK, move
the aircraft, change smoothing or alter the accepted Team Player route.

## Gun presentation and world-depth policy

Vehicle boarding retains the free-head and attenuated-translation policy. This update changes the mounted gun presentation:

- The acknowledged server controller projects visual pitch/yaw from each current
  XR input sample. This does not acquire grips, fire or advance native state.
  The completed native skeleton is retained as an immutable per-epoch baseline;
  each new input sequence produces a fresh pose without compounding earlier IK.
- The captured native joints `tag_aim_pivot`, `tag_aim` and
  `tag_aim_animated` drive yaw and the two pitch branches. Arm targets use the
  same projected `j_mg`; native barrel spin, belt and wire motion are retained.
- The supported `weapon_suburban_minigun_viewmodel` assembly uses world depth
  whenever it is submitted in VR, independently of hand takeover or a fresh
  server publication. Other scene flags and other model identities are preserved.
- Two rigid surfaces using `m/mtl_h2_minigun_suburban_shield` contain the circular
  cover and side covers (captured LOD0 surfaces 3/4, 9,664 triangles). They are
  omitted through the existing immutable rigid-surface filter. No source mesh,
  material or shared DObj hide bits are modified.
- The lower ammo/power box is on `j_cover` (LOD0 ammo surface 8). Its completed
  matrix is anchored to the stable `tag_cover` root using the original bind
  transform. The aiming descendants stay independently posed; the box follows
  the vehicle rather than gun/head yaw. Its material and geometry stay intact.

## Pickup and free-arm ownership

The post-RPG Shepherd pickup is a separate `player_worldbody` scene. Current
script evidence shows `_id_C80F` waits for `h2_intro_done`, then `_id_B5F9` plays
`player_shep_intro`; it unlinks/deletes the body before setting `get_on_the_line`.
That exact linked-body/flag window now selects free head rotation. Native arms,
animation travel and progression are retained. The earlier RPG ride and later
`exit_latvee` body do not match this window.

Mounted wrist calibration already used the shared `tracked_wrist` path. The
remaining dedicated path seeded IK from the native gun-attached animation and
cached its result without the camera spatial frame. Camera tag queries can run
before HMD composition, so a later skin query with the same input sequence must
refresh when the camera publication or view offset changes.

Mounted arms now use their own model's stable bind pose rebased at the current
head, as ordinary empty hands do. Only acknowledged, currently held handles
constrain wrist targets and preserve native finger articulation. Grip release
removes that constraint immediately. Gun movement no longer supplies the free
arm's source pose, and all targets/grip constraints use one input/publication
snapshot. Public wrist alignment and stabilization settings are unchanged.

## Native skeleton refresh

The previous camera-aware cache key only ran inside `DObjCalcSkel`. Native
camera tag queries can complete the skeleton before HMD composition; rendering
then reuses those calculated bones without entering that callback again.
The mounted adapter now refreshes after the scene's `CG_DObjCalcPose` call
(`0x14075ED07 -> 0x14038C9E0`). This runs under the original DObj lock
(`0x14075ECF4`, released at `0x14075ED12`), before bounds, culling and skin jobs.
It requires the exact supported object and matrix pointer, then reuses the
native baseline with the completed camera frame. It does not invalidate
native bone bits, rebuild the animation or acquire another native lock.
The call target, callee prologue and surrounding lock/unlock calls are checked
before installing the redirect. The native call runs exactly once and keeps
its original returned pointer, including the null/failure path.

## Native ownership and permission

Mounted admission reads `e_flags` at entity offset `0x58` for vehicle ownership. It must not substitute `pm_flags` at player-state offset `0x54`, which controls movement. Weapon permission and native attack history remain independent gates.

## Confirmed native boundaries

The live turret definition is `minigun_laatpv_player`; its model is
`weapon_suburban_minigun_viewmodel`. Definition tokens, entity numbers, pointers,
and script-string IDs from a capture are session evidence, not profile keys.

### Ownership and phases

The native `useby` method at `0x140502B30` checks the player's turret entity at
PS+`0x1e` together with PS+`0x58` mask `0x3000`. The turret use handler
`0x140537F60` calls mount setup `0x140536040`, which writes those fields.
Turret entity+`0x138` is the turret-state pointer; entity+`0x10c` is the owner
handle (entity number plus one). Validate both sides before accepting a profile.

Mounted state may have native weapon zero while the weapon-disable bit `0x80` is
set. Therefore `disableweapons` alone cannot distinguish this turret from a
cinematic. It still blocks personal weapon use; dedicated turret eligibility is
separate.

`maps/roadkill_code::_id_A9A3` runs `player_getin`, enables weapons, and calls
`_id_AA05`, which links the player to the vehicle and invokes the turret's
`useby`. Other level logic also disables weapons. Do not infer the entire
mounted interval from that single `enableweapons` call.

The [boarding free-look adapter](vr-scripted-free-look.md) frees HMD rotation
only while the player is linked to the temporary `player_rig` under this convoy
vehicle. It reuses the linked physical-head scale without reducing authored
boarding travel or replacing the cinematic arms. The policy ends before the
normal mounted turret camera takes over.

`maps/roadkill::_id_C64C` disables weapons before the dismount stage.
`_id_A8E2` plays `exit_latvee` using `player_worldbody`, then unfreezes movement
and later calls `enableweapons`. Movement unfreeze and weapon restoration are
distinct transitions. The script can also remove a Javelin and choose another
weapon before restoring weapons. The existing selection-pause policy must
remain authoritative for this case.

### Firing

The independent carried-weapon route must exclude mounted states. Otherwise
`controller_component.cpp` clears native attack input, including mouse input.
The server update separately rejects suppression mask `0x103000`. Mounted
ownership reads entity flags; movement flags are a separate field.

The native player-turret update is `0x140537650`. It reads client+`0xe90c`, checks
attack bit 1, handles cooldown, and calls `0x140536390`. That function checks
the rotating-barrel readiness state and enters `0x1405349A0`. Barrel spin logic
at `0x140537850` also consumes native attack input.

The adapter should supply one eligible attack intent through the native turret
command route, exclude it from carried-weapon ownership, and preserve native
spin-up, cadence, effects, damage, and script events. Do not call the handheld
bullet adapter or synthesize an additional shot. The exact hand gate must cover
every input source during VR turret ownership.

The aim update alone does not establish bullet direction. Native fire later
calls `0x140534500` from `0x140534A17` to build weapon parameters. Its player
branch reads player view angles again through `0x140680AE0`, after the mounted
aim adapter has restored independent head look. This made the visible gun and
actual shots disagree (public issues #19 and #27).

For the local player's currently owned `minigun_laatpv_player` in VR, the
parameter-call adapter selects the existing non-client geometry branch using
the turret as the geometry source. That branch composes native turret base and
local aim axes and takes the native muzzle tag position. The surrounding fire
function retains the real shooter for attribution, spread, damage and script
notifications. No player angles are modified. Other profiles and ownership
states retain the original geometry source. The call target, parameter-function
prologue, player-geometry branch and native matrix composition are verified
before installing this adapter. `vr_turret_status` reports
`fire_parameter_calls` and `native_fire_parameters`; headset alignment and
affected-mission firing acceptance remain required.

### Mounted aiming laser

After VR hand takeover, Team Player's Suburban minigun displays a thin red
laser from its unique `tag_flash`. The final frontend camera refreshes the
completed gun skeleton and captures its emitter, then releases the native
DObj lock before querying collision. The emitter binding is cached per asset
epoch. Authored entry, pause, focus/tracking loss, recenter and dismount omit
the laser until ownership and fresh tracking are valid again.

The shared weapon-laser service supplies the existing client collision trace,
0.6 mm beam half-width, 4 mm contact spot and per-eye reverse-Z depth rendering.
A bounded sample ring qualifies the emitter and collision result against the
exact scene-source camera; older/future cameras and samples over 150 ms are
rejected. Both eyes reuse one world ray. The composition callback performs no
native collision calls, and missing depth omits the beam. No map asset, native
laser definition or separate renderer is added. `vr_weapon_laser_status`
reports `mounted_traces` and `mounted_pairs`. Headset verification must confirm
the emitted direction, both-eye occlusion and alignment with actual shots.

### Aim and camera

Native aim update `0x140534250` reads the player's view pitch/yaw at PS+`0x108`
and PS+`0x10c`, subtracts turret entity angles at entity+`0x100/104`, and clamps
the relative angles into entity+`0x58/5c`. This explains why the existing HMD
game-view command route steers the turret. A separate mounted aim intent must
replace this dependency while retaining the native turret limits.

Client camera call `0x1403B000F` invokes `0x1403ABCB0`. While mounted, it reads
the turret DObj and resolves `tag_player` directly into refdef origin. The loaded
model parents `tag_player` to `j_mg`, in the aiming portion of the skeleton.
Thus the native eye position is attached to the moving gun. The camera needs a
vehicle/seat reference independent of gun pitch, composed with the existing HMD
tracking frame. Freezing world height or cancelling HMD motion would be wrong
when the vehicle moves or tilts.

There is also server placement code at `0x1405321D0` using turret origin minus
34 units along its aim direction. Its Z write is conditional on PS+`0x5c` bit
`0x08000000`; that bit was absent in this witness. It is not sufficient evidence
to attribute this capture's height change to that server Z assignment. The
client `tag_player` path is the directly confirmed camera dependency.

## Runtime integration and remaining validation

`mounted_turret_policy.hpp` owns takeover, per-side grip/fire rearming, bounded
hand deltas and right-stick pitch/yaw. It has no native or rendering dependency.
`mounted_turret.cpp` binds the agreed definition/model by name; it never stores
a capture's entity number, weapon token or script-string IDs as configuration.

The existing native server aim function is the update boundary. It validates
player/turret ownership, consumes one tracked frame and publishes a bounded
snapshot. A scoped view-angle substitution lets the native function retain its
angle normalization, limits and animation flags; the player's angles are
restored before returning to native placement/firing. Camera and command view
history are not written. Hand deltas use separate root-space joints: `tag_aim`
for pitch and `tag_aim_pivot` for yaw. The authored wrists sit behind the pitch
joint but ahead of the base yaw joint; using the gun pivot for both axes reverses
physical yaw and gives it the wrong lever length. Previous and current wrist
positions are compared around the same current joints, preventing joint animation
from feeding back into stationary controller input.
Right-stick yaw/pitch use 90/60 degrees per second after a 0.2 deadzone and
neutral rearming. Full-circle native yaw wraps at 180 degrees.

In Team Player, horizontal stick input turns the player's view and moves the
off-center seat around the model's `tag_aim_pivot`. The native seat-to-pivot
offset rotates with the player, preserving its radius and height when looking
sideways or behind. Vertical stick input and physical hand aiming move only
the gun; HMD look remains independent.

The final native-camera boundary integrates horizontal input at the display
cadence using the mounted controller's existing speed, deadzone, rearming and
limits. The gun and camera share that frame's projected pose. Server aim
publications acknowledge the baseline without quantizing the view to server
ticks; releasing or reversing the stick does not rewind a predicted angle.
Repeated camera queries in the same client frame reuse the pose. Pause and
tracking interruptions hold the orbit, then require neutral input; a new mount
starts a new orbit. The shared camera rig preserves head look and restores the
player's heading on dismount. Previous physical wrists are re-expressed in the
current tracking-to-vehicle frame so orbiting the player cannot steer the gun
a second time. The Blackhawk retains its independent gun/view behavior.

The native client turret controller at `0x14038D100`, called from `0x14038D093`,
has an independent view-dependent branch: pose+`0x70` selects an angle pointer at
pose+`0x60`. Updating only server turret angles does not remove this dependency.
For the current owner's supported DObj, the adapter invokes this controller with
a private pose copy selecting the native local pitch/yaw branch instead. It uses
the published server angles and preserves controller bone indices and barrel
spin. Neither the shared pose pointer nor global HMD/view angles are changed.

The command owner excludes mounted state from independent carried fire. For the
supported profile it replaces attack with the OR of each gripping hand's own
armed trigger. The server aim boundary clears ineligible attack again before
native spin-up/cadence/fire. No extra bullet or damage call is issued.

Both existing native mounted camera tag-call branches publish current `tag_cover`,
`j_mg`, left/right button and both aim-joint transforms. This profile's `tag_cover` must be a root
bone, above the native aim controllers. The neutral authored `tag_player` bind
transform is composed with that moving root for the camera base. In the native
matrix branch its orientation is stabilized too. Existing HMD composition follows
afterward. This preserves vehicle motion without inheriting gun pitch; actual
headset seat alignment still needs acceptance.

The live DObj is `weapon_suburban_minigun_viewmodel` (48 bones) followed by
`viewhands_player_us_army` (68 bones). The original single-model admission rule
rejected this entire assembly, preventing camera updates, takeover and fire.
The authored hands belong to this combined DObj, not personal viewmodel 4000.
Before takeover, camera tag queries on this same DObj supply calculated
`j_wrist_le` and `j_wrist_ri` world transforms. Both must lie near the matching
button and have valid rotations. Their gun-relative transforms are latched for
the attachment lifetime. Missing or incompatible animation data leaves the
authored presentation active and reports `waiting for authored turret wrists`;
button bind positions are never substituted as wrists. Live distances were
5.32 and 5.51 game units, within the existing 12-unit calibration admission bound.
After takeover, the existing arm solver operates on the second model's subspan
of the completed, locked native skeleton after the render-aim projection above.
The arm solve itself writes only arm descendants and uses a stable hand-model
bind source; held handle targets use the projected gun pose. This reuses the
authored arms themselves and creates no additional hand DObj. Matrices are
view-relative (the native world-tag helper adds render-view+`0x58`), so targets
use the same existing view offset. Gripping wrists compose their calibrated
gun-relative poses with `j_mg` from the current completed skeleton, avoiding the
older gun transform cached by a camera query. This removes one stale-transform
dependency; total input-to-render latency still needs VR measurement. A missing
tracked hand relaxes while the other can remain controlled.

### Scene depth

The flat scene entry for this assembly carries flags `0x1002441`. Native scene
insertion at `0x140776F10` preserves bit 0 into renderer metadata; the backend
uses it to select the depth-hack projection. The supported two-model turret assembly in VR
clears only this bit at the common scene insertion boundary, preserving other
flags and all unrelated DObjs. The personal-viewmodel enqueue boundary alone
would miss this world-entity assembly. Both native callers and the common
function prologue are verified before installing the detour.

### Dismount reference

The 180-second read-only flat capture contains 2,901 records with no read errors;
HMD testing confirmed normal mouse aim, firing and dismount with no ground/stuck
fault. The exit temporarily uses prone/scripted camera linkage and low eye
height, then restores movement and later weapons. A low view height during the
animation alone is therefore not evidence of collision failure.

Native `freezecontrols` changes server client+`0xe908` bit 4. Do not substitute
a guessed movement flag for this state. A future abnormal VR capture must compare
server/predicted positions, this control state and the VR camera/tracking offset
before changing collision, teleportation or cinematic release behavior.

Native mounted flags keep personal carry/equipment suspended, with inventory
reconciliation and script-authoritative selection restoration unchanged.
The server scheduler invalidates attachment state on exit even if no further
turret aim callback occurs. Checkpoint time reversal, reused entity generation,
stale input, recenter and focus loss require rearming. No render/input consumer
scans the entity pool, waits on the server, or calls the script VM.

`vr_turret_status` prints/saves attachment, phase, captured wrists, grip/fire masks,
pitch/yaw, client-controller updates, world-depth submissions, hand applications/rejection and camera/update
counters to `minidumps/overlord-turret.txt`.
Offline regressions exercise held input on entry, separate takeover/grab,
wrong-side contact, all 16 grip/trigger combinations, overlapping trigger release,
tracking/focus/reference loss, stale samples, missing calibration, physical hand
aim, neutral stick arming and native angle limits/wrapping.

Added aim/client-controller prologues, view-angle reads and native aim/camera/controller call targets are
verified before patching. Unsupported turret profiles retain the native
presentation/control route; their personal carried weapons remain suspended.

Verification must cover entry with buttons held, takeover without accidental
grab, wrong-side grabs, each single-hand fire combination, overlapping triggers,
loss of focus/tracking, recenter, checkpoint rollback, turret deletion, gun aim
limits, right-stick pitch/yaw without head coupling, moving/tilting vehicle eye
stability, authored dismount arms, and script-authoritative weapon restoration.
Headset acceptance remains required. Flat-game behavior establishes the native
baseline but does not verify VR aiming, firing, camera placement, or dismount.
