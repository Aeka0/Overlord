# SPAS-12 and W1200 physical pump feeds

The [Oilrig variant audit](vr-oilrig-weapon-audit.md) documents
`spas12_arctic` and `spas12_arctic_reflex` with their separate
`h2_viewmodel_spas12_base_arctic` recipe. All 17 bone names, parents and bind
transforms match the base receiver exactly, so authored pump/shell/hand poses
are reused. Exact native names, seven-round capacity and attachment topology
remain required.

SPAS-12 (`spas12`, `spas12_reflex`) and W1200 (`winchester1200`) extend the individual-shell tube family with a manual pump policy. Their captured MW2CR definitions have seven-round capacity, one-round segmented loading, eight pellets and the bolt-action flag. The native loaded count projects the tube plus a separate live chamber/port round; a spent case is never counted as ammunition.

## Controls and state

- Hold the fore-end with the side grip and move that hand along the weapon to operate the pump. A free hand may also pinch the fore-end. The weapon keeps its existing rear-hand and support-hand ownership.
- A live chamber locks the pump. Hold the magazine-release button (B/Y or its controller equivalent) on either holding hand to unlock it. Keep the button held to cycle repeatedly without releasing the button or regripping the fore-end; both full cycles and partial returns preserve the unlock. Releasing that button before starting the stroke cancels the unlock. Once a stroke starts, finish the cycle; closing relocks it if the button has been released.
- Firing consumes the chamber only, retains its empty case and unlocks the pump. Full rear travel ejects the case once. Returning to the front stop chambers one tube round. Native automatic rechambering is bypassed through the existing independent-fire boundary.
- A fully open port accepts one live shell. The pump remains open and cannot fire. Pushing it forward seats that shell without consuming another tube round. An occupied or partly open port rejects another insertion.
- Use the existing waist pinch to draw one shell and insert it through the lower loading port to replenish the tube. Loading an empty tube does not chamber a round.
- Letting go of the pump preserves its travel. Partial strokes, tracking loss and dropping/reacquiring the same recorded weapon cannot complete a cycle automatically. Forced cleanup refunds a held shell through the shared ammunition disposition policy.

Both control hands are supported. Cases are emitted through the native oriented weapon FX at full extraction; unspent ejected shells use the existing rigid-shell presentation. Gun, pump, bolt, carrier and shell remain separate bones. Pump movement also updates the support anchor used by pose solving and carry contact tests.

## Source data

| Adapter | Receiver | Pump / bolt / carrier | Pump stroke |
| --- | --- | --- | --- |
| SPAS-12 | `h2_viewmodel_spas12_base` (17 bones) | `j_pump` / `j_reload` / `j_reload_plate` | 7.340 cm |
| W1200 | `h2_viewmodel_winchester1200_base` (16 bones) | `j_pump` / `j_slide` / `j_load` | 7.659 cm |

Rest grips come from each weapon's native idle frame 0. Open bolt and pump endpoints come from the empty-load animation, where the actual port is open; W1200's ordinary rechamber animation does not move its bolt. Port and lower-entry contacts follow the original shell approach, adjusted to the receiver opening surfaces.

Loading grips use SPAS-12 reload-loop frame 6 and W1200 reload-loop frame 4. Both source animations load with the **right** hand. Complete palm/finger chains are retargeted to canonical left around the rigid shell's local plane; the runtime mirrors for the actual operating hand. Source hashes are recorded next to the authored poses. Offline skinned-shell contact distances are approximately 1.37 mm and 1.35 mm. These are geometry checks, not headset acceptance.

## Validation

`pump_reload_tests.hpp` exercises both weapons and both rear hands: live lock/release, shot/case accounting, partial strokes, port/bottom loading, occupied-port rejection, held release after a cycle, stale tracking, cleanup and rejected native comparisons. `pump_profile_tests.hpp` binds the captured model hierarchies and checks moving contacts for both hands. Existing M1014 and detachable-magazine tests remain in their respective suites.

Headset checks remain necessary for pump travel feel, shell orientation, receiver attachments and ejection appearance. `vr_tube_status` reports tube/chamber/reserve, action phase, travel and the latest controller decision.

## AK right-hand contact correction

The AK's two canonical-left edge grasps previously reflected their palm through the same outer charging-tab contact, placing the right hand inside the receiver. Each mirrored grasp now has a source-mesh-fitted wrist displacement while preserving its rotation. The wrist-local contact is recomputed to keep acquisition and display on the real right tab. Left-hand grasps and bilateral-handle policies are unchanged.
