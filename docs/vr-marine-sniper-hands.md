# Marine sniper hand binding

The `boneyard` player can use `viewhands_marine_sniper`, a 67-bone hand model
without `j_webbing_le` and `j_webbing_ri`. These two skin-deformation leaves
exist in the 36-joint authored grip profiles; their absence previously rejected
the whole profile, including both arms and the held weapon. Physical release
then failed because no held weapon/muzzle pose had been published.

Read-only live diagnosis confirmed valid tracking for both hands,
focused input, active carry, native/predicted weapon flags zero, no mounted
state or scripted-sequence ownership, and an owned M14 EBR. Its receiver/scope
profile matched, all weapon rest parts existed, but `pose_library.valid` was
false. The only missing authored hand nodes were the two webbing leaves.
The diagnostic label was `profile animation/hand-pose schema unavailable`.

The shared pose binder now permits just those two named leaves to be absent.
Present webbing nodes still undergo normal duplicate, parent and transform
validation. Wrist, articulated finger, palm and weapon-part requirements are
unchanged. This does not replace the native hand model or bypass script gates.

`marine_sniper_hand_tests.hpp` uses compact skeleton/bind metadata from the
actual loaded model. It verifies M14 binding with 34 available pose joints,
continued rejection of missing fingers and detached palms, retained webbing
when present, and rejection of corrupt present webbing transforms.
No meshes or animation assets are distributed in the fixture.

Re-entering this level with the corrected build still requires headset
acceptance of hands, weapon display and physical release. Historical status
files can contain a different level's state; compare live instance/binding
status and the running executable's PDB before diagnosing a new report.
