# Player death in VR

The death adapter uses the common scripted-camera publication and native weapon
permission boundary. A lethal health edge or native dead movement type blocks
interactive weapons on both prediction and the server. The carry owner releases
both grips and clears manipulation/render leases without issuing a native drop,
removing ammunition, or changing the death animation. Checkpoint restore uses
the existing inventory/timeline reset. Ordinary scripted weapon suspension still
retains grips; death does not.

The original falling camera position and native body animation remain intact.
The shared `free_head_seeded` policy aligns heading at entry, then composes full
tracked yaw, pitch and roll independently of the authored camera constraints.
Death owns its physical-head translation baseline even when the player is not
linked to a script entity. `vr_scriptedHeadScale` retains its existing
setting/default and maximum displacement envelope. It scales physical HMD
translation, not the native camera animation. Native body arms remain visible;
controller-driven interactive arms are suppressed. Stick locomotion, stick turn
and world-use actions are disabled during death.

This reuses the camera ownership mechanism used by reviewed cinematic adapters.
It does not change the explicit native-camera policies of other scenes or infer
camera ownership from an arbitrary weapon prohibition.

## Native evidence

The player moved from health 100 / movement type 0 to health 0 / type 7.
Native server code at `0x1404AD7A1` checks player-state health at `+0x1ec`,
selects dead types 5/6 according to linking and promotes animated free death to
7 at `0x1404AD7FF`. This is distinct from weapon-disable bit `0x80`, which was
not set in this death. That explains why permission-only VR interaction survived.
The health layout is signature-checked before use; unavailable reads cannot
grant weapon permission.

The capture included the localized cooked-grenade reason and its follow-up
instruction with backend text flags zero. They come from `CGDeadQuote` (native
ownerdraw 97), also used for ordinary death quotes. Its verified call at
`0x14038AEE3` now records an exact allocator-owned narrative command range.
The original renderer runs once, preserving localization, wrapping, fonts,
alpha and timing. Other zero-flag HUD/menu text remains excluded. Existing
allocator fingerprints, reset invalidation and 250 ms expiry apply. Different
special-scene producers still require their own evidence; this capture does not
establish coverage of every mission failure screen.

## Native fullscreen blur

The native final display transform at `0x1407B0740` precedes a separate blur
stage (`0x1407A8411` calls `0x1407A73D0`). VR previously copied each eye after
the display transform, omitting that later stage. The captured view's `+0x234`
blur parameter rose from zero, with observed positive samples up to 20.6354.
These are discrete observations, not a measured maximum or replacement curve.

VR now invokes the verified native Gaussian/compose routine `0x1407B0550`
after each eye's display transform and before VR HUD composition. The native
enable flag, eligibility test, current radius, materials and target-24 blur
scratch remain authoritative. No synthetic death timer, threshold, blur shader,
GPU readback or GPU wait is added. Other native fullscreen blur requests retain
their original eligibility, with exactly two VR exclusions:

- Automatic injury blur from `maps/_gameskill::blurview` is suppressed at its
  `setblurforplayer` calls, including the delayed ramp back to zero. Death and
  every other script caller keep the original native method. The adapter binds
  this function's live bytecode range on level start and clears it on shutdown;
  it does not infer injury from health thresholds, radius or duration.
- The `CL_GetMenuBlurRadius` contribution is removed before the native frontend
  combines gameplay, HUD and menu radii. Gameplay and HUD contributions remain,
  including death/cinematic blur beneath a menu. The native pause/menu state and
  animation still run, and flatscreen rendering retains its complete sum.

There is no cinematic allowlist. Blood/red damage feedback, native depth of
field and local HUD backdrop blur keep their existing paths. Filtering before
view publication gives both eyes the same per-frame radius without renderer
reads of live script or menu state.

The two ping-pong targets retain their existing registry identities. A lazy,
device/extent-matched GPU backup preserves the raw HDR source; native blur reads
the encoded display target and writes to the other slot, the result is copied
back, then raw HDR is restored for the natural desktop tail. Each eye completes
this sequence before the other eye reuses scratch. A resource/signature failure
rejects the display operation instead of publishing a partially processed eye.
`vr_death_effect_status` reports applications, skips, failures and latest radius,
plus source-filter readiness, injury binding, excluded requests and menu radius;
`vr_sequence_status` and `vr_narrative_status` expose camera and text state.

## Acceptance

Checkpoint restoration also preserves VR FX definition identity. World-space
shell graphs and followed effects use bounded presentation names derived from
the native asset name (plus the attached-element mask for followed effects).
The save adapter adds those descriptors to the existing native name/old-pointer
dictionary before its worker streams are allocated. Restore reconstructs the
same private presentation from the current native assets, including child FX;
shared assets, native collision, physics and timing remain unchanged.

Older checkpoints can contain private pointers without a dictionary entry.
After native remapping and before particle reads, only an empty, unbound root
tail with the observed single-reference status may enter recovery. The original
particle reader runs first; native reference release and garbage collection then
retire that tail. Unmapped effects still owning particles or other references
reject the checkpoint through the native recoverable error path. No definition
is guessed and no save file is rewritten. `vr_weapon_fx_status` reports adapter
readiness, registered/saved/restored variants, retired empty tails and rejections.
The native checkpoint reader, death retry and resumed FX presentation still need
live desktop/HMD acceptance after deployment.

Offline validation covers death classification, both-grip revocation without
inventory loss, unchanged-selection resume, shared free-head/translation
behavior, native text ownership and the stereo source contract. WARP executes
the production GPU backup/commit/restore helper with distinct eye colors,
resized resources, rejected draws, exceptions and aliased targets. Debug and
RelWithDebInfo clients build; controller-input, weapon-grip, spatial-panel and
engine-stereo-probe-smoke regressions pass. The client/PDB feature audit also
checks that death text ownership and fullscreen blur are emitted in both builds.
The exclusion policy tests cover flat-game passthrough, injury pulse/clear,
different script callers, invalid ranges and simultaneous gameplay/HUD/menu
contributions. This filter change passes Debug/RelWithDebInfo client builds,
spatial-panel tests and engine-stereo-probe-smoke; live injury/menu acceptance
remains pending. Native GPU routine execution and perceived comfort still require
HMD acceptance: both-eye
blur, sharp quotes/hints, free rotation while falling, hidden interactive arms,
trigger/grip held across retry, recenter, pause and checkpoint/level transitions.
