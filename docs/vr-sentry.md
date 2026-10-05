# Wolverines sentry placement

In `invasion`, the existing world Grip interaction starts native sentry pickup.
Once pickup completes, either hand's Trigger requests placement through the
native attack button. There is no firearm-hand or physical rear-grip requirement.

The adapter observes the original `_id_D2A4` sentry script on the server thread:
`player.placingsentry` identifies the turret, `turret.carrier` must identify the
local player, `sentrytype` must be `sentry_minigun`, and `player._id_BA84` must be
one. `_id_B557` sets this last field after pickup and clears it before dropping.
Native weapon disable and player life are checked on both observation and command
delivery. A fresh, bounded snapshot carries only the session epoch to the command
thread. No script objects cross threads.

`sentry_input.hpp` reuses the shared digital input gate. Each hand must first be
neutral, including after a new carry session, pause, recenter or tracking loss.
Short taps remain visible for 100 ms because `_id_ABD2` polls attack/use every
50 ms. Holding either accepted Trigger keeps attack held: the original release
loop requires release before retrying an invalid location. Both hands produce
the same single attack bit. Input is appended at the existing final CreateCmd
boundary, including when native script control bypasses normal weapon input.

The original script retains placement previews, ground/stair/no-sentry checks,
drop animation, weapon restoration, automatic firing and mission teardown.
No carry model, arm pose or camera behavior is changed by this input adapter.