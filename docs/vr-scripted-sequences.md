# Scripted sequence policy

The shared sequence adapter publishes camera, body-arm, weapon-permission, and
action ownership while preserving native body animation. Free head observation
and controller-triggered actions are supported where the scene profile admits
them. Headset acceptance remains scenario-specific.

## Implemented candidate

[scripted body arms](vr-scripted-arms.md) adds an explicit body/hand
owner separate from camera, weapon suspension and native movement input.
Cliffhanger retains authored crouched rest, blends the real body arms toward
the controllers during the native stand-up animation, then independently
controls both arms. Root travel, camera bones and native climbing logic remain
unchanged. The shared binding/solver path is available for later scene adapters;
Endgame now uses this path during knife extraction and throw preparation;
see its separately scoped adapter below.

The ending adapter applies free looking,
relative authored yaw and attenuated physical head translation from shoreline
wakeup through rescue. Native arms own wakeup and subdual; empty VR arms own
the stabbed-player crawl/kick interval; the injured native body's arms follow
controllers during physical knife extraction and throw preparation. Original
animations take over for the final throw and rescue. Player storage/drop is
blocked while native inventory transitions remain intact. Direct later entries
share the phase policy; museum/credits modes remain excluded. This replaces the
earlier presentation-only policy that retained native arms throughout the fight.

[The Oilrig adapter](vr-oilrig-sequence.md) reuses the publication,
story-action and instruction path. Snapshots now distinguish body ownership,
camera policy, weapon suspension and movement/turning permissions.
Oilrig retains native-facing gaze during the guard approach, then locks to the
authored assassination camera until weapon recovery. Rappel keeps its free-head
policy. The [breach adapter](vr-breach-control.md) suspends VR weapons while
planting and releases them at native slow-motion combat, even before unlink.
It observes shared breach rigs on every map before the specific
adapters. Linked breach cameras use native yaw with tracked HMD orientation,
discarding native pitch/roll; they publish spatial coordinates during combat.

`gameplay/scripted_sequences` owns lifecycle/publication; `sequences/rappel`
owns only the af_caves native phase predicates and instruction keys. Each
server tick reads saved native flags, alive and linked predicates. No script
asset is modified and no mission flag is forced. Other maps and later linked
segments after the completed rappel do not enter the rappel-specific adapter.

- Hookup, descent, melee opportunity, execution and release retain native body
  presentation. Ordinary VR hand/weapon submission, manipulation and physical
  damage use the existing scripted-control boundary and stay suspended through
  the early native weapon-enable event. Inventory reconciliation continues.
- The head keeps a stable world heading established on entry; native animated
  translation continues. Full tracked rotation is composed independently of
  native linked-view clamps. Native scripted pitch/roll and authored yaw do not
  rotate the viewer. Exit restores the current heading to native commands and
  holds the prior frame until prediction reaches that command. Recenter is
  accounted for, including recenter immediately before exit.
- Hold either physical Trigger to brake during descent. At the melee opportunity,
  release and press a Trigger again to request native melee. The native weapon
  state and original script perform the assassination. An early rejected press
  can be retried; holding never repeats it. No physical strike is required.
- Shared button gates reject stale tracking, missing hand poses, pause/focus
  loss and input carried across phases or reference generations. Commands are
  appended at CreateCmd return before native command-history insertion, including
  scripted branches that skip normal locomotion. Ordinary VR movement is suspended.
- Action prompts use the shared [game text i18n catalog](game-text-i18n.md),
  currently English and Simplified Chinese, and native
  font measurement/shadow rendering. They are visible throughout the relevant action
  phase rather than waiting for the native delayed reminder. They do not reproduce
  the flat HUD background. A missing complete translation omits the custom
  instruction and retains the original game-language script reminder and its
  original timing. Exact authored text ranges reuse the existing native
  allocator/fingerprint registry and narrative canvas; arbitrary plain HUD text
  remains excluded. These rendering facilities are available in optimized and
  Debug builds. Since , exact native script-HUD ranges also admit plain
  hints across maps; see [estate and common hint handling](vr-estate-scripted-scenes.md).

`vr_sequence_status` prints phase, epoch, native time and observation failures and
saves `minidumps/overlord-sequence.txt`, including instruction range admission
counters. `vr_input_status` reports
`story_brake_commands` and `story_melee_commands`; these count requests, not accepted
script outcomes. The new observer uses the existing server scheduler and passes
value snapshots to camera/input/HUD consumers. It adds no polling thread or VM
access from rendering/input threads.

