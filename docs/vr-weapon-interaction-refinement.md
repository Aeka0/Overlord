# Weapon interaction refinement

The consolidated [VR impact-release overview](vr-impact-release.md) lists all
current magazine-latch and receiver-button releases, including this increment.

The shared refinement rules use the existing hand-pose library, physical
magazine transactions, and support-grip policies. Model exports remain offline
authoring inputs; runtime profiles contain immutable coordinates and directions
only.

The [whole-magazine contact policy](vr-magazine-latch-contact.md) uses a complete
body enclosure instead of older top/bottom slabs. The consolidated overview
describes the two-release bullpup correction.

M82 now uses the complete shared index/pinky charging hooks on both hands. Its
old right-index override contained duplicate, nonexistent `j_leng*` bones and
is removed together with the unused wrist/contact data. Both styles remain
selectable and retain the M82's real exposed handle contact and full travel.

Dragunov and M14/M21 reduce charging-handle capture assistance from 11 to 6 cm
and reuse the AK receiver-side wrist boundary. The wrist must remain outside
the right receiver wall; angular assistance cannot reach through the magazine.
The boundary follows handle travel without moving the hardware or hand poses.
Seated-magazine bounds and the authored magazine grasp are regression inputs.

The following releases accept deliberate impacts from either end of a held
spare magazine. Direct physical extraction remains available.

| Weapon | Release location on the exported model | Strike direction in gun space |
| --- | --- | --- |
| UMP45 | Paddle behind the magazine | Forward |
| AUG | Release behind the magazine | Rearward and upward |
| TAR-21 | Lever ahead of the magazine | Forward and upward |
| F2000 | Recessed pad ahead of the magazine | Upward |
| FAMAS | `j_reload_trigger` ahead of the magazine | Rearward |

Each profile has two mesh-fitted end slabs, 2 cm deep, excluding cartridge
geometry. The latch radius is 2.5 cm, with 6 cm separation to rearm, at least
1.2 cm of directed approach and 0.15 m/s contact speed. Spawned overlaps, slow
pressure, reverse movement, tracking jumps and repeated samples cannot eject a
magazine. The spare remains held; chamber state and ammunition accounting use
the existing transaction path. Failed native writes consume the attempt until
a new separated approach. Cosmetic receiver variants inherit the same policy.

M203 host foregrips already share the ordinary rifle's authored positional
breakaway distance, but added a 100-degree retained palm gate. That angle gate
now applies only to acquisition intent, using the existing 75-degree threshold:
once support is held, wrist roll alone cannot release it. The host's distance,
tracking continuity, Grip release and ownership rules still apply. Changing to
the launcher firing grip requires a fresh acquisition; its tighter firing
contact and facing requirements remain unchanged. Shotgun and GP25 policies
retain their separate behavior.

Validation covers compiled both-hand M82 poses against the native glove/receiver,
precision handle and magazine contacts at both action-travel limits, all newly
enabled latch families/skins, both magazine ends, rotated guns, rejection paths,
and M203 wrist roll inside/outside the ordinary breakaway distance. Physical
reload, weapon-grip and underbarrel regressions pass. Offline model review and
synthetic tests do not establish headset ergonomics; in-game acceptance remains
pending for the six requested refinements.
