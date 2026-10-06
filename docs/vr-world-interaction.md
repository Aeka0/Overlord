# Empty-hand world interaction

An empty hand uses its controller aim pose to select a world interaction. Grip
press acquires that target; native use holds last until grip release. Weapon
pickups use the existing native carry transaction and equip the requesting hand.
Parts, a held gun and successful body-slot draws claim the hand first. An empty
body slot does not swallow an otherwise valid interaction.

The default reach is 2.2 metres, bounded from both hand and head. Weapon selection
tests each weapon's composed native model bounds against a 12-degree half cone,
using the entity's current translation and rotation. Larger models therefore
have larger selectable volumes. The tiny native entity collision box is not a
visual weapon bound; short weapons can lie entirely behind that entity origin.
Contact within 12 cm of the model bounds takes
priority and does not require pointing toward the centre. Otherwise angular
alignment wins before distance, with a deterministic entity tie break. Both
paths retain native eligibility and visibility checks. Native type-5 script
models use server DObj bounds with the same bounded volume/contact selection;
their weapon token stays zero, so grip starts native use rather than pickup.
Their head/hand obstruction checks reuse GetUseList's model-derived trace point
and native use trace (contents 0x11), instead of the weapon capsule mask. Other
Native `trigger_use` / `trigger_use_*` entities use their authored world-space
bounds for volume/contact selection; other non-model interactions keep point
selection. The scoped breach rule below adds its own bounded enlargement.
This allows floor
pickup while standing without selecting everything in a large proximity sphere.
The game still owns item eligibility, script orientation, touch-only trigger
volumes, hold duration and the final use/touch handlers.
Native origin-based broadphase and range checks include the bounded model-origin
envelope; the final test still enforces the configured reach on the actual model.

## Ownership and native integration

### Partially obstructed dropped weapons 

A trainer Desert Eagle capture showed the native GetUseList sight test rejecting
the weapon's buried model-derived point, returning an empty candidate list
before the VR selector could consider its mostly exposed mesh. Expanding only
the later VR bounding-volume test cannot fix that rejection.

Only during a VR hand query, a rejected native sight test for a supported
physical weapon may retry at up to 14 support points taken from actual indexed
rigid-mesh vertices. The same fallback also serves final head/hand visibility
when its ordinary closest-volume point is obstructed. Both observers must see
the same point. Reach, hand intent, generation, native pickup eligibility,
script restrictions and each stage's original collision mask remain in force.
An exposed AABB corner with no mesh is not used as evidence of visibility.
Native script objects and non-VR queries keep their original sight point.

Samples are cached lazily per composed native model identity, with bounded
surface/vertex/triangle scans and chunked guarded CPU reads. Dynamic/skinned
surfaces are not stable witnesses. Unavailable or invalid geometry preserves
ordinary admission; no GPU readback or per-frame mesh scan is added. At most
64 supplemental traces are shared across a hand query, including native and
final admission. The asset-unload boundary clears the cache. No dropped weapon,
floor collision or save data is repositioned.

Offline regressions cover exposed versus fully buried mesh, walls, incompatible
head/hand sight lines, wrong hand direction, invalid geometry and exhausted trace
budgets. The captured Desert Eagle has six rigid surfaces and 12,769 referenced
vertices. Post-fix headset acceptance remains pending.

### Shared ammunition caches 

Read-only inspection beside a museum cache identified a `cargo_belt` visual
model and a separate `tag_origin` use model. The visual bounds are approximately
50.1 x 65.6 x 41.5 native units; the invisible use model is only 0.394 x 0.394 x 0.
The shared `maps/_load::ammo_cache_think_global` creates that use model 28 units
above the cache and stores it in `cache.use_trigger`. Its `_id_AB48` helper also
toggles usability every 50 ms using the player's camera dot product (> 0.7).
Thus native admission can reject a valid controller aim before VR geometry or
prompt rendering runs. Scoring the use model's own bounds cannot select the box.

All maps using this shared cache script now resolve the authored `use_trigger`
relation and reuse the visual-proxy path below. The current model's root tag,
inverse bind transform and bounds define the selectable box. Hands can point at
its body or touch its edges. The real use entity retains activation ownership;
the prompt reads `Hold Left/Right Grip to resupply ammo` (localized) and anchors
14 cm above the visual bounds' top. The large `trigger_radius` remains only the
native ammo icon's proximity trigger and never becomes a VR interaction volume.

A narrowly scoped builtin interceptor replaces only the verified dot-product
return in `_id_AB48`, only for a currently linked ammo-use entity, while native
VR use and physical carry are enabled. The native callback, makeusable/unusable,
`dont_allow_ammo_cache`, refill animation lock, notifications and ammunition
grant logic remain authoritative. Ordinary script callers and non-VR play retain
the native gaze rule. No trigger is moved and no supply event is synthesized.