Offline verification: Debug x64 v142 client build; controller-input tests
(phase, rearming, retries, stale data, localization); stereo probe smoke
(actual head-pose bridge entry/exit); weapon-grip regressions; spatial-panel
tests (owned hint selection and existing WARP composition). Hardware acceptance
must still confirm both-eye free looking, one arm pair, localized prompts,
braking, controller-only assassination, checkpoint retry and exit continuity.

## Evidence

HMD testing identified constrained head rotation, missing instructions, keyboard E
being necessary to advance the assassination, and duplicated native/VR arms in
Just Like Old Times. The loaded level scripts identify the sequence as the
`af_caves` rappel.

The native method disassembly resolves `ismeleeing` to weapon states 14..16
and attack input to client fields `0xe918 | 0xe90c`, bit 0. The flat E edge
carried bit `0x4`; retained native text additionally shows command packing at
`0x1403CEE85` ORing `0x4` into `usercmd + 4`. The adapter submits this normal
command bit; it never writes weapon state 14..16 directly.

Times below are recorder elapsed time, not hardcoded sequence durations:

| Time (seconds) | Observed state |
| --- | --- |
| 29.66 | Raw player type byte changes to 1; weapon prohibition bit 0x80 set |
| 42.83 | Prohibition clears while type remains 1; weapon token becomes 59 |
| 42.98 | `h2_viewmodel_afgan_rappel_end_pullout_knife` sampled |
| 43.84-51.78 | `h2_viewmodel_afgan_rappel_end_idle` sampled |
| 52.00 onward | `h2_viewmodel_afgan_rappel_end_start_kill` sampled |
| 52.38 | Presented server weapon becomes zero during the continuing sequence |
| 61.42 | Type returns to 0; normal weapon presentation follows |

These animation ranges are sampled witnesses, not exact start/end events.
Weapon tokens and raw type bytes are evidence, not sufficient policy keys.

- `maps_af_caves_code.gsc`, `_id_BFDA` (line 493): native weapon restrictions,
  temporary `rappel_knife`, animated player rigs, linking, descent and cleanup.
- `_id_BCD3` (line 484) and `maps__utility.gsc::_id_C3A2` (line 8338):
  `playerlinktodelta` and angle resistance constrain the linked view.
- `maps_af_caves.gsc::_id_BE20` (line 1707): `rappel_end` enables weapons,
  tightens the view clamp to 8 degrees and schedules the melee instruction.
- `maps_af_caves_code.gsc` lines 619-626 and 772-780: descent polls native
  ADS/attack for braking and waits for `ismeleeing` to advance the kill.
- `maps_af_caves.gsc` lines 88-90 and
  `maps__utility_code.gsc::hintprint` (line 287): registered instructions use
  client HUD text with a background and their own lifetime predicates.

## Findings

The findings below describe the pre-fix implementation.

1. **Camera:** the native script intentionally constrains the linked view.
   `head_pose_bridge.cpp` currently subtracts recorded HMD yaw from native yaw
   before composing the latest pose, assuming native prediction retained that
   contribution. Script clamping/link replacement can invalidate that assumption
   and cancel head rotation. This is a source-supported mechanism consistent
   with the report; the capture cannot prove the precise HMD/render feedback
   path without those additional channels.
2. **Instructions:** these are script hint HUD elements, not dialogue subtitles.
   The current narrative selector admits subtitle/typewriter flags only. A
   semantic hint capture path is missing from that contract. Actual rejection
   flags and placement need a targeted HUD command capture during reproduction;
   this recording alone cannot distinguish rejection from off-view placement.
3. **Assassination:** the script polls native `ismeleeing`. Existing physical
   melee traces contacts and applies native damage/feedback; it does not start
   the stock melee state. Controller command buttons currently supply sprint
   and jump, with a separate jump notification bridge. Adding only a generic
   script notification would not satisfy this sequence's state predicate.
4. **Duplicate arms:** native weapon permission resumes before scripted body
   ownership ends. `native_scripted_control.hpp`, `empty_hands_native.cpp` and
   `hands/component.cpp` gate VR objects/posing on that permission. The capture
proves an early reopening of the gate; headset observation supplies the
   visual evidence. The capture does not identify every duplicated draw.
5. **Braking:** the descent explicitly accepts ADS/attack while normal weapons
   are disabled. Ordinary VR firing admission cannot serve as the story-input
   permission. Braking success was not established by this recording.

## Design constraints

Extend the existing scripted-control boundary with independent camera, body,
combat and story-action decisions. Do not replace it with one `in_cutscene`
boolean, a map-name-only rule, or an unconditional linked-player restriction.
Mounted guns and vehicles that allow firing require their existing policies.

