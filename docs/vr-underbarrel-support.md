# Underbarrel support capture and retention

The SCAR report reproduced a support-capture restriction rather than a left-hand
tracking failure: the left hand could enter the firing grip, but support attempts
3–6 cm from the anchor had palm-up scores around 0.32–0.69 and failed the former
0.766 threshold. Right-hand successful samples had scores near 0.98. The player
confirmed that wrist angle was the cause and requested a wider, retained gate.

## Rules

| Gate | Previous underbarrel restriction | Current rule |
| --- | --- | --- |
| Support acquisition angle | 40 degrees from palm-up and dominance over inward score | 75 degrees from palm-up |
| Support retention angle | 50 degrees plus score dominance; later 100 degrees from palm-up | M203 uses positional breakaway without a wrist-roll gate; shotgun retains its 100-degree up/inward gate |
| Support distance | Host radius, intersected with narrow angle gate | Host authored acquisition radius; M4/M16/SCAR use 10 cm |
| Empty shotgun pump acquisition | Separate 5.5 cm radius | Host support acquisition radius |
| Support/mechanical breakaway | M203 12 cm; pump 18 cm with 16 cm lateral tolerance | Host release radius, 22 cm for these rifles |
| Secondary firing grip | Strict local firing volume and orientation | Unchanged |

Both hands use the same rules. Clear firing intent excludes support only inside
the firing contact; outside it, a moderately tilted palm can acquire support.
Held M203 support remains engaged through wrist roll inside the host's breakaway
distance and does not become a firing grip by turning the wrist. Shotgun support
retains its separate angle threshold. Changing roles requires the input/ownership
rules. Grip release, stale tracking, occupancy conflicts, large jumps and genuine
separation continue to end a grasp.

The comparison baseline is `ebb94fd^`, before underbarrel implementation. Its
`weapon_carry_grip.hpp` used the host's `acquire_meters`, and `grip_presenter.hpp`
retained against the solved two-hand anchor using `release_meters`. The current
module now transports both values instead of substituting small generic action
radii. Ordinary support checks use the solved anchor; M203 support uses the
equivalent raw hand-span distance, independent of steering rotation.

## M203 close transition

Closing a loaded M203 retains the existing grip as support. The loaded support
state validates the authored hand span and support breakaway rather than treating
all subsequent motion as an unlocked slider stroke. Empty support retains the
slider constraint so it can open without an artificial dry shot. Pump movement
keeps the same native stroke and completion thresholds while using the host's
larger lateral breakaway. No ammunition or firing admission is widened.

## Evidence and verification

The read-only live capture preserved current module state, hand sessions, last
arbitration decisions and rendered contact distances/facing. It did not alter
the running game. Regression checks include recorded left SCAR contacts, both
hands across wrist rolls, acquisition/retention boundaries, firing-overlap
priority, open/load/close/regrasp, pump lateral travel and tracking jumps.

Headset acceptance: compare left/right support acquisition, wrist tilt while
holding, and deliberate separation. Fire an M203, slide open, load and close;
support should remain engaged. Repeat after deliberately releasing and
regripping. Verify inward secondary firing remains available only at its own
grip and cannot be triggered from ordinary support.
