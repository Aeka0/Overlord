# Native ADS and optic presentation

This guide covers automatic ADS selection, physical lens presentation, and
independent optic rendering. Native weapon permission, firing direction, and
the authored scope assembly remain authoritative.

## Dragunov housing visibility

ADS hides only the replaced optical lens and reticle in the normal surface mask.
The ordinary rear eyepiece, scope housing, intermediate glass surfaces, and
separately authored flattened ADS subtree retain their existing masks.

The independent optical scene instead starts beyond the forward housing.
The measured front end is +11.16 cm relative to the optic root, versus the rear
lens at -19.22 cm: a 31.4 cm axial clearance includes a 1 cm margin. Its endpoint
is projected onto the chosen source eye's camera-forward axis. This near-plane
override applies only to the private cropped scene request; both HMD views,
native materials and shared assets are unchanged. Existing circular lens/pupil
composition bounds the result to the sight. It requires `vr_scopeRender`; the
same-eye magnifier fallback cannot recover geometry behind its source pixels.
Woodland uses the same explicit optic variant rules. GPU/headset acceptance
is pending despite CPU projection and material-role regression checks.

## Controls

`vr_autoAds 1` (default, saved) requests native ADS when the currently projected
firearm is raised, aligned with the head and held by both its firing and support
hands. A nearby free hand is not a support grip. `vr_autoAds 0` disables the new
request on the next command. A/X continues to toggle the weapon HUD; B/Y retains
mechanical release. No new SteamVR bindings are required.

The policy uses a broad center-eye corridor behind the solved muzzle, not a
calibrated sight/eyepiece anchor. It admits either aiming eye. Relative to the
gun, entry requires:

- Head 0.15-1.5 meters behind the muzzle, within 0.09 meters laterally.
- Head between 0.025 meters below and 0.16 meters above the barrel axis.
- Head forward within 30 degrees of the barrel, continuously for 100 ms.

For admitted optics on long assemblies, the far-depth limit is the greater of
1.5 meters and the actual rear-optic-to-muzzle distance plus 0.60 meters of eye
reach. The exit limit and approach fade keep their additional 0.20 meters. Both
ADS intent and approach use this same assembly distance from the unassisted
solve, independent of whether the lens image is active. It reuses the reviewed
rear lens or native rear bounds already measured for eye clearance. This allows
the silenced M200 to aim without its long muzzle consuming the eye-to-sight reach;
the true muzzle remains the firing origin, and lateral/height/angle gates remain.

While aiming, the corridor expands to 0.10-1.7 meters behind, 0.13 meters lateral,
0.06 meters below / 0.22 meters above, and 40 degrees. Leaving this corridor for
100 ms releases the request. These are initial comfort values for headset
evaluation, not measured optical eye relief. Timing advances with new solved
pose samples; repeated command reads cannot turn a frozen pose into entry.

Lost/stale tracking, invalid geometry, menu/focus changes, calibration,
ownership changes, empty hands, ladders, mantle, mounted/scripted control and
disabling the setting cancel or gate the request. New valid alignment must dwell
again after cancellation. Releasing the support grip or losing either hand's
tracking cancels immediately, without the positional exit grace. Regripping
starts a new 100 ms dwell, including a release/regrip between command reads.
Single-handed alignment cannot enter ADS. Bullet weapons and the existing
supported launcher aim/lock-on route share this automatic request policy. A
weapon swap must match the final command's weapon token.

## ADS eye-distance assistance

Eligible two-handed aiming adds a bounded translation toward the eye along the
solved barrel axis. The maximum distances are:

| Installed optic | Maximum approach |
| --- | --- |
| Iron sights, red dots, Tavor MARS, unknown assemblies | Unchanged |
| EOTech holographic | 5 cm |
| ACOG, AUG and L86 low-power scopes | 10 cm |
| Sniper scopes | 15 cm |
| Thermal scopes | 45 cm, targeting 5 cm rear-lens clearance |

