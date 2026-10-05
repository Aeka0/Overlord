#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"
#include "game/scripting/array.hpp"
#include "game/scripting/entity.hpp"
#include "game/scripting/script_value.hpp"

#include "gsc/script_extension.hpp"

#include <utils/hook.hpp>

#include <shared_mutex>

namespace movement
{
	namespace
	{
		enum class move_state
		{
			any,
			ground,
			air,
		};

		enum class action_kind
		{
			impulse,
			velocity_curve,
			acceleration,
		};

		enum class stance
		{
			unchanged,
			stand,
			crouch,
			prone,
		};

		enum class vertical_mode
		{
			unchanged,
			set,
			add,
		};

		enum class state_flag
		{
			none,
			sprinting,
		};

		enum class resource_reset
		{
			none,
			landing,
		};

		struct input_projection_policy
		{
			bool configured{};
			int activation_button{};
			bool latched{};
			bool project_to_forward{};
			state_flag active_flag{state_flag::none};
			stance required_stance{stance::unchanged};
		};

		struct action_definition
		{
			int id{};
			int trigger_button{};
			move_state required_state{move_state::any};
			action_kind kind{action_kind::impulse};
			stance native_stance{stance::unchanged};
			stance target_stance{stance::unchanged};
			stance landing_stance{stance::unchanged};
			vertical_mode z_mode{vertical_mode::unchanged};

			state_flag required_flag{state_flag::none};
			state_flag cleared_flag{state_flag::none};
			bool require_moving{true};
			resource_reset reset{resource_reset::none};
			bool consume_trigger{};
			std::string charge_pool;
			std::string exclusive_group;
			float max_forward_input{1.0f};
			float min_ground_distance{};

			float horizontal_speed{};
			float horizontal_speed_scale{1.0f};
			float exit_speed{};
			float vertical_speed{};
			float vertical_acceleration{};
			float horizontal_half_life_ms{};
			float origin_offset_z{};
			int duration_ms{};
			int min_air_time_ms{};
			int state_grace_ms{};
			int cooldown_ms{};
			int max_charges{};
		};

		struct player_configuration
		{
			input_projection_policy input_projection;
			std::vector<action_definition> actions;
			std::uint64_t generation{};
		};

		struct action_runtime
		{
			bool active{};
			int start_time{};
			int end_time{};
			int last_trigger_time{-1};
			float initial_speed{};
			float direction[2]{};
		};

		struct charge_runtime
		{
			int last_charge_time{-100000};
			int charges{};
		};

		struct player_runtime
		{
			bool input_requested{};
			bool stance_requested{};
			bool button_timeline_initialized{};
			bool air_time_initialized{};
			int button_server_time{-1};
			int previous_buttons{};
			int pressed_buttons{};
			int air_start_time{};
			int last_sprint_time{-100000};
			bool timeline_initialized{};
			bool latest_on_ground{};
			int max_server_time{-1};
			stance pending_landing_stance{stance::unchanged};
			std::uint64_t generation{};
			std::unordered_map<int, action_runtime> actions;
			std::unordered_map<std::string, charge_runtime> charge_pools;
		};

		struct pending_impulse
		{
			const action_definition* definition{};
			float direction[3]{};
		};

		struct runtime_slot
		{
			game::playerState_s* player_state{};
			std::uint64_t last_use{};
			player_runtime runtime;
		};

		utils::hook::detour walk_move_hook;
		utils::hook::detour air_move_hook;

		std::shared_mutex configuration_mutex;
		player_configuration configuration;
		std::mutex runtime_mutex;
		std::array<runtime_slot, 8> runtime_slots;
		std::uint64_t runtime_use_counter{};
		thread_local bool processing_move{};

		class move_scope final
		{
		public:
			move_scope()
			{
				processing_move = true;
			}

			~move_scope()
			{
				processing_move = false;
			}
		};

		scripting::script_value get_argument(const int index)
		{
			if (index >= static_cast<int>(game::scr_VmPub->outparamcount))
			{
				return {};
			}

			return game::scr_VmPub->top[-index];
		}

		bool is_local_player(const scripting::entity& entity)
		{
			const auto entref = entity.get_entity_reference();
			return entref.classnum == 0 && entref.entnum == game::LOCAL_PLAYER_ENTITY_NUM;
		}

		scripting::array get_definition_argument(const int index)
		{
			const auto value = get_argument(index);
			if (!value.is<scripting::array>())
			{
				throw std::runtime_error(std::format("parameter {} must be an array", index + 1));
			}

			return value.as<scripting::array>();
		}

