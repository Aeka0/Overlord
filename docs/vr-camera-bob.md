# VR movement camera bob

`vr_cameraBob` is a saved boolean, enabled by default. The launcher exposes it
under **VR Settings > Basics > Visual comfort** in English and Simplified Chinese.
It controls horizontal and vertical native movement bob for the local VR camera,
including firearms, empty hands and the body-mounted tactical knife. Console
changes apply on the next camera update. Headset tracking remains independent.

## Native source of the missing motion

The camera already evaluates native vertical and horizontal bob with its current
movement speed and phase. Both routines select prone (`ps + 0x114 == 11`), crouch
(`== 40`), sprint (`ps + 0x54 & 0x4000`) or standing amplitudes. They retain the
engine's ADS interpolation, speed scaling, waveform and amplitude limits.

When the current native weapon token (`ps + 0x3bc`, masked to 9 bits) is zero,
the routines multiply by separate weaponless dvars whose registered defaults
are zero. The body knife does not equip a native firearm, so it follows this
same path. With a firearm, native WeaponDef view factors are used instead.

The VR camera uses a neutral weaponless multiplier of 1 when that native value
is zero. Existing nonzero native tuning is preserved. Captured standard firearm
definitions also use view multipliers of 1. This is an amplitude correction;
it neither invents a second movement oscillator nor supplies a fake weapon.

## Boundaries and native contracts

`camera_bob.cpp` scopes the correction to the two local VR camera calls using
thread-local state. Other consumers of the native routines retain their original
results. The native dvars are never temporarily overwritten. Disabling the
setting returns zero at both camera calls, for armed and unarmed movement alike.

| Purpose | Address | Guarded original bytes |
| --- | --- | --- |
| Camera vertical call | `0x1403ADF80` | `E8 3B D4 2F 00` |
| Camera horizontal call | `0x1403ADF9D` | `E8 EE D2 2F 00` |
| Horizontal weaponless multiply | `0x1406AB363` | `F3 0F 59 70 10` |
| Vertical weaponless multiply | `0x1406AB49A` | `F3 0F 59 78 10` |

The entries of both native routines (`0x1406AB290`, `0x1406AB3C0`) are also
checked before installing any patch. A mismatch leaves every site untouched
and reports an error. Five-byte calls use near relays to reach generated code.
The two scalar sites require `utils::hook::create_preserving_near_jump`, which
uses `JMP [RIP+0]`. The generic `MOV RAX, target; JMP RAX` relay destroys the
native dvar pointer before bridge entry and must never be used at those sites.
This shared preserving helper also serves the stereo query-result observation.
The multiply bridge preserves volatile GPRs, flags, XMM0–5 and upper SIMD lanes;
only the original scalar amplitude changes. Stack alignment and shadow space
follow the Win64 ABI. Compile-time checks pin the actual WeaponDef view-factor
offsets to `0xae4` / `0xae8`; older struct offset comments are not authoritative.

`vr_camera_bob_status` reports installation, enabled state and call counters.
`empty_calls` includes the body knife while the native weapon token is zero.

## Verification and headset acceptance

- `vr-camera-bob-tests` executes generated bridges for both SIMD lanes against
  native MULSS baselines, with a deliberately hostile callback. It checks GPRs,
  flags, SIMD lanes, stack alignment, zero fallback and nonzero native tuning.
  The regression also executes the complete patched rel32 CALL -> preserving
  near relay -> generated bridge route, rather than testing bridge entry alone.
- Launcher C++ and JavaScript suites check defaults, saving off, restoring on,
  invalid inputs, localization and preservation of unrelated config/bindings.
- Retained native disassembly confirms the shared sprint/crouch/prone branches;
  these are not recreated in tests or application code.

Headset acceptance: with the option on, compare empty hands, knife-only and a
firearm while walking, sprinting, crouch-walking and crawling. Check stationary
recovery and stance transitions. With it off, all movement bob should disappear
while physical head translation/rotation still work. Reopen the launcher and
confirm the saved choice. Automated tests do not establish perceived comfort
or live installation of the native hooks; those require this game check.
