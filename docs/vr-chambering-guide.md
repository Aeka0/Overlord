# Chambering guide

`vr_chamberingGuide` is a saved, default-off option in the launcher's Gameplay
settings beside waist quick reload. It can also be changed
with `seta vr_chamberingGuide 1` / `0`.

The physical detachable-magazine/manual-bolt presenter reads the existing
mechanical readiness rules: live ammunition must already be in the weapon, and
the action must require chambering. Reserve ammunition and a held spare magazine
do not qualify. Ready open-bolt weapons do not light up merely because their
chambers are empty. Belt-fed weapons wait until the belt is laid and the cover
closed. Holding the slide/bolt suppresses both the yellow part and the chambering
caption; the temporary hand-held firing block is not a request to chamber again.
Hints do not change ammunition, input, sounds, or mechanical state.

The guide colors the operable slide, charging handle or manual bolt. M4, M16 and
Vector also describe their separately bound receiver-release paddles; these are
requested only during a loaded follower lock. SCAR/UMP paddles share receiver
geometry without an authored face partition, so their charging handles supply
the cue. Existing M16/FAL handle partitions and F2000 folding pieces retain
their independent motion. M1014's automatic tube-fed bolt uses the same guide
consumer and readiness rules. Pump/lever, break-action and launcher controls
remain outside the requested bolt/slide scope.

M4's handle and release paddle and M82's handle live in native skinned surfaces.
`rigid_part::create_rigid_bone` admits their exact triangles only when every
vertex influence belongs to that one bone. Mixed weights and triangles crossing
the bone boundary reject the part; no dynamic skinning is approximated. This
accepts mixed rigid/skinned receiver models while leaving native geometry intact.
M4's `j_reload_trigger` latch and AA12's upper `j_reload_end` are explicit
`action_detail_bone` entries, each following its own final solved bone pose.

The first version depended on a loaded opaque objective material. A read-only
Museum (`ending`) witness confirmed the option enabled and M9/SCAR requests
present, but that material absent: preparation never started. The level had only
the additive C4 objective material, which cannot meet the opacity requirement.

The material-independent solid-yellow version passed user HMD acceptance. The
next visual iteration preserves the native surface's completed color, texture,
normal-map lighting and reflections. It applies a gentle yellow multiply and a
small additive yellow lift only inside the exact admitted part triangles:
`native * lerp(white, yellow, tint) + yellow * emission`, with objective yellow
`(1, 0.843137, 0)`. This is an emission-like adjustment of the displayed surface,
not a change to the source material's emissive channel or native bloom pass.

Tint moves from 10% to 18% and emission from 0.015 to 0.040 in linear color over
a two-second sine cycle. The effect never switches fully off during the trough.
Its time/strength is sampled once for both eyes. Alpha remains 1 and blending
stays disabled; native reverse-Z depth testing and the one-quantum surface bias
retain hand/receiver occlusion. The caption colors and warning logic are unchanged.

One cached GPU color copy is refreshed per active eye, before any guide part is
drawn. All parts sample that same immutable native result, preventing overlapping
triangles from accumulating tint or brightness. Target size, format and device
changes refresh the copy resources. Disabled/inactive guides do no copying.
No level material, donor texture, native sorted-material registration or per-frame
native material mutation is involved. Claymore rendering is unchanged.

Preparation runs on the main asset owner after an enabled presenter requests an
exact catalog receiver. Requests resolve posed profile copies back to immutable
catalog entries by receiver and mechanical capability, covering both magazine
and automatic tube feeds. `rigid_part` supplies the exact bone/face subset;
the overlay owns compact immutable vertex/index buffers containing only used
positions. An eye pair retains shared ownership independently of asset retirement.
Render requests retain only catalog-owned strings, never a stack-local posed
profile. Mechanical presenters publish into one `chambering_guide_presenter`
history and eye consumer. The overlay selects the same object, matrix buffer, pose epoch and weapon
generation as the native scene. Both eyes freeze one selection and use their own
current placement/eye origins and view-projection matrices. Drawing restores the
complete immediate-context state through the existing marked command-list path.

Unsupported/deforming part geometry, invalid identity, missing scene depth or cache
exhaustion leaves the original geometry intact. `vr_reload_interaction_status` reports geometry
preparation/rejection reasons and guide pairs, eye draws, pose misses and GPU failures.

Validation covers feed-state selection, default/save/reset/language behavior,
and WARP pixel checks for original-detail preservation, bounded emission,
overlap without accumulation, opaque alpha, equal-depth coverage, nearer-object
occlusion, transform composition and context restoration. Pulse tests cover its
period, continuity, long uptime and invalid time inputs.
Client compilation does not establish in-game part coverage or HMD acceptance. Test those on
loaded pistols, follower-lock rifles, open-bolt weapons and a manual rifle, then
finish chambering, toggle the option, switch hands/weapons and load a checkpoint.
