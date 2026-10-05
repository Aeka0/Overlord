# P90 and Striker physical interaction

## Asset evidence

Bindings were authored offline from exported MW2CR viewmodels and native animation
tracks, checked against the weapon definitions captured. No live
addresses from that capture are reused. Export directories are tool arguments,
not runtime dependencies.

P90 base and arctic receivers have 37 bones with identical bind transforms.
Both charging handles are present in the mesh and weighted to `j_bolt`; the
exterior left/right geometry is symmetric (nearest mirrored vertex error below
0.00006 mm). Both hands therefore use mirrored contact poses on the same moving
part, without inventing a second bolt bone.

The P90 magazine pose comes from `h2_wpn_smg_p90_reload`, frame 13; the handle
pose comes from `h2_wpn_smg_p90_reload_empty`, frame 93. Mesh contact residuals
are below 0.8 mm. The native handle stroke is about 70 mm. Poses remain subject
to headset verification against controller and wrist calibration.

Striker has 20 receiver bones. `j_clip` is the permanent drum, `j_ammo` the loose
shell, and `j_ammo_cover` the loading cover. `j_button` belongs to the original
indexing animation. `j_reload` is not treated as a conventional charging handle.
The shell loading location is sampled from `h2_wpn_sho_striker_reload_intro`,
frame 20. The hand uses the shared SPAS-12 shell grasp.

## Interaction policy

- P90 uses a 50-round removable top magazine, physically withdrawn upward and
  seated downward. It preserves a chambered round, has no last-round hold-open,
  and needs a full charging-handle cycle after an empty reload. Both model skins
  share mechanics; each retains its own magazine material source.
- Both P90 charging-handle capture volumes have their rear edge trimmed forward
  by 1.5 native units (38.1 mm), reducing overlap with the top-magazine approach.
  The shared 11 cm capture radius allowed overlap: the authored magazine wrist
  was only 10.6 cm from the
  assisted handle contact region. P90 now uses a 6 cm radius. Comparing nearest
  contacts then caused the magazine to steal side-handle presses: each part uses
  a different finger point and box size, so their distances are not comparable.
  A fresh press in both regions now uses raw wrist orientation against the
  authored magazine/handle grasps. A tie retains action priority; a held part
  cannot switch targets until released. Outside overlap, ordinary contact
  admission applies. Both hands use their own mirrored grasp poses. Other weapons
  retain their existing radius and priority. Authored contact poses and the
  70 mm stroke remain unchanged.
- P90 magazine contents are mutually exclusive native mesh arrangements:
  `j_bulletempty` and `j_bullet1` through `j_bullet10`. The follower/mechanism stays
  in every detached magazine. The visual policy selects five-round buckets;
  this bucket mapping is an authored approximation, not a traced native hide-bit
  policy. Only one arrangement is selected at a time.
- Striker uses a fixed 12-round drum. Each successful shell insertion advances
  one position automatically, as requested. Accepted shots also advance it.
  It has no extra chamber slot, manual indexing button, removable drum, or
  tube-shotgun rack operation. A single shell loaded into an empty drum permits
  firing immediately.
- Striker shares shell supply, contact, escrow, native commit and interrupted
  interaction handling with the existing single-shell system. `feed_layout`
  separates fixed-drum rules from tube and pump rules. Drum index is retained in
  the weapon's mechanical state through hand transfer and drop/pickup.

## Verification and remaining visual checks

The  facing correction passed the client build, controller-input,
weapon-grip and physical-reload suites. Tests use actual mirrored wrist/contact
poses and runtime bounds, including real intersecting volumes sampled along
all three axes, 30-degree wrist variation, both skins and held-part retention.
Actual magazine/handle separation during headset use remains an acceptance check.
This candidate was deployed with the native launcher input fix.

The weapon grip suite covers exported receiver hierarchies, supported attachment
combinations, both P90 handle contacts, magazine subset selection, and Striker
part separation. Physical reload tests cover both hands, P90 extraction and
insertion, chamber conservation, Striker full/empty limits, automatic indexing,
interleaved firing, failed writes, retries and focus loss. Existing tube/pump
tests remain in the same suite.

Exported P90 magazine groups have no mixed-weight vertices or triangles crossing
into the receiver. Runtime rigid extraction still validates native surface flags
and group separation before publishing an asset. Native surface availability,
transparent magazine contents, Striker shell approach and both controller hand
poses still need a headset check; offline tests do not establish visual quality.
