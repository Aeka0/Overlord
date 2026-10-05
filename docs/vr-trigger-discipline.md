# Trigger discipline

Quest/Touch capacitive trigger contact drives the rear holding hand's index
finger on successfully bound firearm assemblies. Contact preserves the authored
trigger pose; removing the finger opens it nearly level and towards the receiver,
with natural distal flexion. Actual trigger presses retain the existing firing behavior.

## Input contract

The native gameplay input path is SteamVR/OpenVR. `oculus_touch.json` maps each
trigger's native capacitive component to an optional boolean action. A separate
`button` source routes its actual `touch` output directly to the contact action.
The existing firing source stays in `trigger` mode. A live comparison on
 confirmed that this direct output is true with zero trigger travel,
while the earlier `force_input: touch` plus logical `click` mapping stays false.
That forced mapping must not be restored. `steamvr_input` reads both actions at the existing
runtime publication boundary. Presentation consumes the immutable input frame;
it never polls the headset or modifies button histories.

SteamVR distinguishes an unbound/inactive action from a false contact state
through `InputDigitalActionData_t::bActive` (see the
[Valve input contract](https://github.com/ValveSoftware/openvr/wiki/SteamVR-Input)).
Absent contact support, missing bindings, lost focus or invalid tracking leaves
the authored pose intact. A pressed trigger takes precedence over a contradictory
untouched sample. Vive and Index defaults are unchanged; no click-derived touch
substitute is installed for them.

Custom SteamVR bindings may need the new left/right trigger contact actions
assigned to their trigger `touch` output. The executable and `vr_input` directory
must be updated together; the existing build/package copy includes both.

An acceptance failure exposed a deployment omission: the installed
EXEs contained the contact actions, but the installed manifest and Touch bindings
still predated them. The binary-only pair deployer had left `vr_input` behind.
`deploy_client_pair.py` now stages, backs up and rolls back the matching input
package with both binary pairs; missing or mismatched packages reject deployment.

## Presentation boundary

`gameplay/trigger_discipline.hpp` owns the bounded blend and index articulation.
`hand_component` applies it after weapon/mechanical hand poses and before the
solved arm is captured for weapon presentation. Admission requires a selected,
bound firearm profile, an actual rear grip, and no competing mechanical pose for
that hand. The Exodus laser designator also participates, reusing the USP index
fit with its existing translated native wrist/finger pose. Shield, unbound assemblies, empty hands and
support-only holds are excluded. Each independent weapon solver uses its own
actual rear hand, including left-handed and dual-wielded weapons.

Only `j_index_{le,ri}_{0,1,2}` is adjusted. Segment axes and offsets come from the
current glove's bind geometry. `trigger_discipline_profiles.hpp` contains
independent left/right directions and retained curl for 46 registered firearm
families. `bind_poses` resolves the family once into the immutable pose library;
rendering does not scan the catalog or perform mesh fitting. Missing tuning keeps
the authored pose and is covered by the registry regression. Distal joints only
open towards their incoming segment, never past it into reverse flexion. This preserves
the wrist, all other fingers, the other hand, gun bones and native bone lengths.
The values come from offline receiver-mesh and skinned-hand inspection. They
are not a runtime collision solver or a claim of verified trigger-guard clearance
on every skin or intermediate pose.

Striker and Striker woodland share their independently fitted drum-side poses.
The former fully hooked pose stayed behind the drum plane but could cross into
the receiver; the new fit follows the exterior side instead. Left/right
parameters are separate because the receiver geometry is not assumed symmetric.

Leaving the trigger has a 35 ms half-life; returning on contact uses 12 ms
(about 40 ms to reach 90 percent). Reversals
continue from current progress. Repeated eye/model draws do not advance time;
tracking generations and long gaps rebase it. Weapon identity, binding and rear
ownership changes restart from the new authored pose. Runtime state is fixed-size
and rendering performs no allocation or new synchronization.

## Verification

`trigger_discipline_tests.hpp` runs in `vr-weapon-grip-tests`. It checks contact
versus click, left/right input isolation, 60/72/90/120/144 Hz transitions,
duplicate draws, tracking interruption, recentering, instance changes and scope
exclusions. Geometry tests use the captured marine sniper glove against every
eligible registered assembly, both rear hands and rotated guns; they check
near-level proximal extension, forward-only distal flexion, native lengths and
bit-identical unrelated bones. `test_trigger_touch_binding.py` protects the
explicit native touch selector and independent firing source.

`vr_hands_status` prints left/right `trigger_touch_active`, `trigger_touch_down`,
`trigger_click`, the input sequence, and each weapon solver's last
`safe_index_blend`/`safe_index_applied`. A persistent false touch with a true
physical contact must be investigated at the binding/driver boundary; reducing
animation half-life cannot repair a missing sensor report.

Headset acceptance must still check real capacitive reports through the controller's
Quest transport and inspect trigger-guard/receiver clearance on pistols, rifles,
shotguns and launchers, including left-hand transfers and two independent guns.
CPU geometry tests and a client build do not establish visual headset acceptance.
