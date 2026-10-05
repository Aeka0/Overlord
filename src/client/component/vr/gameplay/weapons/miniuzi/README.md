# Mini Uzi open-bolt adapter

Offline single-wield candidate, 2026-09-09. The exact native candidate is `uzi`,
base capacity 32, with receiver `h2_viewmodel_miniuzi_base`. Native name, capacity,
complete assembly, owned ammo cells and prepared part assets must all agree
before physical admission. No new in-game capture was required. Akimbo and
unreviewed attachments/native aliases do not inherit this base profile.

## Mechanics and controls

This is an explicit open-bolt feed policy inside the existing detachable-magazine
transaction system. It shares magazine ownership, escrow, pistol-well contact,
native compare/commit, prediction replay protection and interruption handling.
Closed-bolt weapons keep their separate chamber/feed transitions.

- All loaded ammunition belongs to the magazine. `chamber_loaded` is always
  false, capacity stays 32, and native chamber-plus-one adaptation is not enabled.
- A full top-handle pull cocks the internal bolt without extracting or feeding
  ammunition. It works with a loaded, empty or absent magazine. Repeated full
  pulls never spend a round; partial pulls do not cock a closed action.
- Insertion/ejection preserves cocking. Cock before removing the empty magazine,
  with the well empty, or after insertion; all orders work. A closed action still
  needs cocking after insertion. A cocked action needs no second pull.
- Firing requires a cocked-open action, an inserted nonempty magazine and a free
  action hand. Each accepted native automatic shot consumes one magazine round.
  The last shot closes the action; there is no empty-follower lock or chamber-only
  shot. Native prediction replay does not consume an additional round.
- A fresh trigger press with an empty or absent magazine releases the cocked
  bolt into the closed position, without a shot or ammunition change. A physically
  held handle blocks this release. Held input, duplicate frames, stale tracking,
  ownership changes and failed comparisons cannot queue another release; let go
  and press again after recocking. This event has tactile feedback and one native
  `wpn_dryfire_smg_plr` sound, with no shot or compound cocking sound.
- B/Y releases the magazine only. It cannot close or release the sear. The free
  hand uses trigger for waist-magazine, insertion and top-handle grabs. A support
  lease excludes reload-part grabs; releasing support while pinching requires a
  fresh press. One hand cannot hold both a spare and the handle.
- Tracking/switch interruption releases a fully pulled handle into the cocked
  state; partial pulls retain their original state. Reserve grants cannot cock,
  fill the physical magazine or invent a chamber round. Returning to an owned
  weapon preserves its mechanics rather than re-inferring them from loaded ammo.

At first stable native-idle admission only, a loaded weapon begins cocked and an
empty weapon begins closed. This matches the exported idle/empty presentation;
it is not a reload shortcut. Explicit feature exit restores native control.

## Hand and part evidence

The native Mini Uzi idle has no usable support grip. `stock_support.hpp` borrows
the left wrist orientation and 18 finger/palm/webbing rotations from
`h2_wpn_pst_mp9_idle` frame 0. After headset feedback the wrist is at gun-local
cm `(10, 3, -5)`, one centimetre back and right from the preceding fit, wrapping
the front end of the folded `j_support_chin_arm` stock. The stock stays folded;
no stock operation is added. The left wrist yaw is five degrees clockwise
viewed from above. Both hands share that physical position; only orientation
and fingers mirror. Carry acquisition and rendering use the same support anchors.
Runtime retains current glove segment lengths.
The Mini Uzi native rear wrist/right fingers stay unchanged. Its previous free
hand reference is pinned separately so the new support orientation cannot rotate
waist-magazine or charging-handle input. The shared front/rear baseline determines
aim when support is engaged; release restores rear-hand aim. The stock contact
was reviewed on the exported skin and still needs HMD acceptance.

Mini Uzi `reload` frame 42 supplies the replacement-magazine grasp. The well is
gun-local cm `(-0.28135, 0, -5.76768)`, near the grip mouth at Z=-5.93cm rather than
the extended magazine baseplate at Z=-14.65cm. Shared pistol capture uses a 3.5cm
radius, 6cm above/below the mouth, and 85-degree insertion tolerance.

`pullout_first` frame 22 supplies the top-handle hand, normalized to the forward
handle and moved rearward 0.974288cm. The foremost reference finger is at X=11.5cm;
nearest finger/handle vertex distance is 0.647mm. The acquisition bounds cover
the top knob. This proximity measurement does not certify glove collision or
ergonomics throughout a physical stroke.

The receiver's names are misleading: `j_bolt` is the top charging handle, while
`j_open_reload` is the internal bolt. Both are direct children of `j_gun`.
`tag_front_sight_on` follows the handle. Native handle travel peaks at 84.634mm;
the internal bolt reaches 52.516mm. Manual range is 85mm, full-stroke threshold
77mm. The shared optional `charging_handle_bolt` interpolates their distinct
native pullout channels; brief animation anticipation before handle movement is
omitted from hand-driven motion. The handle always returns forward and does not
reciprocate with shots. The internal bolt remains 52.513mm rearward at the sear,
including after handle release, until the last shot closes it.

The internal-bolt closed rest uses receiver bind. Native idle already includes
the open offset; using idle as the closed rest would apply that offset twice.
Native accepted-shot/lastfire bolt animation remains visible. A missing,
aliased, wrongly parented or zero-retention internal bolt rejects physical
admission instead of presenting a forward handle as evidence of an open action.

Receiver SHA256:
`995818e57c0c2e54a9ca81656a54f983fd8325f9cb54041eb0d389697868ce58`.
Animation hashes and frame numbers are beside the authored poses. The 21-bone
receiver has six rigid surfaces, with no mixed-weight vertices or crossing
triangles. Metal surface 4 includes eight groups: handle, release, internal bolt,
selector, trigger, magazine, sight and round. Exact receiver subsets isolate the
726-vertex/854-triangle magazine body and 338-vertex/416-triangle round; visibility
keeps the other six groups intact when the magazine is empty or detached. The
standalone world clip aligns within 0.000007cm, but the shared subset service is
used for independent empty/loaded presentation. No native asset is modified.

The exported `weap_miniuzi_clipout_plr`, `weap_miniuzi_clipin_plr` and
`weap_miniuzi_chamber_plr` keys resolve through the selected live WeaponDef. The
compound cocking clip plays once on handle completion (`action_close` is the
shared completion effect, not an instruction to close the internal bolt). The
rear stop is tactile; automatic shot audio remains native.

For empty-trigger closure, a read-only live `uzi` WeaponDef check identified
`wpn_dryfire_smg_plr` as its player empty-fire alias. It is explicitly routed as
an alias through the existing positional native sound service, rather than
looked up as a notetrack key. The sound is queued only for the confirmed dry-fire
transaction; duplicate/held input and rejected writes cannot replay it.

## Verification boundary

Client Debug x64 compilation and all twelve VR regression executables passed.
Coverage includes native admission limits, exhaustive short operation sequences,
both cock/reload orders, absent/empty/live-magazine strokes, partial and repeated
pulls, support exclusion, rejected commits, interruption, reserve grants, native
shot replay, exact rig/part binding, magazine masks and shared WARP rendering.
Existing closed-bolt, M4/AK assembly and revolver regressions still pass.

Both-eye presentation, audible dry closure and the new stock grip still need
headset acceptance. Native sound identity was verified read-only. Offline
exports, parsing tools and previews are local-only and are not build inputs.
