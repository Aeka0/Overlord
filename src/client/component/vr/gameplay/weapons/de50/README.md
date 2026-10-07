# Desert Eagle adapters

The existing base single-wield interaction has headset acceptance.
`de50` and `desert_eagle` are mixed original
asset families; `akimbo_l/r` is excluded. This directory owns poses, mechanics,
slide/contact tuning, notetrack keys and reload data, not raw exported assets.

Receivers: `h2_viewmodel_desert_eagle_base` and `h2_viewmodel_desert_eagle_gold`.
Native identities: `deserteagle` and `deserteagle_gold`, base/effective capacity 7.
Runtime verifies identity, capacity and complete model assembly. Unknown skins
and native dual-wield meshes are not admitted by a family-name guess.

Idle frame 0, reload frame 31 and first-time-pullout frame 13 provide candidate
poses. Contact uses the existing oriented magazine well. Each receiver's
`magazine_fill.hpp` recipe selects body-only or the original top one, two or
three rounds; native materials and the nested `j_bullet01` hierarchy are retained.
The shared rigid-part service supplies the inverse `tag_clip` bind transform
once. Gold has a separate geometry recipe and shares the existing `de50` feed,
grasp poses and sounds. This counted presentation still needs headset acceptance.

Full specification, known shared issue and acceptance checklist:
[pistol adapters](../../../../../../../docs/vr-pistol-adapters.md).
