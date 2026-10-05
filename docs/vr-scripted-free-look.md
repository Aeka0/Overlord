# Scripted free look

Current policies and their shared composition/porting contract are documented
in [the camera architecture](vr-camera-architecture.md). The former camera-mode
enum is now independent head rotation, head translation and script-transform
policies, with source and one-time entry alignment configured separately.

These adapters own camera rotation only. They select shared free-head policies
while the player is attached to the reviewed native body.
Headset yaw, pitch and roll remain free after entry, even when the script keeps
changing its view clamps or animating its camera. Body travel, animation,
weapon permission, locomotion and progression remain native. Linked-head
translation retains the existing comfort gain and limit.

Gulag's helicopter additionally carries the free viewing reference through
authored horizontal turns, as described below. The independent head orientation
is never derived from a clamped native view.

## Early Cliffhanger

The opening uses the shared scripted camera bridge
while the living player is linked to a native parent, the entry is
`default / cave / e3 / climb / jump`, and the initialized `reached_top` flag is
still clear. This covers intro/idle, ledge walk, pick preparation, ascent,
authored falls, gap jump, slide/hang/catch and final ascent. It ends on native
unlink or completion; later clifftop, hangar and snowmobile phases do not inherit
it. Missing phase data cannot admit the policy.

For a `worldbody`, `authored_yaw` inherits horizontal turns from its original
`tag_player` camera. HMD pitch, roll and relative yaw remain independent of the
animation's angle limits and view resistance. Temporary helpers without that
camera tag retain `free_head`. Native view angles remain available to relative
movement and script predicates; HMD turning does not write back into that
reference. Replacing the native body rebases the authored-yaw source without an
extra camera snap. This camera adapter changes no GSC clamp calls, body animation,
hand visibility, weapon permission, movement axes or climbing receivers.

Linked translation uses the existing `vr_scriptedHeadScale` (default 0.1, maximum
physical offset 5 cm). Only added physical head displacement is reduced, whether
the original body is moving or stationary. Native climb, jump and slide travel
is retained at full amplitude. Recenter, rig replacement and checkpoint lifetime
use the existing independent position reference; ordinary unlink restores
normal head translation and command heading.

The camera and temporary-prop providers share the small opening-entry policy,
but neither waits for the other's input, resources or weapon definition.

## Team Player turret-vehicle boarding

`maps/roadkill_code::_id_A9A3` stores the convoy vehicle in `level._id_BA6B`,
attaches a temporary `player_rig` to its `tag_body`, and after the seat's native
use trigger sets `player_gets_in` and links the player to the rig's `tag_player`.
The rig plays `player_getin`, with either the original or remastered clip.

During that exact relationship, the camera uses `free_head`: head pitch/yaw/roll
are unrestricted, while the native angles remain the script/movement reference.
The common linked-position path applies `vr_scriptedHeadScale` only to added
physical head movement; the boarding animation's original travel is not scaled.
Authored arms, weapon prohibition, the seat trigger and animation timing remain
native. The scope requires the real rig-to-current-vehicle identity, not just
the generic `player_rig` name or the persistent boarding flag.

After the rig is deleted, `_id_AA05` links directly to the vehicle and calls the
turret's `useby`. The boarding policy no longer matches that parent and also
explicitly rejects native mounted entity flags. The existing turret camera,
grip/aim/fire behavior, mission intro and later ejection animation are unchanged.

## Team Player: Shepherd helps the player up

After `h2_intro_done`, `_id_C80F` starts the Shepherd pickup. `_id_B5F9` links the
player to a `player_worldbody` running `player_shep_intro`, then unlinks/deletes
that body and sets `get_on_the_line`. While the player is linked to that rig and
the latter flag is still unset, the adapter selects `free_head`. It preserves
the authored arms, native movement/weapon restrictions and animation travel.
The RPG truck intro, boarding `player_rig`, and later `exit_latvee` body remain
outside this pickup classification.

