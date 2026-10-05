# VR pickup and switch presentation

The shared pickup/switch presentation adapter is implemented and covered by
offline regressions. It suppresses native camera motion, sound notetracks, and
moving parts for admitted transitions while leaving controller-driven arms
under their existing owner. Verify each weapon profile in the headset.

## Shared boundary

`gameplay/native_equip_presentation.cpp` redirects five verified local client
presentation calls to the native action-to-XAnim-slot selector. For a reviewed
first-person pickup/putaway clip, use the current weapon's native idle selection
instead. The native idle selector retains its empty-idle choice. Do not assume
action IDs equal XAnim slot numbers or copy another weapon's idle asset.

This acts before consumers select animation curves/notetracks, rather than
only overwriting final arm transforms after native evaluation. Existing tracked
hand, physical-part and per-weapon rest-pose layers remain independent consumers.
No shared XAnimParts, animation assets, PS tables or gameplay state are modified.

Admission requires initialized local gameplay, enabled VR, the exact local PS,
matching current weapon identity, bounded valid animation slots/names, primary
side and non-alternate mode. Names must identify a first-person family and an
explicit equip suffix. Missing/unreviewed assets retain native behavior; NPCs,
scripted breach animations, firing, reloads, inspection, sprint and alternate-mode
transitions are not classified as ordinary weapon pickup/switch clips.

The current all-weapon boundary covers the existing primary/right-hand weapon
stage; it is not an implementation of dual-wield or arbitrary left-hand pickup.
It does not require a pistol adapter to admit a rifle or attachment variant.
Live M4/GL evidence includes idle slots 1/2 and equip slots 39/40/42/46/47/48/49;
slot 41 breach and slot 43 grenade-to-bullet are deliberately excluded. These
slot numbers are evidence, not a hardcoded per-M4 policy.

## Native safety and controls

The converter entry, right-hand PS animation-table instructions and all five
call targets are checked before patching. Only client callsites are redirected;
server converter calls, weapon selection, inventory, ammo and switch timers keep
native authority. Suppression therefore does not promise instant gameplay
switch completion or remove unrelated script-level pickup sounds.

The adapter executes the native selector first, preserves its resulting volatile
GPR/SIMD/flags, and allows the policy to replace only RAX. This preserves the
whole-program native caller contract beyond the ordinary Win64 ABI. Offline JIT
tests execute the exact emitted bridge against a synthetic six-argument native
callee and a deliberately hostile policy, checking stack arguments/alignment,
copied query fields and all native post-call outputs except the selected index.

The first deployed candidate failed during startup with a rel32 range error:
the default JIT allocator is not constrained to the native image's +/-2 GB
call range. The corrected boundary reuses `utils::hook::create_far_jump` (also
used by native TLS hooks) to create a near-image absolute relay. All five sites
are checked for relay reachability before any site is patched. Each original
CALL remains exactly five bytes; no adjacent instructions are overwritten.
The relay's RAX scratch is valid specifically at this bridge entry, which
initializes RAX before invoking the native selector; do not use this relay for
hooks that require preserving an incoming RAX value.

The regression now allocates synthetic sites at all five native addresses in
the test process and an explicit far entry over 2 GB away. The old direct calls
must reject without writing; real patched CALLs through the shared near relay
must execute the bridge and preserve the tested native argument/output contract.
These tests do not start the game or imply a successful HMD startup.

- `vr_suppressEquipAnimations 1`: saved default; suppress reviewed pickup/switch
  clips in admitted VR presentation. `0` restores native clip selection on the
  next selection; it does not rewind an already playing animation.
- `vr_equip_status`: reports hook installation, setting, idle remap count and
  rejected idle replacements. Counts prove selection, not HMD visual acceptance.

## HMD checks

1. Switch/pick up each pistol and a rifle/attachment variant repeatedly. Confirm
   no equip camera kick, equip foley or moving-part equip curves; controllers and
   normal firing/reload feedback should still work.
2. Include first pickup, quick switch and empty weapons. Check ready time and
   ammunition stay valid, even though the presentation uses idle.
3. Verify ordinary non-VR remains native. Use the setting to isolate this boundary
   if a weapon has unrelated animation/sound behavior.

No exported assets, local audit scripts or sound files are bundled.
