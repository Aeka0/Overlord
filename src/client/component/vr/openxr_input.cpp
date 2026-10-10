#include <std_include.hpp>
#include "openxr_input.hpp"
#include "hud_controller.hpp"
#include "controller_haptics.hpp"
#include "pose_filter.hpp"
#include <cmath>

#if H2V_OPENXR_HEADERS_AVAILABLE
namespace vr::openxr
{
	namespace
	{
		struct binding_recipe
		{
			const char* profile;
			const char* axis;
			const char* squeeze;
			std::array<const char*, 2> primary, secondary;
			bool trigger_touch;
			bool menu_button;
			controller_input::analog_policy trigger_policy, squeeze_policy;
		};
		constexpr std::array recipes{
		    binding_recipe{.profile = "/interaction_profiles/oculus/touch_controller",
		                   .axis = "/input/thumbstick",
		                   .squeeze = "/input/squeeze/value",
		                   .primary = {"/input/x/click", "/input/a/click"},
		                   .secondary = {"/input/y/click", "/input/b/click"},
		                   .trigger_touch = true,
		                   .menu_button = true,
		                   .trigger_policy = controller_input::value_button,
		                   .squeeze_policy = controller_input::value_button},
		    binding_recipe{.profile = "/interaction_profiles/valve/index_controller",
		                   .axis = "/input/thumbstick",
		                   .squeeze = "/input/squeeze/force",
		                   .primary = {"/input/a/click", "/input/a/click"},
		                   .secondary = {"/input/b/click", "/input/b/click"},
		                   .trigger_touch = true,
		                   .menu_button = false,
		                   .trigger_policy = controller_input::index_trigger,
		                   .squeeze_policy = controller_input::index_squeeze},
		    binding_recipe{.profile = "/interaction_profiles/htc/vive_controller",
		                   .axis = "/input/trackpad",
		                   .squeeze = "/input/squeeze/click",
		                   .primary = {},
		                   .secondary = {},
		                   .trigger_touch = false,
		                   .menu_button = true,
		                   .trigger_policy = controller_input::value_button,
		                   .squeeze_policy = controller_input::click_button}};
	}

