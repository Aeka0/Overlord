# Weapon recoil and single-hand penalties

Checked against the implementation. This guide describes the
current VR recoil model, its tuning, and its integration boundaries. The source
constants and tuning table are authoritative; update this guide when changing
them. The current multipliers reflect headset calibration. They do not establish acceptance of every weapon,
attachment, grip transition, or extreme pose.

## Behavior and configuration

An admitted shot adds upward pitch to that physical weapon's recoil state.
Automatic fire accumulates pitch, and the gun returns toward the tracked aim
over time. The firing wrist and fingers pitch with the receiver. An attached
support hand follows the foregrip through arm IK. The HMD and raw controller
poses are unchanged.

The launcher exposes these saved dvars under **VR Settings > Gameplay > Recoil**:

| Dvar | Default | Meaning |
| --- | --- | --- |
| `vr_recoil` | `1` | Enables muzzle and hand recoil. `0` stops recording new impulses and suppresses their presentation. |
| `vr_recoilPenalty` | `long` | `all`: apply the single-hand penalty to every admitted weapon; `long`: apply it only to long weapons; `off`: disable the penalty while retaining ordinary recoil. |

The native enum indices are `all=0`, `long=1`, and `off=2`. The launcher reads
both the text values and numeric enum values written by the engine. Its save
payload uses the text enum and a Boolean for `vr_recoil`.

The penalty is **4x** when the selected policy applies, the player is standing
or crouched, and the firing weapon has no support hand. Prone always suspends
the single-hand penalty, including in `all` mode. The shot adapter checks `owner.support != hand::none`;
pressing the other grip button, merely placing that hand near the gun, or
holding a second weapon does not establish support ownership. Support is
evaluated for each shot. Acquiring or releasing support afterward does not
rescale impulses already stored.

Each shot uses the actual native movement posture: standing strength is 3x,
crouched strength is 2x, and prone strength is 1x. Both firing routes snapshot
the emitting player's `playerState_s::pm_flags` immediately before native
dispatch and pass the decoded posture to `confirmed_shot` after emission.
They reuse `controller_input::native_posture` from
[controller_stance.hpp](../src/client/component/vr/gameplay/controller_stance.hpp):
bit 0 means prone, bit 1 means crouched, and neither means standing. Prone takes
precedence if both bits are present. This samples accepted movement state,
not a pending stance input request or physical headset height. Unknown posture
uses standing strength and does not waive the penalty. See also
[posture controls](vr-stance-controls.md).

Posture affects new impulses. Changing posture afterward lets earlier recoil
settle normally without rescaling it; standing/crouching restores the configured
penalty for subsequent shots without changing the saved penalty mode.

## Calculation and units

The adapter reads `WeaponDef` for `owner.weapon & 0x1ff`. It first converts
`hipGunKickPitchMin` / `hipGunKickPitchMax`. If that conversion returns zero,
it tries `hipViewKickPitchMin` / `hipViewKickPitchMax`. If both return zero,
the shot contributes no recoil.

These native values represent pitch velocity. They are **not angles** and
their names do not guarantee ascending endpoints. Valid observed examples
include M4 `-10/-15`, AK47 `5/-15`, MP5K `35/40`, M1887 `50/60`, Dragunov
`80/85`, and M9 / Desert Eagle `-30/-35`. Rejecting values above 30 or rejecting
descending ranges previously suppressed recoil on most weapons. M9 happened
to survive through its view-kick fallback.

`native_pitch` computes a deterministic expected absolute speed for the
uniform range. It accepts either endpoint order and maps both native pitch
signs to upward VR climb. For sorted endpoints `lo` and `hi`:

```text
if lo < 0 < hi:
    speed = (lo*lo + hi*hi) / (2 * (hi - lo))
else:
    speed = (abs(lo) + abs(hi)) / 2

posture_strength = 3 if standing, 2 if crouched, 1 if prone
base_degrees = min(speed * 0.05 seconds, 8 degrees) * posture_strength
penalty_scale = 1 if prone, otherwise the selected grip/weapon penalty
shot_degrees = base_degrees * weapon_scale * penalty_scale
```