The complete weapon moves, including the muzzle, attachments and optic; sights
are not moved on their rails. Attached wrists follow with the existing arm IK,
while shoulders and free hands retain their original positions. Raw controller
tracking and the carry system's physical contact reconstruction remain unchanged.
Mechanical hand interaction suppresses the assistance so reload/part handling
does not acquire the aiming offset.

Approach follows the current unassisted head/barrel angle continuously. A
quintic smoothstep curve rises from zero at 45 degrees to full approach at 5 degrees;
25 degrees applies half of the available distance. It starts before the native
ADS entry angle/dwell is satisfied and remains independent of that boolean latch.
Position fades between the entry and exit corridor boundaries, preventing
parallel guns at the hip or side from being pulled inward. A critically damped
spring retains position and velocity using fresh sample time: 10/s for ordinary
optics and 24/s for thermal. Movement is limited to 25 cm/s for ordinary optics
and 90 cm/s for thermal; repeated reads cannot advance it. Turning/lowering reverses
the target smoothly instead of immediately reversing the velocity.
The quintic curve has zero velocity and acceleration at its endpoints.

Actual eye-to-sight distance also scales the target approach. The current tuning
retains full distance weight up to 30 cm, fades with the same quintic curve from
30 to 60 cm, and applies no approach at 60 cm or farther. At 45 cm the distance
weight is 50%. This multiplies the angular/positional weight before the spring,
so extending the sight gradually restores its ordinary position;
bringing it back reverses the transition smoothly.

Distance is measured from the center eye to the reviewed rear lens center,
or the native bounds' center projected onto their rear plane when lens metadata
is unavailable. It uses the unassisted pose from the same solve, in meters, so
the applied translation cannot feed back into its own distance weight. The
distance fade changes the target, while the 8 cm near-eye safety cap stays
separate and can still clamp immediately. Native ADS eligibility remains on
its shared positional corridor and support-grip policy, with the thermal angular
and dwell differences described below.

Grabbing/releasing only the support hand changes the target distance without
resetting the current offset or velocity. A large target change starts from rest
and obeys the speed ceiling; the sub-0.05 mm settling tail finishes once motion
is below 1 mm/s. Native ADS still cancels immediately on release; the rear
hand's weapon then eases back to its original single-handed position. Regripping
during that return smoothly reverses its existing velocity, without a snap. An
already single-handed gun cannot acquire new approach, and the released free
hand is not pulled along with the returning gun. Presentation permission is
kept separately from the grip-revision-bound native ADS request.

The maximum approach preserves at least 8 cm of axial clearance (5 cm for thermal) from the center eye to
the reviewed rear lens, or the rear native model bounds for optics without lens
metadata. This is a comfort limit, not a measured optical eye box. Already-close
optics receive no additional approach. Tracking/focus loss, firing-hand/weapon
ownership changes, reference changes and mechanical interaction clear the old
transition. Near-eye clearance still clamps immediately during a smooth return.

Classification uses the admitted assembled model, including reviewed color
aliases, rather than the weapon name or available magnifier materials. Thermal
uses the reviewed rear lens and retains its own comfort tier, with full distance
assistance through 50 cm and a smooth fade to zero at 70 cm. Its existing spring,
speed bound and two-hand ownership requirements also apply to the larger approach.
`vr_scopeZoom 0` disables only lens magnification; `vr_autoAds 0` also releases
the approach. Only the existing projected weapon can own the ADS request.

The published muzzle carries its comfort translation separately. ADS corridor
testing subtracts that translation, so the assistance cannot move its own input
across the near boundary and repeatedly enter/exit. Rendering, lens projection
and firing still use the actual translated muzzle from the same solved pose.
The translation precedes mechanical presentation, so separately submitted
inserted magazines, chamber rounds and partitioned meshes capture that same
gun pose. `vr_hands_status` includes the applied `ads_approach_m` per owned solver.

