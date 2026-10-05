# Special inventory knives

Status: native metadata and offline poses reviewed; CPU regressions pass;
native gameplay and HMD acceptance pending.

The special knives use the ordinary physical inventory lifecycle outside
script-owned sequences: either hand can hold them, waist/back storage remains
available, and release into clear space uses native drop/pickup transactions.
They are independent of the automatically returning chest knife.

| Native identity | Reviewed receiver |
| --- | --- |
| `ending_knife`, `alt_ending_knife` | `viewmodel_commando_knife` |
| `ending_knife_bloody`, `alt_ending_knife_bloody` | `viewmodel_commando_knife_bloody` |
| `h2_cheatcommandoknife` | `wpn_h1_melee_rifle_bayonet_vm` |

The bloody alternate is included because the loaded bloody definition links to
it. Admission uses exact names, including the alternate inventory class, never
a substring match or fixed weapon index. No new inventory items are granted.

## Native evidence and capability boundary

Read-only capture found native type 1, category 3, clip capacity 0
and melee damage 200 on these definitions. The ending alternates have inventory
class 3; the other definitions have class 0. Treating their native type as an
ordinary bullet firearm would incorrectly admit firing and ammo projections.

The registered assemblies therefore have a dedicated `melee` capability, with
no reload, support grip, ammunition feed, muzzle, ejection port or ADS. The
independent hand DObj accepts the reviewed two-bone `tag_knife` assembly or
three-bone bayonet assembly. Ordinary firearm admission still requires a valid
muzzle. Knife poses do not publish a dummy muzzle to the firing/render cache.

Pose sources are frame 0 of `h2_wpn_melee_knife_dizzy_idle` and
`h1_wpn_melee_bayonet_knife_idle`. Compact wrist/finger/rest transforms and source
hashes live in `weapons/special_knives/poses.hpp`; the shared anatomical mirror
and glove lengths support the opposite hand. No exported meshes or animation
curves ship with the mod.

The ordinary inventory grip now derives its controlling rotation from the
current glove's neutral wrist basis relative to `tag_weapon`, just like the
empty VR hand. It cancels the authored knife-in-wrist rotation when orienting
the object, preserving both the physical wrist pivot and the native knife/hand
contact. This is shared by clean/bloody/alternate/bayonet knives and both hands;
it does not rewrite animation fingers or add per-knife calibration offsets.
The rig-derived rotations are copied by value into carry snapshots, so secondary
weapons, interaction sampling and presentation use the same basis without
retaining stack-owned profiles. Script-owned finale arms remain unchanged.

Blade sweeps reuse the existing melee trace, native damage, obstruction check,
400 ms shared cooldown and contact withdrawal rules. They use full knife damage
rather than the firearm blunt multiplier. Switching between the chest knife
and an inventory knife invalidates swing history even when lease counters match.
Blood FX resolves each actual knife's `tag_knife_fx`, with the current carried
instance and ownership revision checked before emission.

## World presentation

The ending definitions point to the unrelated `weapon_usp` composite. The
bayonet's NPC model has a different orientation and offset. These bindings cannot
be aligned through a firearm muzzle, since none exists.

The VR world-model accessor selects the verified native view knife as the new
world source. The engine's drop path at `0x1404c5b69` calls this accessor before
setting the entity's model, so the change persists after the short drop preview.
Native item identity, deletion, physics/trajectory and pickup remain native.
Shared `WeaponDef` and `XModel` resources are never edited.

For body storage and the drop preview, the main asset owner creates immutable
rigid root subsets through the existing `scene_models::rigid_part` service and
registers their source identity. In particular, the bayonet contains a separate
`tag_clip` sheath (1,112 exported vertices); its 2,368 blade vertices belong to
`j_gun`. Only the blade enters the rigid world model. The same sheath is masked
in the tracked assembly and native item DObj hide-tag path. The preview hands
off when the native source DObj appears, rather than displaying both models for
its entire timeout.

All placement and release clearance use the same blade root. Missing subset
resources reject release until available. Assets are freed only at the drained
native zone-unload boundary. The captured view models have no model-level
physics preset or collision map; native weapon-level physics and the existing
gravity-trajectory fallback retain authority. Actual ground contact remains an
in-game acceptance item.

## Story boundary and verification

This change adds no ending mission state machine, throw gesture or QTE mapping.
Existing native script/weapon permission gates remain in place. The ending
script listens to `+attack` and `+melee`; its pulling, throwing and cinematic
phases need a separate interaction design and acceptance pass.

`vr-weapon-grip-tests` covers exact identity rejection, all admitted assemblies,
missing-muzzle separation, malformed skeletons, sheath visibility, both hands'
full IK/finger solve, handle registration, body storage and rejected/accepted
release transactions. `vr-melee-tests` covers source changes and existing
damage/motion gates. Hand-rig and physical-reload regressions also pass.

In-game acceptance must check clean/bloody/bayonet rendering in both hands,
blade impact and blood FX, waist/back return, drop model continuity, ground
contact, pickup, pause/recenter, checkpoints and zone changes. CPU tests and
read-only native signature checks do not certify those outcomes.
