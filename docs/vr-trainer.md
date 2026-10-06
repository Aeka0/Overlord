# S.S.D.D. / The Pit

## Dunn's sidearm lesson

The native `maps/trainer::_id_B168` lesson polls `getcurrentweapon`, but its
nested `_id_CED3` hint helper also waits for `did_action_primary` or
`did_action_sidearm`, registered through `notifyoncommand` and `weapnext`.
The trainer adapter also handles this nested blocking helper; replacing the
outer query alone cannot resume its synchronous hint wait. The lesson compares
against `level._id_D033` (the
lesson pistol) and `level._id_A836` (the rifle). The initial weapon determines
whether Dunn first asks for the rifle or the sidearm. Native dialogue, hint
loops and `sidearm_complete` remain authoritative.

VR can hold the pistol while the rifle remains the native selected projection.
The trainer adapter replaces only this function's local-player, no-argument
weapon queries with the latest held firing grip, using the carry inventory's
shared rear-grip revision. Either hand works. Support-grip changes do not count
as new draws; returning the pistol exposes the still-held rifle. Empty hands
retain the last teaching weapon so the script's `current != pistol` check does
not interpret putting everything away as drawing the rifle.

The adapter never sends keyboard input, switches the native firing owner, or
changes ammunition. The native complete-weapon-name formatter is retained.
The method entry at `0x1404B6350` is signature-checked. Script function bounds
come from the existing loaded-script registry and are rebound on level spawn
and saved-level loading, then cleared on shutdown. Other callers and disabled
VR retain the original query.

The adapter observes only the trainer hint helper's two switch
event registrations. After the native helper has installed its wait, a server
update checks the actual held firing grip against the native lesson pistol.
A matching physical draw emits the corresponding hint action once. Empty hands,
the wrong weapon, native weapon suspension and menus do not complete it.
`clearing_hints`, the lesson-end notification and level/save lifecycle changes
cancel pending requests. On saved-level loading, one bounded scan of native
notify-thread metadata recovers an existing level-owned switch wait, since its
earlier registration need not execute again. An observed live wait for
`did_action_primary` confirmed this ownership and the actual blocked event.
The original helper retains its flash, delay, cleanup
and continuation; no keyboard input, weapon grant or completion flag is forced.
`vr_trainer_status` reports query, hint-registration and physical-completion
counts, also saved to `minidumps/overlord-trainer.txt`.

## Firing-range pickup camera

Both `firing_range_player_pick_up_items` branches disable weapons and link the
player first to a moving `script_origin`, then to the animated `worldbody`.
Their zero-width native view clamps were feeding back into HMD orientation.
During that linked, weapon-disabled firing-range interval, the shared sequence
adapter now uses continuous free-head rotation through both parents. Native
translation, pickup animation and weapon grants remain owned by the script;
ordinary input and weapons return at native unlink/enable. Completed training,
unrelated rigs and missing flags do not acquire this camera policy.

## Partly buried dropped weapons

The reported Desert Eagle remained a live native item. Observational tracing
showed GetUseList rejecting its single model-derived sight point with contents
`0x11` before the VR volume selector ran. Most of the rendered mesh was above
ground, so the rejected point was not sufficient to reject the whole weapon.
The shared [world-pickup visibility adapter](vr-world-interaction.md) now checks
exposed rigid mesh points at both native admission and final VR visibility.

## Physical knife and targets

The physical melee trace additionally admits damage-enabled `script_model`
entities with `script_noteworthy` equal to `target_enemy` or `target_friendly`,
only in `trainer` and only for knife strikes. These are the native visible
targets; the hidden aim-assistance entity and linked base are not substitute
targets. Script models need not have positive actor health.

The existing blade sweep, head-to-contact clearance, motion/tracking checks,
entity generation validation and shared 400 ms contact gate still apply. The
adapter rechecks target identity and damage permission before calling the
normal `G_Damage` entry with `MOD_MELEE`. The native target listener owns
hit-count updates, knife-only requirements, civilian penalties, sounds,
lowering and rearming. No synthetic `damage` notification or direct score/flag
write is used. Props can confirm a knife contact without emitting the
actor-only knife blood effect. NPC and blunt-melee behavior is retained.

## Script HUD results

`maps/trainer::killtimer` creates ordinary GSC HUD text, dynamic values and
two `h2_hud_ssdd_results_line` separators. Time strings are composed by the
native `settimeformat` helper. This display is separate from LUI.

Trainer script HUD allocations use one 2 m narrative canvas, including pulsing
scores and plain labels. Previously, pulse flags moved parts of the table to
the 1.6 m announcement canvas. Both native label/value calls already pass
through the verified `CG_DrawHudElem` text producer; no number formatting,
localization, animation timing or result calculation is duplicated.

The two result separators join the existing script-image ink path. Exact
allocator ownership, fingerprints and expiry are required, so an unrelated
image with the same material cannot qualify by name alone. Separators are not
fullscreen fades. Native aspect ratio, alpha, sorting and disappearance are
preserved through the shared capture and stereo composition. Other maps'
announcements and the separate dialogue producer keep their existing depth.

## Deferred difficulty menu

`difficulty_selection_menu` is included in the
[common VR menu candidate](vr-native-menus.md); its scenario-specific HMD
acceptance remains pending.
The native `_id_B431` sequence freezes controls, applies
blur, clears the results and opens this LUI menu. It waits for a `menuresponse`
of `continue` or `tryagain`; other responses reopen it. The original menu must
ultimately own difficulty selection and those responses. Rendering results
does not make the subsequent difficulty menu operable in VR.

## Evidence and validation

`vr-melee-tests` covers both hands, simultaneous pistol/rifle holding,
holstering/redrawing, empty-hand rejection, target identity and damage-disabled
or non-knife rejection. Existing motion/contact tests cover cooldown and
tracking discontinuities. `vr-spatial-panel-tests` covers the shared table
depth, native separator ownership, overflow rejection and WARP composition;
the weapon-grip suite also passes. Debug and RelWithDebInfo clients compile.

Headset acceptance remains pending. Verify Dunn's
lesson from both initial weapons, the compulsory stairway knife target,
ordinary enemy and civilian targets, repeat runs/checkpoint reloads, and the
complete animated results table in both eyes. Difficulty-menu interaction
still requires scenario-specific HMD acceptance.