		std::optional<scripting::array> get_array(const scripting::array& values,
			const std::string& key, const bool required = false)
		{
			const auto value = values.get(key);
			if (value.get_raw().type == game::SCRIPT_NONE)
			{
				if (required)
				{
					throw std::runtime_error(std::format("{} is required", key));
				}
				return {};
			}
			if (!value.is<scripting::array>())
			{
				throw std::runtime_error(std::format("{} must be an array", key));
			}

			return value.as<scripting::array>();
		}

		scripting::entity get_player_argument(const int index)
		{
			const auto value = get_argument(index);
			if (!value.is<scripting::entity>())
			{
				throw std::runtime_error(std::format("parameter {} must be a player", index + 1));
			}

			const auto entity = value.as<scripting::entity>();
			if (!is_local_player(entity))
			{
				throw std::runtime_error("PMove declarations currently support only the local campaign player");
			}

			return entity;
		}

		std::optional<scripting::script_value> get_value(const scripting::array& values, const std::string& key)
		{
			const auto value = values.get(key);
			if (value.get_raw().type == game::SCRIPT_NONE)
			{
				return {};
			}

			return value;
		}

		bool get_bool(const scripting::array& values, const std::string& key, const bool fallback)
		{
			const auto value = get_value(values, key);
			if (!value.has_value())
			{
				return fallback;
			}
			if (!value->is<int>())
			{
				throw std::runtime_error(std::format("{} must be an integer boolean", key));
			}

			const auto result = value->as<int>();
			if (result != 0 && result != 1)
			{
				throw std::runtime_error(std::format("{} must be 0 or 1", key));
			}

			return result == 1;
		}

		float get_number(const scripting::array& values, const std::string& key, const float fallback,
			const float minimum, const float maximum)
		{
			const auto value = get_value(values, key);
			if (!value.has_value())
			{
				return fallback;
			}

			float result{};
			if (value->is<float>())
			{
				result = value->as<float>();
			}
			else if (value->is<int>())
			{
				result = static_cast<float>(value->as<int>());
			}
			else
			{
				throw std::runtime_error(std::format("{} must be numeric", key));
			}

			if (!std::isfinite(result) || result < minimum || result > maximum)
			{
				throw std::runtime_error(std::format("{} must be between {} and {}", key, minimum, maximum));
			}

			return result;
		}

		int get_integer(const scripting::array& values, const std::string& key, const int fallback,
			const int minimum, const int maximum)
		{
			const auto value = get_value(values, key);
			if (!value.has_value())
			{
				return fallback;
			}
			if (!value->is<int>())
			{
				throw std::runtime_error(std::format("{} must be an integer", key));
			}

			const auto result = value->as<int>();
			if (result < minimum || result > maximum)
			{
				throw std::runtime_error(std::format("{} must be between {} and {}", key, minimum, maximum));
			}

			return result;
		}

		void validate_keys(const scripting::array& values,
			const std::unordered_set<std::string>& allowed, const std::string& definition_name)
		{
			for (const auto& key : values.get_keys())
			{
				if (!key.is<std::string>())
				{
					throw std::runtime_error(std::format("{} keys must be strings", definition_name));
				}

				const auto name = key.as<std::string>();
				if (!allowed.contains(name))
				{
					throw std::runtime_error(std::format("unsupported {} field '{}'", definition_name, name));
				}
			}
		}

		std::string get_string(const scripting::array& values, const std::string& key,
			const std::string& fallback, const bool required = false)
		{
			const auto value = get_value(values, key);
			if (!value.has_value())
			{
				if (required)
				{
					throw std::runtime_error(std::format("{} is required", key));
				}
				return fallback;
			}
			if (!value->is<std::string>())
			{
				throw std::runtime_error(std::format("{} must be a string", key));
			}

			return value->as<std::string>();
		}

		int parse_button(const std::string& value)
		{
			static const std::unordered_map<std::string, int> buttons{
				{"attack", game::BUTTON_ATTACK}, {"sprint", game::BUTTON_SPRINT}, {"melee", game::BUTTON_MELEE},
				{"reload", game::BUTTON_RELOAD}, {"use_reload", game::BUTTON_USE_RELOAD},
				{"prone", game::BUTTON_PRONE}, {"crouch", game::BUTTON_DUCK},
				{"stance", game::BUTTON_PRONE | game::BUTTON_DUCK},
				{"jump", game::BUTTON_JUMP}, {"ads", game::BUTTON_ADS},
			};

			const auto entry = buttons.find(value);
			if (entry == buttons.end())
			{
				throw std::runtime_error(std::format("unsupported trigger_button '{}'", value));
			}

			return entry->second;
		}

