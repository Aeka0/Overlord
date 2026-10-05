# Dragunov physical reload

The captured native `dragunov` definition has a ten-round magazine and ordinary non-segmented reload. The viewmodel consists of `h2_viewmodel_dragunov_base` (19 bones) plus `attach_h2_dragunov_scope_vm` (4 bones). Both the complete receiver and its scope attachment are validated before physical interaction is admitted.

The adapter uses the existing detachable-magazine state machine:

- Physically extract the magazine, or strike the real `j_mag_release` latch with a spare magazine using the AK interaction. The spare remains in the operating hand after the seated magazine falls away.
- Preserve a chambered round during a tactical reload; support ten rounds in the magazine plus one chambered round.
- Lock open after the last shot. Inserting a magazine preserves the lock; pull and release the charging handle to chamber a round. The release button neither ejects this magazine nor closes the bolt.
- Both physical hands use the same per-instance ammunition and transfer authority. Loading, empty-state and handover behavior use the existing transaction boundaries.

## Authored geometry

Idle frame 0 supplies rear/support hands and moving-part rest transforms. The native `j_bolt` idle transform is translated relative to its static bind, so charging contacts are fitted to the posed mesh rather than the raw model bone origin. Full action travel from `reload_empty` and `pullout_first` is 15.231 cm. The shared AK edge-grasp styles are fitted to this rifle's own external handle.

The magazine grip comes from `h2_wpn_sni_dragunov_reload`, frame 20. The source loads with the right hand; complete finger and palm chains are retargeted to canonical left around the rigid magazine's local plane. The current glove's closest contact required a 0.247 mm fit adjustment. Position and rotation provenance hashes are stored alongside the poses.

The physical magazine uses receiver subsets `tag_clip` and `tag_bullet_single`, preserving the original model, materials and bullet hierarchy. The single visible cartridge lies at the magazine lips, below the chamber; it follows the magazine. The latch contact is taken from `j_mag_release` geometry. The original clip-out, clip-in and chamber sound aliases are used.

## Related refinements

- `deserteagle_gold` reuses the ordinary Desert Eagle reload profile and hand poses. Its distinct gold receiver remains selected for rendering. Native capacity, magazine model, sound aliases, and all 11 bone binds were verified against the ordinary version. Admission accepts only the two witnessed native names.
- AK, M14/M21 and Dragunov share two left charging hooks. The intermediate and distal joints of the contacting index/pinky finger open by 30 degrees, and each wrist places the handle inside that finger's curl. This replaces the earlier 5 mm wrist nudge and outer-skin-point fit. The left wrist now sits 3 mm closer to the receiver. Right-hand hooks use their own barrel-axis facing before anatomical mirroring; the earlier outermost-vertex translation and closed-hand override are removed. Each rifle retains its own actual handle contact and travel.
- SPAS12's support wrist moves forward 2 cm. Pump presentation and support acquisition share that wrist.
- SPAS12, M1014 and W1200 expand both shell-loading contacts downward 4 cm and horizontally 1.5 cm each way. The original sphere is swept through this asymmetric region: previous contacts stay valid, and the upper boundary stays unchanged. Contact rearming and overlapping port selection use the same expanded distance.

Profile tests bind captured Dragunov/scope and gold Desert Eagle hierarchies, verify shared hand contacts and exact native-name admission. The shared precision reload tests also exercise Dragunov extraction, latch impact, chamber retention, empty-lock behavior and manual release for both hands. Tube boundary tests cover all extension directions and the unchanged upper limit. Headset validation is still required for grip comfort, magazine alignment and the full attachment rendering path.

AK, M14/M21 and Dragunov share a 4 cm latch-strike radius (previously 2.5 cm), with 7.5 cm rearm separation. Direction, minimum speed/travel, tracking-jump rejection and one attempt per contact remain required.

The charging contact target stays outside the right receiver wall and limits
assistance to 6 cm, keeping the seated-magazine region clear. See
[weapon interaction refinement](vr-weapon-interaction-refinement.md).
