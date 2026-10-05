# MP5, AUG and UMP physical reload

The cross-weapon [VR impact-release overview](vr-impact-release.md) collects
magazine-latch strikes, receiver-button releases and HK handle slaps together.

These adapters share the existing closed-bolt magazine system with an explicit
manual receiver catch. They use two-hand aiming, physical magazine removal and
chamber plus-one. Controller magazine/bolt-release buttons remain disabled.
MP5K and AUG have no automatic last-round hold-open. UMP45 additionally supports
follower hold-open and an unbuttoned left-side receiver-catch slap. That side
release is blocked while its upper handle is manually latched up; lower/slap
the raised handle instead. See [receiver bolt-catch slap](vr-receiver-bolt-release.md).

## Native coverage and authored data

| Family | Verified native names | Receiver recipes | Capacity | Rear travel |
| --- | --- | --- | --- | --- |
| MP5 / MP5K | `mp5`, `mp5_reflex`, `mp5_arctic` | `h2_viewmodel_mp5k_base`, `_arctic` | 30 | 65 mm; full stroke at 60 mm |
| AUG | `aug_reflex_arctic`, `aug_scope_arctic` | `h2_viewmodel_steyr_base_arctic` | 30 | 120 mm; full stroke at 113 mm |
| UMP45 | `ump45`, `_arctic`, `_reflex`, `_acog`, `_eotech`, `_digital_acog`, `_digital_eotech` | `h2_viewmodel_ump45_base`, `_arctic`, `_digital` | 25 | 85 mm; full stroke at 79 mm |

Native WeaponDef names, capacities, model composites, sound maps and rigid
surface groups were read. MP5 means MP5K in these H2 assets. This
loaded-zone inventory does not prove coverage of every campaign variant.
Reviewed common optics and suppressors use the existing attachment contracts;
unknown geometry, duplicate roles, wrong parents and wrong capacity reject
physical admission. Exact native instance identity remains separate from skin.

AUG requires the exported foregrip plus either the rail or original Steyr scope.
Its native scoped and rail/red-dot composites share the same support position.
MP5K and UMP have integral support geometry. All three preserve their own native
idle hand poses. Static receiver mount tags are deliberately omitted from equip
part overrides because their names also appear on attachment roots.

The source headers record animation/receiver hashes. Native left-hand grasp
samples are MP5K `reload_empty` frame 11, AUG `reload_empty` frame 92 and UMP45
`reload_empty` frame 66, transformed with each action pivot to closed rest.
Magazine poses use regular reload frames 22, 28 and 16 respectively. UMP's
frame 16 retains a stable magazine grasp; frame 24 had already transitioned
toward releasing it, placing the lower body through the palm. Each idle
pose includes 36 finger/palm joints; each manipulation includes 18. Reference
hand/tab skin distances are 0.646, 0.334 and 0.450 mm, respectively. These are
offline geometry measurements, not HMD ergonomic acceptance.

MP5K's raised orientation comes from `reload_empty` frame 19; UMP45's from
`first_pullout` frame 0. AUG has translation-only native handle animation: its
15-degree upward rotation is newly authored around the guide centre, at
part-local (0, 2.3865217, 0.14288378) centimetres. The animation bone is on the
receiver centreline; rotating about that bone instead displaced the whole guide
outside the receiver. All rotations use the authored hinge and offset contact,
so the guide, tab, grab bounds and held wrist retain their alignment. Export distances are converted from centimetres to native
units once; gesture thresholds use metres.

## Interaction and ammunition

1. Pinch and pull the magazine outward by 50 mm to detach it into the free hand.
   MP5K and UMP also accept a forward strike on their magazine-release paddle
   with the complete body of a held spare magazine. AUG accepts a rearward/upward strike
   against the release behind its magazine. These use each model's own latch
   and magazine-end geometry, independently of the charging-handle catch.
2. Pinch the charging handle and pull through the full stroke. A loaded chamber
   ejects once. Tilting a partial stroke cannot latch or extract ammunition.
3. Near the rear stop, lift about 21 mm relative to the gun or turn about
   19.5 degrees around its forward axis to engage the catch. The handle stays
   raised/rearward when released, frees the support hand and prevents firing.
4. While caught, remove/replace the magazine normally. Insertion leaves the
   chamber empty until the handle returns.
