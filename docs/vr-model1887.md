# Model 1887 lever action

Lever gesture detection uses producer continuity when slow simulation spaces
consumer updates 200/250 ms apart. It covers manual ejection/rechambering,
fixed-grip return, and spin/catch. Real input discontinuities and a bounded
500 ms maximum motion span stop the gesture without inventing travel. Both-hand
offline regressions cover these paths; slow-motion headset acceptance remains
separate. See [shared breach control](vr-breach-control.md).

The native cycle contract preserves unlocking, partial-opening loading, and
control-grip reacquisition. Headset acceptance should include these interactions
with the complete lever presentation.

 follow-up: shell loading has broader distance/orientation tolerance,
foregrip-only carry frees the former lever hand for ammunition, and a fresh Y/B
press at the original grip performs a short mechanical return.

## Controls

- Keep the holding hand's Grip pressed and release its Trigger. A closed gun
  with a live chamber is locked; B/Y temporarily unlocks it. Once a stroke has
  started, B/Y may be released and the opening/return can continue. Releasing
  B/Y before starting a stroke leaves the loaded gun locked.
- A spent/empty chamber or a partially open action is automatically unlocked.
  Pitch the operating wrist upward to open, then back to close, without B/Y.
  Only closing a loaded action restores the lock. The initial full stroke is
  0.85 radians (about 49 degrees) of tracked wrist motion; the closed-stop
  tolerance is 2% of that travel. Returning through it snaps to the exact closed
  pose before relocking.
- A deliberate fast upward flick with the supporting hand off the gun starts
  an assisted spin. The same mechanical lock rules apply; B/Y is not a spin
  modifier. Return to the starting wrist orientation and briefly settle to
  finish the catch. Trigger pauses an open mechanism; in the closed catch tail,
  Trigger intent or a real fore-end Grip ends the flourish. Trigger must still
  return to neutral before firing, so this cannot queue a shot.
- At 65% opening, extraction clears a live/spent chamber once and permits shell
  insertion. The remaining opening travel stays available. Use the auxiliary
  Trigger waist pinch and the authored tube/chamber contacts. A smaller opening
  still rejects insertion. Shell handling temporarily pauses lever motion.
- Once opened far enough, keep the supporting hand on the fore-end, release the
  main Grip, and grab either the original grip or the visible lever. The closest
  authored contact wins through the central Grip arbiter. Original-grip carry
  leaves the lever angle untouched during ordinary aiming.
  Grabbing the lever resumes operation from its retained angle without B/Y.
  Alternatively, at the original grip press Y/B once to return the lever in
  about 0.18 seconds for full travel. This passes through the normal close/feed
  transaction; the same held press cannot immediately unlock it again.
  At small openings the two contacts coalesce. Releasing the only holding hand
  retains the ordinary holster/drop behavior; the gun does not float in place.

The shell contact radius is 7.5 cm (previously 3.5 cm), swept an additional
2 cm sideways and 2.5 cm downward. Approach alignment permits up to 120 degrees
from the authored insertion axis. The usable-opening threshold, occupied chamber,
capacity, tracking-jump rejection and contact withdrawal/rearm checks still apply.

When the fore-end alone carries the gun, its opposite free hand can draw and
insert a shell. It gains no firing or lever-operation authority. Per-hand
anatomical/mirror bindings are retained so the release frame can switch loader
hands without waiting for a new render. Tracking loss refunds held ammunition
through the normal compared-write path.

Rapid return requires a fresh Y/B press, a fixed grip and a neutral Trigger. It
is not queued while a shell is held, or by holding Y/B across reacquisition.
Focus/tracking/ownership changes stop it at the retained opening. Failed native
closure cannot feed or visually snap closed; a new press can retry. Completing
return restores the shared closed grip, while shooting still needs neutral rearm.

Firing consumes the chamber and retains its spent case. Reaching the usable
opening ejects once; closure feeds once. Deliberately opening a live chamber
uses the existing ammunition disposition path. Empty and partial strokes never
manufacture ammunition. The native five-round total budget is preserved; this
recipe does not opt into a sixth round.

During a spin the mechanism can close before the cosmetic tail finishes. Once
the action is closed and chambered, a fresh Trigger press can fire and immediately
finish that tail; there is no invisible post-closure firing gate. Holding Trigger
while the action is open still cannot complete it or queue a shot. Focus/tracking
loss, reference changes, long sampling gaps, ownership changes and scripted
control clear motion history and preserve committed mechanical/ammunition state.
Fresh tracked samples can resume an unlocked action without using B/Y. No missed
interval is simulated as catch-up travel.

## Spin completion and rearming

Cosmetic spin completion must not leave a mechanically closed action locked.
Once the action is closed and chambered, a fresh Trigger press may fire even if
the visual tail is still finishing. Catch stability is evaluated at the
mechanical transition rather than restarted on each tail frame.

Catch confirmation is now latched once. The remaining visual tail progresses
independently of ordinary wrist aiming and Trigger, while invalid tracking still
interrupts it. Trigger can end a flourish only after the authored remaining
mechanical curve is entirely closed; it never advances an open mechanism or
queues a shot. Neutral Trigger rearming remains required.

At the closed catch tail, the shared support predicate admits a real fore-end
Grip through ordinary spatial arbitration. Acquiring it ends the flourish,
without repeating feed/ejection. Both rendering and carry use that predicate.

With no live chamber and no tube round available, an opening flick stops at the
first full-open sample instead of executing the spin-close curve. It ejects a
last spent case once and leaves the action ready for loading. Residual wrist
recovery is ignored until a quiet tracked baseline; subsequent manual closing,
regrasping and fixed-grip Y/B return retain their existing behavior. A spent
chamber with another tube round still supports the full rechambering spin.