Non-finite endpoints or endpoint magnitudes above 10,000 return zero. The
50 ms conversion and posture strengths are VR tuning constants. This is
an adaptation of native weapon data, not a reproduction of the complete
native recoil simulation: it uses hip values, has no per-shot random sample
or horizontal recoil, and does not implement ADS interpolation or native
kick acceleration/reduced-kick sequences.

Based on the performance of most weapons, a threefold increase is a fairly
reasonable baseline. Taking into account the original game's stance-based
recoil modifiers, this can be mapped quite naturally to three times forstanding,
two times for crouching, and one time for prone. From there, each weapon can
be fine-tuned based on its actual in-game performance.

Adjustments within a range of several times the original value should be
considered reasonable and representative of the intended behavior, rather
than exaggerated effects. Crouched/prone use the same family scale with 2x/1x base
strength. The 4x penalty is applied afterward only while standing or crouched.
New requested scales replace previous table values rather than multiplying the
previous tuning.

### Accumulation and recovery

The state stores pitch in degrees. At a new shot it decays the previous
value to that shot's timestamp, adds the new impulse, and clamps the result:

```text
previous = stored_degrees * exp(-elapsed_seconds / 0.22)
stored_degrees = min(previous + shot_degrees, 55 degrees)
```

Reads use the same exponential recovery without advancing or mutating the
state. A timestamp before the stored shot returns zero; after two seconds
without a shot the result is exactly zero. Timing uses the controller's steady
clock, so recovery follows elapsed wall time rather than simulation time.

The **55-degree** cap applies to the accumulated pose and is also enforced by
the pose adapter. It can reduce the visible ratio of large impulses. For
example, Desert Eagle's captured `-30/-35` range produces `4.875` degrees
after the standing 3x baseline, `24.375` degrees after its 5x family scale, and `97.5`
degrees with the `all` single-hand penalty. That last result displays at the
55-degree cap. In `long` mode the pistol has no penalty. While prone, the same
weapon contributes `1.625 * 1 * 5 = 8.125` degrees with no single-hand penalty.

## Weapon tuning and length policy

[weapon_recoil_tuning.hpp](../src/client/component/vr/gameplay/weapon_recoil_tuning.hpp)
centralizes native family names, scales, optional native-class guards, and
short-weapon overrides. Unlisted families use scale `1.0`.

The recoil values of the original weapons were designed with flat-screen gameplay
in mind. After introducing our damping system, the rate of fire has a significant
impact on how recoil behaves. In general, weapons with extremely high rates of fire
(such as SMGs) need to have their recoil reduced substantially compared to the
original values, while slower-firing weapons (such as semi-automatics) need their
recoil increased.

| Weapon | Native family / aliases | Family scale (standing 3x baseline) | Short override |
| --- | --- | --- | --- |
| Desert Eagle | `deserteagle` | 5.0 | No; native pistol class already excludes it from `long` |
| .44 Magnum | `coltanaconda` | 5.0 | No; native pistol class already excludes it from `long` |
| USP .45 | `usp` | 0.6 | No |
| M4 | `m4`, `m4m203` | 0.5 | No |
| ACR | `masada` | 0.5 | No |
| AK47 | `ak47` | 1.5 | No |
| M14 EBR | `m14`, `m21`, `m14ebr` | 0.5 | No |
| TMP | `tmp` | 0.3 | **Yes** |
| PP2000 | `pp2000` | 0.4 | **Yes** |
| Mini Uzi | `uzi` | 1.0 | **Yes** |
| P90 | `p90` | 0.5 | No |
| MP5K | `mp5` | 0.25 | No |
| Vector | `kriss` | 0.25 | No |
| FAL | `fal` | 2.0 | No |
| SCAR-H | `scar_h` | 1.2 | No |
| F2000 | `fn2000` | 1.5 | No |
| FAMAS | `famas` | 1.2 | No |
| TAR-21 | `tavor` | 0.75 | No |
| UMP45 | `ump45` | 1.2 | No |
| M93R | `beretta393` | 0.5 | No |
| AA-12 | `aa12` | 3.0 | No |

Family matching reuses
[native_weapon_family.hpp](../src/client/component/vr/gameplay/native_weapon_family.hpp):
an exact stem or a nonempty underscore suffix, lowercase letters/digits/
underscores, and fewer than 64 characters. Thus attachment, skin, and akimbo
variants inherit the same tuning, while unrelated prefix matches do not.
This is a recoil policy lookup, not authority to admit a new weapon assembly
or manipulate its ammunition.

