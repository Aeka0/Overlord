# M1911 base adapter

In-game: admission and interaction work; the user accepted the final slide-grasp
position on 2026-09-08. Shared-surface visibility is tracked separately below.
This directory owns weapon-specific poses,
mechanics, slide/contact tuning, native notetrack keys and immutable reload data.
No original assets or extraction tools are packaged.

Receiver: `h2_viewmodel_colt45_base`; magazine: `h2_weapon_colt45_clip`.
Native candidate: `colt45`, base/effective capacity 7; runtime verifies all three
contracts before physical admission. Ordinary right-rear single wield only.

H2 empty-reload frames 46/22 provide settled grip and magazine grasp. First-pullout
frame 13 provides the slide grasp. The relative H1 idle is not interpreted as an
absolute H2 hand pose. Native glove segment lengths are retained.

The user-provided crash-frame image still shows ejection-port obstruction with
the deployed 2 cm rear shift; reading the loaded pose confirmed that shift was
present. The next candidate targeted the foremost finger-skin edge at preview
X=+2.5 cm, not a wrist coordinate or a further 2.5 cm shift. HMD feedback confirmed
that movement worked but went too far. The preceding candidate returned one quarter
of that latest increment, retaining the older 2 cm baseline: 2.3434 cm forward,
edge X=+4.8434 cm. The next HMD feedback requests another 1.5 cm forward;
the current edge is X=+6.3434 cm, total rear shift 7.5301 cm from source and
wrist X=-10.8818 cm. Orientation and
curls are retained; acquisition contact is recalibrated onto the rear slide
surface, not translated off the gun. Mechanical stroke/tolerance are unchanged.
The user accepted this final position in HMD testing. Actual hand sets retain
their dimensions; this does not imply acceptance for every glove combination.

The receiver's body surface
also contains magazine/round groups, so this profile uses renderer-local rigid
group visibility, not the whole-surface hide mask used by M9/Desert Eagle.

Full specification, known shared issue and acceptance checklist:
[pistol adapters](../../../../../../../docs/vr-pistol-adapters.md).
