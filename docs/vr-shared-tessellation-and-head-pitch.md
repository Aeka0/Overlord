# VR Shared Terrain Tessellation and Full Head Pitch

## Terrain gaps at the outer edge of the right eye

On , polygon gaps appeared along the right edge of the right eye in a snowy
slope scene. HMD testing confirmed in the live session that temporarily changing
`r_tessellation` from 1 to 0 removed the gaps. It was then restored to 1.

The CPU stereo union frustum did not cover every culling path. The native call chain is:

`7A7DA0` left-eye owner → `2A1330` shared GPU commands → `2A0DD0` →
`29DE70` terrain tessellation → `29E9D0` tessellation view constant upload.

Runtime sampling captured 52 consecutive uploads at `29E9D0`, all with the left eye
as the primary view: the projection center term was approximately `+0.242513`, and
the horizontal half-width was approximately `1.107741`. These calls ran on the owner
thread. The native scene record had already switched to the left eye, but both eyes
shared the tessellation result, so regions visible only to the right eye were discarded early.

The implementation retains a complete union-view snapshot before the owner takes over.
It replaces the primary view data in the tessellation buffer before the native Unmap
only when the frontend, record index, and owner thread match and the stereo transaction is valid:

| Buffer offset | Data | Bytes |
| --- | --- | --- |
| `0x20` | Primary view VP | 64 |
| `0x8E0` | Primary view projection | 64 |
| `0x11A0` | Primary view world origin XYZ | 12 |

The complete buffer is `0x1600` bytes and contains 35 views. Before replacement, each
relevant field in the native upload is checked against the current record. Other views,
shadows, tessellation parameters, and origin W retain their original values. Each Map,
fill, and Unmap call still executes once. Rendering continues to use each eye's exact
optical projection.

`vr_tessellation_status` reports hooks, prepared, applied, and rejected.
In a normal terrain scene, applied should increase and rejected should remain 0.

Native `2AF550` (Umbra) and `779FA0` also reconstruct symmetric frusta from half-angle
scalars. These scalars now use the maximum absolute value of the two sides of the
asymmetric projection to ensure coverage of the wider side.

## Headset pitch beyond 90 degrees

The full orientation matrix continues to drive the camera. Game input and horizontal
body orientation use continuous Euler decomposition, choosing the equivalent angle
branch closest to the previous frame. Heading remains continuous through vertical
orientations. Recentering uses this heading as well, rather than deriving yaw again
from an inverted forward vector.

After decoding a command, native `68D7B0` applies the normal ±85° pitch clamp and
rewrites delta_angles. The pre-call pitch delta and unclamped pitch are restored only
for player commands recorded as headset input whose time, packed pitch, and reset
generation all match exactly. The original function still runs once, preserving other
behavior such as shields. The game continues to handle stance, vehicle, and script
constraints in the parent function.

This function also confirmed an offset error in the old structure definition:
delta_angles is actually at player-state `0x98`, and viewangles is at `0x108`.
The old `0x128` offset is not delta_angles. The camera recovers its world reference
from native Euler yaw, avoiding another reversed heading calculation from a forward
vector pitched beyond 90°.

## Validation scope

`vr-engine-stereo-probe-smoke` covers a culling counterexample for regions visible only
to the right eye, buffer modification bounds, rejection of mismatched uploads,
symmetric half-angle coverage, continuous motion through positive and negative 90°,
full yaw/roll rotations, camera/body heading while leaning backward, recentering in
that position, and native command matching. The Debug client build passed.

Headset acceptance testing is still required: inspect the original snowy slope's right
edge with tessellation enabled; lean backward continuously beyond 90° and check that
the scene, hands, and chest equipment follow smoothly; confirm normal turning,
recentering, and behavior under script control.
