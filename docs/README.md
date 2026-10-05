# H2-MOD VR documentation

This index groups the engineering guides by the task they support. Start with
the project guide or architecture overview before changing a shared subsystem.
Each implementation topic distinguishes source behavior, offline checks, and
headset acceptance where those boundaries matter.

## Project guides

| Task | Guide |
| --- | --- |
| Install a client overlay | [Client installation](client-installation.md) |
| Generate projects, build, and select focused validation | [Build and validation](development.md) |
| Prepare a public release | [Release preparation](releasing.md) |
| Review source and asset redistribution boundaries | [Source and asset provenance](source-provenance.md) |
| Add or revise localized game text | [In-game text and VR action prompts](game-text-i18n.md) |
| Change launcher/game language selection or locale coverage | [Launcher and installed game languages](launcher-localization.md) |
| Understand PMove script integration | [PMove scripting](pmove-scripting.md) |

## Architecture and shared contracts

| Area | Guides |
| --- | --- |
| Native version bindings | [Build catalog, adaptation workflow and H2/H1 reuse](native-bindings.md) |
| Gameplay | [Gameplay and interaction architecture](vr-gameplay-interaction-architecture.md), [controller interaction](vr-controller-interaction.md), [world interaction](vr-world-interaction.md) |
| Weapons | [Weapon interaction and mechanical state](vr-weapon-interaction-architecture.md), [weapon registration](vr-weapon-registration.md), [variants and attachments](vr-weapon-variants.md), [weapon asset workflow](vr-weapon-asset-workflow.md), [weapon mechanics inventory](vr-weapon-mechanics.csv) |
| Camera and tracking | [Camera policy](vr-camera-architecture.md), [runtime and rendering contracts](vr-runtime-rendering.md), [movement camera bob](vr-camera-bob.md), [headset resume heading](vr-hmd-resume-heading.md), [pose stabilization](vr-stabilization.md) |
| Scene resources | [Scene surface storage](scene-surface-storage.md), [static surface lists](scene-static-surfaces.md), [model lighting cache](scene-model-lighting.md), [shared tessellation and head pitch](vr-shared-tessellation-and-head-pitch.md), [transparent particle shadows](vr-transparent-shadow-stereo.md) |

## Player, hands, and interaction

| Topic | Guides |
| --- | --- |
| Hands and body | [Hand interaction redesign](vr-hand-interaction-redesign.md), [hand interaction runtime](vr-hand-interaction-runtime.md), [scripted body arm control](vr-scripted-arms.md), [empty-hand poses](vr-empty-hands.md), [marine sniper hands](vr-marine-sniper-hands.md), [forearm twist](vr-arm-twist-investigation.md) |
| Comfort and controls | [Interaction comfort](vr-interaction-comfort.md), [posture controls](vr-stance-controls.md), [trigger discipline](vr-trigger-discipline.md), [presentation options](vr-presentation-options.md) |
| Carry and use | [Weapon carry and holsters](vr-weapon-carry.md), [pickup and switch presentation](vr-equip-presentation.md), [empty-hand world interaction](vr-world-interaction.md), [body-mounted equipment](vr-body-equipment.md), [ladders](vr-ladders.md) |
| Close combat | [Physical melee](vr-melee.md), [enemy combat](vr-enemy-combat.md), [special inventory knives](vr-special-knives.md), [riot shield](vr-riot-shield.md), [player death](vr-player-death.md) |
| Vehicles and mounted weapons | [Driver weapons and vehicle interaction](vr-vehicles.md), [mounted turrets](vr-mounted-turret.md), [Wolverines sentry](vr-sentry.md) |
| Mission actions | [Scripted control](vr-scripted-control.md), [scripted sequences](vr-scripted-sequences.md), [scripted free look](vr-scripted-free-look.md), [shared breach control](vr-breach-control.md), [special equipment](vr-special-equipment.md), [physical grenades](vr-grenades.md), [signal flare](vr-signal-flare.md), [official campaign cheats](vr-official-cheats.md), [cheat throwables](vr-cheat-throwables.md) |

## Shared weapon mechanics

| Mechanic | Guides |
| --- | --- |
| Ammunition and firing | [Ammunition presentation](vr-ammunition-presentation.md), [ammunition disposition](vr-ammunition-disposition.md), [weapon recoil](vr-weapon-recoil.md), [independent weapon fire](vr-independent-weapon-fire.md), [weapon instances](vr-weapon-instances-todo.md), [aim assistance](vr-aim-assist.md) |
| Reload interactions | [Quick reload](vr-quick-reload.md), [reload diagnostics](vr-reload-diagnostics.md), [falling reload items](vr-falling-reload-items.md), [magazine grasp comfort](vr-magazine-grip-comfort.md), [magazine latch contact](vr-magazine-latch-contact.md), [button magazine catch](vr-button-magazine-catch.md), [foregrip reload](vr-foregrip-reload.md), [manual handle and catch](vr-manual-handle-catch.md), [receiver bolt release](vr-receiver-bolt-release.md), [folding handles](vr-folding-handles.md) |
| Actions and presentation | [Bolt presentation](vr-bolt-presentation.md), [chambering guide](vr-chambering-guide.md), [weapon interaction refinement](vr-weapon-interaction-refinement.md), [impact release](vr-impact-release.md), [weapon interaction sounds](vr-weapon-sound-cues.md), [support-grip visibility](vr-support-render-audit.md) |
| Composite feeds | [Underbarrel native contracts](vr-underbarrel-native-contracts.md), [underbarrel VR interaction](vr-underbarrel-runtime.md), [underbarrel support](vr-underbarrel-support.md), [break actions](vr-break-action.md), [pump shotguns](vr-pump-shotguns.md), [belt-fed weapons](vr-belt-fed-weapons.md) |