## Common sliding

`maps/_utility::beginsliding` assigns `player.slidemodel`, then links the player
to that exact object. Its paths cover an animated `worldbody`, reverse entry
and an unanimated `script_origin`; `custom_linkto_slide` uses a blend into the
same object. `endsliding` sets `sliding_out` and starts the out animation before
eventually unlinking the player and deleting the slide model.

The shared adapter compares the actual parent and `player.slidemodel` object
identities. It does not use a map list, the broad `worldbody` name or a timer.
This covers the common slope/sewer sliding mechanism used across levels and
keeps free look through the final slide-out animation. A missing/deleted model,
another linked body, death or unlink cannot retain the policy. Gulag's direct
call and the same common helper in the captured af_caves scripts were checked;
Contingency has not received a separate live acceptance run.

## Gulag helicopter: authored yaw/roll with independent head look

The opening helicopter distinguishes two exact native parent identities.
The temporary `_id_CC0B/TAG_aim` is a `tag_turret` target-aiming helper, so it
uses the existing yaw-only policy with preserved entry. The remastered
`_id_C438/tag_player` selects `gulag_intro`: entry alignment once, free head
rotation and additive native tag yaw/roll.
The original scripts link the player to those tags. A bounded client camera-tag
query reads the current animated world basis before HMD composition. It includes the aircraft and the
authored camera motion, but excludes native player-view clamps and physical
head input. No new native hook or per-eye VM scan is installed.

This shot and rope evacuation inherit native Yaw and Roll, with native Pitch
excluded both on entry and during later animation. Physical head pitch remains
responsive. The native source is filtered before relative matrix composition;
continuous angle branches prevent a pitch pole from introducing false yaw/roll.
Entry retains physical head tilt and establishes the authored yaw/bank basis. Native
view clamps never supply these deltas. Repeated samples apply no duplicate turn.
Manual recenter retains the rendered orientation while accepting script motion.
The first valid remastered tag aligns the opening shot once; the temporary
helper and missing data cannot consume that alignment. Entry uses the scene
lifetime, so subsequent source replacements only rebase the delta source.
Checkpoint
rollback rearms entry alignment. Unlink hands the combined heading back once.

The  passive witness found `tag_player` roll at 8.56 degrees and HMD
roll at 6.04 degrees, but the accumulated VR base at 86.88 degrees and final
view at 92.72 degrees. The still-existing legacy helper's raw `TAG_aim` roll
was 119.20 degrees. Treating both objects as one bank-capable camera let the
helper seed a sideways basis and retain it after handoff. The split policy
excludes that helper bank without adding a fixed angle correction, changing
the general camera solver or altering rope evacuation. A regression replays
these two native source poses through the same sequence lifetime.

Invalid/missing tag data is reported and cannot substitute constrained view
angles. On recovery a fresh baseline prevents a delayed catch-up snap.
`vr_sequence_status` includes `rotation_tag`, accepted authored-yaw sample count,
misses and their last reason. The existing linked head-position scaling and full
native translation are unchanged. The helicopter's weapon-retention, explicit
stow/draw and firing rules remain unchanged.

A just-loaded scripted state additionally receives one tracking/camera-reference
reset after valid state and tracking arrive. Ordinary gameplay consumes that
load opportunity without a reset. This does not turn later scene entries or
native rig handoffs into repeated tracking resets.

## Price knockdown after the second Gulag breach

`maps/gulag_ending_code::_id_B331` transfers the player from the breach to the
`player_rig` used for `price_breach` and `price_rescue`. That rig remains linked
below `level.price_breach_ent`. `_id_BF06` subsequently changes the native view
clamps several times. The read-only flat capture found player parent
entity 2333 / script object 35375, below entity 2140 / object 35371, exactly
matching `price_breach_ent`.

