# H2 SP native entry points

This directory owns the current project's MW2CR/H2 engine ABI integration. Keep
it separate from the inherited H2-Mod `game` symbol table and from portable VR
policies. Reuse existing correctly typed `game::` symbols instead of declaring
another address for the same native function.

`entrypoints.hpp` declares one fixed signature and address for each migrated
shared query. `binding.hpp` calls that signature directly; it does not deduce a
new native ABI from a consumer's argument list or let the consumer select a
different return type.

| Entry | Meaning and retained boundary |
| --- | --- |
| `server_entity_dobj` | Server entity to opaque DObj identity. Native carry/use/ending adapters retain their signature, entity-generation and layout checks. |
| `client_entity_dobj` | Client entity handle plus local-client index to opaque DObj identity. Camera and arm adapters retain readiness, instance and before/after identity checks. |
| `angles_to_quaternion` | Native angle buffer to the caller's quaternion buffer; no extra normalization or coordinate conversion. |
| `clip_capacity` | Live player-state/token/alternate capacity query. Do not substitute the static WeaponDef capacity. |
| `weapon_type` | Native integer type query; existing bullet/projectile/mission admission remains in each adapter. |
| `weapon_inventory_type` | Native inventory classification; the physical-copy adapter still owns its primary-only gate. |
| `weapon_dual_wield_flag` | Existing integer/zero dual-wield query. It does not admit native paired weapons as independent physical instances. |
| `weapon_selection_request` | Live request that FinishMove writes into `usercmd.weapon`. It is not confirmation of the actual equipped player-state weapon. |

The SP addresses retain the existing supported image/base assumptions. Function
declaration does not validate executable bytes, initialize the game, establish
thread ownership or prove asset lifetime. Keep all existing per-feature checks
and hook-site contracts at their original admission/install boundaries. Do not
add per-frame scans or a permissive fallback to another build.

A DObj is deliberately opaque. A consumer may inspect its existing reviewed
layout only at its guarded/locked view boundary; declaring a pointer type does
not authorize skeletal reads or cache a scene object's lifetime. Preserve the
bounded reader when accessing a global through `get()`; use `read()` only where
the old caller already performed an admitted direct read.

For further migration, verify the function signature and native semantics before
adding an entry. Call-site instruction addresses, continuation points and feature
predicates belong to the owning hook adapter, not to a generic entry-point list.
Keep native game writes in their current owner; this layer introduces no second
gameplay authority, source-provenance claim or license change.