		move_state parse_move_state(const std::string& value)
		{
			if (value == "any")
			{
				return move_state::any;
			}
			if (value == "ground")
			{
				return move_state::ground;
			}
			if (value == "air")
			{
				return move_state::air;
			}
			throw std::runtime_error(std::format("unsupported required_state '{}'", value));
		}

		action_kind parse_action_kind(const std::string& value)
		{
			if (value == "impulse")
			{
				return action_kind::impulse;
			}
			if (value == "velocity_curve")
			{
				return action_kind::velocity_curve;
			}
			if (value == "acceleration")
			{
				return action_kind::acceleration;
			}
			throw std::runtime_error(std::format("unsupported action kind '{}'", value));
		}

		stance parse_stance(const std::string& value)
		{
			if (value == "unchanged")
			{
				return stance::unchanged;
			}
			if (value == "stand")
			{
				return stance::stand;
			}
			if (value == "crouch")
			{
				return stance::crouch;
			}
			if (value == "prone")
			{
				return stance::prone;
			}
			throw std::runtime_error(std::format("unsupported target_stance '{}'", value));
		}

		vertical_mode parse_vertical_mode(const std::string& value)
		{
			if (value == "unchanged")
			{
				return vertical_mode::unchanged;
			}
			if (value == "set")
			{
				return vertical_mode::set;
			}
			if (value == "add")
			{
				return vertical_mode::add;
			}
			throw std::runtime_error(std::format("unsupported vertical_mode '{}'", value));
		}

		state_flag parse_state_flag(const std::string& value)
		{
			if (value == "none")
			{
				return state_flag::none;
			}
			if (value == "sprinting")
			{
				return state_flag::sprinting;
			}
			throw std::runtime_error(std::format("unsupported state flag '{}'", value));
		}

		resource_reset parse_resource_reset(const std::string& value)
		{
			if (value == "none")
			{
				return resource_reset::none;
			}
			if (value == "landing")
			{
				return resource_reset::landing;
			}
			throw std::runtime_error(std::format("unsupported resource_reset '{}'", value));
		}

		action_definition parse_action(const scripting::array& values, const int id)
		{
			static const std::unordered_set<std::string> fields{
				"trigger_button", "required_state", "kind", "native_stance", "target_stance", "landing_stance",
				"vertical_mode",
				"required_state_flag", "state_grace_ms", "clear_state_flag", "require_moving",
				"resource_reset", "consume_trigger", "charge_pool", "exclusive_group",
				"max_forward_input", "min_ground_distance", "horizontal_speed",
				"horizontal_speed_scale", "exit_speed", "vertical_speed", "vertical_acceleration",
				"horizontal_half_life_ms", "origin_offset_z", "duration_ms", "min_air_time_ms",
				"cooldown_ms", "max_charges",
			};
			validate_keys(values, fields, "action");

			action_definition result{};
			result.id = id;
			result.trigger_button = parse_button(get_string(values, "trigger_button", {}, true));
			result.required_state = parse_move_state(get_string(values, "required_state", "any"));
			result.kind = parse_action_kind(get_string(values, "kind", "impulse"));
			result.native_stance = parse_stance(get_string(values, "native_stance", "unchanged"));
			result.target_stance = parse_stance(get_string(values, "target_stance", "unchanged"));
			result.landing_stance = parse_stance(get_string(values, "landing_stance", "unchanged"));
			result.z_mode = parse_vertical_mode(get_string(values, "vertical_mode", "unchanged"));

			result.required_flag = parse_state_flag(get_string(values, "required_state_flag", "none"));
			result.require_moving = get_bool(values, "require_moving", true);
			result.reset = parse_resource_reset(get_string(values, "resource_reset", "none"));
			result.cleared_flag = parse_state_flag(get_string(values, "clear_state_flag", "none"));
			result.consume_trigger = get_bool(values, "consume_trigger", false);
			result.charge_pool = get_string(values, "charge_pool", std::format("action:{}", id));
			result.exclusive_group = get_string(values, "exclusive_group", {});
			if (result.charge_pool.empty() || result.charge_pool.size() > 64)
			{
				throw std::runtime_error("charge_pool must contain between 1 and 64 characters");
			}
			result.max_forward_input = get_number(values, "max_forward_input", 1.0f, -1.0f, 1.0f);
			result.min_ground_distance = get_number(values, "min_ground_distance", 0.0f, 0.0f, 5000.0f);

			result.horizontal_speed = get_number(values, "horizontal_speed", 0.0f, 0.0f, 4000.0f);
			result.horizontal_speed_scale = get_number(values, "horizontal_speed_scale", 1.0f, 0.0f, 10.0f);
			result.exit_speed = get_number(values, "exit_speed", 0.0f, 0.0f, 4000.0f);
			result.vertical_speed = get_number(values, "vertical_speed", 0.0f, -4000.0f, 4000.0f);
			result.vertical_acceleration = get_number(values, "vertical_acceleration", 0.0f, -20000.0f, 20000.0f);
			result.horizontal_half_life_ms = get_number(values, "horizontal_half_life_ms", 0.0f, 0.0f, 30000.0f);
			result.origin_offset_z = get_number(values, "origin_offset_z", 0.0f, -32.0f, 32.0f);
			result.duration_ms = get_integer(values, "duration_ms", 0, 0, 30000);
			result.min_air_time_ms = get_integer(values, "min_air_time_ms", 0, 0, 30000);
			result.state_grace_ms = get_integer(values, "state_grace_ms", 0, 0, 30000);
			result.cooldown_ms = get_integer(values, "cooldown_ms", 0, 0, 120000);
			result.max_charges = get_integer(values, "max_charges", 0, 0, 16);

			if (result.kind != action_kind::impulse && result.duration_ms == 0)
			{
				throw std::runtime_error("duration_ms must be positive for continuous actions");
			}

			return result;
		}