	bool input_actions::initialize(const dispatch_table& xr,
	                               XrInstance instance,
	                               XrSession session,
	                               XrResult& result,
	                               std::string& error)
	{
		instance_ = instance;
		prompt_profile_retry_.reset();
		for (auto& retry : analog_profile_retry_) retry.reset();
		hud_controller::set_knuckles(controller_input::input_backend::openxr,false);
		profile_refresh_pending_ = true;
		XrActionSetCreateInfo info{XR_TYPE_ACTION_SET_CREATE_INFO};
		strcpy_s(info.actionSetName, "gameplay");
		strcpy_s(info.localizedActionSetName, "H2 VR gameplay");
		result = xr.create_action_set(instance, &info, &set_);
		if (XR_FAILED(result))
		{
			error = "xrCreateActionSet";
			return false;
		}
		const auto create = [&](const char* name, XrActionType type, XrAction& action)
		{
			XrActionCreateInfo action_info{XR_TYPE_ACTION_CREATE_INFO};
			strcpy_s(action_info.actionName, name);
			strcpy_s(action_info.localizedActionName, name);
			action_info.actionType = type;
			result = xr.create_action(set_, &action_info, &action);
			if (XR_FAILED(result))
				error = std::string{"xrCreateAction: "} + name;
			return XR_SUCCEEDED(result);
		};
		if (!create("move", XR_ACTION_TYPE_VECTOR2F_INPUT, move_) ||
		    !create("turn", XR_ACTION_TYPE_VECTOR2F_INPUT, turn_) ||
		    !create("sprint", XR_ACTION_TYPE_BOOLEAN_INPUT, sprint_) ||
		    !create("jump", XR_ACTION_TYPE_BOOLEAN_INPUT, jump_) ||
		    !create("menu_toggle", XR_ACTION_TYPE_BOOLEAN_INPUT, menu_) ||
		    !create("menu_recenter", XR_ACTION_TYPE_BOOLEAN_INPUT, recenter_))
			return false;
		for (unsigned h = 0; h < 2; ++h)
		{
			auto& hand = hands_[h];
			const std::string prefix = h == 0 ? "left_" : "right_";
			const auto named = [&](const char* suffix, XrActionType type, XrAction& action)
			{ return create((prefix + suffix).c_str(), type, action); };
			if (!named("trigger", XR_ACTION_TYPE_FLOAT_INPUT, hand.trigger) ||
			    !named("squeeze", XR_ACTION_TYPE_FLOAT_INPUT, hand.squeeze) ||
			    !named("trigger_touch", XR_ACTION_TYPE_BOOLEAN_INPUT, hand.touch) ||
			    !named("primary", XR_ACTION_TYPE_BOOLEAN_INPUT, hand.primary) ||
			    !named("secondary", XR_ACTION_TYPE_BOOLEAN_INPUT, hand.secondary) ||
			    !named("grip", XR_ACTION_TYPE_POSE_INPUT, hand.grip) ||
			    !named("aim", XR_ACTION_TYPE_POSE_INPUT, hand.aim) ||
			    !named("haptic", XR_ACTION_TYPE_VIBRATION_OUTPUT, hand.haptic))
				return false;
		}
		// Core profiles only. Bindings preserve physical left/right roles, while
		// weapon ownership and dominant-hand choices remain in gameplay.
		binding_profiles_.resize(recipes.size());
		for (std::size_t recipe_index = 0; recipe_index < recipes.size(); ++recipe_index)
		{
			const auto& recipe = recipes[recipe_index];
			std::vector<XrActionSuggestedBinding> bindings;
			const auto bind = [&](XrAction action, const std::string& path)
			{
				XrPath handle{};
				result = xr.string_to_path(instance, path.c_str(), &handle);
				if (XR_FAILED(result))
				{
					error = "xrStringToPath: " + path;
					return false;
				}
				bindings.push_back({action, handle});
				return true;
			};
			for (unsigned h = 0; h < 2; ++h)
			{
				const std::string user = h == 0 ? "/user/hand/left" : "/user/hand/right";
				const auto& hand = hands_[h];
				if (!bind(hand.grip, user + "/input/grip/pose") ||
				    !bind(hand.aim, user + "/input/aim/pose") ||
				    !bind(hand.trigger, user + "/input/trigger/value") ||
				    !bind(hand.squeeze, user + recipe.squeeze) ||
				    !bind(hand.haptic, user + "/output/haptic") ||
				    !bind(h == 0 ? move_ : turn_, user + recipe.axis) ||
				    !bind(h == 0 ? sprint_ : jump_, user + recipe.axis + "/click"))
					return false;
				if (recipe.primary[h])
				{
					if ((recipe.trigger_touch && !bind(hand.touch, user + "/input/trigger/touch")) ||
					    !bind(hand.secondary, user + recipe.secondary[h]))
						return false;
					// Left face-primary is the dedicated tap/hold menu control. Never
					// bind the runtime/system button or emit a simultaneous HUD action.
					if (!bind(h == 0 ? recenter_ : hand.primary, user + recipe.primary[h]))
						return false;
				}
			}
			if (recipe.menu_button && !bind(menu_, "/user/hand/left/input/menu/click"))
				return false;
			XrPath profile_path{};
			result = xr.string_to_path(instance, recipe.profile, &profile_path);
			if (XR_FAILED(result))
			{
				error = "xrStringToPath: interaction profile";
				return false;
			}
			binding_profiles_[recipe_index] = profile_path;
			const XrInteractionProfileSuggestedBinding suggested{
			    XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING,
			    nullptr,
			    profile_path,
			    static_cast<std::uint32_t>(bindings.size()),
			    bindings.data()};
			result = xr.suggest_interaction_profile_bindings(instance, &suggested);
			if (XR_FAILED(result))
			{
				error = std::string{"xrSuggestInteractionProfileBindings: "} + recipe.profile;
				return false;
			}
		}
		const XrSessionActionSetsAttachInfo attach{
		    XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO, nullptr, 1, &set_};
		result = xr.attach_session_action_sets(session, &attach);
		if (XR_FAILED(result))
		{
			error = "xrAttachSessionActionSets";
			return false;
		}
		for (auto& hand : hands_)
			for (const auto pair :
			     {std::pair{hand.grip, &hand.grip_space}, std::pair{hand.aim, &hand.aim_space}})
			{
				XrActionSpaceCreateInfo space{XR_TYPE_ACTION_SPACE_CREATE_INFO};
				space.action = pair.first;
				space.poseInActionSpace.orientation.w = 1;
				result = xr.create_action_space(session, &space, pair.second);
				if (XR_FAILED(result))
				{
					error = "xrCreateActionSpace";
					return false;
				}
			}
		return true;
	}

