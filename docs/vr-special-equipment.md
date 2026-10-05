# Abdominal equipment and physical claymores

The abdominal slot is centered 24 cm below the existing tactical chest slot.
It does not replace the native lethal or tactical selections. Inventory comes
from the four native action slots (PS +0x1FA0 types / +0x1FB0 weapon parameters).
Only a unique supported weapon action is presented; ambiguous slots are not
silently reordered. `vr_abdominalEquipment` enables this provider by default.

All abdominal providers use one body-aligned rounded grab volume centered on
the displayed slot: a 20 cm wide, 14 cm tall, 6 cm deep box with the chest rig's
10 cm contact tolerance. Both anatomical palm and wrist contact are admitted.
The existing chest pickup's 200 ms grip intent is reused, allowing a squeeze
just before contact without accepting an indefinitely held button.

## Claymore controls

- Grip the abdominal item with either free hand. Acquisition costs no ammo.
- Grip the other authored handle with the free hand for two-hand holding.
  Poses come from `h1_wpn_eqp_claymore_idle`, with separate left/right grips.
- Release support to retain primary holding. Release primary while support
  remains held to promote support; the old trigger preview is cancelled.
- Release the last grip to return the unplaced claymore to the abdominal slot.
- Aim the mine's front toward the ground, up to 2 m beyond its held origin.
  Hold the primary hand's trigger for the yellow placement preview. A trigger
  already held during acquisition cannot start the preview.
- Release the trigger to validate the surface again and place once. Invalid
  ground cancels that attempt without spending ammo. Preview itself never debits.
- Aim an empty hand at a player-owned placed claymore within 2.2 m and press
  grip to recover it into native inventory. Full inventory rejects recovery.

Placement checks line of sight, a 45-degree slope limit, five footprint support
samples and overhead clearance. Actors and other missiles are not terrain.
The straight-down aiming fallback preserves the mine's handed yaw. Recovery
intersects linked entity bounds explicitly because native player traces can
ignore player-owned missiles; a separate world trace checks occlusion.

## Native behavior

The shared guarded type-2 projectile entry creates the actual native claymore,
with the original `grenade_fire` notification, owner, collision and subsequent
`missile_stuck` handling. Native scripts own laser FX, detection, detonation,
damage attribution and mission callbacks. The placement direction is applied
through the existing validated native angle setter.

One compared ammo debit precedes spawning; a failed spawn rolls back only the
unchanged expected debit. Recovery admits one compared credit before cleanup,
then sends `death` to stop detonation and wake trigger cleanup before entering
the existing G_FreeEntity bridge. That bridge already validates the function and
tracks entity generations. An unchanged surviving entity after cleanup failure
rolls back only the expected credit. No shared scripts or definitions are edited.

The central hand arbiter owns all grips. Script inventory replacement, a forced
gun in the same hand, tracking loss, pause/context suspension, death and native
inventory timeline replacement cancel the preview. Releasing one hand cannot
silently commit a preview that belonged to another hand.

## Yellow preview

The effect uses a loaded native golden
`m_unlit_add_lin_ndw_cltrans_objective` material, as observed on the estate DSM.
The preview has private material handles, private vertex/index buffers and
constant UV sampling, so the donor's DSM/other prop artwork is not mapped across
the claymore. The original model, material, shader and texture are not edited.
The preview mesh comes from the native projectile model's first LOD. Runtime
descriptors remain retained until the drained asset-unload boundary.

C4 controllers and other supported
native item/exclusive weapon actions use the same abdominal entry and activate
their original action-slot command flow. They do not inherit claymore placement
or recovery behavior. Mission-specific presentation/control remains native and
has not been separately validated in every relevant level.

Exodus was inspected live: action slot 4 binds `usp_laserdesignator` (ordinary
weapon inventory type, matched by its designator identity). Its abdominal
display uses `h2_viewmodel_laser_designator`; the native world model is a generic
`h2_weapon_glock_reflex` and does not depict the device. Abdominal grip invokes
the original action-slot flow.

### Exodus laser designator

The designator now has its own registered `h2_viewmodel_laser_designator_base`
hand profile. It reuses USP fingers and support grip, translated by the measured
trigger offset. The normal firearm hand adapter supplies left/right control and
support poses. Native class 11 is admitted only for `usp_laserdesignator`, so its
flat viewmodel is suppressed without treating every mission item as a gun.