Controller-input, weapon-grip and spatial-panel/WARP regressions cover the ADS
corridor, required support lease, immediate ADS release, smooth positional return,
mid-return regrip, regrip dwell, eye-distance fade/return, feedback
exclusion, optic tiers, continuous angle/position weighting, frame-rate-independent
filtering, continuity resets, scale/rotation, near-eye clearance, attached-hand
contacts and free-hand isolation. HMD testing confirmed the improved angular and
support-transition feel; this eye-distance fade still needs headset
evaluation. Its three regression suites and both client builds passed; Debug and
RelWithDebInfo also passed the feature-parity audit. Both EXE/PDB pairs and input
bindings were deployed together with backups and installed SHA-256 verification.

## Boundaries

`controller_ads.hpp` is a pure policy. It consumes the existing muzzle publication
and input snapshot. The muzzle now includes head position and full head forward
from the same camera transaction used to solve the weapon. No second tracking
poll, independent head/muzzle world-origin mix, engine call under a lock, render
wait, or per-command allocation is introduced.

The command adapter adds `game::BUTTON_ADS` after native FinishMove has selected
the weapon and before the command enters its history ring. It does not clear
keyboard ADS, edit playerState, change WeaponDef, force a weapon animation or
override native eligibility/transition timing. Existing independent bullet
firing still has its own cadence and native ADS-baseline dispersion; this input
change does not merge it into PM_Weapon's firing state machine.

Independent carried-object presentation also requires the skinned arm-visibility
hook. It is installed in all build configurations. The former Debug-only gate
left optimized clients on the native first-person DObj, allowing native scoped
ADS visibility/animation to remove weapon geometry. Restoring independent
objects fixes that build discrepancy; it does not broaden the physical-lens
surface mask or change native gameplay ADS eligibility.

With independent carried weapons, only the existing native projection can own
this ADS request. The prototype does not select a different projection by eye
alignment or provide two independent ADS states. Optical view ownership will
need that separate design.

The captured H2 keyboard builder clears bit 11 at `0x1403D0F79` and sets it at
`0x1403D0F82`. Both instruction signatures are checked at startup; a mismatch
disables automatic ADS alone. The local native text capture was inspected for
these instructions; that archived capture is not a build/runtime dependency.

`vr_input_status` reports `auto_ads`, `verified`, `requested`, and `ads_commands`. These show
VR intent and commands carrying the injected bit, not proof of native acceptance
or rendered thermal pixels. HMD projection and PostFX remain on their current
paths. See [independent optic-rendering feasibility](vr-optic-rendering-feasibility.md).

## Physical-lens image magnifier

`vr_scopeZoom 1` (default, saved) enables the lens presentation for an accepted
automatic ADS request. `vr_scopeZoom 0` restores ordinary lens surfaces on the
next solve. `vr_scopeMagnification 0` uses prototype defaults: ACOG/AUG/thermal 4x and
sniper scopes 6x. Values 1-12 override the image magnification; 1 is useful for
checking transparency and reticle placement. These are prototype ratios, not a
claim that native WeaponDef FOV values equal optical magnification.

The catalog covers ACOG, M200, Dragunov, M14/M21, M82, WA2000, AUG and thermal scopes.
Known color variants reuse the same geometry only after native model/root,
material, rigid-surface and reticle-image checks. Unmatched or skinned layouts
keep their previous presentation. Red dots, holographic sights and Tavor's MARS
are excluded. Thermal uses the independent native scene described below;
ordinary-color image magnification is not reported as thermal imaging.

Offline source review found that `tag_scope_ads_off` includes opaque lens
surfaces as well as the physical tube. Hiding this whole bone would remove the
tube. The new adapter selects only the identified lens and existing reticle
surfaces within that object's admitted assembly. It passes those exact surface
pointers to the existing immutable rigid-group visibility filter. Native assets,
world-model copies and material definitions are never edited. The flattened
`tag_scope_ads_on` assembly stays hidden.

Lens centers and conservative radii come from rear-lens vertices in the existing
SEModel exports. The tracked catalog contains only reviewed geometry metadata;
exports are not runtime dependencies. The native reticle color image is reused,
including its alpha, without exporting or shipping textures. The image is
composited after magnification so target zoom does not enlarge the reticle.