Rifle entries require native class `0`, and AA-12 requires class `4`.
These guards prevent alternate feeds such as `fal_shotgun_attach` or
`scar_h_m203` from inheriting their host rifle's scale. M4's `m4m203` rifle
alias is distinct from its `m203_m4` launcher feed.

The long-weapon policy uses verified **H2 runtime IDs**:

| H2 class | Category | Default `long` penalty eligibility |
| --- | --- | --- |
| 0 | Rifle | Yes |
| 1 | Sniper rifle | Yes |
| 2 | Machine gun | Yes |
| 3 | SMG | Yes |
| 4 | Shotgun | Yes |
| 5 | Pistol | No |
| 6 | Grenade launcher | Yes |
| 7 | RPG / rocket launcher | Yes |
| Other | Other categories | No |

Do not substitute the labels in the shared `game::weapClass_t` enum: those
labels originate from another engine version and do not match these IDs.
The policy runs only after the firing route admits the weapon; this table
does not enable throwable equipment or unsupported firing routes.

TMP, Mini Uzi, and PP2000 explicitly override the native category to **short**.
The override includes their variants and applies only to `long` mode.
`all` gives them the 4x penalty when unsupported and standing/crouched; `off` and an actual
support grip give a penalty factor of 1. P90 and MP5K remain long weapons.
This flag is specific to recoil and does not change holster or reload rules.

## Shot, state, and pose integration

There are two current production callers of `recoil::confirmed_shot`:

1. [weapon_interaction.cpp](../src/client/component/vr/gameplay/weapon_interaction.cpp)
   calls it after the admitted local native `G_FireWeapon` dispatch returns.
   Ownership, gameplay/tracking freshness, delivery, and muzzle-clearance
   gates run first. Independent firing suppresses this native local route.
2. [independent_fire_runtime.cpp](../src/client/component/vr/gameplay/independent_fire_runtime.cpp)
   calls it only when `native_ballistics::fire_owned` returns `emitted`,
   using that instance's owner and controller reference generation.

Record recoil once per emitted shot, not once per trigger press, shotgun
pellet, penetration segment, haptic event, or rendered eye. The current shot's
geometry has already been committed before the impulse is recorded.

The separate [underbarrel runtime](../src/client/component/vr/gameplay/underbarrel_runtime.cpp)
currently dispatches through its own `native::fire` path and does not call
`confirmed_shot`. Its feedback event alone does not apply this recoil model.
If extending recoil to that route, resolve the discharged module's data while
retaining chassis/hand ownership and ensure that the shot is counted once.

### State ownership and threading

The bounded state array in
[weapon_recoil.hpp](../src/client/component/vr/gameplay/weapon_recoil.hpp)
has 16 entries and a mutex protecting short reads and writes. It keys each
entry by weapon identity (native token plus instance generation),
`rear_revision`, and tracking reference generation. New entries replace the
oldest shot entry when necessary. Native/legacy identities may have generation
zero; the adapter requires a valid firing owner rather than a nonzero physical
instance generation.

Different physical copies, regrips that change `rear_revision`, and tracking
reference changes do not inherit each other's impulses. Support-only changes
retain existing recoil. Shutdown clears the state. Disabling `vr_recoil`
suppresses presentation and new impulses; it does not explicitly clear a
recent stored impulse, which continues to expire by time.

### Wrist, fingers, and muzzle

[hands/component.cpp](../src/client/component/vr/gameplay/hands/component.cpp)
applies recoil after its normal grip solve and before publishing the weapon
scene, muzzle, and skinned pose. The profiled route supplies the actual solved
support hand; the generic route supplies no support hand.

`apply_to_pose` rotates about the firing wrist's solved position, around
the gun's local negative Y axis. The receiver and its weapon bones, muzzle,
firing wrist rotation, and wrist-descendant finger bones receive the same
rotation. The firing wrist's position remains the pivot; this adds wrist
pitch rather than an independent translation or movement of the HMD.

An attached support wrist receives the same rigid transform. The existing
`hands::solve_arms` routine then positions its shoulder/elbow chain. At
maximum reach, the wrist is corrected to the exact foregrip contact using
the same forearm-stretch policy as physical-part constraints. The free hand
or the other firing hand keeps its own pose.

