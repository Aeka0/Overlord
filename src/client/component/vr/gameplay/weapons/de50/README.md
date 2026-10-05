# Desert Eagle base adapter

User acceptance passed (2026-09-08) for the tested base single-wield adapter.
`de50` and `desert_eagle` are mixed original
asset families; `akimbo_l/r` is excluded. This directory owns poses, mechanics,
slide/contact tuning, notetrack keys and reload data, not raw exported assets.

Receiver: `h2_viewmodel_desert_eagle_base`; magazine:
`h2_weapon_desert_eagle_clip`. Native candidate: `deserteagle`, base/effective
capacity 7; runtime verifies identity, capacity and complete model assembly.
Gold/silver variants and left-rear/dual-wield meshes are not silently admitted.

Idle frame 0, reload frame 31 and first-time-pullout frame 13 provide candidate
poses. The standalone magazine needs inverse `tag_clip` bind rotation; contact
uses an oriented magazine well, avoiding double tilt and gun-axis assumptions.

Full specification, known shared issue and acceptance checklist:
[pistol adapters](../../../../../../../docs/vr-pistol-adapters.md).
