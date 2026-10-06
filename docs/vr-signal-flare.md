# Whisky Hotel signal flare

The `dc_whitehouse` mission provides one flare in the existing abdominal slot
when VR abdominal equipment is enabled. It lies across the body with the cap
on the player's left. Either free hand can take it before the story cue; taking
it does not select a native weapon, spend ammunition or advance the mission.
The stowed flare sits below the chest knife's blade with 3 cm of vertical
clearance, measured using the flare bounds and the existing knife profile.
At the default world scale this lowers its center about 13.5 cm from the generic
abdominal anchor. Its acquisition volume follows that same center; other
abdominal equipment retains its own placement.

At the native `player_flare` cue, hold the tube in one hand, hold either trigger
or grip with the other hand at its cap, and pull approximately 4 cm along the
tube. The acquiring button remains latched through the pull and detached-cap
hold; releasing the other button does not interrupt it. Pressing either button
alone does not ignite the flare. Before the cue, the cap cannot be removed. Grip the
lower tube with the other free hand to transfer it. A cap grasp cannot also
transfer the tube.

Releasing an unlit tube returns it to the abdomen. After ignition the cap
remains in its hand until released. The lit tube remains under the player's
control; releasing it drops it using the shared free-fall motion and native
world collision trace. It does not refill the belt. Its native green effect
emits for 10.225 seconds, then existing particles drain normally. Burnout does
not discard the tube or the removed cap; each remains held until its own hand
releases. Dropped caps are brief cosmetic debris. Ground pickup of the spent
tube is not provided.

The original hint is replaced by the approved sentence:

> Pull the flare from the equipment slot, remove the cap, and ignite it.

The native HUD retains its timing and layout. Simplified and Traditional
Chinese have translations in the shared game-text catalog; missing translations
leave the game's original current-language flare instruction intact as a whole.
Flat mode and disabled abdominal equipment retain the
original controls and hint. Both native source aliases resolve through
`hud_prompts::replace`, with producer, map and physical-feature scope in the
[shared HUD prompt registry](game-text-i18n.md); the flare module does not own a
text replacement callback.

## Native mission boundary

Read-only evidence from the loaded flat game identifies:

- `maps/dc_whitehouse::_id_C03D` waits for `player_flare` and starts
  `maps/dc_whitehouse_code::_id_C660` on the player.
- `_id_C660` gives/selects `flare`, disables switch/offhand/pickup, displays
  `how_to_pop_flare`, then waits for `drop_flare` or `weapon_fired`.
- `weapon_fired` enters the original `player_flare_popped` flag, music and
  objective path, then launches `_id_B886` and waits for `end_firing`.
- `_id_B886` waits for `flare_lookup` before the authored additive animation.
- A separate mission thread emits timed `drop_flare` and `flare_lookup`.

The adapter validates a unique 259-byte instruction block inside `_id_C660`.
Only relocated script calls and string IDs are masked; flare, fire, drop and
hint operands are additionally checked by identity. A changed or ambiguous
script is rejected as a whole. Runtime hooks skip the forced weapon changes,
weapon restrictions, animation thread/wait and automatic weapon restoration.
The original `flag_set`, music and mission continuation still execute.

The waiter and comparisons use private ignition/cancellation events. Physical
cap removal sends only `vr_flare_ignited` to the actual player. No mouse/attack
command, general `weapon_fired` or `end_firing` is synthesized. Flat gameplay
forwards its original fire/drop notifications into those private events.
Waiter authorization is observed at the first `GetString` after its
`PreScriptCall`. The VM directly decodes a redirected target opcode without
checking a hook there: observing `PreScriptCall` itself would leave `waiting`
false after the VR `allowfire` skip and prevent every cap grasp. The following
instruction is dispatched normally on both the redirected and fallthrough
paths; flat mode does not acquire VR authorization.
VR routes the player's timed `drop_flare` to an ignored private event; it cannot
wake the cancellation waiter or discard either physical piece. The mission's other dialogue and optional-objective
timeout remain native. A late physical ignition remains possible after the
native hint disappears, until the level exits.

Saved flat flare sessions are ended through their own `remove_flare` event.
Only an actually selected native flare has its native locks cleared and its
previous valid primary selection restored. The guarded waiter is then restarted
if the native popped flag is still clear. Completed saves do not grant a new
flare. Control-flow hooks survive checkpoint shutdown; VM string references
are released and rebound. Map unload restores operands and removes hooks.

## Physical ownership and rendering

The implementation is a mission provider of the existing abdominal equipment
system. It reuses its reach volume and 200 ms grip intent, unified hand arbiter,
hand mirror/finger library, scene rigid-part resources, exact skin-record
attachments, haptics, free-fall motion and native collision query.

