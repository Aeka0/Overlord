# Carried thermal ADS: isolate SSR and world dimming

Status: the first record-level isolation candidate is implemented and built on
; headset acceptance is pending. The investigation below records the
source boundaries and remaining script-lighting questions. The ADS speed/eye-box changes are recorded in
[native ADS](vr-native-ads.md#carried-thermal-scopes).

## First implementation

The scene-bound physical optic capability authorizes this policy only for a
current carried thermal weapon. It captures the public SSR scale once per
admitted pair, validates its native hash/type and range, and leaves both global
controls unchanged. Exact native signatures cover the SSR producer, the
backend constant consumer at `0x1407815A0`, and the thermal PostFX gate.

The auxiliary record is prepared from the original native thermal scene before
the two world records are normalized. World normalization clears only thermal
bits 0/1 at `+0x204` and sets `+0x2CA4` to the public SSR scale, including an
explicit user scale of zero. Thus the world uses the native ordinary rendering
and color path while the lens retains native full thermal, its original SSR
suppression and thermal color/grain/scanlines. Normalization also runs when the
eye box cannot produce an optic request, so looking past the lens does not
restore whole-world thermal behavior. Camera, geometry, ordinary/thermal vision,
exposure, damage, flash, blur and temporal-history bytes remain unchanged.

Eye 0 executes an arena-owned native record. Its five modified bytes have an
explicit owner-thread lease through rendering and display, restored on every
left-eye completion and transaction cancellation. Restoration preserves native
target-routing and other updates made while the owner ran. Eye 1 uses its private
record; the scope uses the untouched thermal snapshot. Story M82 epochs, weapon
display epochs, native forced full-view thermal and invalid/ambiguous sources
bypass normalization. Existing per-eye/auxiliary SSR image and matrix histories
remain separate; no extra scene pass, GPU allocation or polling thread was added.

Validation: optic/WARP checks passed, including dual-eye native-source
preservation, left-arena completion/cancellation/idempotent restoration,
native target mutation preservation, explicit SSR disable, invalid input and
scripted ownership bypass. The optimized client and stereo probe built, and
the stereo probe passed. These are offline results, not final reflection/heat
pixels. `vr_optics_status` now reports `world_contract`, `world_pairs`,
`native_ssr` and `world_ssr`; the pair counter counts prepared record policies.

This candidate retains the current-frame native ordinary color block rather
than copying an older frame or adding a brightness compensation shader. It
does not yet selectively reconstruct thermal shellshock or the optional
scripted lightset. Their script state and all other contributors remain native.
If a level still darkens outside the lens after the rendering/SSR split, its
remaining script-lighting contribution must be isolated with paired evidence
before changing that producer. The implementation stages below describe that
remaining work as well as the implemented record boundary.

## Confirmed SSR producer

The running native scene was thermally active (`GfxViewInfo +0x204`, bit 0 = 1)
with full native ADS fraction 1.0. Its SSR blend field at `+0x2CA4` was 0.0.
The public `r_ssrBlendScale` remained 1.0. A separate native float dvar, hash
`0x213BC6BE`, had current value 0.0 and reset value 1.0. Its original name is
not in the repository's dvar name catalog; do not invent a name for it.

The exact native producer chain was recovered from the running image:

| Boundary | Observed behavior |
| --- | --- |
| `0x1403AC3B0` | Computes the thermal state and derived rendering controls |
| `0x1403AC43D..0x1403AC478` | If the native thermal state is active, subtracts `max(0, (ADS - 0.75) * 4)` from 1 and passes the result to the native float dvar setter |
| `0x14077EBC0..0x14077EBD8` | Multiplies that derived value by `r_ssrBlendScale` and stores the product at scene record `+0x2CA4` |
| `0x14077ECF7..0x14077ECFE` | Publishes native thermal flags into the scene record at `+0x204` |
| `0x1407AFFC0` | Later chooses native thermal/ordinary color and scope stencil behavior in PostFX |

The captured constants were 1.0, ADS fraction 1.0, threshold 0.75 and gain 4.0.
Thus full thermal ADS makes the derived SSR multiplier zero before either VR
world eye or its private optic view renders. The two world records are cloned
from that native record and inherit the zero. Native parameters establish this
upstream suppression; no per-draw GPU reflection readback was taken.

The existing private full-thermal bit changes the scope stencil and ordinary
color redraw, but does not recover the already-zero SSR contribution in the
world records. Raising the public SSR scale cannot recover a zero multiplied
by it. Do not write a temporary global dvar around eye rendering: frontend
workers and the natural owner can consume those shared controls concurrently.

## Dimming and script state

The current loaded `maps/_load` ScriptFile (token `0xA553`, decimal 42323)
contains the `thermal_shellshock`/`thermaleffect` markers. Existing decompiled
native source shows that thermal ADS starts a repeated `shellshock("thermaleffect",1)`
while the player is not flashed, and stops it on thermal exit. This is a
player presentation effect, separate from the private optical PostFX choice.
Its exact color/exposure contribution has not yet been isolated in GPU pixels.

The loaded script set also contains `maps/_thermal_scope_lightset`. Its native
source conditionally applies `lightset2(level.thermal_scope_lightset,1.0)` when
the level has configured that lightset and thermal ADS is active, restoring
with `lightset3(0.5)` on exit. Script presence alone does not prove that the
current level enabled or switched this optional lightset. Scene lighting may
therefore need a separate boundary from final color correction.

The ordinary and thermal vision blocks used by native PostFX are at record
`+0x2CC` and `+0x378`, respectively. Their existence does not establish that
the ordinary block is free of thermal shellshock, vision blending or level
lighting. A previous normal frame cannot safely stand in for the current
non-thermal view: damage, flashes, fades and changing scene exposure must remain
current. Brightening the submitted world image would not restore lost lighting
or reflections and would change unrelated presentation effects.

## Proposed ownership and implementation stages

1. Preserve a same-publication native thermal record for the auxiliary view,
   before normalizing either world record. This is required because the current
   `apply_thermal` contract rejects a record whose native activation bit is absent.
   The mounted story M82 keeps its existing full-canvas ownership path.
2. Give carried-optic world records an ordinary rendering policy: keep native
   camera, geometry, user SSR scale, and each eye's image/matrix history. Remove
   only thermal activation from those private world records and recover the SSR
   field using the original public scale, preserving a deliberate user scale of
   zero. Audit all SSR field consumers and upstream heat/material gates before
   treating this record substitution as sufficient.
3. Build a same-frame ordinary vision snapshot that excludes only the exact
   thermal shellshock/thermal vision contributor while retaining damage, flash,
   mission fade, blur and ordinary exposure. Feed it to world records; feed the
   original thermal snapshot to the auxiliary record. Trace optional lightset
   selection separately and determine which lighting inputs can be published
   for both consumers without changing the mission script or shared assets.
4. Validate the producer and consumer boundaries with paired ADS-off/on native
   parameter samples and final GPU color/SSR witnesses. Cover entry/exit ramps,
   flashes/damage, optional thermal lightset levels, dropping/holstering,
   recenter/device rebuild, both eyes, the desktop tail and story M82.

The existing auxiliary pass and independent SSR histories provide the resource
ownership structure; a second optic pass is not implied by this proposal.
Whether shared frontend lighting/material generation needs an additional
consumer-specific snapshot remains open. Source/data coverage must resolve that
before deciding the final implementation or its performance cost.