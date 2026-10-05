# Estate weapon variants

Read-only sampling covered the native DB hash and override chains
used by the weapon asset-pool listing: 126 unique weapon assets, 120 registered
definitions and no read errors. Pool presence does not prove an active pickup.
The player held `masada_digital_grenadier_eotech`, `beretta`, `claymore`,
`fraggrenade` and `flash_grenade`.

## Adapted variants

| Family | Captured native names | Binding change |
| --- | --- | --- |
| AK-47 | `ak47_woodland`, `_eotech`, `_reflex`, `_grenadier` | Woodland receiver, cover and shared optics; existing GP25 support pose |
| TAR-21 | `tavor_woodland_acog`, `tavor_woodland_eotech` | Woodland receiver and shared optics |
| FAMAS | `famas_woodland`, `_eotech`, `_reflex` | Woodland receiver and shared optics |
| Dragunov | `dragunov_woodland` | Exact native name, woodland receiver and native scope |
| Striker | `striker_woodland`, `striker_woodland_reflex` | Woodland fixed-drum recipe and receiver |
| SPAS-12 | `spas12_eotech` | Exact native alias; existing 17-bone H2 receiver and EOTech contract |
| TMP | `tmp_reflex` | Exact alias shared by physical reload and native chamber policy; explicit two-bone reflex contract |
| L86 | `sa80lmg_scope` | Exact native alias and one-bone `tag_sa80_scope` attachment |

AK, TAR, Dragunov and Striker woodland receivers have byte-identical bind
matrices, bone names and parent indices to the ordinary live models. Woodland
AK cover, Dragunov scope and ACOG/EOTech/reflex models likewise match their base
assets. FAMAS woodland matches the existing 20-bone arctic export: equal names,
parents and global rotations, with maximum position difference below 0.000001
native units after centimetre conversion. Existing poses and mechanics are
reused; detached magazines retain the specific receiver as their material source.

`estate_variant_data.hpp` preserves native skeletons and bind hashes for 16
actual assemblies, including the held digital ACR with EOTech and M203. Tests
check scene selection, hand/rest-pose binding, physical feed parts, capacity,
skin source, independent underbarrel binding and rejection of wrong parents or
unknown receiver names. These are asset-contract fixtures with reconstructed
attachment roots, not a captured animated player DObj or headset acceptance.

All 11 primary/secondary underbarrel name pairs classify. Ten use H2 models
from existing weapon families. The unregistered `m4m203_motion_tracker` / `m203_m4_motion_tracker`
pair uses the old integrated `viewmodel_m4` (35 bones), so its name match is not
an assembly pass. It remains unbound; no runtime registration bypass was added.

## Deferred bindings

Follow-up: [Model 1887 lever interaction](vr-model1887.md) now implements a
candidate binding from this captured receiver. Headset acceptance remains pending;
the sampling history below records its state at the time of the audit.

Model 1887 uses its dedicated [lever-action integration](vr-model1887.md).
This pool contains one definition, `model1887`: capacity 5, eight pellets,
native bolt-action flag, segmented reload and one-shell additions. Its
`h2_viewmodel_model_1887_base` has nine bones and four surfaces:
`j_gun`, `j_action`, `j_ammo_01`, `j_ammo_02`, `j_ammo_03`, `j_bolt`,
`tag_brass`, `tag_flash`, `j_hammer`. The hammer is a child of `j_action`;
other listed parts are receiver children. No ordinary pump adapter is assigned
to this lever-action weapon. Its definition and skeleton are retained in the
local sampling archive for that work. A later animation-metadata read detected
changed asset addresses and stopped; animation metadata was not archived.

`rpg`, `rpg_player`, `javelin` and `javelin_estate_jeep` remain separate launcher
work; a successful ordinary-gun audit does not imply their physical adaptation.
SPAS silencer and heartbeat adaptation remains canceled; EOTech uses the current
H2 skeleton rather than the canceled legacy 26-bone model.

## Validation and deployment

Client compilation and weapon-grip, pistol-profile, physical-reload and
closed-bolt regressions pass. Newly admitted skins and attachments still need
VR visual/interaction acceptance. Builds remain local; the running flat game
was not closed, restarted, patched or deployed to during this audit.
