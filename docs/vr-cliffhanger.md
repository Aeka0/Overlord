# Cliffhanger story props

This page covers the mission's temporary ice picks and C4 detonator. The native
script remains authoritative for body animation, root movement, weapon grants,
mission flags, explosion behavior, death, and recovery. The VR adapters add
presentation and input only at the ownership boundaries described below.

## Scripted body and arm ownership

The opening body, native arm animation, linked movement, and camera remain under
the story sequence. During the stand-up transition, the scripted-arm adapter
blends only the native arm chains toward the tracked controllers. It does not
replace the body or root movement. See [scripted body arm control](vr-scripted-arms.md).

When the native story body or its weapon-disabling phase owns the player, the VR
prop adapters do not publish another hand rig or another pair of picks. Once the
native sequence returns hand control, the two mission picks may be presented as
separate hand-bound props. Pausing, death, checkpoint restore, tracking loss,
and a body-ownership change invalidate stale held input.

## Temporary climbing picks

- The left and right picks have separate object identities, poses, and hand
  ownership. Releasing Grip does not drop a pick.
- The picks do not occupy ordinary weapon slots and cannot be stowed, discarded,
  or selected as firearms.
- A native story phase may hide these temporary props while the story body owns
  the hands. A temporary native `giveweapon` / `takeallweapons` transition does
  not permanently consume them.
- The native definitions and script attachments remain available. VR excludes
  the picks from the ordinary carried-weapon inventory rather than replacing
  their script role.

The native ice-pick models and left/right idle poses are used for presentation.
These props do not define climbing contact, support, pulling, or player
movement. Those mechanics are documented in
[physical climbing](vr-cliffhanger-climbing.md).

## C4 detonator

The VR detonator is a separate abdominal prop available from the start of the
mission. Early access does not grant the native C4 weapon, spend ammunition,
occupy a native weapon slot, or advance the mission. During native body control,
the prop remains unavailable to the hands. When hand control is available, an
idle hand may pick it up; releasing Grip returns it to the abdominal anchor.
There is only one instance.

Placing C4 and detonating it are separate native actions. The VR adapter allows
detonation only when all of these conditions hold:

1. The native hangar sequence has set `player_can_see_capture`.
2. The native script has granted and selected C4, weapon permission is valid,
   and `player_detonate` is not already set.
3. The detonator is held by a currently valid hand, the predicted weapon is that
   C4 instance, and no weapon timer is active.
4. Tracking and input are valid, a neutral trigger state has been observed after
   authorization, and the player presses Trigger afterward.

The adapter submits the ordinary native attack input at the existing
`CreateCmd` boundary. It does not synthesize keyboard input, set mission flags,
or send a fake `detonate` event. The native C4 logic emits the accepted event;
the adapter observes it once to start the detonator animation. The native script
continues to own the explosion, failure state, slow motion, C4 recovery, and
weapon restoration.

While native C4 owns weapon selection, stowing another firearm must not request
an empty weapon or switch away from C4. Independent personal-weapon firing is
suspended for this session so the other hand cannot bypass the selected native
weapon. The restriction ends when the script recovers C4 and restores the prior
weapon. It does not alter climbing or scripted-body ownership.

## Model and animation

The presentation uses only the detonator material surface from
`h2_viewmodel_c4`; the explosive pack is not carried. The native animation
`h1_wpn_eqp_c4_detonator_fire` supplies the mechanical motion. Its 25 frames are
precomputed into immutable deformations at load time, preserving the rubber's
mixed bone weights and normals. Playback selects cached poses without modifying
shared assets, queued GPU buffers, or the VR camera transform.

The model is registered as one bounded resource batch and remains alive until
the native asset-unload barrier. Rendering uses the current shared hand skin
record. An unavailable model or invalid profile keeps the prop inactive rather
than substituting an unrelated asset.

## Diagnostics and acceptance

`vr_cliffhanger_status` reports the story phase, prop states, authorization,
selected native C4, hand ownership, resource readiness, observed native
detonations, and rejection reasons. Offline regressions cover authorization,
busy native state, held-trigger transitions, pause/tracking generations,
duplicate events, and independent pick ownership.

Headset acceptance should verify the native body and movement remain intact,
scripted arms transition without duplication, each pick follows its own hand,
and temporary props return or hide at the correct story transitions. For C4,
check early pickup, rejection before authorization, neutral-trigger rearming,
one accepted detonation, animation timing, and native recovery after pause,
death, checkpoint restore, and tracking loss. These checks do not establish
acceptance of the physical climbing mechanics.
