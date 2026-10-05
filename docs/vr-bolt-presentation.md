# Bolt presentation

The first batch uses existing bones for Mini Uzi, TMP, L86 and the shared
AK/FAL/SCAR underbarrel shotgun. The second batch separates existing polygon
components for M16, FAL, MP5 and UMP. SPAS-12 retains its pump-driven action.

## Separating shared-bone bolt surfaces

| Family | Source group | Moving faces | Fire / manual maximum | Retained stop | Receivers |
| --- | --- | ---: | ---: | ---: | --- |
| M16 | `j_reload` | 134 | 81.026 mm | 78 mm | Base |
| FAL | `j_bolt` | 124 | 144 mm | 135 mm | Base |
| MP5K | `j_gun` | 75 | 65 mm | 60 mm | Base, arctic |
| UMP45 | `j_gun` | 228 | 85 mm | 79 mm | Base, arctic, digital |

Each receiver has an explicit source identity, complete surface vertex/triangle
counts, exact inclusive face ranges and geometry bounds. Source hashes remain
in the authored headers. Offline checks compare the selected triangles and
their rigid weights across every listed skin; matching bone names alone is
insufficient. M16/FAL originally bound the bolt to the handle, while MP5/UMP
bound it to the receiver. All four have disconnected polygon components.

The existing manual stroke supplies a linear bolt-following curve. Fire travel
and retained stops are authored visual values; the source fire clips do not
provide these bolt cycles. The non-reciprocating handles keep their original
input, return and latch rules. In particular, HK handle lift never rotates the
internal bolt, and the handle may return while the bolt stays at its catch.
No feed/ammunition policy is changed by these partitions.

`native_partition_assets` generalizes the former folding-handle asset provider.
It uses the existing `rigid_part::create_face_partition` to prepare the moving
piece and the complementary source group together. The source XModel, materials
and vertex buffers remain unmodified. It registers both immutable runtime
models before enabling the recipe, retains them until the native asset drain,
and bounds storage by the registered profile catalog. F2000's folding tip uses
the same provider and keeps its existing pivot and faces.

The presenter hides only the exact original rigid group after both pieces are
ready. It does not hide the receiver's descendant bones, attachments, magazine
or unrelated skinned straps. Both replacement pieces use the same native object,
matrix buffer, pose epoch and scene origin as the gun. `partition_asset(...)`
in `vr_reload_interaction_status` reports preparation/rejection. Unsupported
surface layouts reject the partition without hiding the original geometry;
they do not silently substitute a guessed slice or a whole-receiver transform.

M4 remains deferred: its selected receiver bone also participates in a skinned
surface, which the current rigid partition contract correctly rejects. Handling
it requires face filtering that preserves the original skinning. ACR's thin
window, AUG's overlapping layers and FAMAS's fixed backing need more geometry
work before enabling them. They are not included in this batch.

## Open-bolt firing

Mini Uzi now uses the same accepted-shot return semantics as RPD. At discharge
the bolt is forward; it returns to the sear over 60 ms only if the authoritative
feed state retained it. A last shot or dry release leaves it forward. Manual
handle travel takes priority. Selection follows the feed type, not whether the
handle happens to be parented below the bolt.

Previously Mini Uzi's closed-bolt-style presentation took the maximum of the
shot displacement and the retained sear position. That constrained firing motion
to 52.51312–52.51632 mm, only 0.0032 mm of visible travel. The new shared
`displayed_internal_bolt` path is also exercised by the profile tests.

## TMP and L86

TMP's internal bolt has an explicit 48.65016 mm firing stroke measured from
`h2_wpn_pst_mp9_fire`. Independent models use that direct stroke with the shared
80 ms visual cycle, rather than passing firing motion through the manual handle's
take-up curve. Its original 43.64531 mm manual stop and lock position remain.
The native animation-tree path retains the source fire animation.

L86's `j_reload` geometry includes both the visible bolt and its handle. That
whole group now reciprocates over the existing 86 mm stroke and stays at an
authored 78 mm follower stop after the last round. Its manual acquisition and
return use the same retained position. The original fire clip has no action
translation, so `authored_action_fire` opts it into the visual cycle even on a
model that still has a native animation tree. Other weapons keep their existing
native-animation policy. The follower stop is an authored presentation value,
not a sampled native empty pose.

## Underbarrel shotgun

`j_plate_shotgun` is the visible bolt, a sibling of `j_pump_shotgun`. It is now
bound separately and follows pump progress from closed to 78 mm rearward over
the original 104.789593 mm pump stroke. The bolt's exported mesh is 77.31 mm long;
the authored stroke clears the window. Stock rechamber animates only the pump,
so this coupling is deliberately authored rather than attributed to that clip.

Both parts are posed from their immutable rest transforms in the attachment's
actual host frame. Partial travel, return, reversed/rotated hosts and repeated
render applications cannot accumulate displacement. The lifter, cartridge,
host weapon, muzzle and hands are not descendants of either moving part and
keep their existing presentation. No ammunition transaction is added.

## Validation and headset acceptance

The weapon-grip, physical-reload and underbarrel suites cover the affected
profiles. Added checks exercise open-bolt fire/last-shot/manual priority at
45/72/90/120/144 Hz, TMP firing versus manual stroke, L86 follower retention,
and the shotgun's transformed host frames, partial/full/overtravel, repeated
posing and rejection before writes for invalid inputs or matrix spans.

The second batch also checks exact source-group binding on complete weapon
assemblies, per-skin identity, independent bolt/handle travel, malformed or
overlapping face recipes, mutually exclusive drivers and exact rigid-group
hiding. The rigid-part suite exercises shared-bone complementary slices and
unmodified source buffers with WARP. Native in-game surface admission still
needs to be observed for each new recipe.

In-game acceptance remains: visible cycles at the actual fire cadence, TMP
window clearance, L86 empty reload and rear-stop grasping in both hands, and
the attachment bolt at partial/full pump travel on all three hosts. Offline
geometry previews and passing CPU tests do not establish headset acceptance.
For the polygon batch, additionally inspect fire, manual cycling, catch/release,
window backing and depth from both eyes on every listed camouflage. Confirm
the handle stays forward during firing and does not drag the bolt when lifted.