The Gulag adapter requires that `player_rig` parent relation. Ordinary active
and passive breacher rigs retain their existing camera policy. It does not end
at `escape_the_gulag`, which is set before the victim is finally unlinked by
`maps/gulag::_id_B035`.

## Falling rock and Price's assistance

`maps/gulag_ending_code::_id_D551` links the player to `worldbody` for the
`player_downed` animation during `do_cafeteria_anims`. Free look follows that
link. The short `player_falls_down` flag is not used as the ownership lifetime.
The evacuation setup later replaces it with `level.player_rig`, used by the
animation where Price removes the rock. That second exact object identity is
also admitted until native unlink. Other world bodies remain outside this gate.
Their one-time heading alignment now samples the actual `tag_player` rather
than saved/clamped player view angles; subsequent head rotation remains free.

## Accepted rope attachment and evacuation

Once `evac_begins` and `player_uses_rig` are set and the player is linked to a
`player_rig`, the attachment selects a separate camera policy. It covers both
the temporary hidden rig and the main `level.player_rig` used by the native
half-second handoff. The first valid tag establishes the authored yaw/roll basis;
the replacement rig only establishes a new delta source, without a second cut.
The camera follows the exact original animated position with no physical head
translation added, keeping the rope/carabiner presentation in that coordinate
frame. Free head rotation continues, with native Yaw/Roll superimposed and native
Pitch excluded, including at entry.
The earlier rock-removal scene, interactive rope-ready stage, native gameplay
permission and body animation are separate and retain their original lifetimes.

Portable camera tests and the real head-pose bridge smoke test cover yaw/roll
entry, native-pitch exclusion across vertical poles, independent head turning,
relative script motion, rig replacement,
checkpoint reset and exact native translation. These new camera changes still
require headset acceptance for prop alignment and comfort.

## Validation

The Cliffhanger follow-up passes controller policy tests and the real head-pose
bridge smoke suite, including authored pitch/yaw/roll changes, unchanged native
movement references, full-amplitude native travel, reduced physical displacement,
rig replacement without yaw reseeding, recenter and unlink. Debug and
RelWithDebInfo clients build successfully. This is offline validation; early
Cliffhanger headset acceptance and deployment remain pending.

The Team Player boarding follow-up passes controller policy tests for the
actual rig/vehicle relation, pre-trigger rejection, unrelated bodies, stale
identities, checkpoint entry and mounted takeover. The weapon-grip suite
(including turret interaction) and head-pose bridge smoke suite pass; Debug and
RelWithDebInfo clients build. The saved native script captures establish the
binding; boarding in a headset still requires acceptance. No deployment was done.

Gulag authored-yaw tests pass for independent script/head motion, clamp-only
changes, retained physical pitch/roll, repeated samples, ±180-degree wrapping,
recenter while turning, rig changes, missing-reference recovery, checkpoint
rollback and command handback. Controller, head-pose bridge and vehicle suites
pass; Debug and RelWithDebInfo clients build. Native client-object/tag-matrix
entry signatures were read and verified against the running engine, which was
in flat Team Player; that is not a live Gulag or headset acceptance run.

Controller policy tests cover the three positive bindings, missing and stale
identities, unrelated breaches, death and unlink. The real head-pose bridge test
changes all three native camera axes while holding the tracked pose constant,
then verifies that unlink returns the rendered heading to ordinary control
without double yaw. Controller and stereo smoke tests pass.

The authored yaw solver compares the horizontal angle of a stable axis across
adjacent native poses. It switches between forward and left axes when either
projection approaches vertical, while preserving the absolute heading. This
avoids a false half-turn as the axis crosses vertical and does not use HMD yaw
or a clamped player angle as its baseline.

For free Cliffhanger climbing, authored yaw is used only for entry and story
handoff. The free phase uses an unanimated carrier and `free_head`, restores
one-to-one head translation, and does not read the hidden body's `tag_player`
rotation or shoulder position. Showing scripted arms does not grant ordinary
weapon authority.