- **Camera ownership:** native simulation owns the path and attachment; the
  viewer retains tracked head rotation and bounded room-scale displacement.
  During confirmed scripted camera ownership, compose HMD pose relative to a
  validated sequence anchor without assuming constrained native yaw contains
  the full HMD contribution. Keep scripted pitch/roll out of involuntary HMD
  rotation; define authored yaw behavior explicitly. Resolve the anchor from
  native ownership evidence, not arbitrary time or angle thresholds. Rebase
  coherently on entry, rig handoff and exit to avoid snapping or double yaw.
- **Body ownership:** a native scripted rig/viewmodel can own presentation while
  weapons are technically enabled. In that interval suppress additional VR
  hands/held objects and physical combat, retain native animation, and preserve
  carry identities. Restore selection according to final native inventory.
- **Story actions:** normalize brake, use, jump and scripted melee independently
  of ordinary combat. Each adapter declares the native receiver it requires:
  held button, command notification or native weapon state. Do not fake a
  keyboard key, directly complete a mission flag, or substitute melee damage
  for the state that the script awaits. Exact melee command admission still
  needs verification against the native weapon/input path.
- **Instruction presentation:** reuse native localized hint text, background,
  timing and removal, captured by source ownership rather than matching text
  or screen coordinates. Compose in the existing head-relative narrative
  canvas. Button labels must describe the admitted VR action; localization
  and native layout remain authoritative.
- **Transitions:** qualify state by level/player generation and native ownership.
  Checkpoint rollback, death, map unload, pause, tracking loss and recenter must
  invalidate pending actions and rearm safely. A held brake input must not
  become an assassination press or gunshot when the phase changes.

One bounded policy snapshot should be published at an existing game-thread
boundary. Rendering consumes immutable presentation decisions consistently for
both eyes; input and server damage retain their own final admission checks.
Do not query the script VM from render/input threads, install per-scene polling
threads, or add cross-thread waits.

## Takedown opening vehicle

The mission's native map is `favela`. Read-only loaded-script inspection shows
one passenger `player_rig`, linked to the car's passenger tag from initial entry
through the `getout` animation. `start_chase` precedes departure, while the
exit flag is cleared during the animation; neither is a release boundary.

The map adapter requires a living player linked to this exact rig. It selects
the existing seeded free-head camera, leaving HMD yaw/pitch/roll unrestricted.
The shared positional lease anchors the current physical head to the authored
camera and applies `vr_scriptedHeadScale` (default 0.10) with the existing 5 cm
limit. The authored camera continues following the vehicle and duck/getout
animation. Native unlink restores ordinary head motion; death, checkpoint
rollback and level lifecycle clear the lease through the shared paths.

This is camera ownership, not an extra weapon/input restriction. Native weapon
permission and linking retain control, including the existing right-stick
stance binding and the script's `go_crouch` notification. Sequence adapters now
publish their own weapon-suspension policy instead of deriving it solely from
the phase name.

`FAVELA_DUCK_HINT` and `FAVELA_DUCK_HINT_KEYBOARD` use the same localized VR
instruction: move the right stick down to duck. The operation is enclosed in
native yellow `^3` / restore `^7` markers. Identification uses the original
HUD label/text config-string key before binding substitution, so the
controller's glyph-only variant cannot match an unrelated control. Label and
value offsets and the native key reader are signature-checked. Fonts, timing,
visibility and the actual script interaction remain native.

All native language options have a translated operation label and car hint.
Regression coverage includes key identity, highlighting/fallback, camera-only
control ownership, unrelated rigs, entry, death/unlink and the existing bounded
head offset. Headset appearance and the complete car-to-foot transition still
need acceptance with the updated executable.

## Acceptance scope

The rappel adapter identifies hookup, descent, melee opportunity, kill
animation and release through saved native flags and linked/alive predicates.
Spawn/load and shutdown callbacks invalidate its lifecycle. A transient
`enableweapons`, zero weapon or rig replacement cannot terminate ownership by
itself. The general mechanism owns presentation and safety; the adapter owns
only sequence recognition and required actions.

The phase policy is exercised offline for early weapon enable, rig
handoff, stale state, checkpoint rollback, input held across phases and exit
inventory reconciliation. A subsequent HMD pass must verify free looking,
braking, visible instructions, controller-only assassination, exactly one arm
pair and smooth restoration. Recheck Team Player and a firing-capable mounted
or vehicle segment so the shared policy does not over-suppress them.
