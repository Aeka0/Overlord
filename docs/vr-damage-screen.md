# Native damage-screen effects in VR

VR presents the native blood-splatter and critical-health draws through the
existing HUD capture observer. The renderer captures the currently bound
shader, textures, lighting constants, UVs, and vertex colors. The original native
drawing continues, and the VR path does not invent a health threshold, damage
state, or recovery animation.

## Native sources

| Material | Native technique | Role |
| --- | --- | --- |
| `h1_fullscreen_lit_bloodsplat_01` | `lit_2d_hudblood` | Lit blood-splatter layer |
| `overlay_low_health` | `unlit_2d` | Critical-health red layer |
| `overlay_low_health_alt` | `unlit_2d` | Alternate critical-health material |
| `h1_screen_blood` | `unlit_2d` | Screen-blood material |

The two layers use different native materials and different color channels.
Preserve complete RGBA and the dedicated blood-splatter red channel; do not
reduce the effect to alpha-only tinting. Native command order, material, texture,
and blending remain the source of truth. The captured output is an immutable
transparent texture for presentation; the original assets and queued GPU buffers
are not modified.

## Stereo composition and lifecycle

The shared damage composition layer maps the captured native canvas to the
union of both eyes' angular fields of view before subtitles and black fades.
Each eye projects the same angular canvas through its own asymmetric projection.
Equal view directions therefore sample equal blood UVs, including at nasal
edges; the effect behaves as an infinite-distance overlay rather than a
near-plane surface with IPD parallax.

Both eyes retain the same capture lease for one pair. Empty or rejected streams
clear publication. Captures expire after 250 ms and are rejected after device,
graphics-context, or tracking-reference changes. Menus and pause suppress the
presentation. Native death and recovery remain controlled by the native draw
stream.

`vr_damage_screen_status` reports composed pairs, composition failures, the
last rejection reason, capture dimensions, and age.

## Acceptance

Headset acceptance should verify blood-edge visibility with asymmetric eye
fields of view, death/retry, recovery, pause/resume, level transitions, device
changes, and black fades. Both eyes must remain aligned through recentering and
reference-space changes. Asset names and desktop draw behavior do not establish
stereo headset acceptance.
