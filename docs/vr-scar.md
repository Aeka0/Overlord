# SCAR-H physical reload

The SCAR-H profile uses an exact native weapon identity, reviewed model
assemblies, and authored physical-reload data. Headset acceptance remains
specific to the admitted attachments and hand configurations.

## Behavior and scope

The host rifle uses a twenty-round detachable magazine, a retained chamber round
for tactical 20+1 reloads, and an empty follower lock. The holding hand's existing
magazine-release control ejects the magazine. Insert a physical spare, then use
the same control or pull/release the charging handle to close an empty action.
The shared per-instance reload controller owns ammunition and part leases in
either hand; this adapter introduces no global current-SCAR state.

Bare, underbarrel shotgun and M203 rifle assemblies share the host mechanics.
Each uses its own native idle support wrist and fingers, with a shared rear-hand
and free-hand basis. Underbarrel roles are mutually exclusive and their complete
model topology is checked. Common optics, silencer and heartbeat attachment
contracts remain subject to the receiver's actual parent tags.

Only the main rifle is adapted. The shotgun shell, pump, plate and action retain
their own model bones; none participates in the rifle magazine or bullet mask.
No shotgun shell loading or pump gestures are introduced. The M203 feed is also
excluded. Thermal optics use the existing rendering behavior; the deferred
[scope rendering work](vr-native-ads.md) is unaffected.

The captured host identities `scar_h`, `scar_h_acog`, `scar_h_reflex`,
`scar_h_thermal`, `scar_h_shotgun` and `scar_h_grenadier` all have capacity 20.
`scar_h_shotgun_attach` (capacity 4) and `scar_h_m203` (capacity 1), including
suffixed members of those alternate families, are explicitly rejected even if
a caller supplies capacity 20. Native mode/instance checks remain mandatory.

## Source binding and geometry

Receiver: `h2_viewmodel_scar_h_base`, 19 bones, SHA-256
`9062f1d89ff16e6a57afb0c77c86bd097b27a0d81a0a8956894f35816afa78cf`.

| Purpose | Source |
| --- | --- |
| Magazine body | `tag_clip`, direct receiver child; 2,448 rigid triangles |
| Magazine rounds | `j_bullets`, child of `tag_clip`; 757 rigid triangles |
| Reciprocating action | `j_reload`, direct receiver child |
| Bare / shotgun / M203 support | `h2_wpn_asl_scar_h_idle`, `shotgun_idle`, `gl_idle`, frame 0 |
| Magazine grasp | `h2_wpn_asl_scar_h_reload`, frame 44 |
| Charging-handle grasp | `h2_wpn_asl_scar_h_pullout_first`, frame 17, native left hand |

Per-animation hashes accompany the compiled poses. Model/attachment hashes and
source parent chains accompany the test fixture. The magazine subset has no
triangles crossing into the receiver; it reuses the first-person materials and
the existing independent-part depth policy. There is no cosmetic spare magazine
bone and no separately authored chamber-cartridge mesh to hide or detach.

The source action travels 138.77 mm along gun-local -X. Its fire animation also
moves this bone, and the empty additive pose holds it rearward. It therefore uses
the shared reciprocating action policy rather than an independent returning
charging handle. Manual travel adds 6 mm beyond the follower lock, with the full
stroke threshold 3 mm beyond it, to distinguish a release pull from merely
acquiring an already-open handle. This is interaction tolerance, not an exported
animation measurement. Contact uses the external left tab rather than the long
internal bolt mesh; hand retargeting does not relocate the tab.

The reviewed magazine mouth is at gun-local Z = 1.5 cm. The native reload's
clip-out frame has the support hand away from the dropped magazine, so it is not
a grasp reference. Frame 44 grips the returning magazine body before insertion.
Nearest source mesh contacts are 0.63 mm for that grasp and 0.38 mm for the
charging-handle grasp. Side/top geometry review precedes compilation; these
distances do not substitute for headset ergonomic testing.

## Validation

`vr-weapon-grip-tests` and `vr-physical-reload-tests` pass, and the Debug x64
client builds. SCAR coverage includes 336 attachment/order/glove combinations,
real receiver and underbarrel parent chains, duplicate/competing underbarrels,
ambiguous part names, alternate feed rejection, attachment visibility isolation,
rotated magazine clearance, both holding hands, tactical 20+1 reloads, actual
last-shot lock, button/handle release, partial strokes and one-round extraction.

Headset acceptance: test bare and shotgun-equipped SCARs in both hands; verify
magazine grasp/insertion, rearward handle contact while empty, release/regrip,
and handover. Confirm rifle reload leaves the shotgun shell/pump visible and
stationary relative to their own assembly. The current profile admits the exact
reviewed H2 receiver only; legacy SCAR models require separate source review.
