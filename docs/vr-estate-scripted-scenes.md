# Estate scripted scenes and native hints

## Follow-up: transient viewmodels, hint wording and entry heading

 headset feedback identified transient flat firearm hands at mine
explosions, breach entry/exit, and DSM recovery. The independent-hand adapter
previously re-enabled every native submission when scripts prohibited weapons;
its normal path also fell back when the VR owner/resources were unavailable.
Native DObjs can outlive the selected weapon: a read-only witness has selected
weapon zero while the native viewmodel still contains the woodland AK and GP25.

The native submission boundary now identifies ordinary personal firearm models
against registered native definitions, including composite children. It suppresses
those legacy submissions whenever VR independent hands are requested, across
permission and resource transitions. Native calculation/animation still runs;
separate bomb/knife props and world-body animation are retained. MOD-owned
submissions use their existing route. Positive model classifications are bounded
and cleared at zone unload; unpopulated registry slots and model reads are checked.
Native H2 class values are zero-based, unlike the legacy shared class enum.

Mine-avoidance text matches the complete native control-binding templates,
limited to the estate hint keys, and uses the shared EN/ZH-CN catalog. The new
instruction names Right Stick Down in the active language and wraps the operation in native
`^3` yellow and `^7` restore markers. Other Ctrl instructions are unchanged.
Native hint lifetime, fade and visibility still determine when it appears.

The captured DSM rate/count/time labels contain numeric placeholders such as
`&&1Mb/s`, `&&1/2067`, and a localized numeric-minute token. Matching now recognizes a numeric replacement
inside that template, so the resolved rate value joins the whole HUD element
on the lower/nearer progress plane. This does not alter download progression.

Estate's noninteractive ending selects the centralized `estate_ending` policy:
align once to the original `worldbody/tag_player` camera, then add its authored
yaw while retaining free head observation. This avoids seeding Shepherd's shot
from rear-facing drag/clamp angles. A new native body aligns again within the
same story epoch. Playable dragging uses native weapon permission independently
of the selected weapon; stowing or exchanging a gun cannot enter the execution
camera. Translation retains the existing 10% rule. See the
[camera architecture](vr-camera-architecture.md).

 local candidate. Debug and RelWithDebInfo builds and targeted offline
tests pass. No deployment or post-fix headset acceptance is implied.

## Camera position and execution look

Linked player cameras have a separate positional epoch, keyed by linked entity
and native timeline. The current physical head position is anchored at the
native camera on entry. Later physical translation is multiplied by
`vr_scriptedHeadScale` (saved, default 0.10, range 0–0.25), with a 5 cm spherical
limit. Recenter creates a fresh positional baseline. Unlink returns ordinary
1:1 translation. Stereo eye separation is unchanged. The tracking-frame origin
is translated consistently so hand poses remain aligned relative to the head.

Position ownership does not imply weapon suspension or rotation ownership.
The existing breach yaw/free-head/full-native rotation policies are retained.
Estate's linked `worldbody` during `play_ending_sequence` selects native combat
permission separately: playable Ghost dragging keeps the normal angle policy;
the noninteractive ending uses aligned free-head look with additive native yaw,
including Shepherd's execution and the body-toss sequence. This overrides native view clamps only
for the rendered scripted look and uses the existing command-heading handback.

## Scripted arms and retained weapons

The estate scripts create `viewbody_tf141_forest` separately from the normal
weapon viewmodel. Once dragging permits gunfire, only the linked body's two
shoulder subtrees are omitted from its skin packets. The existing immutable
skinned-surface partitioner now supports a pair of roots. No shared XModel,
native skeleton pose, game script or actor animation is edited. Other entities
using that model are outside the binding. Full body presentation returns when
the playable stage ends.

The shared carry release transaction accepts a drop permission. During playable
dragging, releasing the final holding hand in empty space retains that hand's
weapon and consumes the release. It never queues a drop for the end of the
scene. Support-hand release, transfers and explicit holstering retain normal
rules. Retention occurs before mechanical transfer preparation and native drop
calls, so relaxing Grip cannot interrupt a reload merely to discard the gun.
The same policy is now used by the
[Gulag helicopter adapter](vr-gulag-weapons.md#helicopter-release-policy), which
matches the opening helicopter's native linked view controller.

## Native hints and DSM progress depth

The estate bouncing-mine instruction is registered through the common
`hintprint` path (`ESTATE_LEARN_PRONE` and control-specific variants). It creates
a native client HUD font string, including its original visibility/break/fade
conditions. Those ordinary text commands were excluded by the prior
subtitle/typewriter-only selector.

The native `CG_DrawHudElem` boundary at `0x14037B080`, and its label/value calls
at `0x14037B202` and `0x14037B24B` to `0x14037AE70`, now record exact native
allocator ranges through the existing waypoint ownership registry. Signatures
are checked before installation. The font component's existing patch inside
the text renderer remains intact. Only text from those owned ranges gains
narrative admission; arbitrary menu, ammo and pickup text does not gain it by
style number. This works across maps without copying hint strings or changing
script conditions. Approved action text is adapted separately through the
[shared HUD prompt registry](game-text-i18n.md). The three mine-hint keys now
select `mine_prone` by their original config-string identity, without matching
translated sentences or retaining language-specific hint caches. The native
hint lifetime, wrapping and fades remain authoritative.

DSM elements are identified from complete native localized labels while estate
download progress is active. Their label and changing number are grouped as
one HUD element, not classified separately. They receive an independent
transparent capture lease and a 1.2 m head-relative plane, lowered by 20% of its
canvas height. Other narrative text stays at 2 m. Both eyes share the same
capture and placement; the original full-field fade composites last and covers
both planes. Empty captures, scope failure and allocator reuse clear ownership
and stale ink. No framebuffer copy or GPU readback is added.

## Validation

Targeted checks cover positional entry/recenter/unlink, 10% gain and movement
limit, camera/hand coordinate agreement, estate phase policy, retained release
and ordinary later dropping, both-arm triangle partitioning, native plain hint
admission, UTF-8 progress labels, numeric label/value grouping and distinct HUD
depth. Existing WARP composition/fade, native ABI bridges, weapon grip,
controller-input and stereo smoke suites pass. Both finished binaries are
audited for the functional HUD and scripted-body entry points.

Headset verification remains necessary for the mine hint, DSM text placement,
camera/body alignment, Ghost dragging with gunfire and relaxed Grip, and free
look during Shepherd's execution.