	void input_actions::set_grip_reference(controller_pose_reference::configuration reference, controller_pose_pipeline::mode mode)
	{
		invalidate();
		grip_reference_ = std::move(reference);
		pose_pipeline_ = mode;
		profile_matches_.fill(grip_reference_.required_profile.empty());
		profile_refresh_pending_ = true;
	}

	void input_actions::profile_changed() noexcept
	{
		prompt_profile_retry_.reset();
		for (auto& retry : analog_profile_retry_) retry.reset();
		hud_controller::set_knuckles(controller_input::input_backend::openxr,false);
		invalidate();
		for (auto& hand : hands_)
			hand.trigger_policy = hand.squeeze_policy = {};
		profile_matches_.fill(false);
		profile_refresh_pending_ = true;
	}

	bool input_actions::refresh_profiles(const dispatch_table& xr,
	                                     XrSession session,
	                                     XrResult& result,
	                                     std::string& error)
	{
		if (!profile_refresh_pending_ || grip_reference_.required_profile.empty())
			return true;
		XrPath required{};
		result = xr.string_to_path(instance_, grip_reference_.required_profile.c_str(), &required);
		if (XR_FAILED(result))
		{
			error = "xrStringToPath: grip reference profile";
			return false;
		}
		std::array<bool, 2> matches{};
		for (unsigned hand = 0; hand < hands_.size(); ++hand)
		{
			XrPath user{};
			result = xr.string_to_path(instance_, hand == 0 ? "/user/hand/left" : "/user/hand/right", &user);
			if (XR_FAILED(result))
			{
				error = "xrStringToPath: grip reference hand";
				return false;
			}
			XrInteractionProfileState profile{XR_TYPE_INTERACTION_PROFILE_STATE};
			result = xr.get_current_interaction_profile(session, user, &profile);
			if (XR_FAILED(result))
			{
				error = "xrGetCurrentInteractionProfile";
				return false;
			}
			matches[hand] = profile.interactionProfile == required;
		}
		profile_matches_ = matches;
		profile_refresh_pending_ = false;
		grip_reference_.error.clear();
		for (unsigned hand = 0; hand < matches.size(); ++hand)
			if (!matches[hand])
				grip_reference_.error += std::string{hand == 0 ? "left" : "right"} +
				                         " grip unavailable: active profile does not match " +
				                         grip_reference_.required_profile + "; ";
		return true;
	}

	bool input_actions::refresh_prompt_profile(const dispatch_table& xr,XrSession session) noexcept
	{
		bool queried=xr.string_to_path && xr.get_current_interaction_profile;
		XrPath index_profile{};
		if(queried)queried=XR_SUCCEEDED(xr.string_to_path(instance_,
			"/interaction_profiles/valve/index_controller",&index_profile)) && index_profile!=XR_NULL_PATH;
		bool knuckles=queried;
		for(const auto hand:{"/user/hand/left","/user/hand/right"})
		{
			if(!knuckles)break;
			XrPath user{};
			XrInteractionProfileState profile{XR_TYPE_INTERACTION_PROFILE_STATE};
			queried=XR_SUCCEEDED(xr.string_to_path(instance_,hand,&user)) &&
				XR_SUCCEEDED(xr.get_current_interaction_profile(session,user,&profile));
			knuckles=queried && profile.interactionProfile==index_profile;
		}
		hud_controller::set_knuckles(controller_input::input_backend::openxr,knuckles);
		return queried;
	}

