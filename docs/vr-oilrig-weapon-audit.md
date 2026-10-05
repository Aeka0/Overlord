# Oilrig loaded-weapon variant audit

This audit maps Oilrig weapon variants to reviewed profiles and offline
assembly contracts. Mission-input repair and headset acceptance remain
separate validation areas.

## Scope and evidence

The audit executable uses the actual C++ `registered_profiles`, `resolve_rig`,
`select_profile` and native feed admission methods. Composite assets are expanded
and semantic root attachments reconstructed. Where a definition delegates its
hands to the player, the captured canonical 68-bone hand model is supplied and
marked `hands_assumed`. This is an asset/recipe audit, not a capture of every
live DObj or a certification of in-headset appearance. Primary gunModel slot 0
is evaluated; legacy alternate slot assets are retained but not mistaken for
the active first-person model.

## Confirmed omissions repaired

| Native variant | Omission | Reused implementation |
| --- | --- | --- |
| `spas12_arctic` | Native-name admission and arctic receiver registration | Existing 17-bone SPAS pump, shell loading, hands and sounds |
| `spas12_arctic_reflex` | Same arctic recipe plus registered arctic red-dot assembly | Same pump recipe and existing shared optic contract |
| `aug_reflex` | Plain receiver and plain foregrip alias; only arctic assets were registered | Existing 18-bone AUG magazine/handle and hand poses |
| `dragunov_arctic` | Arctic receiver, native identity and scope alias | Existing 19-bone Dragunov mechanism and four-bone scope |

Each repaired receiver has exactly the same bone names, parents and all bind
transform components as its already-authored counterpart. The plain/arctic AUG
foregrip and Dragunov scope also have zero bind-transform difference. Separate
model/recipe identities are retained; no new hand poses are guessed, no native
name wildcard is added for SPAS/Dragunov, and capacity/topology checks remain.

The post-fix primary-assembly audit matched 64 of all 86 definitions, including
the four repaired variants. This total includes some alternate-fire descriptors
that reuse their host model and is not a count of 64 independently playable guns.
No additional missing ordinary-firearm variant of an already-registered family
was found in this captured level.

## Items that are not ordinary missing variants

- `usp_scripted` has a matched USP model but a separate native identity and
  inventory type 4 (ordinary witnessed firearms use 0). It remains excluded
  from ordinary physical magazine admission pending a specific script contract.
- `m203_m4`, `gl_ak47_arctic`, `m203_m16`, `m203_m4_reflex` and
  `m203_m4_silencer_reflex` share host assemblies but have one-round secondary
  feeds. Failure to match the host's primary magazine is expected; existing
  underbarrel adapters own their ammunition and input.
- `none`, `defaultweapon`, cheat knife, grenades, standalone launchers and
  vehicle/NPC-only descriptors are not treated as skins of an existing manual
  firearm merely because the level's definition table contains them.

Verification: Debug x64 v142 client build, controller-input, weapon-grip and
physical-reload suites pass. Tests include arctic SPAS with its red dot and
rejected wrong attachment parent, plain AUG accessory assemblies, arctic
Dragunov scope/native identity, pump behavior for both hands and derived catalog
coverage (51 detachable recipes, six individual-shell recipes).