5. Regrasp, lower/reverse the turn and let go or move forward; alternatively,
   slap the raised tab downward without pressing the offhand trigger. Return
   feeds one round when available. With no magazine it closes empty.

`latched_open` is separate from the hand-held stroke and automatic follower
lock. Latching/unlatching at the rear does not extract or feed again. Tracking
loss, recenter or a changed input owner cancels gestures but preserves the
mechanical catch. A complete new forward/rear cycle can extract another round.

Slaps require a separated approach, a downward swept contact, at least
0.5 m/s and 25 mm directed travel. Twenty-one contacts cover the palm, all
fifteen finger joints and five extrapolated fingertips, with a 60 mm contact
radius; rearming needs 100 mm separation. The current native glove shape is
rebased onto the raw wrist, never its snapped IK placement. Hand orientation
does not gate the slap. Motion can deviate about 69.5 degrees from gun-relative
downward; near-horizontal and upward sweeps reject. Slow overlaps,
spawn-inside contacts, held support, tracking jumps and moving the gun under a
stationary hand do not release the catch. Failed native compare/write consumes
the gesture; duplicate render/input frames cannot retry it.

Directed travel starts at the approach's highest point, so lifting from below
before the downward stroke does not subtract that lift from the slap. Repeated
small oscillations do not add up to the required travel. Contact continuity
uses the larger of 250 mm and 8 m/s times the consumed sample interval, while
the independent world-wrist speed remains limited to 0.5–8 m/s. Gaps longer
than 150 ms reset the approach. See the diagnostic export for actual and allowed
step lengths; its recorded sample intervals can differ from display frame time.

MP5K and UMP use their verified rear and hit/close notetrack keys. AUG uses its
compound chamber sound on return. Catch/unhook use haptics; no unrelated sound
alias or raw extracted audio is shipped. A physical `magazine_take` with no
dedicated sound falls back to the same profile's `magazine_out` sound, preserving
the take event and preventing a cosmetic dropped copy. AUG's verified removal
key is `weap_styaug_clipout_plr`, resolving to `h2_wpn_aug_foley_plr_clipout`.
An explicit take sound still takes precedence.

## Rendering and validation

MP5K's magazine body and visible round are separate material surfaces; its
skinned strap is unrelated. The left front nylon strap reuses the native
`j_front_strap`, `_mid` and `_end` chain. Its base ring remains fixed. A damped
angular spring responds to gravity and mount acceleration with fixed link
lengths and per-link limits. This is bounded skeletal secondary motion, not a
cloth collision simulation. It does not touch the rear strap, magazine or
charging handle. Duplicate input does not integrate twice; owner/reference
changes, pauses, tracking jumps and invalid poses clear transient velocity.
The shared immutable rigid-part service selects
groups across up to 32 surfaces, preserves each material and GPU vertex layout,
creates independent index buffers, and computes combined bounds. A skinned
surface is excluded only when its native part bits prove separation from every
requested bone. Selected or unproven skinned geometry rejects the whole result.
Late validation failures never publish partial geometry.

UMP's body, round and charging handle share one surface. Magazine visibility
hides only the body/round groups. AUG keeps the magazine mouth behind the main
hand, with its own insertion rail. Runtime subsets retain the source asset
identity and never mutate the engine's source model or materials.

Deterministic tests cover all six receiver recipes, native capacities,
attachments and rejection paths, part/finger bindings, rotated insertion rails,
offset handle arcs, physical replacement while caught, repeated rear toggles,
empty/no-magazine states, duplicate input, failed transactions, every slap
contact with arbitrary wrist orientation, oblique motion and rejection paths.
They also cover raw-wrist contact rebasing, clip-out sound fallback, guide hinge
invariance, strap length/angle bounds, frame-rate stability and reset paths.
D3D11 WARP tests cover multi-material subsets, source immutability, original
index formats, unrelated/selected skinned surfaces and atomic failure.

HMD acceptance remains pending: inspect actual handle reach/rotation, native
glove fit, UMP magazine clearance, fingertip/oblique slap feel, strap motion,
AUG removal sound, both eyes and the scoped AUG variants.
The read-only live inventory does not represent interaction testing of the new
candidate binary.

For intermittent slap failures, use the opt-in [HK slap diagnostic view](vr-hk-slap-diagnostics.md)
to compare raw/visual hands and retained simulation rejection reasons before
changing thresholds again.
