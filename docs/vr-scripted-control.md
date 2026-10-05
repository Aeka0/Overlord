# Native scripted weapon control

The VR hands, carry input, weapon manipulation and firing paths respect H2's
native `disableweapons` / `enableweapons` interval. This is a shared permission
boundary, independent of the level name, animation duration and player linking.
An animation or vehicle attachment alone does not establish that combat is disabled.
The shared permission check also rejects native player death independently of
`disableweapons`. Death releases both VR grips and hides interactive hands;
it does not retain the ordinary cinematic grip snapshot. See
[player death](vr-player-death.md).
The [scripted sequence adapter](vr-scripted-sequences.md) additionally retains
body ownership through the af_caves rappel's early weapon-enable event; ordinary
combat admission consults that publication independently of native permission.

## Team Player evidence

Read-only capture confirmed the `roadkill` opening flow:

- `h2_roadkill_new_intro` calls `common_scripts/utility::_disableweapon`, then
  links the player to a separately animated `player_worldbody` inside the jeep.
- After the crash it unlinks briefly, then links to the Shepherd pickup animation's
  separate player rig. The native weapon prohibition remains active across this
  handoff; checking only player linking would incorrectly show VR hands between them.
- In the recorded retry, the server permission cleared at command time 26266 ms.
  Prediction caught up shortly afterward; the selected weapon remained token 64
  throughout, while the native presented weapon changed from zero back to 64.
  These times and tokens are evidence, not hardcoded gameplay conditions.

The native method table identifies `disableweapons` as method 0x8328 and
`enableweapons` as 0x8329. Verified paired instructions at 0x1404B706E and
0x1404B73C2 set/clear bit 0x80 in player state +0x3c0. The adapter checks both
signatures once and performs bounded read-only state access. It adds no script
VM callback, engine patch, polling thread or cross-thread wait.

## Pause and resume

Prediction controls extra VR object submission and hand posing. The native
scripted viewmodel and world-body animation continue through the original path.
The server independently checks permission for carry, reload mechanics and shots,
including the final ammunition debit and the second weapon of a dual-wield pair.
Controller locomotion and native script-advance jump notifications keep their
existing behavior; only free hand/weapon interaction is suspended here.

Carry preserves native-owned instance identities, hand assignments and storage
positions in its existing inventory. It continues reconciling native ownership
but suspends selection requests and grip actions. On exit, unchanged native
selection retains the previous grip; a changed final native selection, including
empty hands, remains authoritative. Removed weapons are never recreated. Player
replacement and backwards level time use the existing checkpoint reset boundary.

Transient poses, pending firing bursts and manipulation gestures are discarded.
Holding the trigger through the scene cannot fire on exit without releasing and
pressing again. Reload mechanics retain committed feed/ammunition state while
suspended, then use their existing reconciliation and interruption rules on resume.
This does not promise exact continuation of an in-progress magazine/bolt gesture.

## Exceptions and verification

Mounted weapons, vehicle segments that permit fire, and scripted weapon/button
sequences need their own gameplay policy if their native permission does not
represent the intended VR behavior. Exact restoration of a magazine held during
an interrupted reload is also a separate decision. No map-specific override is
introduced by that original permission-boundary change. The later af_caves
adapter is documented separately in [scripted sequences](vr-scripted-sequences.md).

Client Debug x64 builds with v142. Weapon-grip regression tests exercise the
captured flag categories, carry retention, script replacement/removal, checkpoint
reset, burst cancellation and neutral-trigger rearming. They do not certify HMD
visibility or animation alignment. The candidate is local only: the interaction contract requires
continued collection in the running level, with no restart or deployment.
