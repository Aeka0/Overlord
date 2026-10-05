# Reload presentation while carried by the foregrip

HMD testing identified that a spare magazine disappeared while holding a weapon only
by its support grip, on either hand and across weapon families. Releasing the
magazine Trigger made the falling magazine visible again. Insertion sometimes
appeared to fail as well.

## Confirmed rendering defect

The weapon render-pose cache called the firing-oriented `weapons::ready` gate.
That gate requires a real rear/control grip. A foregrip-only weapon correctly
has no firing authority, but still has a valid solved skeleton. Rejecting its
snapshot caused the held-magazine placement callback to omit the model because
it could not obtain the matching native skinned record. Falling magazines use
their own committed world-space event, so releasing the Trigger made them
visible independently of that rejected attachment snapshot.

`pose_ready` now validates the actual holding hand, complete grip identity,
tracking/camera freshness and geometry. The render cache uses it. The existing
`ready` firing predicate additionally requires `owner.can_fire`, so the fix
does not authorize foregrip firing. Scene binding also checks the support hand
and owner revision; held-magazine consumption checks support and magazine-hand
identity against the live state. A regrip cannot substitute another hand's pose.

This reuses the existing exact object/buffer/epoch and stereo-record pipeline.
No latest-pose fallback, permissive firing gate or insertion-tolerance change is
introduced.

## Validation and acceptance

Render-cache regression walks publish, skin, record preparation and held-object
lookup with either support hand and no control grip. It confirms firing stays
disabled, correct snapshots are available, and stale tracking, an unheld gun or
changed support identity are rejected.

Reload regression draws a spare with the other hand, derives an aligned magazine
from the real M4, MP5K, Vector and M9 well/grasp definitions, samples raw controller
geometry and commits insertion. Both holding hands and translated/rotated gun
frames preserve ammunition and release the magazine lease normally. These cases
pass with the existing insertion logic. The reported intermittent insertion
failure has not been independently reproduced offline; it must be retested once
the player can see the held magazine reliably.

Headset acceptance: carry by the foregrip alone, acquire a spare with Trigger,
observe it while rotating both hands, insert into an empty well, then repeat with
hands reversed. Release an uninserted spare and confirm its falling visual remains
continuous. Reacquire the control grip and verify ordinary firing/reloading.
