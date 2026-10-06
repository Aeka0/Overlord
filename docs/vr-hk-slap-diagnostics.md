# HK slap diagnostics

This optional view observes the MP5K, AUG and UMP manual handle catches. It
does not change slap thresholds, tracking admission or ammunition transactions.
The view itself does not affect gesture behavior. Captured failures can be
checked against the motion rules below; HMD acceptance still needs a retest.

## Commands

- Enable **VR Settings > Debug > Handle slap geometry**, save and restart.
  The saved `vr_hkSlapDebug` selection defaults off and works in optimized builds
  as well as Debug. Console edits take effect at the next launch.
- `vr_hkSlap_status`: print the latest pose age, exact profile thresholds, current
  simulation observation and last contact, including all measured gates.
- `vr_hkSlap_dump`: save that status and up to 128 pose-history observations to
  `overlord-hk-slap.txt` in the game's working directory. Repeated simulation
  sequences are omitted. This replaces the previous diagnostic dump.

Opening the console can pause updates. `LAST` persists until the next contact
or interaction reset, so a fast failed attempt remains readable afterward.
Status/dump include sample ages, input sequences, weapon, instance and reference
generation. They copy existing mod data; neither traverses native scene pointers
nor writes ammunition. The file is written only on explicit dump, never per frame.

## Reading the view

| Mark | Meaning |
| --- | --- |
| White glove skeleton and tip/palm crosses | Current raw-wrist contact shape used by the actual gesture producer: 15 joints, 5 tips and palm |
| Blue glove skeleton | Final rendered glove after manipulation poses; displacement from white reveals visual-versus-input differences |
| Cyan sphere | Actual swept-contact radius around the raised handle tab |
| Faint yellow sphere | Rearm distance: every contact point must leave this sphere before another approach can be armed |
| Green cone above the tab | Allowed incoming motion directions from the actual cosine limit; no wrist-orientation requirement |
| Green/magenta arrow | Sampled movement direction, green when its direction gate passes |
| Orange/green segment and cross | Last contact's consumed movement and point, green only after the native transaction succeeds |
| `NOW ...` | Most recent simulation verdict, not a verdict on today's white glove |
| `LAST ...` | Most recent contact verdict, retained after the hand leaves or the handle closes |

The sampled path and direction are expressed in the current gun frame for
comparison; they are historical observations, not a trail fixed in world space.
With no contact yet, the arrow may show the latest examined movement. Successful
geometry alone is not a successful reload. The full status separates geometric
hit, raw world-wrist speed and native compare/write result.

## Useful verdicts

| Verdict | Interpretation |
| --- | --- |
| `NOT LATCHED` | Handle is not mechanically caught open |
| `HAND BUSY` / `SUPPORT HELD` / `TRIGGER HELD` | A manipulation, support grip or offhand trigger excludes a bare-hand slap |
| `FIRST SAMPLE` / `SAMPLE GAP` / `INVALID TRACKING` | No valid continuous pair of samples is available |
| `CONTACT JUMP` | At least one contact exceeded the per-sample step limit; status identifies the point |
| `NEED SEPARATION` | The approach was not armed; move the entire white hand outside yellow first |
| `OUTSIDE TARGET` | Examined segment missed the cyan sphere |
| `CONTACT TOO SLOW` / `WRONG DIRECTION` / `TRAVEL TOO SHORT` / `FROM BELOW` | The corresponding motion gate rejected the examined contact |
| `WRIST TOO SLOW` / `WRIST TOO FAST` | Relative contact passed, but raw world-wrist speed failed the motion/tracking guard |
| `WRITE REJECTED` | Gesture passed; authoritative transaction did not commit |
| `ACCEPTED` | Slap transaction committed |

All measured gates are exported, even when more than one fails. On a successful
hit the reported point is an actual winning contact. Otherwise measurements use
the nearest swept contact, while continuity and rearming still consider all 21.
Indices 0–14 are thumb/index/middle/ring/pinky joints in groups of three; 15–19
are their fingertips; 20 is palm. Missing measurements are labeled unexamined.

## Sampling and travel

For continuous samples up to 150 ms apart, HK contact continuity allows the
larger of the base 25 cm step and `max_slap_speed * dt`. The independent world
wrist speed check still requires 0.5–8 m/s. Longer gaps reset the approach.
The export distinguishes `base_max_step_cm`, measured `max_step_cm` and the
per-observation `allowed_step_cm`; `jump_point` identifies an offending contact.

The  MP5K dump captured a rejected 68.47 ms interval: the maximum
contact step was 31.48 cm and wrist speed was 2.07 m/s. Direction, travel and
radius passed, but the former fixed 25 cm continuity limit rejected the sample.
The elapsed-time allowance for that interval is now 54.78 cm.

HK travel uses `travel_origin=approach_peak`: while an approach is armed, each
contact remembers its furthest position opposite the slap direction. Lifting
from below and then slapping measures the downward travel from that peak.
It does not sum oscillations or rearm a consumed hit. The below-to-above travel
issue is covered by a synthetic regression; the captured dump above established
the continuity failure, not a separate `TRAVEL TOO SHORT` event. AK magazine
strikes retain their existing rearm-position origin and fixed continuity rule.

## Validation and capture

Enable the view, manually catch the handle, release all offhand controls, move
the whole white hand outside yellow and perform a normal slap. After a failure,
note `LAST` and run `vr_hkSlap_dump`. Compare a successful attempt using another
dump when useful. Do not retune from an isolated apparent overlap alone.

Deterministic tests compare diagnostic-on/off gesture behavior, individual
rejection causes, successful versus rejected transactions, retained contact and
duplicate input. Geometry tests cover real profile radii, complete label bounds,
eye disparity, current scene placement, stale/foreign epoch rejection and bounded
history. D3D11 WARP tests validate line/outline drawing and native state restoration.
An offline preview uses projected production geometry. None substitutes for
HMD alignment, readability or a captured intermittent failure.

The existing magazine-well view remains available independently through
`vr_reloadWellDebug`; see [physical reload diagnostics](vr-reload-diagnostics.md).
