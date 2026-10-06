# Physical reload diagnostics

 added measurement and conservative magazine culling padding; M1911
slide-grasp position was accepted, intermittent insertion remained unconfirmed.

 HMD visualization feedback: waist-drawn magazines can approach at
about 75 degrees, beyond the former 32-degree limit. After an angle rejection,
the player often already holds the tip above the former shallow upper boundary.
The shared pistol policy was subsequently refined by  HMD feedback.
Current tuning applies to M9, M1911, Desert Eagle, USP/suppressed USP and the
other existing pistol-well adapters, for both bare-hand and knife manipulation:

- Angle limit approximately 95 degrees (cosine -0.08715574), formerly 85 degrees.
  A small overshoot past perpendicular is allowed; reversed magazines still fail.
  Position, occupancy and tracking guards remain required.
- 6 cm below and 9 cm above/inside the actual mouth. The latest increment extends
  only the upper boundary by 3 cm, moving the volume center upward 1.5 cm along
  the insertion rail. Diameter stays 7 cm (radius 3.5 cm).
- These tolerance changes do not move the physical mouth, seated pose or ejection
  rail. The separate knife-hand fits are documented in [melee](vr-melee.md).
  M4 and revolver capture tuning are not modified.

Both the controller and overlay read the same immutable profile. Correcting an
angle while already inside the upper half is eligible on the next consumed input;
there is no new dwell or requirement to cross the mouth plane again. Existing
spawn/teleport/failed-transaction withdrawal guards remain. Offline tests cover
the two reload orders, 75/84-degree insertion, angle correction after overshoot,
remote-contact rejection and rotated gun/Deagle magazine geometry; HMD acceptance
is still pending. Do not close the intermittent issue from these tests alone.

## Startup selection and commands

In VR Settings > Debug, select **Magazine well geometry** and restart the game.
The saved `vr_reloadWellDebug` selection defaults off and works in Debug and
optimized builds. Console changes apply on the next launch. **Weapon event
recording** separately enables native reload/closed-bolt observations and automatic
reload-boundary reports; ordinary ammunition hooks remain active when it is off.

- `vr_reload_interaction_status`: report native ammunition, physical instances and asset
  admission in `minidumps/overlord-physical-reload.txt`. The leading
  `runtime_models`, `registration_failures` and `asset_retirements` counters
  distinguish rejected subset registration from a missing contact scene.
- `vr_reloadWell_status`: print a bounded copy of the most recent debug sample,
  its age, current geometric coordinates, last simulation decision and draw counts.
  This does not read native assets, walk scene pointers or change ammunition.

The overlay is independent of the A-button ammunition HUD toggle. It needs an
admitted physical-reload weapon, tracked controllers and an unpaused VR scene.
The two eyes use one matched native skeleton epoch and the current scene's
placement/eye origins, with separate finalized eye projections. No smoothing,
previous-frame placement, latest-pose substitution or desktop projection.
There is no depth occlusion: lines intentionally remain visible through the gun.

| Mark | Meaning |
| --- | --- |
| Cyan cylinder | Actual capture radius, lower reach and upper contact depth from the current profile |
| Faint yellow cylinder | Expanded hysteresis/withdrawal neighborhood; not another insertion region |
| Cyan cross at mouth | Mouth plane at well-local Z=0; cylinder extends along that magazine's insertion rail |
| Large tip cross | Current raw controller-driven magazine top: white outside, green inside and aligned, magenta misaligned |
| Small blue tip cross | Rendered held-magazine top, allowing raw-versus-IK comparison |
| Diamond | Last simulation-consumed tip in well-local coordinates: red requires withdrawal, green contact latched, orange otherwise |
| Short red bar at mouth | Old magazine still occupies the well |

A green cross means only geometric eligibility, not a successful ammunition
transaction. The diamond intentionally represents an earlier simulation sample;
its color must not be interpreted as the current cross's verdict. Both are
expressed in the current well frame. If it is red, move completely outside the
yellow neighborhood before approaching again. A stale status sample is labeled
by age; opening the console can pause scene updates.

## Next HMD test

Repeat both reported orders with the overlay enabled:

1. Hold a new magazine, approach the well, then release the old magazine.
2. Fire, immediately release the old magazine, take a new magazine and insert it.