Relationship discovery and visual snapshots are shared per server command and
reset on level/save lifecycle changes. Entity generations reject recycled
objects. Remote crates are rejected before model/tag queries; at most four
ranked visual candidates receive additional native admission queries. A held
target takes priority over a new hover. Head and hand reach, native admission
and obstruction checks still apply; looking away and looking through walls are
distinct conditions.

### Offset installation prompts (estate DSM, )

The estate script uses separate `dsm_obj` and `dsm_usetrigger` entities. Read-only
capture of the running level found the trigger centre `(110,202,206)`, half
extents `(3,7,5)`, and model entity origin approximately
`(111.251,212.059,201.903)`. These are observations, not runtime constants. The
user successfully activated it by aiming left of the yellow model, confirming
that the native use route works. The script waits for the trigger's event for
both installation and later recovery; the highlighted model is not the use target.

VR previously scored that non-model trigger as a point. It now reuses existing
volume/contact scoring with the native world bounds, admitting a hand aimed
toward the highlighted object in the captured geometry. This does not move the
model, add a fixed hand-angle offset, change a mission flag or widen unrelated
damage/proximity triggers. Large scene volumes outside the existing model-size
limit retain legacy point selection. Native eligibility, occlusion, reach, hold
duration and target-generation checks remain in force.

The subsequent headset retry still required aiming left. Authored trigger
bounds alone were insufficient: they did not change the native candidate query
or the prompt anchor. The estate DSM now uses an explicit visual proxy binding
(`dsm_usetrigger` -> `dsm_obj`). Its current model bounds follow the native
`tag_origin` pose, including rotation and the inverse bind transform. Both the
ordinary trigger and visual entity are excluded from competing base candidates.
The final target keeps the trigger's identity, but its prompt anchors at the
visual model centre.

Only after the actual hand ray passes visual-volume scoring does one additional
native list query point from the VR head toward the original trigger. That
query must still admit the exact trigger. Entity generations, both visibility
traces, reach and the native `dsm_ready_to_use` flag remain required. The trigger
must be within 0.75 metres of the model; this rejects the native download-phase
behavior that moves it 10,000 units below the map. No trigger, model, player
pose, command outcome or mission flag is relocated or forced. A per-command
server cache shares the script/tag reads between hands and resets with level
lifecycle. This binding is limited to estate; generic trigger-volume selection
continues to serve other use targets.

`trigger_use_breach` uses its native bounds, expanded to minimum half extents
0.3/0.3/0.4 metres and capped at one metre per axis. This gives door panels a
bounded selection area and permits hand contact just past the point marker.
If native point-angle filtering omitted it, up to four additional native list
queries admit nearby breach entities from the original area query, using a
head-to-door ray. The actual hand must still pass the bounded volume test;
native eligibility and head visibility remain mandatory. Contact can skip the
hand obstruction trace when the controller lies inside the door surface.
The exact target still uses the existing native hold/use route. No script flag
or use handler is forced. Geometry tests pass; doorway hardware acceptance and
actual trigger-size measurements remain pending.

Within one input frame, a Grip candidate's exact query result is reused by
arbitration, execution and hover. A second query cannot replace that candidate
after its press has already been reserved. The reuse is scoped to the same hand,
input sequence, tracking reference and ray, including an empty result. The next
frame queries again, and a cached target still undergoes live entity/permission
validation before use or pickup. This preserves dynamic eligibility without
allowing selection changes inside one transaction.

World arbitration identities contain a native entity index (1–3999) and its
reuse generation. Generation zero is valid for initial map entities, including
weapon display script models. The hand arbiter validates this world identity
separately from physical weapon identities, which require a nonzero instance
generation. It preserves the exact native generation so recycled entities do
not match an old held target. Native eligibility, visibility and live-entity
checks remain authoritative after arbitration.

The hand-interaction regression covers initial display entities at generation
zero, recycled NPC drops, invalid entity indexes, held-input deduplication and
unchanged physical-instance validation. This fixes a refactor regression where
display Grip candidates were rejected as `invalid` before native use ran,
despite both hands being free and dropped weapon pickup still working.