	void input_actions::refresh_analog_profiles(const dispatch_table& xr, XrSession session,
		controller_input::clock::time_point now) noexcept
	{
		for (unsigned h = 0; h < hands_.size(); ++h)
		{
			auto& retry = analog_profile_retry_[h];
			if (!retry.ready(now)) continue;
			XrPath user{};
			XrInteractionProfileState profile{XR_TYPE_INTERACTION_PROFILE_STATE};
			if (XR_FAILED(xr.string_to_path(instance_, h == 0 ? "/user/hand/left" : "/user/hand/right", &user)) ||
				XR_FAILED(xr.get_current_interaction_profile(session, user, &profile)))
			{
				retry.record_result(false, now);
				continue;
			}
			retry.record_result(profile.interactionProfile != XR_NULL_PATH, now);
			auto& hand = hands_[h];
			controller_input::analog_policy trigger{}, squeeze{};
			for (std::size_t i = 0; i < recipes.size(); ++i)
				if (profile.interactionProfile != XR_NULL_PATH && profile.interactionProfile == binding_profiles_[i])
				{
					trigger = recipes[i].trigger_policy;
					squeeze = recipes[i].squeeze_policy;
					break;
				}
			// A newly identified source must rearm through the existing digital
			// generation contract; changing thresholds is not a physical press.
			if (trigger != hand.trigger_policy)
			{
				hand.trigger_latch.reset();
				(void)hand.trigger_state.sample(false, false, now);
			}
			if (squeeze != hand.squeeze_policy)
			{
				hand.squeeze_latch.reset();
				(void)hand.squeeze_state.sample(false, false, now);
			}
			hand.trigger_policy = trigger;
			hand.squeeze_policy = squeeze;
		}
	}

