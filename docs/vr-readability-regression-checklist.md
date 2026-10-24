# Runtime regression checklist for readability changes

Scope: readability cleanup of carry handling, interaction dispatch and hand presentation, plus migration to named fields in weapon assembly, feeding, interaction and sound configuration. All runtime results remain pending until confirmed by a tester.

Completed offline checks: 10,743 checks across carry and lever-grip cases with zero failures; 177 configuration/assembly snapshots match their pre-migration values; existing physical-reload regressions have zero failures. These results do not replace the runtime checks below.

## Test preparation

- Use the regular build for primary acceptance; also check startup, loading and shutdown once with the Debug build.
- Use a familiar checkpoint in ordinary combat. Keep your usual settings; temporarily disable infinite ammunition and quick reload when checking ammunition behavior so these assists do not mask the results.
- Record the client version, test map/checkpoint and any special weapon variants. Run general checks with each hand; preserve the intended operating side for weapons with fixed controls.

## Pass 1: Shared carry handling and state recovery

| Result | Test | Actions and checks |
| --- | --- | --- |
| Pending | Startup and cold load | Launch and load a save. Check both eye views and the visibility and position of both hands, held weapons and items stored at the waist or back; exit normally. Launch both the regular and Debug builds once. |
| Pending | Pickup with empty or occupied hands | Pick up, stow and retrieve items with empty hands and with a gun already in the other hand; swap left and right hands. Check for duplicate guns, incorrect firing-hand ownership and lost items. |
| Pending | Support-hand and holster priority | Hold a long gun, then grab its support-hand point; repeat near a holster region. One Grip press must not acquire both support and another gun. Release and reattach the support hand; verify that pose and firing return to normal. |
| Pending | Swap, drop and regrab | Swap weapons at an occupied storage slot, then drop and retrieve them; verify that ammunition behavior is preserved. One release commits only one action, and a failed action must not become an unintended world-use interaction. |
| Pending | Pause, focus and checkpoint loading | Pause/resume, switch focus away/back and load a checkpoint with Grip both held and released. After recovery, check for unintended firing or dropped guns, and verify normal hand poses and storage state. |

## Pass 2: Representative weapons by feeding family

Complete at least one normal reload and firing cycle per group. For weapons whose chamber retention or empty-lock behavior differs, also perform both a partial reload and a reload from empty.

| Result | Weapons | Focus |
| --- | --- | --- |
| Pending | M9 or USP | Button magazine release, insertion, slide cycling, empty lock and release; chamber retention, ammunition counts and reload sounds match previous behavior. |
| Pending | M4 and M16 | Test both to cover the shared configuration factory; check charging-handle operation, empty lock and receiver release-tab contact. |
| Pending | AK47; MP5 or UMP | Physical magazine removal, insertion and latch interaction. For MP5/UMP, also test raising, holding and slapping the charging handle; check for unintended interaction with another component. |
| Pending | M200/CheyTac | Unlock, pull back, push forward and lock the bolt; verify ejection, feeding and two-handed grip, and confirm that firing resumes after reloading. |
| Pending | M240 or MG4; RPD | Use M240/MG4 to cover belt-fed reloading; also test the RPD sight-bridge/top-cover interlock. Open, insert the ammunition supply, lay the belt, close the cover, charge and fire; verify grip and sound. |
| Pending | M1014 and SPAS-12 | Cover semiautomatic tube feeding and pump-action feeding respectively; check individual shell loading, recovery from empty and pump operation. |
| Pending | M1887 | Cycle the lever and spin the gun; with only the support hand holding it, regrab the original fixed grip and the moving control point separately. Check for discontinuities in pose, restored firing control and reloading. |
| Pending | Striker | Load individual rounds into the fixed drum and fire; verify that it is not treated as a detachable magazine or an ordinary tube-fed weapon. |
| Pending | Magnum .44 | Open the cylinder, eject cases, load with a speedloader, close and fire; verify hand poses, ammunition and sound. |
| Pending | Ranger and M79 | Cover double-chamber and single-chamber break-action feeding respectively; check opening/closing, ejection, insertion and firing. |
| Pending | RPG; Stinger/AT4 | Complete one physical reload and shot with the RPG; check sights, grip, native firing and empty-tube behavior for Stinger/AT4. Follow each weapon's existing guidance and reload rules. |
| Pending | One attachment/skin variant | Choose M4/M203, a suppressed variant or an Arctic variant; check attachments, textures, moving parts and ammunition display. For M4/M203, also switch modes and reload once. |

## Pass 3: Shared hand behavior and mission control handoffs

| Result | Scenario | Checks |
| --- | --- | --- |
| Pending | Night vision, grenades and knife | Use and stow each item, then verify normal weapon handling/reloading, finger poses, hand occupancy and equipment visibility. |
| Pending | Exodus laser designator | Draw, use and stow it with empty hands and with a gun already held; verify the existing mission handoff and restoration of weapon handling. |
| Pending | One independent hand scenario | Choose a Cliffhanger climbing/ice-axe checkpoint or an ending interaction checkpoint; check hands, scripted takeover and return of control. |
| Pending | One vehicle/mounted-weapon handoff | Enter, use and leave; check hand presentation and subsequent weapon handling with Grip both held and released. |

## Issue reporting

When an issue occurs, record the regular/Debug build, map/checkpoint, full weapon variant, items held in each hand, whether Grip/Trigger is held, reproduction steps and whether the fault affects visuals, sound or ammunition.

Query the existing console status commands for the relevant family manually, and retain the output when needed:

```text
vr_carry_status
vr_reload_interaction_status
vr_tube_status
vr_cylinder_status
vr_break_action_status
```

After runtime verification, mark each corresponding row as Pass, Fail or Not covered. Keep untested scenarios explicitly outside the acceptance boundary; these checks do not establish acceptance for the entire campaign or all hardware.
