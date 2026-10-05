# Native contracts for underbarrel weapons

Underbarrel launchers and shotguns are native secondary modules attached to a
host weapon. The host owns the primary weapon instance; the selected module owns
its own firing mode, ammunition feed, projectile, and reload state. VR adapters
must preserve this split rather than treating the module as another carried
firearm.

## Identity and ammunition

| Host family | Secondary module | Native feed |
| --- | --- | --- |
| M16 / M4 / SCAR | M203 variants | Single-shot, one round |
| AK-47 | GP-25 grenade launcher | Single-shot, one round |
| SCAR-H / FAL / AK-47 | Underbarrel shotgun | Four-shell feed; eight-pellet shots |

M4 optic variants may use different assembly names while sharing module
semantics. The airport M4 and legacy `m203` definition are separate profiles;
similar names or loaded assets do not make them interchangeable. Native token
IDs and ammo-key values vary by map and executable and must be read through the
validated weapon adapter.

The primary and secondary modules have distinct clip keys. Some variants share
reserve keys; other shotguns have their own reserves. Preserve the complete
native key representation, including its flags. Compare key identity rather
than padding or the weapon token alone. Ammunition is committed against the
resolved module and the owning host instance so primary and secondary budgets
cannot consume one another.

## Mode resolution

The primary weapon can remain selected in player state while the secondary mode
is active. Mode selection and module identity are separate state. Resolve a
module from the admitted host descriptor and native alternate-mode selector
exactly once. Do not recursively follow `altWeapon` or secondary links: SCAR/FAL
modules may point back to their host, while other modules have no reverse link.

When an API accepts a host plus an alternate-mode flag, pass that pair. When it
accepts an already resolved secondary definition, do not apply alternate
resolution again. Native attachments can affect alternate capability, so a
weapon-name link alone does not authorize a module.

Leaving secondary mode, switching the host, or losing native weapon permission
ends the module's control lease. An idle animation name or visible module does
not prove that firing is currently authorized.

## Firing and feed behavior

M203 and GP-25 modules use the native single-shot projectile definition and
native fire path. Preserve native ownership, projectile speed, collision,
fuse, shot cadence, and any launcher-specific lock. A shotgun shot follows its
native pellet count and consumes one shell. Native fire acceptance is the only
ammunition commit boundary.

Underbarrel shotguns have distinct start, per-shell loop, end, and rechamber
stages. A shell is transferred once at the corresponding native feed boundary;
the pump stage does not spend another round. Empty and nonempty reloads can use
different native stages, and an end animation may continue after the feed is
full. Do not infer feed state from an animation name or turn native timing fields
into gesture deadlines.

Visual models provide authored buttons, sliders, grenades, shells, sights, and
pumps. Their names and animation notetracks identify presentation resources,
not hand anchors or gameplay transactions. Validate each module's actual parent
chain, muzzle, loading port, and part geometry in its admitted host assembly.

## VR integration boundaries

- Register each host/module assembly explicitly with its primary and secondary
  capacities, ammo identity, muzzle, and feed type.
- Give each Trigger edge at most one side effect. A single press cannot fire
  both the host weapon and its module.
- Keep the primary and module ammunition ledgers independent. Reject stale,
  mismatched, unsupported, or capacity-inconsistent observations before a
  native write.
- Preserve module-specific reload mechanics. A support grip alone does not
  authorize a secondary feed or transfer firing ownership from the control hand.
- Keep mode selection, module firing, belt pickup, and physical reload as
  separate transactions with explicit native permission checks.
- Run VM access and native commits on their validated game-thread boundary;
  publish bounded value snapshots to input and rendering consumers.

The [underbarrel runtime guide](vr-underbarrel-runtime.md) documents the VR
adapter, hand interactions, and remaining headset acceptance. Static asset
admission, offline state tests, and native signature checks do not establish
end-to-end firing or reload acceptance in the headset.