The loaded `h2_viewmodel_flare` contains real rigid groups for `j_flare` and
`j_striker_cap`. Private immutable partitions preserve the source model and
materials. The live idle DObj supplied the tube-to-right-wrist attachment and
15 finger rotations; the opposite hand uses the existing anatomical mirror.
The cap uses the actual left-wrist attachment and 15 finger rotations from
`h2_dcwhitehouse_flare_fire`, frame 20, before separation. The right hand uses
the shared anatomical mirror. The closest of four approaches around the cap
axis is latched at acquisition and retained after separation. A 200 ms input
intent window admits a squeeze just before contact; a 90 ms wrist transition
snaps onto the fixed grip. This is a static pose, with no native take-cap, wave
or drop animation playback. Offline channel decoding was cross-checked against
[Greyhound's H2 animation reader](https://github.com/Scobalula/Greyhound/blob/master/src/WraithXCOD/WraithXCOD/GameModernWarfare2RM.cpp)
and [channel decoder](https://github.com/Scobalula/Greyhound/blob/master/src/WraithXCOD/WraithXCOD/CoDXAnimTranslator.cpp).

Rigid partitions keep vertices and bounds in source-model bind coordinates.
Their scene placement is the desired bone pose multiplied by the inverse
source bind, through the shared `hands::pose_math::rigid_delta` used by vehicle
parts as well. Both initial submission and exact skin-record placement use
that conversion for the tube and cap, including pulling, detached-held and
dropped states. The cap's native +Z 4.970859 bind offset must not be applied
to its mesh a second time. Cap acquisition converts its partition bounds
directly from source space to tube space; it does not add the cap bind again.

Held pieces resolve against the exact rendered hand record in both eyes.
Pulling uses raw relative tracking, while its rendered hand is constrained to
the cap; presentation never feeds motion back into ignition. Acquiring-button release,
sideways separation, large tracking jumps or lost authorization cancel an
unfinished pull. Temporary tracking/reference changes and suspended interaction
cancel only the unfinished pull, preserving held pieces until input resumes.
Death, mission disable and level/checkpoint replacement retire physical ownership;
an interrupted lit tube cannot replenish the belt.

Ignition plays the whole original `scn_dcemp_road_flare_pop_wave` notetrack once,
resolved through the flare's native definition. Its `iw4/level/scn_dcemp_pop_flare`
recording includes the pop and burn; it is neither cut nor stacked with another
burn loop. The ignition FX does not supply that sound itself.

The original `handflare_green_view` effect starts once at the real `tag_fire_fx`.
The shared `native_followed_fx` adapter copies immutable definition metadata and
keeps the core flame/light in effect-relative space; authored smoke and spark
behavior is retained. Exact rendered tube poses are published as values; only
the native FX simulation callback writes its owned `frameNow`, before native
particle update advances `framePrev`. This removes the previous 100 ms stream
of independent bursts without spawning a new effect every frame. Native budget,
particle simulation and lifetime remain authoritative. Separate ignition FX is
queued once. Definitions are retired at the same drained DB boundary used by
runtime rigid-part assets; no gameplay entity or shared FX asset is modified.

H2's native run-frame selector (`0x140462720`) uses spawn/effect/camera/offset/world
modes `0x00/0x40/0x80/0xC0/0x100`. The inherited asset enum had mislabeled those
modes: requesting effect-relative fire wrote `0x80`, which actually uses
`FxCamera` origin and axes. The flame/light therefore stayed centered in each
eye even while the game was paused and its world emitter remained stationary.
The corrected enum agrees with the existing asset inspector labels. The
followed-FX adapter also verifies the native effect/camera selector branches
before admitting its cloned definitions. The original flare core already uses
`0x40`; its native graph/material behavior is retained.

## Validation

`vr_signalFlare_status` prints physical state, native binding/waiter state,
ignition/recovery counters and resource failures. It also writes
`minidumps/overlord-signal-flare.txt`.
It includes `story_ready`, `story_done`, `cap_authorized`, `ignored_drops` and
sound counters, so story admission, contact, timed-drop suppression and audio
requests can be distinguished.

Automated coverage exercises both cap buttons and their independent contact
intent windows, the captured H2 frame-mode mapping, both hands, early/duplicate ignition rejection,
relative stroke and malformed tracking, cancellation and handoff, held-object
exclusion, checkpoint consumption, script signature mutation/ambiguity and
UTF-8 hint identity. The captured script uniquely matches the runtime witness.
Hand interaction, weapon grip and spatial-panel regressions pass. Client
compilation is separate from actual game acceptance.

Cap alignment regressions cover assembled source vertices in both hands,
pulling and detached cap pivots, and the actual immutable rigid-part vertex
contract through WARP. Storage checks cover body yaw, several world scales,
knife clearance and acquisition at the lowered slot. These offline checks do
not replace headset verification of the visible cap seam and hand contact.
Following-FX policy checks cover per-publication orientation/position changes,
single activation, invalid tracking, stale birth/activation rejection and stop
behavior. Burnout checks verify that ending emission retains the held prop.

Headset acceptance is pending: check early belt pickup, both wrist orientations,
two-hand cap contact, flame alignment, ordinary gun use in the other hand,
flat-to-VR save recovery and mission completion. The read-only research did not
modify or advance the running flat-game process.