Abdominal pickup reserves the requesting hand while the original action-slot
script gives/selects the device. The body model and further body interactions
are hidden from the request through held ownership. Releasing before selection
cancels the request or returns a late selection; it never seizes an occupied
hand. Last-hand release returns to `location::abdominal` without a clearance
test or world-drop call. Native primary-inventory weapons bound to action slots
share this return policy; physical claymores and AGM retain their own lifecycle.

Draw acknowledgement requires both actual native selection and completion of
the mission's switch lock. The requested client weapon alone is insufficient.
During startup, normal carry projection changes are deferred; releasing another
gun cannot interrupt the mission before its cancellation watcher exists.
Cancelled draws drain that startup before restoring the previous projection.
Instant selection is retained. The two validated mission switch calls use the
native immediate method in VR. Guards distinguish opening from closing even
when both have the same native lock flags. Closing skips its selection wait and
continues the original take/unlock cleanup; opening skips the animation waits
(including a checkpoint resumed after the first wait) after starting watchers.
The final 50ms toggle pacing wait is also skipped in VR. Flat mode retains native
switch methods and waits. Draw requests dispatch the original use_laser event on
the server directly. Rapid redraw waits for teardown before one bounded retry.
An orphaned opening with empty native selection is resumed and then closed
through the original watcher, restoring permissions without writing either flag.
The immediate switch clears actual selection before its client command arrives.
With another gun held, an older command could reselect that gun and consume the
immediate-switch flag, adding its ordinary lowering time to the device draw.
The shared finish-change adapter defers that stale completion only for a local
pending designator draw with empty actual selection and both native immediate
and mission-lock flags. The arriving device command completes the original
transition; its raise timer uses the existing physical-acquisition bypass.
Cancelled reservations still drain through native startup/unlock before cleanup.
Control-flow hooks survive checkpoint shutdown without retaining VM string
references; event aliases are rebound after loading. Loading alone never sends a
blind use_laser toggle. Native selection does not create a right-hand grip.

Only the designator's stored orientation is changed: its left side faces the
torso, muzzle toward the player's left and 25 degrees downward. Its actual
orientation uses the same body estimate as chest equipment, including its
fallback when that estimate is unavailable. In VR, only this device's native
disableweaponpickup call is skipped; other pickup restrictions remain intact.
Picking up a bullet gun in the other hand keeps the script device selected so
the mission watcher does not close it. Draws lasting at least 100ms emit bounded
state and handoff timing diagnostics under `[VR abdominal draw]`.
`vr_carry_selection_status` also counts deferred stale draw completions for
simulation and prediction. The  capture measured about 0.19-0.20 s
for empty-hand handoff and 0.64-0.84 s with a gun held; the latter included the
old gun's native lowering timer. Post-fix headset timing remains to be verified.
Its
viewmodel is also used for any rigid fallback; the unrelated Glock world model
is never presented as the held device.

The original `maps/arcadia_code::laser_targeting_device` receives the action
binding and owns give/take, laser activation, restrictions and cancellation.
Trigger fire uses the native weapon event, not the independent bullet clock.
The mission still owns `laser_designate_target`, target validation, Stryker and
artillery dispatch, dialogue and target restrictions. A bounded builtin interceptor
changes only the `bullettrace` inside `get_laser_designated_trace`: origin and
direction come from the accepted shot's VR muzzle, retaining the native 7000-unit
range, collision flags, ignored entity and return schema. Other script traces
are untouched. `vr_designator_status` reports binding and intercepted traces.

Ordinary held guns remain independently usable beside the designator. The
native `weapon_fired` notification includes its actual weapon name. An additive
event alias forwards only `usp_laserdesignator` to `vr_designator_confirmed` in
VR, leaving the original notification and arguments intact for other scripts.
Only the uniquely validated waiter and automatic-close event-string operands
in the loaded mission's `laser_designate_target` are redirected; their original
values are restored before script shutdown. Mission files are unchanged.
Flat mode forwards native notifications to preserve original behavior. A saved
active laser session is closed through its original toggle on VR level start,
so a restored old waiter cannot consume another gun's first shot.
Hand alignment and real mission acceptance still require headset verification.

The native frontend laser entry retains its original client collision query.
Its result is a short-lived world collision plane, not an interpolated spot.
At eye composition, the exact scene-bound skinned muzzle is reconstructed using
the pair's shared current model placement. Nearby planes are intersected again
from that origin; large aim jumps wait for a fresh trace instead of extending a
surface through a doorway. Both eyes share the same world geometry and only
their view-projection/depth test differs. Native reverse-Z is preserved and
each eye borrows the scene DSV before it is reused. No depth readbacks or native
physics calls occur in the composition callback.