Include steep waist-draw angles and a tip already 8-9 cm into the grip. Correcting
the angle there should not require lowering and reinserting. A fresh aligned tip
10 cm below the mouth must stay white and must not snap in. Check that cyan extends
6 cm below and 9 cm inside along the gun's well rail, not world vertical.

If insertion waits, hold still briefly. Note cross color, diamond color and the
red occupied bar; then run `vr_reloadWell_status`. A green tip inside cyan plus
a red diamond is different from an out-of-radius white tip or misaligned magenta
tip. Do not infer that the intermittent issue is fixed from normal attempts alone.
Also test quick locomotion, eye edges, HUD hidden, weapon switches and disabling
the diagnostic. No game launch or headset connection is automated.

## Renderer and culling boundaries

`reload_debug` shares the bounded pose history and stereo composition for the
independent magazine-well and HK-slap diagnostics. It owns copied gameplay
metadata only. `reload_well_geometry`
builds geometry directly from the shared reload profile. `spatial_lines` and its
renderer are generic: at most 192 segments, clipped before perspective division,
one instanced draw per batch, cached per-device shaders, no GPU readback. The
well needs one batch per eye. HK slap uses a geometry batch and two text batches;
each text batch has a wider dark underlay for bright backgrounds. Deferred
command recording and ExecuteCommandList(TRUE) restore all native D3D state.
The diagnostic eye-composition layer does not replace the spatial HUD consumer.
Each disabled diagnostic skips pose publication and drawing/resource creation.
Both use the same composition consumer, so they can coexist without replacing
one another or the ammunition HUD.

Independent held and dropped magazines receive 0.25 meters of extra radius at
the native model frustum-admission call. The generic scene-model submission API
converts world-unit padding to local units for the native scale multiplication.
Padding is scoped by model and thread, restored after submission, and bounded;
ordinary engine submissions retain their radius. This does not scale meshes,
modify shared XModels, enlarge interaction regions, disable depth occlusion,
change LOD policy, or bypass the exact-epoch attachment validity checks.

The native callsite is byte-verified before installation. This is a conservative
early-frustum allowance, not a blanket claim that every possible visibility
failure has been eliminated. HMD edge-of-view acceptance is still required.

## Cylinder admission and empty native stores

`vr_cylinder_status` also writes `minidumps/overlord-cylinder.txt`. The report
contains the latest presented cylinder's token, native name/capacity, native
ammunition validity and counts, mechanical phase, loader ownership and rejection
reason. Native observation runs on the server scheduler; file I/O runs async.

An owned weapon with no matching native clip or reserve cell is empty, rather
than invalid. Observation never creates cells or grants ammunition. A compared
positive write may initialize a vacant cell after both destinations pass
preflight; duplicate identities, invalid counts and full destinations still
reject the transaction without changing either store.

The six-round `coltanaconda_shepherd` variant uses the verified Magnum receiver
and cylinder profile alongside `coltanaconda`. Native akimbo and unknown variants
are not admitted by this alias. Receiver part assembly and hiding the original
animation-only loader do not wait for mechanical admission or extracted-part
assets. Drawing an independent loader still requires those assets to be ready.

## Asset lifetime and long status reports 

A captured session could hold and pick up guns while Glock and ACR physical
interactions had no admitted contact scene. Their independent magazine assets
reported rejected runtime-model registration. The old magazine and cylinder
caches created another retained set when player storage changed or game time
went backwards; both retained subsets and the shared identity registry survived
native zone unloads. This permits session-long resource exhaustion. The crash
dump did not contain the registry counter, so its exact occupancy in that
session was not measured.

Checkpoint changes now reuse subsets of the same loaded source. At the existing
drained native asset-unload boundary, magazine, cylinder and tube presenters
discard borrowed frames and releases, free their retained subsets, and reset
the shared identity registry. No descriptor is recycled during queued rendering,
and physical ammunition readiness remains required for firing.

The subsequent crash was a separate diagnostic-output failure: `vr_carry_status`
passed a report longer than 4 KiB into the secure CRT formatter with an invalid
buffer/count combination. Matching dump/PDB frames identify `console::format`
and the carry-status server callback. Console formatting now sizes long output
with copied varargs, preserves complete reports up to 1 MiB, and emits an
explicit bounded message above that limit. Regression tests exercise the old
boundary, mixed varargs, UTF-8, concurrent writers and repeated drained registry
reuse. In-headset recovery across checkpoint and level transitions still needs
runtime acceptance.
