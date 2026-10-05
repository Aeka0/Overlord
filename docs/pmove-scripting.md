# PMove scripting

H2-Mod exposes an atomic, declarative PMove profile to raw GSC scripts. Scripts
submit complete profiles during initialization; predicted and authoritative
PMove execute copied native data without calling the script VM each frame.

The current implementation targets H2's local campaign player. Passing another
entity is rejected instead of silently applying a process-wide profile.

## API

`setpmoveprofile(player, profile)` validates and atomically replaces the active
profile. `clearpmoveprofile(player)` restores pass-through behavior. A profile
contains an optional `input_projection` array and a required numeric `actions`
array.

The input projection accepts `activation_button`, `latched`,
`project_to_forward`, `state_flag`, and `required_stance`. Each action accepts:

- Input: `trigger_button` and `consume_trigger`.
- Conditions: `required_state`, `required_state_flag`, `state_grace_ms`,
  `require_moving`, `min_air_time_ms`, `max_forward_input`, and
  `min_ground_distance`.
- Coordination: `exclusive_group` and `clear_state_flag`.
- Motion: `kind`, `horizontal_speed`, `horizontal_speed_scale`, `exit_speed`,
  `vertical_mode`, `vertical_speed`, `vertical_acceleration`,
  `horizontal_half_life_ms`, and `origin_offset_z`.
- Posture: `native_stance`, `target_stance`, and `landing_stance`. A continuous
  action can use `native_stance` during the original movement calculation while
  applying `target_stance` to its resulting player state.
- Timing and resources: `duration_ms`, `cooldown_ms`, `max_charges`,
  `resource_reset`, and `charge_pool`.

Supported buttons are `attack`, `sprint`, `melee`, `reload`, `use_reload`,
`prone`, `crouch`, `stance`, `jump`, and `ads`. Movement states are `any`,
`ground`, and `air`. Action kinds are `impulse`, `velocity_curve`, and
`acceleration`. State flags currently support `none` and `sprinting`.

Actions in the same non-empty `exclusive_group` replace one another. Actions
using the same `charge_pool` share charges and must use matching cooldown,
maximum charge count, and reset policy. `max_charges = 0` allows unlimited
activations. A zero cooldown means no timed reset; `resource_reset = "landing"`
restores the pool on a forward-timeline landing.

Invalid types, unknown fields, unsupported names, non-finite values, and
out-of-range numbers raise a script error before the active profile changes.
