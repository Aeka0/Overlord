# Official campaign cheats

The weapon adapter consumes the game's effective cheat state. The embedded
launcher also exposes a separate **VR Settings > Cheats** category. Its controls
default to disabled, with a recommendation to finish the campaign
once before enabling them. It does not unlock the native Intel menu.

## Launcher controls

| Preference | Choices | Native integration |
| --- | --- | --- |
| No recoil (`vr_recoil`, inverted UI) | Disabled by default; checked saves `false` | Existing recoil switch; absent from OOBE, while single-hand penalty remains in OOBE and Gameplay |
| `vr_cheatHealth` | `off`, `demigod`, `god` | Same `FL_DEMI_GODMODE` / `FL_GODMODE` bits as the existing native commands; neither prevents scripted deaths |
| `vr_cheatNotarget` | `off`, `on` | Native `FL_NOTARGET`; AI targeting changes, not model visibility |
| `vr_cheatAmmo` | `off`, `reserve`, `infinite` | Lock carried primary/admitted underbarrel reserves at 500, or enable native `player_sustainAmmo` and the existing sustained-shot adapters |

`launcher_cheats.cpp` applies preferences on the server scheduler independently
of Green Beret hook readiness and physical-carry activation. It sets target states
idempotently rather than executing toggle commands. Provenance in
`level.vr_launcher_cheat_flags` and `level.vr_launcher_cheat_sustain` survives
checkpoint serialization, so disabling after a reload releases launcher-owned
state. Disabled defaults do not clear protection already owned by native scripts
or console commands. Independently enabled official Infinite Ammo remains active
when the launcher relinquishes its request.

Reserve mode uses the existing validated, compared primary/secondary ammunition
transactions over the 15 native inventory entries. It preserves loaded rounds,
chambers, physical copies and reload mechanics, skips invalid/unsupported feeds,
and never overwrites a full sparse ammo table. It only writes changed counts,
pauses during Green Beret inventory transfers, and stops replenishing when off;
it does not rewind ammunition acquired while enabled. Native official cheats can
still affect gameplay independently of launcher preferences.

`vr_launcher_cheats_status` reports requested modes, effective sustain state and
reserve write/rejection counts. Runtime/HMD acceptance is still required for
native AI, scripted deaths and checkpoint transitions.

## Infinite ammunition

`player_sustainAmmo` controls independent primary and underbarrel delivery.
A successful sustained shot retains the exact loaded/reserve counts and the
mechanical feed's ready state. This includes manual bolts, pumps, levers,
cylinders, hinged chambers and underbarrel launchers. Empty guns stay empty;
open actions, removed feeds, covered muzzles, cadence, trigger rearming and
tracking admission still apply. Deliberate reload/extraction/discard actions
keep their existing semantics.

An accepted shot and its ammunition debit are separate facts. The feed
transaction receives an explicit sustained flag, increments its revision and
records shot feedback without inventing a debit/refund or an empty chamber.
The native projectile/launcher consume boundary also recognizes verified
non-debit shots, preserving disposable-launcher readiness under the cheat.

## Green Beret

The native `greenberet_giveweapon` and `greenberet_takeweapon` functions still
own the weapon removal/restoration and mission state. Scoped script bridges
wait for the server inventory boundary to settle reload escrow and snapshot
the complete VR loadout before entering those native functions. Repeated
activation callbacks cannot replace the original snapshot with mode pickups.

The snapshot stores up to the existing 15 physical instances, including
same-definition copies; independent loaded counts; shared reserve values;
left/right hand ownership; empty selection and empty slots; waist/back/overflow
placement and draw order; stable feed/action state; and admitted secondary
feeds. Loose reload items and hand-held loading escrow are settled once before
native removal. Native `takeweapon` still invalidates every copy of a definition.

`level.vr_cheat_inventory` and the transition phase are VM-owned arrays/fields,
so native checkpoints serialize them with the level. Only logical fields and
native weapon/profile names are saved. Asset pointers, native ammo-key tokens,
hand leases, timestamps and instance generations are rebuilt on restoration.
The sparse native ammunition stores and physical clip roster are staged before
commit. Conflicting shared reserves or malformed topology reject restoration.
An existing save without a VR snapshot retains the original native restore path.

