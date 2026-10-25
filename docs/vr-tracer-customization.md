# Tracer personalization

`vr_tracerStyle` is a saved choice in the launcher's Personalization page. Native
is the default. The presets change presentation only; native damage, penetration,
ammo consumption and collision remain authoritative.

| Preset | Saved value | Appearance |
| --- | --- | --- |
| Native | `native` | Unmodified native cadence and appearance |
| Blaze | `blaze` | Every shot, retaining the firing weapon's native colors |
| Cerulean | `cerulean` | Every shot, bright blue |
| Royal | `royal` | Every shot, gold |
| Anime | `anime` | Every shot, pink |
| 1776 | `independence_1776` | Red, blue, white, repeating once per shot |
| Rainbow | `rainbow` | Red, orange, yellow, green, blue, indigo, violet, repeating once per shot |

Each physical gun has its own cycle, including two copies of the same weapon.
Pellets and penetration events from one shot share its color. Changing presets
restarts the cycle; changing hands or reloading the same instance does not.
Already published shots retain their appearance. Invalid or absent settings use
Native. Short/occluded trajectories still obey the original visibility rules.
Projectile weapons such as rockets and grenade launchers retain their own effects.

## Native integration

`tracer_style.hpp` owns preset colors and cadence policy. `tracer_events.hpp`
owns the bounded, allocation-stamped event bridge. `native_tracer.cpp` adapts H2
without mutating shared weapon definitions, tracer assets or event fields.

The H2 bullet transaction at `0x1404AAD60` groups all temporary bullet events of
one shot. Independent fire, projected fire and underbarrel shotgun routes supply
the actual instance through `owner_scope`. Scripted vehicle fire supplies its
existing confirmed owner. Other native bullet routes are admitted only when the
source or attacker is the local player; unrelated NPC events are untouched.

`G_TempEntity` (`0x1405183E0`) supplies the native event allocation. On completion
of the bullet call, serialized identity and geometry are captured after the
engine has filled the event. The client's event dispatcher (`0x140373630`) reads
that state at `centity + 0x128`. Entity number, native reuse generation, weapon, source, event
type and serialized geometry prevent an unrelated event from borrowing a color.
The bridge has one slot per native world entity; reuse and world reset invalidate
old slots, and replayed deliveries cannot emit another tracer.
The native generation is the state field at `+0x78`, preserved and incremented
by `G_FreeEntity`; it distinguishes reused slots even with identical endpoints.

For owned impact events, the adapter keeps the native impact/audio work and uses
the event's actual start/end instead of recomputing a selected-weapon muzzle.
Direct tracer events also bypass native cadence. Both paths use the original
tracer creator and renderer. Only its two resource-query call sites receive a
call-scoped copied `TracerDef`; Blaze changes cadence only, and color presets
replace RGB while preserving all five alpha values, speed and geometry.
Colored presets use the separately registered `vr_tracer_neutral` material instead
of the original orange image. Native and Blaze retain the original material.
The original creator copies the appearance before the scoped definition expires.

## Neutral HDR material

`data/zone/h2_vr_tracers.ff` is loaded with the common client resources and included
by normal builds, deployment and packaging. The original `tracer#0` BC6H texture
contains a warm hue before vertex tint; multiplying preset RGB by it preserves
that hue and strongly reduces the blue channel. The neutral image retains all
seven mip levels of the 64x64 image and sets RGB to the original pixel's maximum RGB component.
It uses RGBA16F with the original opaque texture alpha; the native five-point alpha
curve still provides the fade. The original shader, material constants and blend
state remain unchanged, and native resources are not overwritten.

Optional rebuild: compile `vr-tracer-hdr-tool` with the existing `--with-vr-tests`
projects, then run `tools/build_tracer_zone.py --game-root GAME --tool ZONETOOL
--source-dump CODE_POST_GFX_EXPORT --decoder DECODER`. The decoder uses an isolated
WARP device to read the original linear HDR texels, without 8-bit/sRGB conversion.
The asset builder compiles, verifies and dumps the FF, then compares every decoded
neutral mip payload with the prepared data before promoting the packaged file.
Source exports and intermediate data remain local. This adds no texture readback
or color-conversion GPU pass to gameplay.

Hook signatures and native layout assertions guard admission. Partial hook setup
leaves adapters in passthrough mode. No gameplay mutex spans a native call. World
unload, shutdown, ammunition timeline changes and time rollback clear pending
appearances. Temporary events outside the owned shot invalidate reused slots too.

## Validation and diagnostics

`vr_tracer_status` reports native hook readiness, selected style, grouped shots,
published/matched events, submissions, rejections and capacity overruns.
It also reports missing neutral-material requests, which indicate an incomplete
installation of `h2_vr_tracers.ff`. Submission
means a call to the native creator, not proof that the light trace was visible.

Focused policy tests cover preset cycles, independent same-token guns, alpha
preservation, allocation reuse, wrong-source/geometry rejection, replay suppression
and multiple segments sharing a color. Existing launcher tests cover seven-option
serialization, numeric enum saves, defaults and all locale labels.

New-process game/HMD acceptance remains required: RPD Native/Blaze/cycling colors,
simultaneous hands, pellet/penetration segments, NPC isolation, mounted/scripted
vehicle fire, thermal view, and checkpoint/level transitions. The implementation's
offline checks do not establish those visual results.
