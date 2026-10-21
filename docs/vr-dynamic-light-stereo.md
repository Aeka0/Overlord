# Dynamic-light XModel stereo alignment

The reported symptom is a detached, translucent-looking lighting layer on
XModels: it moves outward in opposite directions in the two eyes. Muzzle-flash
illumination exposes it briefly; the helicopter spotlight on Oil Rig provides
a sustained reproduction. The report does not establish that world surfaces are
unaffected, or that every visible component of the layer is specular reflection.

## Native path and missing scope

H2 has two distinct collections of draw-list descriptors. The camera lists
inside the scene record already receive per-eye origins. Dynamic lights also
publish up to eight descriptors in frontend data at `+0x541C08`, with count at
`+0x542588` and stride `0x130`. Cloning the scene record does not clone this array.

Native `0x140751720` publishes `LIGHT_SPOT`, `LIGHT_OMNI`, shadow/cucoloris and
fog lighting techniques, a scene-record pointer at descriptor `+0x108`, and a
camera origin at `+0x110`. These are camera-facing lighting contributions, not
the light-space views used to generate shadow maps. `0x1407A3250` and
`0x1407A33B0` merge them into the draw executor alongside the base scene list.

For instanced XModel passes, `0x14073CD6D..0x14073CD90` and
`0x14073E310..0x14073E337` obtain the current origin from that descriptor.
`0x14073F124` forwards it to the instance matrix builder. The ordinary matrix
path instead uses the current backend eye at `+0x2D60`. Retaining the center
origin only in the lighting descriptor therefore gives the two passes different
geometry translations, with opposite half-IPD errors for the two eyes. Updating
the ordinary eye constant alone does not repair this path.

The source audit used retained native-code captures. It establishes the omitted
descriptor scope; it is not a capture of the currently reported Oil Rig draw.

## Ownership

`engine_stereo_light_views::scope` installs the current eye's scene pointer and
camera origin only for the native dynamic-light technique family whose record
identity and original camera origin match the admitted scene. All entries are
validated before any write. The existing native input-completion gate joins
frontend producers before installation; the native owner consumes left,
optional auxiliary, and right views serially.

At each view's completion, the original metadata is restored. Transaction cleanup
also restores it after a failed view admission. Surface streams, light identity,
material techniques, other records and shadow-map views are not rewritten.
There are no new render hooks, GPU passes, waits or shader replacements.

The optional scene-batch census normalizes these metadata bytes only after
checking that they match the observed eye record and origin. Geometry/stream
differences and stale view metadata remain visible in its comparisons.

## Validation boundary

The stereo smoke test exercises spotlight, point-light, shadow/cucoloris and fog
metadata; left/right/auxiliary restoration; foreign and inactive descriptors;
the eight-entry limit; and failure before mutation. Its native-layout fixture
reproduces opposite half-IPD translation errors and requires lighting/base
geometry agreement after rebasing.

Debug stereo-smoke and scene-batch tests passed. RelWithDebInfo and Debug client
builds passed, and both EXE/PDB pairs were deployed to the local VR installation.

Fresh-game and HMD acceptance remains required: face the Oil Rig helicopter
spotlight, compare the lighting layer with the weapon silhouette in each eye,
then move the head and weapon. Also compare firing illumination and nearby
world models/surfaces, and confirm spotlight shadows remain stable.