Both the lens mask and optic metadata are frozen in one solved-pose transaction.
The existing skin/submission sidecar binds that pose to the exact stereo scene.
Scene registration now belongs to the shared render-pose component, independent
of the Debug weapon-HUD component. Both eyes share one pose and each reads its
own current display image. Pair, publication, device, owner and native model
placement checks prevent reuse across scenes, handovers and device rebuilds.

The shader compresses sampling coordinates around the optical axis inside a
projected circular rear-lens quad. Eye translation moves the reticle relative to
the tube. An eye outside the aperture sees a dark lens; the other eye is never
copied into it. The initial viewing distance is 3-80 cm. Native GPU state is
restored through the existing marked deferred command-list path. No extra scene
render, CPU readback, render wait or change to the HMD projection is added.

This is image magnification: it enlarges available pixels and cannot add distant
detail. Sampling contains whatever is visible through the opened tube in that
eye's completed scene. Severe off-axis viewing and objects crossing in front of
the lens can therefore differ from a separately rendered, depth-tested optic.
The conservative circular aperture and image-bound checks limit sampling of
the surrounding tube; this remains a first headset candidate.

`vr_optics_status` reports successful bindings, rejected layouts, draws, misses
and the current reason. A successful GPU draw is not proof of native asset
admission on every variant or real-headset acceptance.

## Independent scene prototype

The extra-render candidate adds `vr_scopeRender 1` (default, saved).
`vr_scopeZoom` remains the overall lens switch and `vr_scopeMagnification`
retains its existing ratios. Set `vr_scopeRender 0` to select the earlier image
magnifier explicitly. Thermal requires independent rendering; unsupported reflex optics keep their existing
paths. Headset acceptance and performance measurements are pending.

The candidate renders one additional native scene when a supported optic has
an eligible aiming eye. Eye choice has hysteresis. The other eye receives scope
shadow rather than a copy of the aiming eye. The camera is an exact off-axis
crop of the selected eye's finalized projection, enclosing the compressed lens
rays; it preserves origin, near plane and jitter. Projection, VP, inverse VP,
legacy half-angle scalars and fog ray constants are updated together. This
increases angular sampling without changing either HMD projection or adding
new rays outside the existing eye's prepared visibility.

Native scene consumers execute left, optional optic, then right. The optic has
a separate scene record and SSR image/matrix history. Left display color and
depth are copied before the optic can overwrite native scratch targets; after
the optic image is retained, left display is restored to its original native
texture identity for the existing cached transport conversion. Left composition
then uses the saved depth and current optic image, followed by ordinary right
rendering/composition. The right eye remains the last dynamic-upload consumer
and the two-eye publication boundary. No native scene is invoked from a
composition callback or while holding the native-session lock.

History resets cover weapon/holder, aiming-eye, reference/device generation and
large crop-scale changes, plus the existing main-view history reset. A missing
independent image produces scope shadow; it is never mislabeled pixel zoom.
Native owner/resource restoration failures retain the pair rejection path.

This first candidate uses the full native eye extent for the optic and reuses
the completed frontend scene lists, including their original LOD/streaming and
first-person geometry. It is an additional rasterization pass, not independent
high-detail scene preparation or an objective-centered telescope simulation.
Small target tiers and an exact auxiliary first-person filter remain follow-up
work. Full-size color/depth retention and a third history increase memory use.

The lens now supports read-only reverse-Z depth testing in independent mode.
WARP verifies ordinary scene occlusion and saved per-eye depth. H2's foreground
depth-hack interaction with hands and scope housing still needs headset
evaluation; offline depth tests do not establish that native-space match.

`vr_optics_status` reports `mode=extra_scene`, `render_requests` and
`rendered_images`. The mode is configuration; increasing `rendered_images`
means the compositor consumed a same-frame extra scene image. Native owner CPU
time is reported as `auxiliary_invoke` in `vr_status`; it is not GPU time.

Offline validation includes `vr-optic-render-tests` (crop/inverse mathematics,
history ownership/reset, upload ordering and WARP color/depth composition),
the expanded `vr-d3d11-eye-resource-isolation-probe` (three independent histories,
cross-frame persistence and state restoration), and the existing stereo and
spatial-panel regressions. The optimized client compiled successfully.

