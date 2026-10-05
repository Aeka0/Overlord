# Waist quick reload

`vr_quickReload` defaults to `1`. The launcher Gameplay page exposes the saved
toggle in English and Simplified Chinese. Existing profiles without the setting
use the enabled default.

Keep the weapon's holding hand inside either waist holster for one continuous
second. The interaction uses the carry system's configured holster volumes,
including the body estimate and world scale. The back holster does not qualify.
No extra button or free opposite hand is required.

Starting an eligible dwell queues one sound, randomly choosing between
`wpn_handgun_ads_down_01` and `wpn_handgun_ads_down_02`. These are recording names:
the native sound adapter looks up the verified `wpn_handgun_ads_down_plr` alias
and checks its two file variants. Native variant selection chooses one recording
and preserves the stock `h2_wpn_foley_ads_plr` clothing layer. A  live
audit confirmed both extensionless recording names and that companion; the first
matcher incorrectly rejected the companion and remained silent. The corrected
path does one named lookup instead of scanning the SOUND pool. Playback
uses the existing main-thread positional feedback queue and sound toggle. Repeated
samples do not replay the start sound; cancellation and re-entry start a new dwell.

During the dwell, the existing chamber-warning row displays **Quick reloading**
and translated alias texts steadily, even when the gun is empty. It takes
priority over the blinking chamber prompt. Completion/cancellation restores the
ordinary status, and a newly needed chamber prompt starts in its visible phase.
The two captions retain separate GPU textures when the hands show different states.

Eligible authored feeds are M9, USP (including suppressed), .44 Magnum,
Desert Eagle (including gold), M1911, G18, TMP, Mini Uzi, PP2000 and Ranger.
Eligibility is independent of recoil classifications. Existing native profile
and capacity admission still applies.

- Detachable magazines: the old magazine must be absent. An inserted empty
  magazine is not replaced. Fill up to capacity from reserves, preserving any
  chambered round and the complete action state. Empty lock release and cocking
  remain manual; Mini Uzi's open-bolt state is preserved too.
- .44 Magnum: the cylinder must be fully open, with no live rounds or spent
  cases in any of its six chambers. Fill up to six rounds and keep it open.
  The existing gravity-ejection rule still applies to an inverted open cylinder.
- Ranger: the barrels must be fully open and both chambers must contain neither
  live rounds nor cases. Fill up to two rounds and keep the barrels open.

No reserves means no change. Partial reserves fill as much as possible. A spare
magazine, speedloader or shell already held in the opposite hand remains owned
and retains its rounds; quick loading draws only from the current reserve pool.

The shared dwell policy resets on leaving/changing waist slots, an ineligible
feed, hand/instance changes, focus or tracking discontinuity, recentering and
suspension. Producer continuity permits a slow server cadence without counting
stale tracking. Each physical weapon owns its timer; no render-thread ammo writes
or additional runtime polling are introduced.

Each feed plans a single transaction through its existing identity, revision,
conservation and native compare/commit boundary. The second held weapon refreshes
shared reserves before it plans its transaction. Failed native comparisons do not
publish ammunition or feedback. Feedback uses the normal insertion sound at the
weapon; no magazine is animated from the opposite hand for a quick insertion.

Offline coverage: `vr-quick-reload-tests`, existing reload/mechanics/cylinder/hand
interaction suites, launcher config tests and `launcher_settings_ui_tests.js`.
[Driver weapons](vr-vehicles.md) reuse the same dwell with an explicit third chest zone. Driver quick loading is mandatory even when this setting is disabled; it still requires magazine removal, and uses the script-owned 32-round vehicle supply without a chambering step.

HMD acceptance should check both hands and waist slots, default/off persistence,
0/partial/full reserves, two instances sharing reserves, early withdrawal,
pause/recenter/tracking loss, retained chamber and empty lock, cases left in the
Magnum, partially loaded Ranger and manual closure/cocking after the quick load.