The beam has a 0.6 mm half-width and its surface spot a fixed 4 mm radius. No
distance flare scaling, endpoint smoothing or post_light illumination is used,
so the laser cannot illuminate the VR gun itself. This path is shared with
native local carried-weapon lasers, including nightvision IR beams. Those use
their receiver's `tag_laser` and native beam RGB; the designator keeps its muzzle
and existing red color. Both emitter and muzzle are qualified by the consumed
skin and stereo scene. NPCs, script-owned weapons and flat mode retain the
original renderer. Missing pose/depth omits the local beam
instead of falling back to a flat overlay. `vr_weapon_laser_status` (also available
as `vr_designator_beam_status`) reports
frontend traces, scene pairs, eye draws and depth-admission failures.

While the device remains held, the original post-use close event rearms its
original target-waiting function instead. Confirmation has a three-second game
time cooldown, retained across stow/redraw. The native capacity is one; only
this device's compared clip cell is restored to one charge when ready. Other
weapons' ammo is untouched. Cooldown gates command input before native ammo
debit, and a fresh trigger press is required after readiness returns. Stowing,
script cancellation and level changes stop rearming normally.

Delayed pickup adopts the already-held grip and its counters. It does not use
the exchange latch, which deliberately suppresses the current release; the
first physical release after pickup therefore returns the device normally.
Abdominal equipment also reconciles a fresh active button that is already up,
so a missed release edge cannot strand it in hand. Inactive or stale input is
not treated as release, and ordinary world-drop weapons retain their edge rules.

## Whisky Hotel signal flare

Whisky Hotel's separate mission provider supplies a physical signal flare in
this same slot. See [signal flare](vr-signal-flare.md) for early pickup,
two-hand cap removal and the native mission boundary.

## AGM notebook

Wolverines (`invasion`) binds `remote_missile_detonator` to action slot 4;
Contingency uses the same physical notebook and native AGM control path.
Either free hand grips the closed notebook at the abdomen without switching the
native weapon. The other hand uses Grip or Trigger within 13 cm of any of the
main screen's four edges and opens it along its hinge. Both hands use the same
candidate, anatomical wrist and release rules. Palm and wrist contact are
accepted. Grasping near the hinge uses a virtual screen lever so wrist rotation
can open the lid even with little translational leverage. The screen must reach
the captured native maximum, 106.606 degrees, and the player must then explicitly
release the button that acquired the lid. Releasing the other button has no effect.
Reaching the stop while still gripping does not activate it. An early release
leaves its current angle; tracking loss never substitutes for a release.
Before commitment, releasing the case closes and stows the unit.
The stowed case has its back against the torso and its opening lip above the
hinge. Both its root and acquisition volume are 3 cm farther outward than the
shared abdominal slot to clear the knife. Its render reference comes from the current interaction frame, including
before the first physical draw; lifecycle updates do not replace it with the
not-yet-initialized held-session reference.

Before native acceptance, a committed request keeps physical case/lid interaction
available: the lid can be closed again and releasing the case stows it immediately.
Neither action reissues the request or discards a late native acknowledgement.
Carry selection changes cannot overwrite the outstanding device request. Once
native control has started, it retains the original preparation/handoff lifetime.
An unchanged grip keeps the open case visible through native preparation.
`is_controlling_uav` is set only after the native 1-second preparation and
0.25-second blackout; this camera acknowledgement hides the physical model
under the transition, rather than on the input request. The physical hand lease
is then retired after a conservative 1-second scene-settle window in game time,
closing and docking the unit. Once the camera has been acknowledged, the model
stays hidden throughout remote control and cleanup, so a temporary cleared
camera flag cannot expose it again mid-transition.

After native completion, an empty physical inventory must not leave the exclusive
UAV device selected. If both native requested and actual weapon still match that
completed device, the adapter restores empty hands through the existing native
`G_SelectWeapon(0)` path once scripts allow weapons again. This emits native
`sw 0`; it does not edit inventory or player-state flags. A held gun or a different
native final selection is preserved. The original action-slot handler rejects
an already active exclusive weapon, so this reset makes another console use
possible without first picking up a gun.