Fresh grip presses are arbitrated in one server tick: an authored support-grip
contact (or rear-grip reacquisition) wins first, then an occupied body slot,
then ground pickups and native F interactions. Support contact uses current
tracking and the cached local weapon anchors; it does not wait for a renderer
to observe the press. Empty slots do not consume a draw attempt, including
where their expanded volumes overlap an occupied slot. Back overflow counts
as available storage. A matching higher-priority attempt consumes the press
even if its commit fails, so it cannot accidentally activate a world object.
Existing part leases, held weapons and ongoing native-use holds stay exclusive;
moving into another contact while continuing to squeeze does not start a new
action. Release and a fresh press are required.

- `weapon_carry_runtime` remains the server hand arbiter. `world_interaction`
  owns one native-use lease, since the native F command has one hold target.
  A second hand cannot replace an active hold. A held target stays locked while
  admitted, in range, in the cone and unobstructed; hover cannot retarget it.
- `native_use` extends only the candidate-query callsites while a thread-local
  hand query is active. It preserves actual distances and the native rules for
  script triggers. It does not move the player, modify view angles or globally
  increase native use distances. Empty hover queries do not reset the native
  interaction progress dvar. The generated range bridge uses the existing near
  relay table, since JIT allocations can lie outside the native CALL's rel32
  range. All five call targets are checked before installing the adapter.
- The existing command owner emits native `+activate` / `-activate` reliable
  binding notifications (73 / 74), plus the native activate bit (`0x8`) for
  world-use holds. It does not synthesize a keyboard F or invoke the GSC VM from
  the input thread. Notify-only scenes receive grip edges even without a visible
  target. Pickups retain native touch notifications and emit an activate pair.
- The native server use pass rechecks exactly the admitted entity through native
  cursor-hint admission. Cancelled and rejected holds clear the native entity
  handle before an older usercmd can act on a stale target. It never falls back
  to an unrelated target under the head crosshair during a VR hold.
- Entity generations, focus, stale samples, tracking/reference changes, pauses
  and scripted input suppression cancel/rearm the interaction. Resume while
  already gripping does not issue another action. If the native reliable queue
  cannot accept a release while paused, it is deferred until queue admission
  resumes and precedes the next press.

## Presentation

MOD-owned instructions and button labels use the shared
[game text i18n entry point](game-text-i18n.md). English and Simplified Chinese
cover the generic sentence family; the reviewed mission instructions are
currently Chinese-only. If the world-instruction family or its Grip
labels are untranslated, custom world labels yield to the original native
cursor-hint text on the narrative canvas. The native producer keeps the current
game language, original bindings and parameters; no MOD English sentence is
substituted. Native weapon names and attachments retain their game
translations. Highlighting follows the named button argument and authored
interaction-location emphasis, using the same formatter as narrative hints.

Admitted non-weapon targets also publish their native `sethintstring` index.
The shared registry can distinguish DSM connection/recovery, rope attachment,
breach and other reviewed mission actions without changing native admission or
trigger timing. A recognized specific sentence missing in the current locale
also retains the original native cursor text. Unrecognized targets keep the
generic interaction template.

Each selected world object receives a label 14 cm above its model bounds centre.
Weapon prompts read `Hold Left Grip to pick up <localized weapon name>` (or
`Right`), with the entire `Left Grip` / `Right Grip` button label in yellow
and the native weapon icon above. The shared `Left/Right Grip` label is also
entirely yellow. Sentence templates own spaces between colored runs. Their
leading/trailing blanks are measured explicitly because native width measurement
trims trailing spaces; Chinese spans do not gain English separators. Localized
attachments occupy a second line. Base names containing spaces remain intact;
unknown localization hierarchies retain the full name. Explicit inter-run spacing
avoids native text metrics discarding trailing blanks. Other interactions retain
`Hold Left Grip` / `Hold Right Grip`. Two hands aiming at the same entity share
one `Hold Left/Right Grip` label. Font metrics calibrate weapon prompts to the
visual width of 48 cm at 2 metres (other interactions: 28 cm at 2 metres).
The shared projected-pixel path then keeps icon, text and attachment lines at
that visual size across distance and source viewport changes, while retaining
the item's actual world anchor and stereo depth. The native cursor-hint selection prefers `pickupIcon` and its
ratio, falling back to `hudIcon` and its own ratio. A signature-checked H2 adapter
reads these fields: the legacy `WeaponDef` declaration does not match their live
offsets. Assets are queried on the LUI frontend without retaining zone pointers.
The font and shadow match native `cursorHintDef` (`SP_HudCarbon27`,
`fonts/defaultBold.otf` at 27 px, `ShadowedMore`). The font comes from the
native cache, and the native waypoint command-range/atlas pipeline
supplies per-eye world placement. No separate font texture or GPU capture path
is introduced. Text and icons rasterize near the native viewport centre before
the atlas places them at the item's world position. Native flat-screen projection
can place floor items below its viewport despite headset visibility, so it must
not determine the source raster position. Owned prompt ranges carry frontend font-measured crop bounds so
long or localized text is not rejected by the generic byte-count crop estimate.
Command validation and allocator fingerprints still apply. Long names scale to
fit; extreme names use UTF-8-safe ellipsis within the existing atlas capacity.
Native pickup icons use StretchPic opcode 10 (56 bytes). Capture validates its
XYWH/UV/color layout and native handler separately from rotated rectangles
(opcode 12); its trailing padding is not a rotation angle. An unsupported icon
previously rejected the entire owned range, including otherwise valid text.
Labels are submitted at the existing LUI scheduling boundary,
before native HUD command streams are sealed; submitting at renderer end-frame
would leave them outside the captured stream. Behind-head targets have no fixed
edge label.

