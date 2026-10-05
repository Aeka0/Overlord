# Physical ladder climbing

Physical ladder climbing uses a script-owned carrier and collision-approved body motion. Basic climbing works in the trainer. Current headset feedback identifies movement-jitter, palm-fit, and upper-rail clearance work; those comfort checks remain separate from the implementation contract.

## Option and controls

Launcher **VR settings > Gameplay > Climbing > Physical ladder climbing** uses
the saved `vr_physicalLadders` boolean, default **on**. It is also available as a
runtime console dvar. Off preserves the native ladder admission/movement path;
turning it off during a physical grasp first releases the owned carrier. Loading
a save containing that carrier releases it even when the option is now off.
Flat gameplay and scripted weapon-disabled traversal retain native admission.

An empty hand near an admitted mesh rung presses Grip to acquire a fixed contact.
One or two hands can support the body, including two positions on the same rung.
Physical controller displacement pulls the body in the opposite direction.
Motion is measured by projecting both input snapshots into the current tracking
reference; carrier/camera displacement is not counted as another physical pull.
Two hands average their targets rather than doubling displacement. Grip release
immediately ends that support. The last release hands back to native physics.

The body solver commits absolute collision-approved body positions to
the carrier. It does not add a delayed player-state delta onto an already moved
carrier. Ordinary pulling changes body position on all three axes; room-scale head motion
and free look remain tracked. Player-owned tracked cameras keep the existing
tracking origin across entry and release, so grasping cannot cancel a lateral
head offset. The owned carrier retains its script entity reference until deletion;
a temporary VM object number must not become a new camera lifetime every tick. Adding/releasing a supporting hand rebases the
remaining support goals so an old unaveraged goal cannot become a body jump.

The renderer uses the latest held-hand displacement since the published physics
sample to preview motion between server steps. That preview is projected onto
the current pull direction and clipped to a segment swept on the server with the
full player capsule (at most 12 cm each way). Orthogonal changes wait for the next
collision-approved body sample; separate axis limits cannot authorize untested
diagonal travel. It never writes physics or runs server traces on a render thread.
The next server sample supplies a new baseline, avoiding duplicate displacement.
A short render-only filter smooths this position; duplicate camera calls for the
same native view time reuse the same result. Scene discovery/mesh work
is paused while contacts are held; those contacts still receive native checks.
## Downward movement and ground release

The adapter retains the full pre-link player capsule, including the feet. It
does **not** use Cliffhanger's shortened hanging capsule for ladders. Each pull
uses the shared bounded collision slide. Ground contact consumes blocked pull
accumulation, so continuing to raise the controller cannot accumulate a later
downward launch. Bottom release probes support with that capsule: a supported
player receives zero velocity and ordinary standing; an airborne player receives
a small outward/downward release and native gravity. Original world collision
and ladder-adjacent script blockers remain authoritative.

## Integration and geometry

- `ladder_native.cpp` validates and wraps H2's ladder detector at `0x1406880D0`.
  The native caller at `0x14068FD25` supplies `(pmove_t*, pml_t*)` and tests
  `PS+0x54 & 8` before entering `0x14068AB00`. With the option on, only the local
  eligible player's automatic ladder bit is suppressed after native detection.
  With it off, the detector is called unchanged without post-processing.
- `ladder_scene.cpp` discovers static and single-model script instances. Mesh
  extraction uses bounded, incremental LOD0 indexed geometry reads and caches
  actual horizontal edges. It never divides a collision box into synthetic
  rungs. Supported asset families include ladder-named models, the sampled Mack
  truck family and the sampled submarine bridge candidate. A family match alone
  does not authorize a grasp.
- Ladder-named mesh edges are reduced to the dominant repeated rung plane and
  collapsed into rung centres. Forward ledge rails and separate top/hook models
  are excluded. The captured trainer regression resolves nine main rungs and
  rejects its forward upper rail for both acquisition and top selection.
- Each candidate and retained support must still hit a current native
  `SURF_FLAG_LADDER` face; ordinary visibility checks exclude intervening solids.
  This preserves one-sided Oilrig admission, hidden/moved submarine collision,
  destructible pre/post state and nearby scripted blockers. Object leases change
  when a model or entity identity changes.
- Static placement rotation and scale are applied to mesh contacts. Script
  instances use current server origin/angles and unit scale, limited to the
  witnessed single-root rigid form. Animated/multi-model geometry is rejected.
- `ladder_runtime.cpp` is a provider in the existing hand coordinator. It owns
  no separate Grip edge history and cannot steal occupied hands. It uses native
  `playerlinktodelta`/ground-reference operations with an invisible non-solid
  carrier, following Cliffhanger's established movement mechanism.
  The ladder link grants 180 degrees of yaw in either direction and 85 degrees
  of native pitch; its former zero view limits are removed. The controller owner
  suppresses walking/stance while supported but allows ordinary turn input.
- `free_climb.hpp` supplies retained pull targets, rebasing and two-hand
  averaging; `climb_collision.hpp` supplies bounded slide sweeps. Ladder release,
  full-body bounds, face admission and exit rules remain separate from ice picks.
- Hand presentation uses the common arm solver and closed-finger library.
  Gameplay never consumes the rendered hand pose as its movement input. The
  physical carrier keeps normal tracked head translation. Native story and
  vehicle ownership supersede the ladder, and stale tracking rebases motion.
  The bar grip now binds a partially wrapped native finger pose and its contact
  centre from the destination hand skeleton. An anatomical knuckle frame aligns
  with the horizontal rung, and the native wrist basis is cancelled exactly once.
  The prior full-fist/guessed wrist offset is no longer used for ladder grips.
  Acquisition reconstructs this same palm centre from current tracking and the
  immutable model-local binding, instead of testing the wrist behind the palm.
  Final arm presentation enforces each held bar contact after weapon posing on
  empty, weapon and selection-transition models. Only the climbing arm branch is
  committed; the opposite hand, gun and muzzle keep their solved transforms.

The agreed scope excludes carrying the deployable sentry while climbing. The
presence of a sentry climbing-animation branch is not evidence of playable native
support and is not used to authorize that combination.

## Diagnostics and limits

`vr_ladder_status` prints and saves
`minidumps/h2-mod-vr-ladder.txt`: admission readiness, phase, held hands, carrier,
move/fall/exit/block counts, top height, cache state and last reason.

Meshes without usable horizontal indexed edges, the unresolved invisible world
ladder sample, animated/multi-model assets and unverified script-model scaling do
not receive invented anchors. The native option remains available for those
cases. Some visible top rail/ledge geometry still needs in-game assessment; mesh
edge contacts are not a substitute for checking wrist/finger fit in the headset.

Focused validation covers scaled/rotated contacts, two-hand averaging and shared
rungs, immediate release, floor sweep and removal of blocked downward motion,
blocked/invalid exit paths, hand ownership, launcher defaults and option
persistence, captured trainer rail exclusion, bar contact placement and render
translation continuity. Both native-mode switching and physical entry/top-out/bottom release
still require gameplay acceptance on the captured representative ladders.
