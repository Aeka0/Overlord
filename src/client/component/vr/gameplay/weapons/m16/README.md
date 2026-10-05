# M16A4: handguard and M203 rifle support

Candidate, 2026-09-12. Initial authoring used existing SEModel/SEAnim exports and
the retained `dcburning` native asset dump. Follow-up read-only sampling verified
the loaded M16 ACOG receiver surfaces and native hidden bones. HMD acceptance
of the visibility and burst-timing corrections remains pending.

The default support pose comes from `h2_wpn_asl_m16_idle`, not M4's vertical
foregrip. The native recipe joins `h2_viewmodel_m16_base` with
`attach_h2_m16_armor_vm` at `tag_armor`; reflex and ACOG add their matching optic.
The grenadier recipe replaces the cover with `attach_h2_m203_vm` at `tag_m203`
and uses `h2_wpn_asl_m16_gl_idle` for support only. Both retain the same rear
hand, free-hand basis and physical definition. A bare receiver also uses the
handguard pose. Duplicate/conflicting covers, unknown underbarrels and incorrect
attachment topology are rejected. Presence of a receiver tag alone does not
enable an unreviewed attachment or its alternate firing/loading controls.

An admitted optic hides the receiver's `tag_sight_on` carry handle and its
`j_sight_ring` child through the existing assembly mask and rigid-group filter.
Iron sights retain both. The exact two-bone topology is checked during binding;
hands, front sight, magazine and attached optic are not included in that mask.
Each independent assembly resolves its own mask without borrowing the selected
weapon's native DObj state.

Shared M4 closed-bolt rules provide button magazine release, valid bolt-release
priority, 30+1 and a non-reciprocating rear handle. M16-specific source poses
place both wrists, fingers, magazine and contact regions. The native first-raise
handle moves 8.1026 cm, rather than M4's 5.82 cm. It returns forward independently
of the locked bolt. Native right-hand handle contact is mirrored into the left
physical grasp using the established weapon-Y/hand-parity convention; the common
mirror path supplies opposite-hand interactions.

`tag_clip` owns `j_bullets`; `j_reload` is the charging handle. The detached
magazine uses exact receiver body/round subsets through `native_magazine_assets`,
avoiding a separately fitted world-magazine mesh. The export has 5,202 magazine
triangles and 896 round triangles, with no crossing triangles or mixed magazine
weights. Its 88 mixed-weight vertices belong to the separate front-ring mesh.
The shared rigid-part factory still checks the loaded native surface flags,
groups, part masks and GPU resources before admitting physical interaction.
Original source assets and unrelated skinned geometry are not modified.

The recorded `m16_basic`, `m16_reflex`, `m16_acog` and `m16_grenadier` definitions
use capacity 30 and native fireType 3 (three-round burst), with 200 ms burst
cooldown. Firing uses the existing per-instance native timing/shot path; this
profile does not turn M16 into automatic M4 firing or merge two hands' clocks.
The native cooldown accessor returns a float in milliseconds, not seconds.
The common adapter validates and rounds that value without multiplying by 1000;
the captured M16/FAMAS/M93R descriptors also drive the burst regression tests.
Feedback resolves the exact `weap_m16_clipout_plr`, `clipin`, `first_chamber` and
`chamber_close` notetrack keys through the owned native weapon.

Receiver SHA-256:
`58be751a7b9f437110a5ac2176cd92fe12702cb195af7b8a5d463de700a5cfeb`.
Pose headers record animation SHA-256 hashes. Only compact authored data is
shipped; export paths, parsers and raw assets are not build/runtime dependencies.
Equip poses contain physical parts only: duplicate attachment root tags must
remain under the assembly binder rather than being registered as unique parts.

Offline checks cover 336 assembly combinations, mechanical/finger binding,
wrong parents, conflicting attachments, native identity/capacity isolation,
handle contact and common AR gestures for both hands. Source geometry previews
check bare/M203 support, a seated magazine and handle contact. Re-run
`vr_hands_status` and `vr_reload_interaction_status` during HMD acceptance;
expect profile `m16`, variant `handguard` or `grenadier`. Check pickup/drop,
support handover, magazine removal/insertion, bolt release, charging handle,
three-round bursts and simultaneous firing with a different weapon.
