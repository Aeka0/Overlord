# Oilrig: opening, mission equipment and evacuation

The Oilrig sequence adapter retains native mission ownership while applying a
bounded underwater camera extension. The horizontal range is 45 degrees per
side; it does not enable unrestricted underwater head rotation. Headset
acceptance remains separate from offline validation.

## Native evidence

`maps/oilrig::_id_B9B8` binds the player with
`playerlinktodelta(..., 1, 55, 43, 5, 20, 1)`. The opening cinematic later calls
`lerpviewangleclamp(..., 55, 43, 22, 20)`. Both native methods read yaw limits at
argument indices 3/4 and pitch at 5/6. The first two yaw limits become 100/88;
pitch, view fraction, resistance and native interpolation timing retain their
original values. Authored parameters are adjusted once per setter invocation,
never repeatedly added to player state.

Signature-checked float-getter callsites:

- Shared player-link implementation: `0x1404FC856`, `0x1404FC892`, gentity in RSI.
- View-clamp interpolation: `0x1404E652C`, `0x1404E6548`, gentity in RBX.

Small ABI bridges pass that exact owner to the adapter and tail-call its normal
C++ ABI. Preserving near relays keep each native CALL five bytes long even when
JIT code lies outside rel32 range. All four sites are verified before any are
patched. The original `Scr_GetFloat` runs first, preserving argument errors.
The shared link helper's R14D mode is also forwarded and checked: only its
`playerlinktodelta` mode qualifies; sibling link/blend modes remain unchanged.
Only the local player, configured VR mode, oilrig map and confirmed underwater
intro flags admit the extension. There is no global float-getter hook, VM stack
mutation, timer reset or edited game script. After surfacing, ordinary later
setters retain their original bounds.

The assassination differs from rappel: `_id_C26C` waits for
`player_looking_at_grate_guard && meleebuttonpressed`, not `ismeleeing`.
The native looking flag already incorporates the proximity trigger and a
25-degree FOV test. `meleebuttonpressed` at `0x1404B4030` tests bit `0x4` in
client fields `0xe918 | 0xe90c`, matching the existing story action command.

## Shared implementation

The new `sequences/oilrig` adapter publishes its phase and capabilities through
the existing sequence service. Body ownership, free-camera ownership, movement,
turning, native-camera ownership, weapon suspension and optional instruction are separate snapshot fields. HUD consumers no
longer directly call the rappel-specific instruction selector.

| Phase | Camera | Movement and actions |
| --- | --- | --- |
| Underwater transport | Native linked view with widened horizontal range | Script owns translation; controller turning stays available |
| Surface approach | Native game-facing view, so gaze predicates remain meaningful | Controller swimming/turning retained; ordinary VR weapons suspended |
| Facing the nearby guard | Same native-facing view | Shared EN/ZH-CN melee prompt; fresh Trigger submits native bit 0x4 |
| Kill/climb-out animation | Full authored camera (position, pitch, yaw, roll), with stereo eyes | Native body; VR hands/weapons and movement suspended |
| Unlinked weapon recovery | Authored camera continues until native weapon permission returns | Walking retained so leaving the water trigger can complete; turning and VR weapons suspended |
| Weapon permission restored | Normal VR camera and controls resume | Native weapon availability remains authoritative |

The shared `story_melee` i18n key serves both levels. Oilrig now replaces
`SCRIPT_PLATFORM_OILRIG_HINT_STEALTH_KILL` in the original script HUD lifecycle;
its separate generated instruction entry has been removed. The existing
The localized instruction remains “Press Trigger to assassinate the guard.” Looking away removes the
action/prompt; entering the guard cone while holding Trigger requires a release
and new press. No guard flag, mission result or actor damage is forced.

The oilrig script blends to the kill rig half a second before setting
`player_starting_stealth_kill`. Once swimming is complete, the linked parent's
`animname == player_rig` identifies that native handoff. This starts body/camera
ownership without waiting for the delayed flag. The floating surface controller
is a `tag_origin` model and does not carry that animation identity.

The surface start point deliberately skips SDV attachment and must still work.
Conversely, the rig start point's `player_ready_to_be_helped_from_water` sentinel
alone cannot establish ownership of a later linked sequence. The post-kill
recovery additionally requires the native slowed-turn flag and weapon-disabled
bit; it does not turn every later weapon restriction into a camera lock. Death
and the existing spawn/load/shutdown lifecycle invalidate ownership.

The camera bridge follows native heading during this lock and restores a matching
command heading on exit. HMD rotation/translation do not alter the authored
centre camera; per-eye stereo remains active. See [breach control](vr-breach-control.md)
for the separate planting/combat boundary and slow-simulation input repair.

## Validation and limits

###  input feedback repair

