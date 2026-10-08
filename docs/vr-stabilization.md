# Optional pose stabilization

All three controls are available in Launcher > VR Settings > Basics > Stabilization,
in both optimized and Debug clients. They default off and share the existing player
configuration. Console changes take effect at the next producer sample/draw.

| Switch | Strength setting | Initial strength |
| --- | --- | --- |
| `vr_desktopStabilization` | `vr_desktopStabilizationStrength` | 50 |
| `vr_headStabilization` | `vr_headStabilizationStrength` | 30 |
| `vr_handStabilization` | `vr_handStabilizationStrength` | 40 |

Strength ranges from 0 to 100; zero bypasses processing. `vr_stabilization_status`
reports requested strengths and the gameplay/reset epoch. `vr_status` includes
desktop strength, effective FOV, and the fraction of requested correction that
fits inside the source image. Movement bob and recoil are separate settings.

## Pose contracts

`pose_filter.hpp` contains bounded, allocation-free position/quaternion filtering.
Half-life maxima are 15/45/120 ms for head/hands/desktop, multiplied by strength/100
and divided by `1 + normalized_linear_speed^2 + normalized_angular_speed^2`.
Speed references are 0.05 m/s and 30 deg/s for head, 0.25 m/s and 120 deg/s for
hands, and 180 deg/s for desktop rotation. Corrections are capped at 2 mm/0.25 deg
for head and 15 mm/2 deg for hands. These are initial tuning values, not a comfort
guarantee. Filters reset on discontinuities, invalid tracking, reference changes,
strength/bypass changes, backward time, or gaps above 150 ms. Duplicate sequence
IDs reuse their existing result. These filters are not motion prediction.

Hands are processed once after controller calibration at publication. The grip
filter defines one rigid correction also applied to calibrated aim; their relative
transform is preserved. Raw grip/runtime aim remain available as witnesses.
The fixed hand-alignment translation derives its device basis from that raw pair;
using filtered grip with raw aim would rotate alignment by the filter's angular
lag. The physical grip-to-wrist lever uses filtered grip, so it continues to
follow the same filtered pose as the held weapon.
The existing frame is the sole input to rendering, aiming and interaction.
Continuity changes invalidate motion history without synthesizing buttons or
changing holding leases when settings switch.

Head filtering is a small application-space root transform: `filtered * inverse(raw)`.
The camera and tracking-to-world conversion use the same captured correction.
The runtime's original tracking sample and OpenVR submission/prediction protocol
are retained. A newer sample cannot change an already captured spatial frame.
Head filtering can add tracking lag/discomfort: reduce strength or disable it if
uncomfortable. It is deliberately optional and bounded.

## Desktop composition

The native image ring snapshots camera axes, production timestamp, reset epoch
and projection from the exact right-eye scene before publication. The GUI uses
only that image's metadata, smooths three-axis orientation, and applies a UV
homography in the existing sRGB mirror pass. Both tangent extents shrink to 85%
to reserve a fixed border. Corner/forward-depth checks limit correction instead
of varying FOV each frame; no depth reprojection or extra scene render is used.

When the recording guide is enabled, its existing rectangle is the safe outer
envelope: the stabilized output stays inside it and is narrower. Sampling never
crosses into its border/dimmed pixels. The guide is not an exact moving polygon
for the stabilized output. Menus/console/pause use native UI; returning seeds a
fresh desktop filter. Device changes invalidate preview history.

## Validation and deployment

`vr-stabilization-tests` covers noise attenuation at 72/90/120/144 Hz, fast-motion
caps, quaternion wrapping, bypass/reset, rigid grip/aim correspondence, native
head publication and shared root conversion, scene retention, optical bounds and
recording-guide containment. `vr-desktop-mirror-tests` also runs the stabilized
pixel shader on a WARP gradient to verify homogeneous UV division and sRGB.
Launcher C++/Node tests cover settings, localization, saving and defaults; feature
parity checks verify the component is emitted in each executable.

Build RelWithDebInfo and Debug from the same source and use the paired deployment
helper described in development.md. The three settings are embedded in both EXEs;
this feature introduces no new external runtime asset.

Hardware acceptance remains separate: test each switch and combinations during
stationary aiming, fast turning, locomotion, reloads, melee, recentering, tracking
loss, pause/ESC/F10 and device recreation. Compare the optimized client for timing.
CPU/WARP checks and browser previews do not establish HMD comfort or WebView2 layout.