## Weapon integrations

| Family | Guides |
| --- | --- |
| Assault rifles | [SCAR-H](vr-scar.md), [TAR-21 and FN2000](vr-bullpup-rifles.md), [SCAR shotgun grip](vr-scar-shotgun-grip.md) |
| Pistols and revolvers | [M9](vr-m9-reload.md), [pistol adapters](vr-pistol-adapters.md), [revolver interaction](vr-revolver-interaction.md) |
| Shotguns | [AA-12](vr-aa12.md), [M1014](vr-m1014.md), [Model 1887](vr-model1887.md), [SPAS-12 and W1200](vr-pump-shotguns.md) |
| Precision rifles | [M200](vr-m200.md), [Dragunov](vr-dragunov.md), [M14 EBR, M82A1, and WA2000](vr-precision-rifles.md), [fixed thermal M82](vr-fixed-thermal-sniper.md), [fixed-sniper aim assistance](vr-fixed-sniper-aim-assist-research.md) |
| Other firearms | [P90 and Striker](vr-p90-striker.md), [RPD](vr-rpd-binding-plan.md), [M240 and MG4](vr-belt-fed-weapons.md), [marine sniper binding](vr-marine-sniper-hands.md) |
| Launchers and sensors | [Launcher bindings](vr-launchers.md), [Javelin display](vr-javelin-screen.md), [heartbeat sensor](vr-heartbeat-sensor.md), [native ADS](vr-native-ads.md), [optic-rendering feasibility](vr-optic-rendering-feasibility.md) |

## Campaign-specific behavior

| Mission or sequence | Guides |
| --- | --- |
| Cliffhanger | [Story props](vr-cliffhanger.md), [physical climbing](vr-cliffhanger-climbing.md) |
| Estate | [Scripted scenes and native hints](vr-estate-scripted-scenes.md), [weapon variant audit](vr-estate-weapon-audit.md) |
| Oilrig | [Opening, equipment, and evacuation](vr-oilrig-sequence.md), [weapon variant audit](vr-oilrig-weapon-audit.md) |
| Gulag | [Gulag weapon binding](vr-gulag-weapons.md) |
| Other sequences | [S.S.D.D. / The Pit trainer](vr-trainer.md), [Whisky Hotel signal flare](vr-signal-flare.md), [ladders](vr-ladders.md) |

## Rendering, menus, and UI

| Area | Guides |
| --- | --- |
| Weapon UI | [Weapon HUD](vr-weapon-hud.md), [native narrative UI](vr-narrative-ui.md) |
| Menus and displays | [Native menus](vr-native-menus.md), [launcher VR settings](vr-launcher-settings.md), [independent Javelin display](vr-javelin-screen.md), [native ADS](vr-native-ads.md), [optic rendering feasibility](vr-optic-rendering-feasibility.md) |
| World effects | [Nightvision](vr-nightvision.md), [damage-screen integration](vr-damage-screen.md), [native flare sampling](vr-lens-flare-rendering.md), [thermal world isolation](vr-thermal-world-isolation-research.md) |

## Diagnostics and source audits

| Purpose | Guides |
| --- | --- |
| Capture and acceptance | [Diagnostics workflow](vr-diagnostics-workflow.md), [region performance capture](vr-region-capture.md), [render performance review](vr-render-performance-review.md), [equipment visibility capture](vr-equipment-visibility-capture.md), [interaction diagnostics](vr-interaction-diagnostics.md) |
| Weapon faults | [HK slap diagnostics](vr-hk-slap-diagnostics.md), [held-weapon ejection audit](vr-ejection-reference-audit.md) ([structured data](vr-ejection-reference-audit.json)) |
| Native and asset audits | [Optics feasibility](vr-optic-rendering-feasibility.md), [Estate weapon variants](vr-estate-weapon-audit.md), [Oilrig weapon variants](vr-oilrig-weapon-audit.md) |

## Documentation conventions

Write reusable contracts and procedures in English with UTF-8 encoding and
repository-relative links. Keep examples focused on source behavior and
developer tasks; omit dates, local machine paths, process identifiers, private
conversation references, and chronological implementation diaries. Keep raw
asset exports and temporary capture output out of the documentation tree.

Distinguish proposed behavior, implemented source, offline verification, and
headset acceptance. Update the relevant topic when an interface or policy
changes, and add new guides to this index.