		player_configuration parse_profile(const scripting::array& values)
		{
			static const std::unordered_set<std::string> fields{"input_projection", "actions"};
			validate_keys(values, fields, "PMove profile");

			player_configuration result{};
			if (const auto input_values = get_array(values, "input_projection"))
			{
				static const std::unordered_set<std::string> input_fields{
					"activation_button", "latched", "project_to_forward", "state_flag", "required_stance",
				};
				validate_keys(*input_values, input_fields, "input projection");
				result.input_projection.configured = true;
				result.input_projection.activation_button = parse_button(
					get_string(*input_values, "activation_button", {}, true));
				result.input_projection.latched = get_bool(*input_values, "latched", false);
				result.input_projection.project_to_forward = get_bool(*input_values, "project_to_forward", false);
				result.input_projection.active_flag = parse_state_flag(get_string(*input_values, "state_flag", "none"));
				result.input_projection.required_stance = parse_stance(
					get_string(*input_values, "required_stance", "unchanged"));
			}

			const auto action_values = get_array(values, "actions", true).value();
			result.actions.reserve(action_values.size());
			for (auto index = 0; index < action_values.size(); ++index)
			{
				const auto value = action_values.get(index);
				if (!value.is<scripting::array>())
				{
					throw std::runtime_error(std::format("actions[{}] must be an array", index));
				}
				result.actions.push_back(parse_action(value.as<scripting::array>(), index + 1));
			}

			for (auto index = 0u; index < result.actions.size(); ++index)
			{
				const auto& action = result.actions[index];
				for (auto other = index + 1; other < result.actions.size(); ++other)
				{
					const auto& candidate = result.actions[other];
					if (candidate.charge_pool == action.charge_pool &&
						(candidate.cooldown_ms != action.cooldown_ms ||
							candidate.max_charges != action.max_charges ||
							candidate.reset != action.reset))
					{
						throw std::runtime_error(
							"actions sharing a charge_pool must use the same cooldown, charge count, and reset policy");
					}
				}
			}

			return result;
		}

		bool is_synthetic_actor_move(const game::pmove_t* pm)
		{
			// Path-node generation runs PM_WalkMove with a temporary state rooted at
			// the entity's client-pointer field. All real PMove state copies must be
			// accepted so client prediction and server movement stay symmetrical.
			const auto* synthetic_state = reinterpret_cast<const game::playerState_s*>(
				&game::g_entities[game::LOCAL_PLAYER_ENTITY_NUM].client);
			return pm->ps == synthetic_state;
		}

		player_runtime& get_runtime(game::playerState_s* player_state)
		{
			for (auto& slot : runtime_slots)
			{
				if (slot.player_state == player_state)
				{
					slot.last_use = ++runtime_use_counter;
					return slot.runtime;
				}
			}

			auto* slot = &runtime_slots.front();
			for (auto& candidate : runtime_slots)
			{
				if (!candidate.player_state)
				{
					slot = &candidate;
					break;
				}
				if (candidate.last_use < slot->last_use)
				{
					slot = &candidate;
				}
			}

			slot->player_state = player_state;
			slot->last_use = ++runtime_use_counter;
			slot->runtime = {};
			return slot->runtime;
		}

