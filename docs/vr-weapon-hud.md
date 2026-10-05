# Native weapon ammo HUD

The VR ammo HUD presents the game's native ammo counters for currently held
weapons. It keeps the original pip graphics, typography, reserve count, and
weapon-specific layout. The VR adapter changes presentation ownership and
placement; it does not synthesize ammunition values or change native selection
and player state.

## Feed ownership and capture

The HUD keeps four fixed source slots: primary left/right and underbarrel
left/right. It creates a native `WeaponInfoHudDef` tree only for a valid held
feed. Each source is associated with one copied native inventory snapshot,
weapon instance, holding lease, and feed identity. Physical-reload admission is
not a condition for displaying a valid native feed.

Native construction retains fonts, pips, caliber labels, borders, and
non-ammunition subtrees. The ammo subtree is placed under a source-owned
container for capture. Non-ammunition widgets remain attached below an owned
hidden ancestor so native refresh can retain and animate them without exposing
duplicate desktop elements. Visibility and ownership changes retire the private
tree and clear its references; stable refreshes do not rebuild it.

Independent sources never change native weapon selection or player state.
Native watches for the compatibility-selected weapon do not drive these pips.
An absent owner, changed weapon/clip shape, invalid feed, or failed source
construction clears that source. A stale or ambiguous source cannot borrow the
latest controller pose or another weapon's ammo data.

The native HUD draw stream is captured through the existing observer and
presented as an immutable transparent texture. Both eyes consume one capture
snapshot. Capture and presentation are separate from ammo ownership, so native
draw callbacks cannot authorize a shot or alter ammunition.

## Placement and visibility

The completed panel is a camera-facing flat billboard at the current solved
weapon position. It does not rotate with the gun. Both eyes project the same
world-space quad at the weapon's depth, preserving binocular disparity. The
capture can update independently for each held weapon and hand; a lease belongs
to its weapon instance and ends on drop, holster, switch, invalidation, or device
replacement.

`A` toggles the shared visibility preference for all ammo HUDs, underbarrel
rows, vehicle ammo captions, and chambering warnings. The preference starts
shown and is latched until the next A press. Firing, reloading, weapon changes,
pause, tracking loss, fade timers, or input rearming do not toggle it. `B/Y`
remain reserved for mechanical interactions.

During native pause, HUD drawing is suppressed and private LUI trees are
released independently of the visibility preference. On resume, only current
valid feeds are reconstructed. If the native capture or pose temporarily stops
updating, retain the last valid presentation only for the same holding lease.
A visible preference alone cannot create pixels before the first valid capture,
after device replacement, or outside the view frustum.

## Underbarrel rows

An admitted M203, GP-25, or underbarrel shotgun adds one full-width native ammo
row below its host's original counter. Both rows share the host's scene-bound
grip anchor, horizontal alignment, and A-button preference. Their centers are
separated vertically by 5 cm. The source reads the module's own native ammo keys
and resolved secondary definition; it never changes selection, mode, or
ammunition. Removing or invalidating a module clears only its row.

Two hosts with admitted modules can display four rows. Ambiguous secondary
bindings retain the module adapter's rejection rules. See
[underbarrel VR interaction](vr-underbarrel-runtime.md) for firing and reload
ownership.

## Chambering and ammo warnings

A held feed shows a localized chambering instruction when its admitted
mechanical state contains live ammunition but is not ready to fire. The yellow
instruction blinks on a shared 500 ms phase. Chambering, emptying, releasing
that feed, or holding its action clears the prompt. Readiness comes from each
feed's own policy; open-bolt, manual-bolt, tube, and underbarrel feeds do not
guess from reserve ammunition.

When loaded rounds, including the chamber, reach zero while reserve remains, the
HUD shows a steady soft-red “Magazine empty” caption. When the chamber, magazine
or tube, and reserve are all empty, it shows “No ammo.” A chambered final round
counts as loaded ammunition. Quick reload takes the primary message position
while active. The warnings share one row, mirror alignment for the left hand,
and do not blink.

The caption uses the shared game-text catalog and the same smoothed scene anchor,
holding lease, and A-button preference as the native counter. Its outline,
color, and text are composed into an owned texture for both eyes. Language,
font, RTL layout, and spacing require visual acceptance in the headset.

## Blur and diagnostics

Native-mask blur adapts the captured screen coverage to each eye. It samples
each eye's own background and does not replay the desktop shader or use the
other eye's image. `vr_weaponHudBlur 0` disables this blur without changing HUD
visibility. Unsupported mask or material states omit blur instead of drawing a
rectangular substitute.

`vr_weaponHud_status` is an on-demand diagnostic. It reports source ownership,
capture readiness, eye draws, and composition failures. Normal gameplay does
not perform file readback or write continuous HUD reports. `vr_hud_capture_once`
is not part of the normal Debug client.

Offline tests cover native tree ownership, independent feeds, plus-one
saturation, underbarrel rows, warning policy, language composition, capture
selection, visibility, pause, retirement, and stereo placement. CI feature
parity checks ensure production HUD components remain present in optimized and
Debug clients. These checks do not establish headset layout or visual quality.

## Headset acceptance

Verify both eyes, readable mirrored labels and pips, native number updates,
camera-facing placement while the wrist rotates, weapon transfer, pause/resume,
tracking/device recovery, and A-toggle persistence. Check each admitted weapon,
hand, feed type, and underbarrel row. Compare `vr_weaponHudBlur 0` and the native
mask mode, then verify chambering and empty-ammo warnings against actual
mechanical readiness. Debug builds and saved preference values do not replace
this acceptance.