`vr_interaction_status` also reports prompt target, stale, prepared, recorded and
rejected counts. These are cumulative, on-demand diagnostics with no GPU readback.

The HMD's selective HUD composition excludes ordinary centred native
cursor-hint text when localized VR world labels own presentation. On missing
translations, its verified producer instead supplies native text through the
shared narrative range registry. Native flat-screen
HUD rendering remains available. Script-authored instructions without an
associated interactable entity retain their narrative presentation; there is
no invented world anchor for them. Their activate notifications still work.

## Settings and validation

Saved dvars: `vr_worldInteraction` (on), `vr_interactionReach` (2.2, range 0.5–3
metres), and `vr_interactionCone` (12, range 1–25 degrees). Requires physical carry
and the normal VR controller/gameplay gate. `vr_interaction_status` reports the
hover entities, angular scores, lease, pickup counts and paired notifications
to `minidumps/overlord-interaction.txt`.

Offline checks cover angular priority, deterministic overlap, bounds aiming,
near contact (including origins inside bounds), malformed geometry, floor reach,
head/hand limits, invalid tracking, lease cancellation and notification pairing.
Spatial-panel tests cover fixed physical label size and native atlas composition.
The standalone range-bridge regression forces a far target and executes the
production emitter through the near relay, checking the five-byte patch boundary,
native register results, stack alignment, and ordinary/extended radius handling.
A bounded live native query admitted existing weapons at about two metres from
the supplied query origin while retaining visibility/eligibility filtering and
leaving the player's state unchanged. It did not pick up or activate an object.
A subsequent bounded live probe confirmed that LUI label submission produces
matched waypoint ranges and atlas captures with no rejected captures. This
verifies command-stream admission, not headset legibility or placement.

Headset acceptance still needs floor pickup with each hand, selecting between
overlapping weapons, a native hold-to-use object, a notify-only story interaction,
and cancellation on release/pause/recenter. Check both-eye label placement and
legibility against bright and dark scenery.

Debug builds provide independent world-pickup and body-storage views; see
[Interaction diagnostics](vr-interaction-diagnostics.md).

## Cliffhanger C4 repair candidate

Observed; the local repair builds and passes offline regression,
but deployment and headset acceptance are pending. The C4
installation object is natively usable and keyboard F successfully starts its
cinematic. Controller squeezes produce paired activate notifications and the
native activate button, but the VR target remains empty, so installation does
not start and no world prompt is presented.

This scene waits for the object's `trigger`, not just a player activate
notification. The empty-target VR lease intentionally prevents native use from
falling back to an unrelated head-ray target. Here that protection also blocks
the installation because the intended scripted object was not admitted by the
VR selector. Adding another F mapping is insufficient.

The candidate replaces type-5 model centre-point selection with native DObj
bounds and the common 12 cm contact policy. Native script models also reuse the
native model-derived visibility point and point-trace contract from both head
and hand. Missing DObjs retain point selection; malformed or oversized model
bounds fail closed within the existing two-metre model envelope. Native
usability/orientation, hold timing and final event authority remain in place.
No mission events are synthesized and empty leases never adopt a head-ray target.

The existing lease policy is unchanged: squeezing before alignment remains a
notify-only hold; release and press again to acquire an object. Once acquired,
neither hover nor the other hand can replace it. Offline regressions cover a
C4-sized body missed by centre scoring, contact from either side, bounds/reach
rejection, empty/object lease transitions, cancellation and notification pairs.
Debug candidate records now include the actual trace point and independent
head/hand visibility verdicts, with `not-tested` distinct from obstruction.

Model bounds and point-trace checks distinguish the relevant rejection paths.
Successful VR installation of C4 still requires headset acceptance. Verify its
world label, fresh Grip input from either empty hand, installation cinematic,
restored weapon control, notify-only interactions, and ordinary hold-to-use
objects.
