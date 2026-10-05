# VR camera policy and integration boundaries

Camera behavior is composed from independent policies. A scene selects the
native pose source and the policy combination; locomotion, weapon permission,
body animation, and stereo eye separation remain owned by their respective
systems.

## Policy dimensions

| Dimension | Values | Meaning |
| --- | --- | --- |
| Head rotation (`head`) | `fixed`, `pitch_roll`, `limited`, `free` | Ignore tracked rotation; preserve physical pitch/roll; clamp rotation in the entry frame; or use the full tracked orientation. |
| Head translation (`translation`) | `fixed`, `attenuated`, `tracked` | Add no physical translation; apply scaled entry-relative motion with a spherical limit; or apply full tracked motion. |
| Script rotation (`script`) | `override_view`, `additive`, `ignore` | Use the native view as the base; add relative native changes from the first valid pose; or ignore later scripted rotation. |
| Rotation axes (`axes`) | `yaw`, `yaw_roll`, `full` | Select the native rotation components used by the script policy. |
| Native source (`source`) | final view or named tag | Select the uncomposited native camera pose or a specific native attachment transform. |
| Entry alignment (`entry`) | preserve or align once | Keep world heading or align to the native source once at the ownership transition. |

For example, `head=pitch_roll, axes=yaw` preserves physical head tilt while the
native scene controls horizontal heading. A freely observed vehicle can use
`head=free, script=additive, axes=yaw`. Full rotation composition uses matrices;
Euler components are not added directly. `yaw_roll` removes pitch from the
native source before composition while preserving tracked head pitch and roll.
Continuous branch tracking prevents a native pitch crossing vertical from
producing a false 180-degree yaw/roll flip.

Entry alignment and ongoing script rotation are separate controls. Aligning a
camera once does not change its owner, translation policy, or later rotation
policy. Recenter resets horizontal heading while preserving physical pitch and
roll.

## Data flow and ownership

| Component | Responsibility |
| --- | --- |
| `scripted_camera.hpp` | Portable policy, request, sample, and composition types |
| `camera_rig.hpp` | Rotation, limits, translation, reset, recenter, and exit handoff |
| `scripted_view.hpp` | Heading continuity and native-command handoff state |
| `scripted_position.hpp` | Translation baseline and limits |
| `gameplay/campaign/sequences/camera_policies.hpp` | Scene-to-policy mapping |
| `gameplay/sequences/*.cpp` | Native scene identity, phase, and permission adapters |
| `gameplay/scripted_camera_reference.cpp` | H2 DObj/tag lookup |
| `head_pose_bridge.cpp` | Tracking, command history, unit conversion, and core bridge |
| `camera.cpp` | Final native camera boundary and one shared camera composition |

Game-thread adapters publish value snapshots. At the final native view boundary,
the client resolves the scene tag and passes the native pose, tracked pose,
policy, and lifecycle identity to `camera_rig`. The portable core has no VM,
engine, or runtime calls and does not retain native entity pointers. Tag queries
run outside the tracking mutex. Rotation is composed once for the shared camera;
each eye then applies its own eye offset and projection.

Command and rendering paths use the same `camera_request_for` selection. Fixed
thermal optics take priority over mounted weapon views, which take priority over
scripted scenes, followed by ordinary gameplay. Rotation ownership determines
whether HMD yaw may be written into native commands. Translation does not grant
or revoke weapon permission. Javelin/thermal displays, native camera bob, and
stereo rendering keep their own responsibilities.

## Scene policy examples

| Scene | Rotation policy | Translation policy | Notes |
| --- | --- | --- | --- |
| Ordinary gameplay | Full physical head rotation; native command limits remain active | Tracked | Native yaw remains authoritative for gameplay input. |
| Scripted linked body | Free head rotation with native yaw or yaw/roll composition | Usually attenuated | Align once on entry when the linked body changes. |
| Free Cliffhanger climbing | Full physical head rotation; no scripted rotation | Tracked | The free carrier owns locomotion; the hidden story body does not drive camera or arm solving. |
| Vehicle or mounted weapon | Free head rotation with scene-selected native heading | Attenuated or seat-relative | Steering, weapon aim, and camera heading stay independent. |
| Fixed thermal optic | Fixed to the native optic pose | Fixed | Native optic rotation owns the view. |
| Locked story impact | Full native tag rotation | Fixed or attenuated | HMD rotation is disabled only for the identified story phase. |

The full scene mapping lives in `gameplay/campaign/sequences/camera_policies.hpp`. Scene
adapters must identify the native owner and phase explicitly; a map name or
generic animation label alone is not sufficient.

## Alignment and lifecycle

`camera_profiles::align_on_entry(policy)` adds one-time alignment to an existing
policy. `needs_tag` covers both alignment and ongoing scripted rotation, so a
scene that ignores later native rotation can still align to its entry tag.
Alignment is consumed by `camera_rig::apply_entry_alignment` once per
`entry_epoch`, normally the linked-object lifetime. If the source is not ready,
the alignment opportunity remains pending. Recenter, repeated sampling, and
head movement do not consume it again. Leaving and later re-entering the policy
can create a new alignment epoch for the same entity.

| Transition | Alignment source | Result |
| --- | --- | --- |
| Estate drag sequence to its ending | New `worldbody/tag_player` | Free observation with native yaw added |
| Second Sun ground to ISS | `iss_rig/tag_player` | Free observation; scripted rotation ignored after alignment |
| Second Sun ISS to ground | Moving `tag_origin` after native relink completes | Return to player-command camera ownership |

Checkpoint rollback has a separate load reset. It waits for valid gameplay and
tracking snapshots, clears stale camera/command baselines when loading into a
scripted or linked state, and requests recentering. Ordinary gameplay consumes
that load-reset opportunity too; it cannot be deferred until a later scene.
Pause, repeated samples, and animation-node handoffs do not repeat the reset.

Returning to player ownership uses the existing `restore_command` / `record`
handoff. Old prediction frames retain the corrected heading until a new native
command arrives, then ordinary input resumes. Scene adapters provide native
identity, phase, and tag; the composition core has no level-specific branches.

Position/alignment epoch, script-rotation source, tracking generation, and story
epoch are validated independently. A new DObj/tag creates a new rotation
baseline. Invalid sources hold the current heading; recovery does not apply a
catch-up jump. Exit hands control back once and preserves the distinction between
native Euler input and physical world heading.

## Porting and verification

When adapting the portable camera core to another VR mod, replace scene
observation, the native camera boundary, tag lookup, command history, and unit
conversion. Keep H2 addresses, field offsets, map markers, and asset names out of
the policy core. Normalize coordinates at the adapter boundary; the core uses a
forward/left/up row-vector basis and translation in meters.

Offline checks cover independent native/HMD yaw, fixed and free modes, Euler
branch continuity, recenter, invalid sources, angular limits, matrix composition,
translation modes, tag alignment, and ownership priority. They do not establish
native runtime behavior or headset acceptance. See
[diagnostics and acceptance](vr-diagnostics-workflow.md).
