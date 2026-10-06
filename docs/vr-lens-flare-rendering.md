# Lens flare rendering in stereo

This guide documents the native lens-flare draw path, the VR stereo correction,
and the optional bounded diagnostic probe. Finite-distance light flares must
remain attached to their world-space source in both eyes; directional sun
flares and authored optical ghosts retain separate policies.

## Diagnostic probe

Start the game using the normal HMD workflow. Face a visibly misaligned
artificial light, execute `vr_flareSample`, then close the console within two
seconds. Hold the view approximately steady for another four seconds. The
diagnostic stops automatically and writes a uniquely named
`minidumps/overlord-flare-sample-<tick>.txt` relative to the game directory.
Repeat only when another view/light is needed. Do not arm GPU census.

The Debug-only component is isolated in `vr/diagnostics/native_flare_probe.cpp`.
Sampling defaults off, is never saved in configuration, and has separate
64-record budgets for frontend generation, left draw and right draw. Only
one capture runs at a time. No GPU contents, image files, exported assets,
native render-state writes, additional native waits, or process suspension
are involved. Release omits the probe and its command.

The production `native_flare.cpp` owns both call-site hooks and verifies their
native bytes before installing either. The optional probe forwards each
original exactly once; invalid production draw contracts reject only the
flare batch before entering the probe. Arming installs no hooks. Fixed-size records are
published with release/acquire readiness; callbacks never wait for the writer.
Formatting and file IO run on the asynchronous scheduler. In-flight unready
records are omitted rather than read concurrently. Native optional reads are
bounded and SEH guarded; failed reads set validity bits, not rendering errors.

## Proven native path

- `0x14042E1E8..0x14042E52B`: flare element world position at draw-state +0xA0
  is projected using FxCamera at +0x58, axis +0x70, tanHalfFov +0xA8/+0xAC.
  The flare-definition position multiplier is applied to the screen center.
- `0x14042E816 -> 0x140432510`: emits four 48-byte vertices with preprojected
  x/y and z=1, through the dedicated flare allocator. Source position/camera,
  original quad arguments and emitted bytes are observed at this boundary.
- `0x1402AB400` appends a 24-byte material/triangle/index/base-vertex record.
  `0x1402AB510` sorts these into the backend arena at +0xF89100 and publishes
  the scene's first/count at +0x2EA4/+0x2EA6. Flare mesh descriptor: +0x540C80.
- `0x1402950B0` sets the special native rendering mode with `0x1407A0590`.
  `0x140295118 -> 0x140294E90` then consumes that scene's flare records.
  Only the exact active production eye/record/owner thread permits a draw
  sample. Its eye view is copied directly, not obtained from a latest frame.
- Scene materials include `fx_glare_hotspot_rays2_add_z30_e20` and
  `fx_glare_hotspot_add_nodepth`. Their native vertex shaders still apply
  world/view-projection and eye-offset constants. Validate the exact owning
  scene record; nearby view and matrix fields are not interchangeable.

## Correction and state ownership

The frontend wrapper lifts each emitted native quad from NDC onto the source
light's depth plane using that emission's FxCamera. Native UVs, colors and
other vertex attributes remain byte-identical. This happens in the existing
mapped flare allocation before upload, with no new resource, copy pass, GPU
readback, wait or changes to the accepted dynamic-upload lifecycle.

Each backend flare draw temporarily uses the exact owning eye's view and
projection. Absolute light positions require baking that eye's origin into
the native eye-relative matrices exactly once. WORLD0 remains identity and
the eye-offset constant remains zero. The four input matrices at +0x2BF0 and
the depth-hack bit are restored on exit; dependent version stamps advance on
both entry and exit. GPU/cache-valid stamps must not be rewound. HUD and
other draws then recompute their original matrix constants as needed.

Geometry stays world-space throughout its native lifetime; no live toggle
can reinterpret an already queued arena. A non-stereo draw uses the passed
scene record's own view. Invalid perspective/mode/WORLD0 contracts skip only
the flare batch. Invalid finite-depth geometry collapses only its quad.

`vr_flare_status` reports installation, world quads, allocation/geometry
rejections, left/right/flat draws and rejected draws. Allocation rejections
can include native calls that produced no quad. Geometry/draw rejections
should be investigated if they rise while the visible test lights disappear.

The pure tests cover rotated cameras, large world coordinates, preservation
of vertex attributes, asymmetric stereo projection, depth-dependent parallax,
invalid input and scoped restoration without rewinding native cache stamps.
They do not establish actual shader output or replace HMD testing.

For headset validation, verify that finite-distance flare quads meet their
source lights in both eyes while the head turns and translates. Check depth,
occlusion, flashing, HUD composition, and right-eye rendering. Directional sun
flares and authored optical ghosts require separate scenario coverage; a
finite-distance correction does not establish their full stereo behavior.

Read `validity` independently per channel. Frontend bits 1/2/4/8 mean source
and quad arguments / emitted vertices / vertex-buffer identity / surface
record. Draw bits 1/2/4 mean special native view / post-draw matrix caches and
eye constant / vertex-buffer identity. Channel 0 is frontend; 1/2 are eyes.
The native view prefix begins at +0x2BF0. Other arrays can contain mixed-layout
bytes interpreted as float words;
padding is not a mathematical matrix.

GPU buffer identity and nearby timestamps do not alone prove frontend-to-pair
ownership. Compare arena identity, material/surface ranges, and repeated
observations. CPU cache evidence is explicitly not GPU buffer readback proof.
Keep finite-distance source flares distinct from directional sun flares and
authored optical ghosts. Do not apply a fixed pixel offset or invent a sun depth.