		int get_pressed_buttons(player_runtime& runtime, const game::pmove_t* pm)
		{
			if (!runtime.button_timeline_initialized)
			{
				runtime.previous_buttons = pm->oldcmd.buttons;
				runtime.button_timeline_initialized = true;
			}

			if (pm->cmd.serverTime < runtime.button_server_time)
			{
				return pm->cmd.buttons & ~pm->oldcmd.buttons;
			}
			if (pm->cmd.serverTime == runtime.button_server_time)
			{
				return runtime.pressed_buttons;
			}

			runtime.pressed_buttons = pm->cmd.buttons & ~runtime.previous_buttons;
			runtime.previous_buttons = pm->cmd.buttons;
			runtime.button_server_time = pm->cmd.serverTime;
			return runtime.pressed_buttons;
		}

		bool button_pressed(const int pressed_buttons, const int button)
		{
			return (pressed_buttons & button) != 0;
		}

		bool get_wish_direction(const game::pmove_t* pm, const game::pml_t* pml, float* direction)
		{
			const auto forward_move = static_cast<float>(pm->cmd.forwardmove);
			const auto right_move = static_cast<float>(pm->cmd.rightmove);
			direction[0] = pml->forward[0] * forward_move + pml->right[0] * right_move;
			direction[1] = pml->forward[1] * forward_move + pml->right[1] * right_move;
			direction[2] = 0.0f;

			const auto length = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1]);
			if (length <= 0.001f)
			{
				return false;
			}