HMD testing identified a visible instruction with no Trigger response. Read-only
matching-PDB diagnostics recorded 75 emitted story melee command requests and
zero sequence-observer failures. The exact failed interaction was not captured
frame by frame, so these counters establish admission, not native acceptance.

The old adapter emitted melee for one usercmd only. Oilrig's GSC checks every
50 ms; native client processing copies command buttons to `client+0xe90c`,
latches rising edges in `+0xe918`, and clears that latch at `0x1404AD059`.
That latch is not an acknowledgement by the waiting script. A short input can
therefore disappear between script polls.

Oilrig now selects the shared `polled` melee delivery mode: physical hold retains
the native held bit and a short tap stays visible for at least 100 ms. Rappel
keeps its state-transition press mode. Phase/gaze loss, pause/focus loss, invalid
tracking, action generation changes and checkpoint changes cancel the pending
signal immediately. No VM predicate, guard flag or weapon state is overwritten.
The regression simulates 90 Hz commands against all 50 millisecond offsets of
the native poll: the one-command implementation misses some offsets, while
polled delivery covers all. Hardware acceptance of this repair remains pending.

`vr_input_status` now labels the counter `story_melee_commands`, since continued
held delivery is not a count of distinct physical presses. `vr_sequence_status`
also reports `melee_delivery`.

- Client: Debug x64 v142 build passed.
- Controller tests: exact 100/88 bounds, untouched vertical limits, invalid and
  capped ranges, transport/surface/kill phases, guard admission, release-to-rearm,
  surface checkpoint and later-link exclusion; existing rappel tests pass.
- Rigid-part suite: both production bridge variants executed through near
  relays, checking owner/index, float return, nonvolatile registers and Win64
  stack alignment; existing bridge and WARP tests pass.
- Spatial-panel/i18n suite and `git diff --check` pass.

`vr_sequence_status` now reports scenario, movement/turn/free-camera/native-camera permissions, weapon suspension
and `oilrig_yaw_adapter`, `expanded_arguments`, `query_failures`. These counters
show adapter admission, not HMD visual acceptance.

After deployment, restart the underwater opening so the native setters execute.
An old save restored after those setters is not retroactively rewritten. Verify
horizontal reach, unchanged vertical behavior, water approach, gaze-dependent
prompt visibility, Trigger assassination, single arm pair and smooth climb-out
in both eyes. The running session was neither stopped nor patched by this work.

## Mission equipment and helicopter, 

Oilrig retains the native inventory lifecycle, while the VR abdominal slot and
physical carry use one shared admission rule. Before the native
`ambush_c4_triggered` flag, only `c4` is admitted; after confirmation, only
`claymore` is admitted. Missing flag evidence declines both mission items.
Firearms and other maps retain their existing rules. Every server observation
derives the stage from native flags, so checkpoints do not inherit a stale
adapter latch. The native detonation notification and mission outcome are not
generated by the adapter.

The physical remote reuses the existing C4 prop provider's exact
`h2_viewmodel_c4` detonator surface, authored hand poses, native mechanical
animation and `detonate` observer. Oilrig omits ice-pick resources. The remote
can be held before planting, but the adapter never gives native C4 early.
Shared C4 script `43691::_id_CC10` gives the item after planting, zeros its clip
and binds action slot 2. Grabbing the now-owned remote selects it through the
native weapon method. Detonation requires native planting completion, the
selected C4, weapon permission and an observed zero clip; reserve ammo is not
a readiness condition. Stowing restores physical carry selection when native
weapon permission allows it. Native script body/planting ownership stays intact.

`OILRIG_HINT_C4_SWITCH` instructs drawing the remote from the abdominal equipment
slot. `OILRIG_HINT_C4_DETONATE` names the holding hand's Trigger and explains the
post-detonation replacement with the claymore. These Chinese replacements use
the shared HUD registry and require the abdominal equipment adapter.

`_id_CC8C` creates `level.player.worldbody_rig`, stages its `escape_in` first
frame, then links the player to a temporary `script_origin` for 0.7 seconds
before linking to that exact body. `escape_littlebird_landed`, the body identity,
its `worldbody` animation identity, and native weapon prohibition bound the
temporary transition. The boarded flag cannot admit an unrelated parent.
The ride reuses `free_head_seeded`, shared `vr_scriptedHeadScale` (default 0.1),
and `retain_weapon`. Native boarding translation, disable/enableweapons and
the later `takeallweapons` / M14 give-and-switch remain authoritative.

M203 slow-consumer retention and the expanded weapon interaction audit are
recorded in [shared breach control](vr-breach-control.md). In-headset acceptance
still needs the single guard prompt, C4 draw/detonation and claymore replacement,
launcher firing grasp throughout slow motion, and helicopter boarding/free look
with weapon retention.
