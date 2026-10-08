#include <std_include.hpp>
#include "openxr_input.hpp"
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
		};
		constexpr std::array recipes{
		    binding_recipe{.profile = "/interaction_profiles/oculus/touch_controller",
		                   .axis = "/input/thumbstick",
		                   .squeeze = "/input/squeeze/value",
		                   .primary = {"/input/x/click", "/input/a/click"},
		                   .secondary = {"/input/y/click", "/input/b/click"},
		                   .trigger_touch = true,
		                   .menu_button = true},
		    binding_recipe{.profile = "/interaction_profiles/valve/index_controller",
		                   .axis = "/input/thumbstick",
		                   .squeeze = "/input/squeeze/force",
		                   .primary = {"/input/a/click", "/input/a/click"},
		                   .secondary = {"/input/b/click", "/input/b/click"},
		                   .trigger_touch = true,
		                   .menu_button = false},
		    binding_recipe{.profile = "/interaction_profiles/htc/vive_controller",
		                   .axis = "/input/trackpad",
		                   .squeeze = "/input/squeeze/click",
		                   .primary = {},
		                   .secondary = {},
		                   .trigger_touch = false,
		                   .menu_button = true}};
	}

	bool input_actions::initialize(const dispatch_table& xr,
	                               XrInstance instance,
	                               XrSession session,
	                               XrResult& result,
	                               std::string& error)
	{
		instance_ = instance;
		enumerate_sources_ = nullptr;
		source_name_ = nullptr;
		// Optional presentation queries. A loader without them still provides input.
		if (xr.get_instance_proc_addr)
		{
			PFN_xrVoidFunction function{};
			if (XR_SUCCEEDED(xr.get_instance_proc_addr(instance, "xrEnumerateBoundSourcesForAction", &function)))
				enumerate_sources_ = reinterpret_cast<PFN_xrEnumerateBoundSourcesForAction>(function);
			function = nullptr;
			if (XR_SUCCEEDED(xr.get_instance_proc_addr(instance, "xrGetInputSourceLocalizedName", &function)))
				source_name_ = reinterpret_cast<PFN_xrGetInputSourceLocalizedName>(function);
		}
		prompt_bindings_.reset();
		prompt_bindings_at_ = {};
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
		for (const auto& recipe : recipes)
		{
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

	void input_actions::refresh_prompt_bindings(XrSession session) noexcept
	{
		const auto now = controller_input::clock::now();
		if (!enumerate_sources_ || !source_name_ || now - prompt_bindings_at_ < std::chrono::seconds(1)) return;
		prompt_bindings_at_ = now;
		try
		{
			auto labels = std::make_shared<prompt_bindings::snapshot>();
			const std::array handles{hands_[0].trigger, hands_[1].trigger, hands_[0].squeeze, hands_[1].squeeze,
				hands_[0].secondary, hands_[1].secondary, recenter_, menu_, move_, turn_, jump_, sprint_};
			static_assert(handles.size() == prompt_bindings::action_count);
			for (std::size_t i = 0; i < handles.size(); ++i)
			{
				if (handles[i] == XR_NULL_HANDLE) continue;
				const XrBoundSourcesForActionEnumerateInfo info{XR_TYPE_BOUND_SOURCES_FOR_ACTION_ENUMERATE_INFO,
					nullptr, handles[i]};
				std::array<XrPath, 16> sources{};
				uint32_t count{};
				if (XR_FAILED(enumerate_sources_(session, &info, static_cast<uint32_t>(sources.size()), &count,
					sources.data())) || count > sources.size()) continue;
				labels->known[i] = true;
				for (uint32_t source = 0; source < count; ++source)
				{
					const XrInputSourceLocalizedNameGetInfo name_info{XR_TYPE_INPUT_SOURCE_LOCALIZED_NAME_GET_INFO,
						nullptr, sources[source], XR_INPUT_SOURCE_LOCALIZED_NAME_USER_PATH_BIT |
						XR_INPUT_SOURCE_LOCALIZED_NAME_COMPONENT_BIT};
					std::array<char, prompt_bindings::max_label_bytes + 1> name{};
					uint32_t bytes{};
					if (XR_FAILED(source_name_(session, &name_info, static_cast<uint32_t>(name.size()), &bytes,
						name.data())) || bytes > name.size())
					{
						labels->known[i] = false;
						continue;
					}
					const auto length = strnlen(name.data(), name.size());
					if (length < name.size() && prompt_bindings::valid_label({name.data(), length}))
						prompt_bindings::append(labels->labels[i], {name.data(), length});
					else labels->known[i] = false;
				}
			}
			prompt_bindings_ = std::move(labels);
		}
		catch (...) { prompt_bindings_.reset(); }
	}

	void input_actions::set_grip_reference(controller_pose_reference::configuration reference)
	{
		invalidate();
		grip_reference_ = std::move(reference);
		profile_matches_.fill(grip_reference_.required_profile.empty());
		profile_refresh_pending_ = true;
	}

	void input_actions::profile_changed() noexcept
	{
		prompt_bindings_.reset();
		prompt_bindings_at_ = {};
		invalidate();
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
		frame.sequence = ++sequence_;
		frame.sampled_at = controller_input::clock::now();
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
		if (frame.focused && !refresh_profiles(xr, session, result, error))
		{
			invalidate(input_reason::profile_query_failed, result);
			return false;
		}
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
		    [&](XrAction action, controller_input::digital_sampler& state, input_channel channel)
		{
			XrActionStateFloat value{XR_TYPE_ACTION_STATE_FLOAT};
			const XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO, nullptr, action, XR_NULL_PATH};
			const auto api = frame.focused ? xr.get_action_state_float(session, &get, &value) : XR_SUCCESS;
			capture(channel, api, value.isActive, std::isfinite(value.currentState));
			const bool available =
			    frame.focused && XR_SUCCEEDED(api) && value.isActive && std::isfinite(value.currentState);
			return state.sample(available, value.currentState >= .55f, frame.sampled_at);
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
			    analog_button(hand.trigger, hand.trigger_state, hand_channel(input_channel::left_trigger, h));
			frame.squeeze[h] =
			    analog_button(hand.squeeze, hand.squeeze_state, hand_channel(input_channel::left_squeeze, h));
			frame.trigger_touch[h] = boolean(hand.touch, hand.touch_state);
			frame.primary[h] = boolean(hand.primary, hand.primary_state);
			frame.secondary[h] = boolean(hand.secondary, hand.secondary_state);
			pose(hand.grip, hand.grip_space, frame.grip[h], hand_channel(input_channel::left_grip, h));
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
		}
		if (frame.focused) refresh_prompt_bindings(session);
		frame.prompt_bindings = frame.focused ? prompt_bindings_ : nullptr;
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
		prompt_bindings_.reset();
		enumerate_sources_ = nullptr;
		source_name_ = nullptr;
		instance_ = XR_NULL_HANDLE;
		profile_refresh_pending_ = true;
		profile_matches_.fill(false);
		return {};
	}
}
#endif