Always apply the sampled angle to a freshly solved base pose. The angle is
the full remaining recoil, not a per-frame delta; applying it repeatedly to
already modified bones would accumulate recoil incorrectly. Preserve the
existing skeleton-epoch/scene publication boundaries when changing this path.
Both rendering and later muzzle consumers must observe the resulting pose.

## Extending and diagnosing the implementation

- Change weapon balance in `weapon_tunings`. Use native definition names,
  check aliases, and retain class guards for families with alternate feeds.
- Set `short_weapon` for an explicit recoil length exception. Do not change
  the native weapon class or infer support from a raw grip-button state.
- Change global conversion, strength, penalty, recovery, or cap constants in
  `weapon_recoil.hpp`. Keep launcher English/Chinese descriptions and this
  guide synchronized when the advertised penalty changes.
- Keep `WeaponDef` read-only. The adapter uses compile-time layout witnesses:
  class at byte 1488, hip gun-kick endpoints at 3196/3200, and hip view-kick
  endpoints at 3232/3236. Revalidate native data when changing the engine layout.

`vr_fire_status` reports `recoil`, the numeric `penalty` mode, and
`climb_degrees`, along with the selected owner/support and muzzle frame. It
also writes `minidumps/overlord-fire-latest.txt` relative to the game directory.
The angle is a current sample for the selected weapon, not a shot history;
zero after two seconds without firing is expected.

For a missing effect, follow the chain: saved enable flag, admitted firing
route, valid native endpoints/fallback, native family and class, firing owner
and reference identity, then pose application/publication. For a wrong penalty,
inspect the support lease and family override before altering recoil strength.
For apparently unchanged high values, check saturation at 55 degrees. Check
actual `pm_flags` at shot dispatch when a posture reduction or prone exception
appears incorrect; pending stance requests may be blocked by native movement.

## Validation and build workflow

The focused regression entry point is
[weapon_grip_tests.cpp](../tests/vr/weapon_grip_tests.cpp). It covers captured
native ranges, tuning aliases and guards, short-weapon policy in all modes,
instance/reference isolation, accumulation/recovery, posture strength/penalty
composition, and the 55-degree cap.
[recoil_pose_tests.hpp](../tests/vr/recoil_pose_tests.hpp) covers both firing
hands, supported/unsupported poses at 12 and 55 degrees plus oversized input, wrist/finger contact,
support-arm IK, free-hand isolation, and zero recoil.

For configuration or launcher changes, also run
[launcher_settings_tests.cpp](../tests/vr/launcher_settings_tests.cpp) and
[React launcher tests](../src/launcher-ui/tests/model.test.ts).
From the repository root in a configured Visual Studio build environment:

```bat
tools\premake5.exe vs2022 --with-vr-tests
msbuild build\vr-weapon-grip-tests.vcxproj /m:4 /p:Configuration=Debug /p:Platform=x64
build\bin\x64\Debug\vr-tests\weapon-grip\vr-weapon-grip-tests.exe
msbuild build\vr-launcher-settings-tests.vcxproj /m:4 /p:Configuration=Debug /p:Platform=x64
build\bin\x64\Debug\vr-tests\launcher-settings\vr-launcher-settings-tests.exe
npm --prefix src/launcher-ui test
```

The implementation was also built with installed VS2019 BuildTools using its
x64-hosted MSBuild and `/p:PlatformToolset=v142`,
`/p:PreferredToolArchitecture=x64`, and `/p:CL_MPCount=4`. Discover the installed
toolchain rather than assuming the first VS2022 MSBuild has C++ tools. Do not
store a developer machine's absolute toolchain or game path in source.

Follow [build and deployment](development.md#deployment-and-acceptance) for
matching Debug and RelWithDebInfo EXE/PDB pairs and deployment hashes. Offline
tests establish deterministic behavior and pose relationships. Headset checks
should cover single shots/automatic fire, each penalty mode, support
acquisition/release, left/right ownership, dual wield, compact exceptions,
high-recoil cap behavior, stand/crouch/prone transitions (including blocked
stance requests), prone penalty suspension, and recovery during ordinary weapon handling.