Regression coverage includes both hands at 60/90/144 Hz, aiming away after catch,
early Trigger with neutral rearm, fore-end catch, open-action Trigger inhibition,
empty/last-shot opening stops, residual motion and subsequent loading/closing.
The pre-fix completion tests produced 18 failures; the corrected suite passes.
Headset confirmation of this intermittent symptom is still pending.

## Mechanical firing policy and support posture

 live capture identified ten mechanical-feed rejections that each
advanced the firing deadline by 430 ms despite emitting no shot. Such pre-native
rejections now consume only the attempted command/input edge; they do not add
or clear a successful-shot cooldown.

As required, admitted M1887 lever instances additionally use a mechanical
single-shot policy with zero interval/delay. A complete valid cycle and chambered
closed action determine readiness. Fresh Trigger edges and same-command duplicate
suppression remain mandatory, and empty/open actions cannot fire. Other weapons
retain their native cadence. The cosmetic spin tail no longer blocks a closed,
chambered M1887, aligning its close event/readiness without queuing an early hold.

The support-hand posture defect came from the final wrist constraint reapplying
finger poses with support grip weight zero. M1887 now requests a full grip at that
constraint. Its target rotation uses the same blended anatomical/grip basis as
the pose solver, keeping the selected wrist position and angle correct for either
hand while preserving authored finger rotations. Other part constraints retain
their existing relaxed default. Source-mesh before/after views and both-hand
constraint regressions verify this distinction; headset fit remains to be checked.

## Shared integration

`tube_feed.hpp` distinguishes automatic, pump and lever action drives. They
share individual-shell escrow, native compared writes, chamber/case accounting,
and transfer boundaries. Only lever opening/closing admits the existing control
hand as operator; supply actions retain their auxiliary-hand authority.

`lever_gesture.hpp` consumes the existing frozen controller frame. B/Y only releases the closed loaded lock. Fixed and moving control contacts
are revision-bound carry attachments, selected by the central Grip arbiter and
reported as distinct control targets. They never create competing grab sessions. The auxiliary shell
still goes through the central hand-interaction arbiter. Current-frame lever
sampling requires the operating controller, without depending on an invalid
opposite controller.

`lever_pose.hpp` provides the same gun-relative motion to the skinned presenter
and current-frame carry/contact solver. Two-hand direction is re-solved using
the moving control anchor. Gun, muzzle, attachments, loading contacts and native
ejection reference follow that pose. The runtime remains bounded, and introduces
no worker wait or new polling loop.

The initial spin gate uses a 0.30-radian signed pitch window with 5 rad/s speed
and a meaningful fast segment. Rotation jumps above 35 rad/s are rejected. The
assisted segment targets 0.65 seconds; the last part waits for a wrist orientation
within 0.30 radians and speed below 1.8 rad/s for 60 ms. These are candidate
headset tuning values, not measured ergonomic acceptance.

## Authored assets

Exact native admission is `model1887`, capacity 5, segmented single-shell feed
and the manual-action flag. Its receiver is `h2_viewmodel_model_1887_base`, with
the captured nine-bone hierarchy; `j_hammer` remains a child of `j_action`.

Source poses under `weapons/model1887/` retain animation hashes:

- H2 idle frame 0 supplies ordinary grips, fingers and rest parts.
- H2 rechamber frame 12 supplies the open lever and control-hand pose.
- H2 reload-loop frames 12/20 supply the shell grasp and tube insertion.
- H2 reload-end frames 11–44 supply the spin-close hand/wrist path. Eight
  opening samples precede 28 closing/spinning samples. The curve is sampled by
  instance gesture progress; the native animation clock never owns ammunition.

The older akimbo animation was rejected after source-mesh inspection exposed a
different initial grasp relative to the H2 receiver. H2's own spin-close track
provides matching finger motion and avoids that legacy offset. Canonical right
finger extraction uses exact side suffixes: a substring search for `_ri` would
incorrectly include left `ring` joints.

The native `tag_brass` supplies the ejection frame. Duplicate source display
rounds `j_ammo_02` and `j_ammo_03` are hidden by named binding. The live held
shell uses the existing rigid-subset renderer. No external export directory,
replacement model, texture or animation file is a runtime dependency.

## Validation and remaining acceptance

`lever_reload_tests.hpp` covers both operating hands, normal and assisted cycles,
partial reversal/pause, empty feeds, live/spent extraction, rejected writes,
tracking/reference continuity, Trigger inhibition, auxiliary tracking loss,
loading, and 60/72/90/120/144 Hz spin/catch behavior. `lever_profile_tests.hpp`
checks captured hierarchy, admission rejection, fixed/moving control selection,
release/regrasp ownership, same-frame pose handoff, bounded/mirrored poses, exact
grip endpoints, normalized finger tracks, two-contact direction, and current-frame
contact sampling with an invalid opposite hand. Existing pump, tube, carry,
underbarrel, cylinder and hand-interaction regressions remain applicable.

Use `vr_tube_status` to inspect ammunition, travel, lever opening, spin progress,
operating state and the latest controller decision. The family uses the existing
`vr_tubeReload` switch.

Headset acceptance still needs grip/contact fit, loading-mouth reach, spin
direction/speed, two-hand comfort, sounds/ejection, pause/drop/reacquire, mixed
dual wield, and checkpoint/level changes. This work does not deploy or establish
acceptance of an unobserved native akimbo definition or other receiver variant.