	bool input_actions::sample(const dispatch_table& xr,
	                           XrSession session,
	                           XrSpace base,
	                           XrTime time,
	                           bool focused,
	                           XrResult& result,
	                           std::string& error)
	{
		using namespace controller_input;
		controller_input::frame frame;
		frame.pose_pipeline = pose_pipeline_;
		frame.sequence = ++sequence_;
		frame.sampled_at = controller_input::clock::now();
		frame.target_display_time = time;
		frame.reference_generation = head_pose_bridge::get_status().recenter_count;
		frame.source.backend = input_backend::openxr;
		frame.source.runtime_focus = focused;
		frame.source.runtime_focus_known = true;
		const XrActiveActionSet active{set_, XR_NULL_PATH};
		const XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO, nullptr, 1, &active};
		result = xr.sync_actions(session, &sync);
		frame.focused = focused && XR_SUCCEEDED(result) && result != XR_SESSION_NOT_FOCUSED;
		frame.source.gate = XR_FAILED(result) ? input_condition{input_reason::action_update_failed, result}
		                    : !frame.focused  ? input_condition{input_reason::input_unavailable, result}
		                                      : input_condition{input_reason::none};
		if (XR_FAILED(result))
			error = "xrSyncActions";
		if(frame.focused && prompt_profile_retry_.ready(frame.sampled_at))
			prompt_profile_retry_.record_result(refresh_prompt_profile(xr,session),frame.sampled_at);
		if (frame.focused && !refresh_profiles(xr, session, result, error))
		{
			invalidate(input_reason::profile_query_failed, result);
			return false;
		}
		if (frame.focused) refresh_analog_profiles(xr, session, frame.sampled_at);
		const auto capture = [&](input_channel channel, XrResult api, bool active, bool finite = true)
		{
			if (channel != input_channel::count)
				frame.source.channels[index(channel)] =
				    XR_FAILED(api) ? input_condition{input_reason::action_query_failed, api}
				    : !active      ? input_condition{input_reason::action_inactive}
				    : !finite      ? input_condition{input_reason::action_value_invalid}
				                   : input_condition{input_reason::none};
		};
		const auto boolean = [&](XrAction action,
		                         controller_input::digital_sampler& state,
		                         input_channel channel = input_channel::count)
		{
			XrActionStateBoolean value{XR_TYPE_ACTION_STATE_BOOLEAN};
			const XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, action, XR_NULL_PATH};
			const auto api = frame.focused ? xr.get_action_state_boolean(session, &get, &value) : XR_SUCCESS;
			capture(channel, api, value.isActive);
			const bool available = frame.focused && XR_SUCCEEDED(api) && value.isActive;
			return state.sample(available, value.currentState, frame.sampled_at);
		};
		const auto analog_button =
		    [&](XrAction action, controller_input::digital_sampler& state, controller_input::analog_latch& latch,
		        controller_input::analog_policy policy, controller_input::analog_value& raw, input_channel channel)
		{
			XrActionStateFloat value{XR_TYPE_ACTION_STATE_FLOAT};
			const XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, action, XR_NULL_PATH};
			const auto api = frame.focused ? xr.get_action_state_float(session, &get, &value) : XR_SUCCESS;
			capture(channel, api, value.isActive, std::isfinite(value.currentState));
			const bool available =
			    frame.focused && XR_SUCCEEDED(api) && value.isActive && std::isfinite(value.currentState);
			raw = {available, std::isfinite(value.currentState) ? value.currentState : 0.f, policy.source};
			return state.sample(available, latch.sample(raw, policy, frame.sampled_at), frame.sampled_at);
		};
		const auto axis = [&](XrAction action, std::array<float, 2>& output, input_channel channel)
		{
			XrActionStateVector2f value{XR_TYPE_ACTION_STATE_VECTOR2F};
			const XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, action, XR_NULL_PATH};
			const auto api = frame.focused ? xr.get_action_state_vector2f(session, &get, &value) : XR_SUCCESS;
			capture(channel,
			        api,
			        value.isActive,
			        std::isfinite(value.currentState.x) && std::isfinite(value.currentState.y));
			if (!frame.focused || XR_FAILED(api) || !value.isActive || !std::isfinite(value.currentState.x) ||
			    !std::isfinite(value.currentState.y))
				return false;
			output = {std::clamp(value.currentState.x, -1.f, 1.f),
			          std::clamp(value.currentState.y, -1.f, 1.f)};
			return true;
		};
		const auto pose =
		    [&](XrAction action, XrSpace space, controller_input::hand_pose& output, input_channel channel)
		{
			XrActionStatePose state{XR_TYPE_ACTION_STATE_POSE};
			const XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, action, XR_NULL_PATH};
			const auto api = frame.focused ? xr.get_action_state_pose(session, &get, &state) : XR_SUCCESS;
			capture(channel, api, state.isActive);
			if (!frame.focused || XR_FAILED(api) || !state.isActive)
				return;
			XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
			const auto located = xr.locate_space(space, base, time, &location);
			if (XR_SUCCEEDED(located))
				output.quality = {
					(location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0,
					(location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0,
					(location.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) != 0,
					(location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT) != 0, true};
			auto& condition = frame.source.channels[index(channel)];
			condition = XR_FAILED(located) ? input_condition{input_reason::action_query_failed, located}
			                               : input_condition{input_reason::pose_invalid};
			if (XR_FAILED(located) ||
			    (location.locationFlags &
			     (XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) !=
			        (XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
				return;
			const auto& q = location.pose.orientation;
			const auto& p = location.pose.position;
			const float norm = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
			if (!std::isfinite(norm) || std::abs(norm - 1.f) > .01f)
				return;
			output.tracking = {{p.x, p.y, p.z}, pose_filter::rotation({q.x, q.y, q.z, q.w})};
			output.valid = pose_filter::valid({output.tracking.position_meters, output.tracking.orientation});
			if (output.valid)
				condition = {input_reason::none};
		};
		frame.move_active = axis(move_, frame.move, input_channel::move);
		frame.turn_active = axis(turn_, frame.turn, input_channel::turn);
		frame.sprint = boolean(sprint_, sprint_state_, input_channel::sprint);
		frame.jump = boolean(jump_, jump_state_, input_channel::jump);
		frame.menu_toggle = boolean(menu_, menu_state_);
		frame.menu_recenter = boolean(recenter_, recenter_state_);
		for (unsigned h = 0; h < 2; ++h)
		{
			auto& hand = hands_[h];
			frame.trigger[h] =
			    analog_button(hand.trigger, hand.trigger_state, hand.trigger_latch, hand.trigger_policy,
			                  frame.trigger_analog[h], hand_channel(input_channel::left_trigger, h));
			frame.squeeze[h] =
			    analog_button(hand.squeeze, hand.squeeze_state, hand.squeeze_latch, hand.squeeze_policy,
			                  frame.squeeze_analog[h], hand_channel(input_channel::left_squeeze, h));
			frame.trigger_touch[h] = boolean(hand.touch, hand.touch_state);
			frame.primary[h] = boolean(hand.primary, hand.primary_state);
			frame.secondary[h] = boolean(hand.secondary, hand.secondary_state);
			pose(hand.grip, hand.grip_space, frame.grip[h], hand_channel(input_channel::left_grip, h));
			frame.sdk_grip[h] = frame.grip[h];
			if (!grip_reference_.required_profile.empty() && !profile_matches_[h])
			{
				if (frame.grip[h].valid)
					frame.source.channels[index(hand_channel(input_channel::left_grip, h))] = {
					    input_reason::profile_mismatch};
				frame.grip[h].valid = false;
			}
			const bool raw_grip_valid = frame.grip[h].valid;
			controller_pose_reference::normalize_grip(frame.grip[h], grip_reference_, h);
			if (raw_grip_valid && !frame.grip[h].valid)
				frame.source.channels[index(hand_channel(input_channel::left_grip, h))] = {
				    input_reason::calibration_invalid};
			pose(hand.aim, hand.aim_space, frame.aim[h], hand_channel(input_channel::left_aim, h));
			frame.sdk_aim[h] = frame.aim[h];
		}
		controller_input::publish(frame);
		controller_haptics::bindings({frame.grip[0].valid, frame.grip[1].valid});
		const auto pulses = controller_haptics::consume(frame);
		for (unsigned h = 0; h < 2; ++h)
			if (pulses[h].amplitude > 0)
			{
				const XrHapticActionInfo action{
				    XR_TYPE_HAPTIC_ACTION_INFO, nullptr, hands_[h].haptic, XR_NULL_PATH};
				const XrHapticVibration pulse{XR_TYPE_HAPTIC_VIBRATION,
				                              nullptr,
				                              static_cast<XrDuration>(pulses[h].seconds * 1e9),
				                              pulses[h].frequency,
				                              pulses[h].amplitude};
				const auto sent = xr.apply_haptic_feedback(
				    session, &action, reinterpret_cast<const XrHapticBaseHeader*>(&pulse));
				controller_haptics::delivered(h, XR_FAILED(sent) ? int(sent) : 0);
			}
		return XR_SUCCEEDED(result);
	}

	void input_actions::invalidate(controller_input::input_reason reason, std::int64_t code) noexcept
	{
		const auto now = controller_input::clock::now();
		for (auto& hand : hands_)
		{
			hand.trigger_latch.reset();
			hand.squeeze_latch.reset();
		}
		for (auto& hand : hands_)
			for (auto* state : {&hand.trigger_state,
			                    &hand.squeeze_state,
			                    &hand.touch_state,
			                    &hand.primary_state,
			                    &hand.secondary_state})
				(void)state->sample(false, false, now);
		for (auto* state : {&sprint_state_, &jump_state_, &menu_state_, &recenter_state_})
			(void)state->sample(false, false, now);
		controller_input::invalidate(reason, controller_input::input_backend::openxr, code);
		controller_haptics::bindings({});
		controller_haptics::clear();
	}
	call_result input_actions::destroy(const dispatch_table& xr) noexcept
	{
		invalidate();
		XrResult result{};
		for (auto& hand : hands_)
			for (auto* space : {&hand.grip_space, &hand.aim_space})
				if (*space != XR_NULL_HANDLE)
				{
					result = xr.destroy_space(*space);
					if (XR_FAILED(result))
						return {result, "xrDestroySpace"};
					*space = XR_NULL_HANDLE;
				}
		if (set_ != XR_NULL_HANDLE)
		{
			result = xr.destroy_action_set(set_);
			if (XR_FAILED(result))
				return {result, "xrDestroyActionSet"};
			set_ = XR_NULL_HANDLE;
		}
		// Action-set destruction also destroys its child actions.
		for (auto& hand : hands_)
			hand.trigger = hand.squeeze = hand.touch = hand.primary = hand.secondary = hand.grip = hand.aim =
			    hand.haptic = XR_NULL_HANDLE;
		move_ = turn_ = sprint_ = jump_ = menu_ = recenter_ = XR_NULL_HANDLE;
		hud_controller::set_knuckles(controller_input::input_backend::openxr,false);
		prompt_profile_retry_.reset();
		instance_ = XR_NULL_HANDLE;
		for (auto& retry : analog_profile_retry_) retry.reset();
		binding_profiles_ = {};
		for (auto& hand : hands_)
			hand.trigger_policy = hand.squeeze_policy = {};
		profile_refresh_pending_ = true;
		profile_matches_.fill(false);
		return {};
	}
}
#endif