			direction[0] /= length;
			direction[1] /= length;
			return true;
		}

		float horizontal_speed(const game::playerState_s* ps)
		{
			return std::sqrt(ps->velocity[0] * ps->velocity[0] + ps->velocity[1] * ps->velocity[1]);
		}

		bool movement_blocked(const game::pmove_t* pm)
		{
			const auto buttons = pm->cmd.buttons;
			return (pm->ps->pm_flags & (game::PMF_MANTLE | game::PMF_LADDER)) != 0 ||
				(buttons & (game::BUTTON_ATTACK | game::BUTTON_MELEE | game::BUTTON_RELOAD |
					game::BUTTON_USE_RELOAD | game::BUTTON_ADS)) != 0;
		}

		void apply_stance(game::playerState_s* ps, const stance value)
		{
			switch (value)
			{
			case stance::stand:
				ps->pm_flags &= ~(game::PMF_PRONE | game::PMF_DUCKED);
				break;
			case stance::crouch:
				ps->pm_flags &= ~game::PMF_PRONE;
				ps->pm_flags |= game::PMF_DUCKED;
				break;
			case stance::prone:
				ps->pm_flags &= ~game::PMF_DUCKED;
				ps->pm_flags |= game::PMF_PRONE;
				break;
			default:
				break;
			}
		}

		void apply_native_stance(game::pmove_t* pm, const stance value)
		{
			apply_stance(pm->ps, value);
			switch (value)
			{
			case stance::stand:
				pm->cmd.buttons &= ~(game::BUTTON_PRONE | game::BUTTON_DUCK);
				break;
			case stance::crouch:
				pm->cmd.buttons = (pm->cmd.buttons & ~game::BUTTON_PRONE) | game::BUTTON_DUCK;
				break;
			case stance::prone:
				pm->cmd.buttons = (pm->cmd.buttons & ~game::BUTTON_DUCK) | game::BUTTON_PRONE;
				break;
			default:
				break;
			}
		}

		bool stance_matches(const game::playerState_s* ps, const stance value)
		{
			if (value == stance::unchanged)
			{
				return true;
			}
			if (value == stance::stand)
			{
				return (ps->pm_flags & (game::PMF_PRONE | game::PMF_DUCKED)) == 0;
			}
			if (value == stance::crouch)
			{
				return (ps->pm_flags & game::PMF_DUCKED) != 0;
			}
			return (ps->pm_flags & game::PMF_PRONE) != 0;
		}

		bool try_required_stance(game::pmove_t* pm, player_runtime& runtime, const stance required_stance)
		{
			if (!runtime.stance_requested)
			{
				return stance_matches(pm->ps, required_stance);
			}

			if (required_stance == stance::stand)
			{
				game::Bounds bounds = pm->bounds;
				bounds.midPoint[2] = game::PLAYER_STANDING_MIDPOINT_Z;
				bounds.halfSize[2] = game::PLAYER_STANDING_HALF_HEIGHT;
				game::trace_t trace{};
				game::PM_playerTrace(pm, &trace, pm->ps->origin, pm->ps->origin, &bounds, 0, pm->tracemask);
				if (trace.startsolid || trace.allsolid)
				{
					return false;
				}
			}

			apply_stance(pm->ps, required_stance);
			runtime.stance_requested = false;
			return true;
		}

		bool ground_clearance(game::pmove_t* pm, const float minimum)
		{
			if (minimum <= 0.0f)
			{
				return true;
			}

			game::trace_t trace{};
			float end[3]{pm->ps->origin[0], pm->ps->origin[1], pm->ps->origin[2] - 5000.0f};
			game::PM_playerTrace(pm, &trace, pm->ps->origin, end, &pm->bounds, 0, pm->tracemask);
			return trace.fraction * 5000.0f >= minimum;
		}

		bool action_can_trigger(const action_definition& action, const game::pmove_t* pm,
			const int pressed_buttons,
			const bool on_ground, const bool moving, const bool sprinting,
			const int air_time_ms, const int last_sprint_time)
		{
			if (!button_pressed(pressed_buttons, action.trigger_button))
			{
				return false;
			}
			if (movement_blocked(pm))
			{
				return false;
			}
			if (action.required_state == move_state::ground && !on_ground)
			{
				return false;
			}
			if (action.required_state == move_state::air && on_ground)
			{
				return false;
			}
			if (!on_ground && air_time_ms < action.min_air_time_ms)
			{
				return false;
			}
			if (action.require_moving && !moving)
			{
				return false;
			}
			if (action.required_flag == state_flag::sprinting && !sprinting &&
				pm->cmd.serverTime - last_sprint_time > action.state_grace_ms)
			{
				return false;
			}
			const auto forward = static_cast<float>(pm->cmd.forwardmove) / 127.0f;
			return forward <= action.max_forward_input;
		}

		void refresh_charges(const action_definition& action, charge_runtime& resource, const int server_time)
		{
			if (action.cooldown_ms > 0 && server_time >= resource.last_charge_time &&
				server_time - resource.last_charge_time >= action.cooldown_ms)
			{
				resource.charges = 0;
			}
		}

		void start_action(const action_definition& action, action_runtime& runtime,
			charge_runtime& resource, const game::pmove_t* pm, const float* direction,
			const bool consume_charge)
		{
			runtime.last_trigger_time = pm->cmd.serverTime;
			if (consume_charge)
			{
				resource.last_charge_time = pm->cmd.serverTime;
				resource.charges++;
			}
			runtime.active = action.kind != action_kind::impulse;
			runtime.start_time = pm->cmd.serverTime;
			runtime.end_time = pm->cmd.serverTime + action.duration_ms;
			runtime.direction[0] = direction[0];
			runtime.direction[1] = direction[1];
			runtime.initial_speed = std::max(action.horizontal_speed,
				horizontal_speed(pm->ps) * action.horizontal_speed_scale);
		}

		void apply_impulse(game::pmove_t* pm, const action_definition& action, const float* direction)
		{
			apply_stance(pm->ps, action.target_stance);
			pm->ps->origin[2] += action.origin_offset_z;
			if (action.horizontal_speed > 0.0f)
			{
				pm->ps->velocity[0] = direction[0] * action.horizontal_speed;
				pm->ps->velocity[1] = direction[1] * action.horizontal_speed;
			}

			if (action.z_mode == vertical_mode::set)
			{
				pm->ps->velocity[2] = action.vertical_speed;
			}
			else if (action.z_mode == vertical_mode::add)
			{
				pm->ps->velocity[2] = std::max(pm->ps->velocity[2], 0.0f) + action.vertical_speed;
			}
		}

		void apply_continuous_action(game::pmove_t* pm, game::pml_t* pml,
			const action_definition& action, action_runtime& runtime)
		{
			if (!runtime.active)
			{
				return;
			}
			if (pm->cmd.serverTime < runtime.start_time)
			{
				return;
			}
			if (pm->cmd.serverTime >= runtime.end_time)
			{
				runtime.active = false;
				return;
			}

			apply_stance(pm->ps, action.target_stance);
			if (action.kind == action_kind::velocity_curve)
			{
				const auto duration = std::max(1, runtime.end_time - runtime.start_time);
				const auto progress = std::clamp(
					static_cast<float>(pm->cmd.serverTime - runtime.start_time) / static_cast<float>(duration),
					0.0f, 1.0f);
				const auto speed = runtime.initial_speed + (action.exit_speed - runtime.initial_speed) * progress;
				pm->ps->velocity[0] = runtime.direction[0] * speed;
				pm->ps->velocity[1] = runtime.direction[1] * speed;
			}
			else if (action.kind == action_kind::acceleration)
			{
				const auto retention = action.horizontal_half_life_ms <= 0.0f
					? 0.0f
					: std::exp2(-(pml->frametime * 1000.0f) / action.horizontal_half_life_ms);
				pm->ps->velocity[0] *= retention;
				pm->ps->velocity[1] *= retention;
				pm->ps->velocity[2] += action.vertical_acceleration * pml->frametime;
			}
		}

		void update_input_projection(game::pmove_t* pm, const input_projection_policy& policy,
			player_runtime& runtime, const bool moving, const int pressed_buttons)
		{
			if (!policy.configured)
			{
				return;
			}

			if (button_pressed(pressed_buttons, policy.activation_button))
			{
				runtime.input_requested = true;
				runtime.stance_requested = !stance_matches(pm->ps, policy.required_stance);
			}
			if (!policy.latched)
			{
				runtime.input_requested = (pm->cmd.buttons & policy.activation_button) != 0;
			}
			if (policy.latched && policy.active_flag == state_flag::sprinting &&
				(pm->ps->pm_flags & game::PMF_SPRINTING) != 0)
			{
				runtime.input_requested = true;
			}
			if (!moving || movement_blocked(pm))
			{
				runtime.input_requested = false;
				runtime.stance_requested = false;
			}
		}

		void process_move(game::pmove_t* pm, game::pml_t* pml, const bool on_ground,
			const std::function<void()>& invoke_original)
		{
			if (processing_move || is_synthetic_actor_move(pm))
			{
				return invoke_original();
			}

			std::shared_lock lock(configuration_mutex);
			if (!configuration.input_projection.configured && configuration.actions.empty())
			{
				return invoke_original();
			}
			move_scope scope;

			std::unique_lock runtime_lock(runtime_mutex);
			auto& runtime = get_runtime(pm->ps);
			if (runtime.generation != configuration.generation)
			{
				runtime = {};
				runtime.generation = configuration.generation;
			}

			bool landed = false;
			if (!runtime.timeline_initialized || pm->cmd.serverTime > runtime.max_server_time)
			{
				landed = runtime.timeline_initialized && on_ground && !runtime.latest_on_ground;
				if (!on_ground && (!runtime.timeline_initialized || runtime.latest_on_ground))
				{
					runtime.air_start_time = pm->cmd.serverTime;
					runtime.air_time_initialized = true;
				}
				runtime.timeline_initialized = true;
				runtime.latest_on_ground = on_ground;
				runtime.max_server_time = pm->cmd.serverTime;
			}

			if (landed && runtime.pending_landing_stance != stance::unchanged)
			{
				apply_stance(pm->ps, runtime.pending_landing_stance);
				runtime.pending_landing_stance = stance::unchanged;
			}
			const auto air_time_ms = on_ground ? 0 : std::max(0, pm->cmd.serverTime - runtime.air_start_time);

			const auto pressed_buttons = get_pressed_buttons(runtime, pm);
			float direction[3]{};
			const auto moving = get_wish_direction(pm, pml, direction);
			update_input_projection(pm, configuration.input_projection, runtime, moving, pressed_buttons);
			const auto required_stance_ready = !configuration.input_projection.configured ||
				try_required_stance(pm, runtime, configuration.input_projection.required_stance);
			const auto projection_active = configuration.input_projection.configured &&
				runtime.input_requested && required_stance_ready;
			const auto sprinting = moving && required_stance_ready && !movement_blocked(pm) &&
				((projection_active && configuration.input_projection.active_flag == state_flag::sprinting) ||
					(pm->ps->pm_flags & game::PMF_SPRINTING) != 0);
			if (sprinting)
			{
				runtime.last_sprint_time = pm->cmd.serverTime;
			}

			if (projection_active && configuration.input_projection.active_flag == state_flag::sprinting)
			{
				pm->ps->pm_flags |= game::PMF_SPRINTING;
			}

			std::vector<pending_impulse> pending;
			bool projected_state_cleared = false;
			for (const auto& action : configuration.actions)
			{
				auto& action_state = runtime.actions[action.id];
				auto& resource = runtime.charge_pools[action.charge_pool];
				if (landed && action.reset == resource_reset::landing)
				{
					action_state = {};
					resource = {};
				}

				refresh_charges(action, resource, pm->cmd.serverTime);
				const auto replaying_trigger = action_state.last_trigger_time == pm->cmd.serverTime;
				if (action_can_trigger(action, pm, pressed_buttons, on_ground, moving, sprinting,
					air_time_ms, runtime.last_sprint_time) &&
					(replaying_trigger || action.max_charges == 0 || resource.charges < action.max_charges) &&
					ground_clearance(pm, action.min_ground_distance))
				{
					if (!action.exclusive_group.empty())
					{
						for (const auto& candidate : configuration.actions)
						{
							if (candidate.id != action.id && candidate.exclusive_group == action.exclusive_group)
							{
								runtime.actions[candidate.id].active = false;
							}
						}
					}
					start_action(action, action_state, resource, pm, direction, !replaying_trigger);
					if (action.landing_stance != stance::unchanged)
					{
						runtime.pending_landing_stance = action.landing_stance;
					}
					if (action.cleared_flag == state_flag::sprinting)
					{
						runtime.input_requested = false;
						runtime.stance_requested = false;
						pm->ps->pm_flags &= ~game::PMF_SPRINTING;
						projected_state_cleared = true;
					}
					if (action.consume_trigger)
					{
						pm->cmd.buttons &= ~action.trigger_button;
					}
					if (action.kind == action_kind::impulse)
					{
						apply_stance(pm->ps, action.target_stance);
						pending.push_back({&action, {direction[0], direction[1], direction[2]}});
					}
				}

				if (action_state.active)
				{
					if (action.native_stance == stance::unchanged)
					{
						apply_stance(pm->ps, action.target_stance);
					}
					else
					{
						apply_native_stance(pm, action.native_stance);
					}
				}
			}

			if (projection_active && !projected_state_cleared && moving &&
				configuration.input_projection.project_to_forward)
			{
				const auto forward_move = pm->cmd.forwardmove;
				const auto right_move = pm->cmd.rightmove;
				float forward[3]{};
				float right[3]{};
				std::copy_n(pml->forward, 3, forward);
				std::copy_n(pml->right, 3, right);

				const auto input_length = std::clamp(static_cast<int>(std::round(std::hypot(
					static_cast<float>(forward_move), static_cast<float>(right_move)))), 1, 127);
				pm->cmd.forwardmove = static_cast<char>(input_length);
				pm->cmd.rightmove = 0;
				pml->forward[0] = direction[0];
				pml->forward[1] = direction[1];
				pml->forward[2] = 0.0f;
				pml->right[0] = -direction[1];
				pml->right[1] = direction[0];
				pml->right[2] = 0.0f;

				invoke_original();

				// Keep the projected command for the remaining native PMove stages so
				// weapon sprint state observes forward input as well.
				std::copy_n(forward, 3, pml->forward);
				std::copy_n(right, 3, pml->right);
			}
			else
			{
				invoke_original();
			}

			for (const auto& impulse : pending)
			{
				apply_impulse(pm, *impulse.definition, impulse.direction);
			}
			for (const auto& action : configuration.actions)
			{
				auto& action_state = runtime.actions[action.id];
				if (action.kind != action_kind::impulse &&
					(action.required_state == move_state::any ||
						(action.required_state == move_state::ground) == on_ground))
				{
					apply_continuous_action(pm, pml, action, action_state);
				}
			}
			if (projection_active && !projected_state_cleared &&
				configuration.input_projection.active_flag == state_flag::sprinting)
			{
				pm->ps->pm_flags |= game::PMF_SPRINTING;
			}
		}

		void walk_move_stub(game::pmove_t* pm, game::pml_t* pml)
		{
			process_move(pm, pml, true, [&]() { walk_move_hook.invoke<void>(pm, pml); });
		}

		void air_move_stub(game::pmove_t* pm, game::pml_t* pml)
		{
			process_move(pm, pml, false, [&]() { air_move_hook.invoke<void>(pm, pml); });
		}

		void register_script_api()
		{
			gsc::add_function("setpmoveprofile", []()
			{
				get_player_argument(0);
				const auto values = get_definition_argument(1);
				auto profile = parse_profile(values);
				std::unique_lock lock(configuration_mutex);
				profile.generation = configuration.generation + 1;
				configuration = std::move(profile);
			});

			gsc::add_function("clearpmoveprofile", []()
			{
				get_player_argument(0);
				std::unique_lock lock(configuration_mutex);
				const auto generation = configuration.generation + 1;
				configuration = {};
				configuration.generation = generation;
			});
		}
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			walk_move_hook.create(game::PM_WalkMove, &walk_move_stub);
			air_move_hook.create(game::PM_AirMove, &air_move_stub);
			register_script_api();
		}
	};
}

REGISTER_COMPONENT(movement::component)
