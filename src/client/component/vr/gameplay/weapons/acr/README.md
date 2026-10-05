# ACR physical adapter

Candidate with read-only live capture, 2026-09-09. The native family is `masada` / `masada_*`,
base capacity 30. Complete receiver/attachment geometry, unique owned ammunition
cells, primary mode, exact capacity and prepared assets remain mandatory before
physical admission. Native instance names stay pinned separately; discovering a
family does not grant ammunition authority. The observed
`masada_digital_grenadier_eotech` / 30 uses a digital receiver, digital EOTech and
`attach_h2_m203_vm_digital`. The missing launcher alias rejected the entire scene,
including rifle grip/reload. Both reviewed M203 assets now share the launcher
role; the linked `gl_masada_digital_eotech` / 1 remains outside rifle authority.

## Interaction

ACR uses the existing M4-style closed-bolt transactions: B/Y ejects the magazine,
empty follower lock remains open, and a loaded replacement can be chambered by
either the release button or a complete handle pull. Inserting into a closed
empty chamber still requires cocking. Tactical reload supports 30+1; repeated
live full strokes extract one round each, while partial strokes do not. Native
automatic fire, shot replay protection, magazine escrow and interruption rules
are shared. No M4 geometry or handle travel is copied onto the ACR receiver.

The native `j_bolt` is a side charging handle, not an independently visible bolt.
Like M4, it returns forward while the internal mechanical state remains locked
open; no unsupported internal-bolt mesh is fabricated. It does not reciprocate
with shots. Native reload-empty travel is 112.03mm; manual range is 113mm with a
102mm full-stroke threshold. The actual handle is on negative gun Y. Native
pullout uses the right hand, so its frame-16 hand is converted into a left
overhand using the established AK anatomical reflection. Rear weapon ownership
stays with the right hand. A 3.406543cm rearward fitting adjustment places the
foremost reference finger at gun X=34.5cm, with 3.109mm nearest skin/handle vertex
distance. The acquisition volume remains on the actual side handle.

The free hand draws and inserts magazines or manipulates the handle. Support
ownership excludes part grabs; releasing support while pinching requires a fresh
press. The shared rifle well uses 4.5cm radial capture, 6cm above/below the mouth
and 85-degree tolerance. Its authored mouth is gun-local cm `(16.77248, 0, -1)`,
well above the protruding magazine baseplate at Z=-17.01cm. The magazine-lip marker
is clip-local cm `(0, 0, -3.66)`, aligned with that insertion centreline. Native
reload frame 24 supplies the replacement-magazine grasp; it is after clip-out
and before the frame-32 clip-in event.

## Assemblies and support

The 19-bone receiver is authored in base, black and digital appearances. Each
retains its exact receiver as the independent magazine source. Base/black exported
geometry is identical; identities remain separate so live material/asset lifetimes
are never inferred from that fact. Each appearance has two support configurations:

- Bare handguard: both wrists and thirty-six finger/palm/webbing joints from ACR
  `idle` frame 0, with unanimated webbing taken from the reference glove bind.
- M203: left support wrist, fifteen fingers and three palm/webbing joints from
  native `h2_wpn_asl_masada_gl_idle` frame 0. Gun-local wrist in cm is
  `(26.850746, 4.674501, -0.946871)`. This replaces the earlier M4 donor pose.

Both use the shared two-hand aiming baseline; support release restores rear-hand
aim. The launcher pose preserves native rear wrist/fingers and the base free-hand
reference, so a support change does not rotate waist magazines or handle inputs.
Magazine and handle manipulation each override the complete eighteen-joint left
hand chain from their reviewed source frames, preventing support-palm carryover.
M203 only changes grip geometry in this phase; grenade mode switching, firing and
loading are not implemented. Its seven-bone attachment must resolve to ACR's
`tag_m203`. Base and digital M203 models are admitted; duplicate launchers across
either skin, unknown or misparented attachments are rejected. Both base and GL
rifle equip clips retain authored hand/part poses; grenade actions are excluded.

