# VR launcher bindings

These are native projectile weapons with an independent launcher capability;
they do not borrow a detachable-magazine, tube-shotgun or break-action feed.
Javelin shares the launcher binding with native ammunition and reload timing.
Its [independent display](vr-javelin-screen.md) replaces the
rejected physical-eyepiece experiment and uses `vr_javelinDisplay`.

The native H2 RPG pullout's settled frame 21 supplies its two grip poses. Its
source left hand occupies the forward firing grip; source right occupies the
rear support. The binding exchanges their roles with anatomical mirroring, so
either controlling hand uses the forward grip. Free-hand calibration remains
independent of that role change. AT4
and Stinger use their H2 idle poses. The RPG's separate two-bone rocket model is
admitted only at the receiver's `tag_clip`; unknown attachments and wrong
receiver cardinality are rejected. Pose headers record source animation hashes.

## Operation

- RPG: release the support hand, pinch at the waist to draw one rocket, move it
  to the front of the tube, and seat it. Contact covers the last 6 cm of the
  rocket tail against the receiver's actual muzzle ring, rather than comparing
  the rocket root with its fully mounted transform. A 65-degree angle tolerance
  and 7.5 cm radial tolerance admit natural insertion; a reversed rocket or a
  newly drawn overlap cannot load without a front approach. A committed load
  plays native `weap_rpg_insert_plr` feedback and an insertion pulse. Both hands
  use the same anatomical mirroring path. After seating,
  the firing trigger must be neutral before another shot.
- AT4 and Stinger: right hand controls, left hand supports; raise the weapon
  to request native ADS. Stinger retains the native lock-on workflow; campaign
  AT4 retains its native unguided fire. They have no
  physical or button reload. A spent physical instance stays spent even if a
  native ammo grant tries to credit its clip.
- Stinger ADS intent uses its circular tube sight, whose center is 12.61 cm
  left of the barrel axis in the native idle pose. The outside glass pane is
  not the aiming reference. The shared ADS corridor and dwell remain unchanged;
  the offset does not move the physical muzzle or rotate the firing/lock axes.
- AT4 support acquisition uses a 24 cm radius (34 cm release threshold), while
  the authored hand position and ordinary control-grip capture remain unchanged.
- Empty AT4/Stinger remain held; Javelin also stays held across its empty reload
  interval. Native empty-event weapon cycling and PM's empty-inventory removal
  are suppressed for these admitted local launchers. Manual world drop uses the existing carry transaction. Script
  removal, unrelated weapons, non-local actors and scripted control remain native.
- Javelin: the right hand controls its actual CLU grip and the left supports the
  opposite grip. Bringing the actual CLU eyepiece close to the eyes enters the
  independent display; moving away exits. Button entry/exit is temporarily
  disabled. A trigger held during entry must be released before firing.
  Firing, reload initiation, add-ammo, interruption and re-lock timing remain
  native. Solved hand/receiver parts stay in their idle grip during reload;
  the native animation timeline and its sound notetracks continue. There is no
  waist rocket or physical tube replacement. Native reserve remains finite;
  no VR ammunition is granted when it reaches zero. The adapter never writes
  the loaded clip, weapon state or timers to skip a reload.

Left-hand pickup/draw of AT4, Stinger or Javelin carries at the support grip and
cannot fire. Releasing the right grip never promotes the left support. RPG keeps
bilateral control ownership.

Read-only sampling found the held `javelin_dcburn` with one loaded
round and 49 reserve rounds: its descriptor has start/max ammo 50, capacity 1,
fireTime 1060 ms, reloadTime 3000 ms, reloadEmptyTime 2000 ms and reloadAddTime
2500 ms. These are descriptor values, not a replacement cooldown timer. The
native engine decides which timing applies. Its CLU script clears lock while
the clip is empty and uses native re-acquisition; immediate clip refill would
have bypassed that behavior. Reviewed aliases also include `javelin_noimpact`
and `javelin_estate_jeep`; mission-specific guidance/damage stays original.

RPG drawing reserves exactly one native reserve round. Seating transfers that
escrow to the clip; release, handover, tracking interruption and carry transfer
refund it under the existing no-discard-penalty policy. Failed compared writes
retain escrow instead of releasing the hand or duplicating ammunition. Native
server and prediction replays share the existing shot-history contract.

## Native boundaries

Javelin's native `tag_flash` is pitched about 10.3 degrees relative to `j_gun`.
The generic rig's five-degree admission rule therefore rejects it before profile
selection. Its reviewed muzzle contract validates the exact receiver/count,
direct root parent, receiver-local offset and complete bind orientation. It
preserves the original axis for pose and firing instead of relaxing all firearm
admission or straightening the muzzle. Regression uses the exported bind data
of the two exact models observed live and exercises the full admission chain.

The existing projectile parameter builder keeps native missile creation,
attacker, target, dispersion, guidance and ammunition consumption. The launcher
adapter supplies the physical muzzle through the existing firing path.

Target rectangle/circle tests use the shared native projection call at
`0x14051330A`. For an admitted guided launcher, its world target vector is
re-expressed in the native actor basis using the controller muzzle axes. The
original projection still owns FOV and rectangle/circle thresholds; no camera,
entity angles, target eligibility, lock timing or shared WeaponDef is changed.
Stale tracking rejects this local guided query rather than reverting to head aim.
The adapter changes projection only when native code requests it; a VR profile
flag cannot create target acquisition or missile guidance. The campaign AT4
definition observed in oilrig has `MISSILE_GUIDANCE_NONE`, and the loaded
Stinger lock script admits only `stinger`, so AT4 does not opt into this adapter.

Firing does not require ADS and does not automatically raise ADS on attack.
The aim-fire-mode getter at `0x1406A15D0` uses a narrow caller-context bridge:
the forced-aim, ADS-only-fire and aim-delay queries at `0x140694444`,
`0x14069B96C`, `0x14069BC7F` supply their proven playerState registers. Only
admitted local primary launchers return free-fire mode. Native callers, NPCs,
other weapons and alternate modes retain their original result. The native
call instructions stay intact for the projectile signature owner. Raising the
weapon may still request ADS for target acquisition; required target locks
(including Stinger's lock-before-fire rule) remain native.

The two empty-event auto-selection calls are `0x140374018` and `0x140374027`;
PM's empty removal call is `0x140692BA0`. The manual `Drop_Weapon` removal call
and script `takeweapon` are separate and unmodified. Exact native signatures
gate the launcher boundary together with the common reload/fire detours and
presentation provider. `vr_launcher_status` reports admission, clip/spent state
and held rocket; `vr_fire_status` remains the native firing diagnostic.

## Validation

CPU coverage includes both-hand draw/seat/refund, rejected writes, repeated
cleanup, reversed insertion, overlapping acquisition, disposable no-reload,
independent head/launcher reticle directions and receiver/hand/socket binding.
Javelin coverage checks native reload admission, prediction replay, near-eye
entry/exit, no pose mutation, and physical muzzle preservation at the CLU sight.
Offline shaded native meshes check the four launchers' source grips. These checks do not
replace in-game lock acquisition against an eligible target, guided flight,
empty-tube retention, manual drop, or headset visual acceptance.