While the effective `g_using_greenberet_ts` state is active:

- A player cannot stow a weapon or exchange it into an occupied holster.
  Existing legal stored items can still be drawn; support-hand release,
  hand transfer and ordinary physical dropping remain available.
- Accepted physical pickups clear their primary and admitted secondary reserve
  pools without waiting for `weapon_change`. Native claymore and flare mission
  behavior stays separate. Loaded firearm rounds are retained.
- Native level-start removal and `greenberet_disabled` story exceptions remain
  authoritative. The adapter does not repeatedly delete later scripted weapons.
- Outside Cliffhanger, the chest knife uses `wpn_h1_melee_rifle_bayonet_vm` root geometry, excluding
  `tag_clip` sheath geometry. Its +Z blade is mapped into the existing knife's
  +X contact frame. Forward/reverse grips, both hands and magazine/slide co-grasps
  share the established palm contact. Native ownership of
  `h2_cheatcommandoknife` is retained, but the carry roster and physical pickup
  path do not offer a duplicate operable blade while the chest owns it.
- Cliffhanger's native selector precaches `h2_cheatpickaxe`. While the effective
  mode is active, the regular chest knife is hidden and two independent picks
  occupy the configured left/right waist anchors. Either hand can draw either
  tool with Grip; releasing Grip returns that tool to its original side. They
  cannot become ordinary carried firearms, ammunition feeds or world drops.
  Exiting the mode removes both tools and resumes the existing native/VR
  inventory restoration transaction. Their attacks use the shared physical
  melee path: contact points come from the authored pick head, swing speed uses
  continuous controller motion, and hit cooldown/withdrawal rules are shared
  between hands. Native damage, collision, immunity, story callbacks, and hit
  effects remain authoritative. The ordinary story picks and C4 prop are
  documented in [Cliffhanger story props](vr-cliffhanger.md).

Provider lookup reads native definition and name memory through a bounded,
protected adapter. A nonzero table entry is not assumed to be a pointer. The
512-value token encoding is only a search ceiling, not a registered-item count.
The Cliffhanger initializer does not scan for the ordinary cheat bayonet.

Failed snapshot/restore boundaries retain native ownership or the recovery
snapshot and report their reason. During an incomplete transfer, weapon input
cannot race inventory reconstruction. Missing historical VR data cannot be
reconstructed from the native weapon-name dictionary.

## Ragdoll Impact

Each confirmed player melee hit reads the native effective `level._id_AE66`
state. While active, fists, firearms, shields and carried/chest knives all use
the current chest tactical knife's native melee damage (normally 200), rather
than their individual weapon's damage or the ordinary blunt reduction. The
actual striking weapon identity, contact and native `G_Damage` route are retained.

Disabling the mode immediately restores the ordinary per-hit policy: fists and
firearms use approximately one third of their own native melee damage, while
knives and shields use their own full native damage. No WeaponDef value is
modified or cached for later restoration. Unavailable/malformed knife evidence
retains the ordinary policy instead of inventing a damage value.

The original `animscripts/death::doimmediateragdolldeath` now receives the full
damage directly and owns all impulse calculation, direction overrides and
`startragdollfromimpact` dispatch. The previous separate impulse-budget adapter
and per-victim metadata are removed. Actual ragdoll displacement still requires
game/HMD acceptance.

## Verification and diagnostics

`vr_cheats_status` reports hook readiness, effective modes, transition state,
capture/restore/pickup counts and the last failure reason, including the native
Ragdoll Impact state.
`vr_melee_status` reports the selected chest knife resource and its existing
grip/contact/render counters.

The focused official-cheats tests cover sustained shots across feed families,
normal debit after disabling, initially empty feeds, event replay rejection,
independent duplicate clip restoration, shared-pool rollback, explicit empty
topology, storage-vs-support release and bayonet contacts in both hands/grips
and co-grasps. The virtual script can compile without level-specific imports.
Offline validation does not establish headset grip fit, native save/load
integration, mission transitions or visible ragdoll flight.