For initial headset evaluation, use an M200 and a distant fixed target. Compare
`vr_scopeRender 0` and `1` at the same 6x ratio, then test 1x alignment, eye
translation, left-eye aiming, handover, cover, lowering/raising and recentering.
Inspect both eyes and record frame timing. Repeat with ACOG after M200. The
carried thermal implementation below still needs headset acceptance.

## Carried thermal scopes 

Thermal stays on the same automatic ADS + physical lens path as ordinary scopes.
The base, tan, and arctic models use the following reviewed assembly:
ten surfaces, a 20.75 mm rear glass disk at root-local X=-5.706 cm, and the native
`wpn_h2_thermal_new_scope_reticle_col` color map. The 21 mm display overlaps that
glass slightly, while exact material matching replaces the seven layered rear
screen/reticle/glass surfaces. Both native housings and the forward glass remain.
The private scene near plane clears the front housing without removing it from
the ordinary weapon scene. Prototype magnification is 4x; the existing override
still applies.

The carried and fixed M82 paths share one signature check of native PostFX.
Only the auxiliary record sets full-thermal bit 1 at `+0x204`; native activation
bit 0 must already be present. Heat, thermal color, grain and scanlines remain
native. Neither world-eye record nor the desktop tail is modified. There is
one extra scene for the selected aiming eye; the other eye retains the existing
dark aperture. A missing/mismatched thermal scene produces a dark lens, and
`vr_scopeRender 0` keeps the native lens intact rather than presenting magnified
ordinary color as thermal. Scene identity and history include thermal mode,
weapon generation, firing-hand revision and tracking reference.

Spatial-panel and optic/WARP checks cover the captured material mask, thermal
comfort classification, five-centimeter target across scale/rotation and normal
aiming reach, extended-arm attenuation, ordinary optic isolation, native thermal
activation and private-record/history behavior. These are offline checks;
heat visibility, near-eye comfort and runtime cost need headset acceptance.
`vr_optics_status` reports `native thermal scene lens` when its image is composed.

The follow-up tuning accelerates thermal acquisition/return independently of
ordinary high-power scopes: 24/s spring, 90 cm/s speed bound and 40 ms ADS
entry/exit dwell. Thermal head/barrel intent accepts 35 degrees on entry and
45 on exit; ordinary optics retain 30/40 degrees and 100 ms. The native ADS
translation validator now accepts the same 45 cm maximum as thermal comfort,
instead of invalidating it beyond the old shared 20 cm limit. The admitted
thermal capability stays published while the lens image is inactive so this
validation cannot oscillate with the ADS latch it controls.

Thermal's virtual exit pupil is twice as wide and can display a partially
covered image. CPU admission and shader sampling use the same tolerance; the
physical 21 mm lens, aiming-axis UV, authored reticle, selected eye, depth test
and covered source-eye crop remain exact. Departing the pupil reveals progressive
scope shadow instead of rejecting the whole image at the old full-coverage
boundary. Ordinary optics retain their previous pupil and full-coverage rule.
The  rim correction calibrates thermal's reticle-space aperture to
0.94, inside the native image's fully opaque annulus (alpha 255 at radii
0.90-0.94). Calibrated artwork apertures are independent of the doubled eye-box
tolerance, so the transparent outer tail cannot reveal scene pixels beyond the
ring. Physical lens size and reticle scale/coordinates stay unchanged; the
wider ADS/partial-view admission still applies. WARP regressions cover aligned
and laterally shifted thermal rims alongside the existing WA2000 case.
Controller, comfort, projection and WARP regressions passed, as did the optimized
client build; this new tuning still needs headset acceptance.

SSR suppression and world dimming are investigated separately in
[thermal world-isolation research](vr-thermal-world-isolation-research.md).
The first candidate splits native rendering flags and SSR contribution per
scene record: world eyes use ordinary rendering/current native color, while the
scope retains its original thermal record. Native shellshock/lightset state is
preserved and still requires headset checking for residual world dimming.

## Verification