The existing common rifle attachment contracts admit reviewed optics, suppressor
and heartbeat models only when this receiver supplies their required roots.
Missing `tag_laser` prevents laser admission. An exported `tag_shotgun` is not
authorization for an unreviewed shotgun variant. Suppressor muzzle mapping remains
independent of the support choice. Aliases such as `acr`, bare `masada_` and
launcher-feed identities such as `m203_masada` do not enter the rifle family.

## Geometry and source trail

All nine receiver surfaces are rigid, with no mixed weights or crossing triangles.
Magazine surface 1 has 2,737 body vertices / 2,228 triangles; lower-round stack
`j_bullets` has 1,124 / 1,528; top `tag_bullet` has 434 / 594. Both round roots are
children of `tag_clip`. The independent subset service prepares body-only,
body/top-round and body/top/lower-stack variants. Both round groups are hidden
when empty; removing the magazine does not hide the side handle or receiver.

The separate world clip has only 883 vertices and is not substituted for this
receiver magazine. Exact receiver subset and shared visibility services handle
the three appearances; no native asset is mutated and no new render hook is added.
The existing bounded retained-asset cache is reused. Exhaustion rejects admission
and can require a game restart; in-flight descriptors are never recycled.

Receiver source hashes:

- Base and black: `708f880ad6facbacf7f4f4f2499f8cb97aea5b44c39211d2b2331fd4ba84cecc`
- Digital: `0dd140e73b7a6e99ebfb128def58de9e18a274227104b2d1072ec21d5f540f9e`

Animation names, frames and SHA-256 hashes are beside the compact poses. Native
translation channels are additive, while rotations replace parent-local rotation;
both magazine-child round rests preserve that distinction. Raw meshes, animations,
parsers and previews stay in the local audit area and are not build inputs.

The GL idle was read from immutable native XAnim source streams, not the
VR-modified DObj pose. Its static rotation/translation layout follows
[Greyhound's translator](https://github.com/Scobalula/Greyhound/blob/master/src/WraithXCOD/WraithXCOD/CoDXAnimTranslator.cpp).
The same decoder reproduced the exported base idle with maximum hand-bone
position error below 0.00001 cm and quaternion error below 0.00000002. The compact
GL pose records a canonical channel-payload digest. Live digital magazine rigid
groups also matched the reviewed body/lower-stack/top-round geometry above.

Exported `weap_masada_clipout_plr`, `weap_masada_clipin_plr` and
`weap_masada_chamber_plr` keys resolve through the exact live WeaponDef. Compound
handle audio plays once at completion; rear-stop feedback is tactile and firing
audio remains native. No unrelated weapon's sound map is substituted.

## Verification boundary

Client Debug x64 compilation and twelve VR regression executables passed. The
ACR assembly suite exercises 3,024 combinations of skin, bare/base-M203/digital-M203 support,
optics, suppressor, heartbeat, model ordering and glove labels. Shared reload
tests cover both logical rear hands, both lock-release paths, support exclusion,
partial/repeated pulls, magazine ordering, low reserve, exact ammunition
conservation and native automatic-shot replay. Rig tests cover both round roots,
part visibility, free-hand basis, contact transforms and rejected attachments.
Existing Mini Uzi, closed-bolt, M4/AK assembly, revolver and WARP tests also pass.

These combinations are synthetic coverage, not a claim that every combination
exists in the game. The current digital launcher composition and source poses
were captured live; actual physical admission, both-eye rendering, audio and HMD
ergonomics still need an in-game pass after the fix.

## Arctic heartbeat assembly (2026-09-13)

The captured `masada_silencer_mt_camo_on_h2` has an arctic receiver, arctic
red dot, silencer 01 and `attach_h2_heartbeat_vm_arctic`. Its 19 receiver bones
and the sensor's nine bones have the same hierarchy and normalized bind
rotations as their base exports; maximum position differences are below
0.000003 cm. Both missing render identities are now registered. The receiver
keeps its own detached magazine source and shares the existing ACR mechanics.
The captured hierarchy is retained in `tests/vr/acr_arctic_data.hpp`; assembly
coverage includes all four skins and both sensor appearances (6,048 cases).

Heartbeat open/closed equip clips use the existing rifle equip suppression.
Sensor transitions remain native. Grip-driven sensor folding, native scan
enable/disable, alternate-mode integration and stereo screen verification are
deferred pending another live capture; this registration does not implement them.