Once native control starts, either hand supplies the native attack command
(launch / flight boost). Each trigger rearms independently after entry;
holding one unarmed trigger does not block the other hand. Simultaneous presses
produce one reliable native attack notification, and releasing one hand does
not cancel a boost held by the other. Ordinary weapon firing, movement and posture gestures are suspended
during this session. Native camera acknowledgement also suppresses both the
independent VR arms and legacy first-person viewmodels until native cleanup.
Head pitch/yaw changes feed relative native look input, preserving the remote
view's starting pitch, native clamps and mouse input. The normal linked-view
branch uses native view angles; the missile branch uses its signed two-axis
`usercmd` fields at +0x3E/+0x3F, as consumed by native remote-angle evaluation.
Head angular changes are converted using the original pitch/yaw rate dvars and
command duration. Motion beyond one command's range remains pending instead of
being lost to byte saturation; pause, focus loss and recenter clear that motion.
The look stick (right stick by default) adds continuous pitch/yaw in both modes,
independent of walking snap-turn settings. Entry and a control-mode change
require a neutral stick before steering. The complete native
camera rotation is retained; only head motion newer than its exact recorded
command is added at render time. The missile camera's latest-command source is
matched explicitly instead of substituting predicted player-state command time.
Entry, recentering and stale-input recovery
rearm without replaying old head motion. VR aiming and boost delivery still
require headset acceptance. The original mission scripts
retain missile launch availability, cooldown, damage cancellation and camera
cleanup. No missile is spawned directly by this feature.

The remote session retains the activation hand as diagnostic provenance and
owns a separate control revision for both trigger gates.
Automatic docking, case release and recentering do not send `force_out_of_uav`
or clear an ongoing trigger. Input loss only rearms input safely; it does not
abort native AGM. A request never accepted by native gameplay times out after
five seconds. Native cleanup (`using_uav` clears), or a level/checkpoint change,
ends the session. Original mission rejection/damage/keyboard exit paths remain
native. No synthetic exit notification is emitted by this feature.

The complete `h2_viewmodel_uav_control_unit` uses a retained native DObj through
the common `scene_skeletal_model` provider. The screens and cable move
continuously at render cadence through the original bones and blend weights;
there is no baked cable-pose cache. Opening reuses the ACR sensor's accumulated
hinge travel, including stop overtravel. Latest tracking previews operate on a
copy while simulation alone commits the full-open/release AGM transition.
The screens and cable close on stow; no shared model or material is modified.
Native idle right-hand case support and pullout frame-36 screen fingers replace
the borrowed claymore grip, with the common anatomical mirror for either hand.
The model keeps source bind coordinates; hinge delta transforms already
include the source bind compensation. Rendered screen angle is reconstructed
from the latest tracking frame, without writing model-relative render poses into
the server/world-space state snapshot.

The scene provider explicitly requests the native skeletal path (scene flag
`0x2000`). Without that flag, this single-model DObj with no XAnim tree enters
the native static-model shortcut, which ignores its posed bones. The provider
keeps absolute world-space bones and subtracts the current native render origin
when applying them; the tracked head is not the render origin. Its cpose and
lighting origin follow the root bone at the abdomen or held case.

The UAV HUD has a dedicated native capture and stereo layer. It captures both
scene UI and independent UI dispatch, retaining separate target and instrument
leases for each. Target layers are composed last so opaque enlarged instrument
backing cannot cover the red frames. An invalid instrument projection does not
discard another valid target layer. Additive native markers retain their emitted
RGB with zero alpha; the final compositor discards only fully empty RGBA ink,
so splitting targets from instruments or removing the vignette cannot hide that
emission. Background attenuation remains controlled by native alpha.
Text, rulers and reticle use an aspect-preserving 80-degree
instrument canvas, independent of the native zoom. The native
`h1_ac130_screen_overlay` vignette is excluded from VR HUD composition.
Unrelated empty dispatches cannot erase the other channel; both leases still
expire after 150 ms. Its bounded 1,024-command selection is separate from the
small ordinary HUD scopes. Original LUI
instrument text, pitch/yaw rulers, reticle and GSC target materials (including
colorblind variants) keep their native fonts, values, colors and draw order.
Target rectangles remain view-projected HUD at optical infinity, mapped from
the native field of view into each eye's asymmetric projection. Their size and
placement do not follow the enlarged instrument canvas. Captures are scoped to the
remote-control epoch, device, tracking generation and short freshness window;
menus and narrative text retain their existing owners.

The native transparent static-model list overflow is separate from this 2D HUD
capture. See [static scene surfaces](scene-static-surfaces.md) for its independent
storage contract and capacity correction.
`Too much model lighting used` refers to a separate persistent lighting cache;
see [model lighting cache](scene-model-lighting.md) for its bounded native sizing.

## Verification

`vr_special_equipment_status` reports slot, weapon, quantity, both hand owners,
preview validity, placement/recovery counters and resource/native rejection
reasons. It writes `minidumps/h2-mod-vr-special-equipment.txt` on demand.