Controller policy regressions cover dwell, hysteresis, repeated samples,
stale/future data, nonfinite geometry, invalid hand indices, either hand,
support changes, instance replacement, focus/calibration/reference continuity,
world scale, rotated/translated poses, and lowering/looking away.

Debug x64 client and controller-input tests passed locally for the input stage.
The magnifier's spatial-panel WARP tests verify actual target-pixel enlargement,
separate eye sources, fixed-size reticle composition, a dark invalid eye box,
unchanged pixels outside the lens, source/destination alias and extent rejection,
and restoration of native GPU state. Pure tests cover lens projection, world
scale, catalog exclusions, ownership metadata and invalid geometry.
The final magnifier candidate built successfully in Debug x64; controller-input,
spatial-panel (including WARP), and weapon-grip regressions passed. Native asset
admission and aiming comfort require headset validation.

The first deployed magnifier showed no visible change. Read-only inspection of
an M200 runtime diagnostic reported `bound=0`, numerous binding rejections and
`draws=0`, with the installed image matching the candidate SHA-256. Its native
eight-surface scope uses `m/mtl_h2_sni_cheytac_lens_base` and
`m/mtl_wpn_h2_cheytac_ret`; the ADS glass uses the `mc/` category. Exported
SEModel names omitted these prefixes. The model topology and rigid lens/reticle
surfaces matched the existing contract; the color-map reticle had semantic 2,
a 512x512 image and a live SRV.

The corrected matcher strips only `m/` or `mc/` once before catalog comparison.
Captured M200 material-order regression selects exactly surfaces 4 and 5,
preserving its housing, laser and other surfaces. Arbitrary and nested prefixes
remain rejected. `vr_optics_status` now retains `binding` separately from
`presentation`, so a later missing-pose result cannot erase the binding failure.
The corrected Debug build and spatial-panel/WARP regression passed; new headset
appearance remains pending after redeployment.

Subsequent headset feedback reported square reticle-texture edges at large eye
offsets. WARP reproduced both scene leakage past the shifted UV rectangle and
visible square corners inside the physical lens. The lens shader now intersects
the physical circular aperture with a circular exit pupil centered on the aiming
axis before reticle sampling. Outside that pupil is opaque scope shadow; both
circle boundaries fade toward black. Lateral and diagonal GPU regressions
verify the shadow while preserving the visible target and the outside world.

The  adjustment keeps physical display coverage separate from reticle
masking. M200 uses a 20.4 mm display radius: a live mesh cross-section measured
the rear inner wall at approximately 19.9–20.2 mm and the outer wall at 25.9 mm
or more. WA2000 keeps its 19.5 mm display radius and limits its shifted
reticle-space pupil to 0.94, inside the texture's black annulus; transparent
texels outside that annulus cannot reveal the scene again. WARP regressions
cover both aligned and laterally shifted pupils.

A read-only Dragunov sample recorded 277 newly consumed independent images in
20 seconds while the lens pose moved. Its separate front disk uses
`m/mtl_wpn_h1_shared_lens`, 19.76 mm ahead of the display plane, and was missing
from the lens replacement. While the optic is active, the Dragunov descriptor
hides that disk, the original lens/reticle, and the rear eyepiece shells:
`mtl_wpn_h1_sni_dragunov_scope_eyepiece_base` and
`mtl_dragnunov_scope_ads_base`. The front tube and mount retain their shared
`mtl_wpn_h1_sni_dragunov_scope_base` material. Lowering the weapon restores the
ordinary model through the existing ADS visibility gate. This does not claim a
general auxiliary first-person filter or completed headset acceptance.

Headset evaluation should check red-dot, ACOG, sniper and thermal variants:
raise/lower repeatedly, look away while holding the gun up, move and turn while
aiming, transfer hands, holster/drop, open the menu and recover tracking. Check
native state/animation and both-eye presentation separately. Check transparency
at `vr_scopeMagnification 1`, then 4/6x zoom while aiming at a distant fixed mark;
move the head within and outside the eye box, fire, and check reticle alignment.
Thermal visibility is unresolved. Compare with `vr_scopeZoom 0` for lens changes
or `vr_autoAds 0` for the native ADS request.
