#include <std_include.hpp>
#include "native_scripted_control.hpp"
#include "mounted_turret.hpp"
#include "campaign/cliffhanger/physical.hpp"
#include "ladder_runtime.hpp"
#include "fixed_sniper.hpp"
#include "javelin_screen.hpp"
#include "notebook_runtime.hpp"
#include "sentry_runtime.hpp"
#include "sentry_input.hpp"
#include "campaign/cliffhanger/runtime.hpp"
#include "vehicles/runtime.hpp"
#include "designator_events.hpp"
#include "../controller_input.hpp"
#include "../diagnostics/input_status.hpp"
#include "../diagnostics/steamvr_input_status.hpp"
#include "../diagnostics.hpp"
#include "../vr_runtime.hpp"
#include "../engine_stereo_owner_pass.hpp"
#include "../settings.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "controller_buttons.hpp"
#include "controller_firing.hpp"
#include "controller_ads.hpp"
#include "optic_runtime.hpp"
#include "controller_locomotion.hpp"
#include "../remote_look.hpp"
#include "controller_stance.hpp"
#include "game/dvars.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include "weapon_interaction.hpp"
#include "weapon_hud.hpp"
#include "world_interaction.hpp"
#include "independent_fire_runtime.hpp"
#include "break_action_runtime.hpp"
#include "launcher_runtime.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::controllers
{
	namespace weapons = gameplay::weapons;
	namespace
	{
		// Verified in live H2 code: CreateCmd -> normal input -> FinishMove ->
		// CL_CreateNewCommands stores one 64-byte command in a 64-entry ring.
		// Do not modify CL_GetUserCmd: prediction may read an old command again.
		constexpr std::uintptr_t create_command_address = 0x1403D20F0;
		constexpr std::uintptr_t normal_input_call = 0x1403D2223;
		constexpr std::uintptr_t normal_input_address = 0x1403D13C0;
		constexpr std::uintptr_t remote_input_call=0x1403D21CA,remote_input_address=0x1403D1870;
		constexpr std::uintptr_t finish_move_call = 0x1403D22A2;
		constexpr std::uintptr_t finish_move_address = 0x1403D0300;
		constexpr std::uintptr_t update_view_angles_call = 0x14068D680;
		constexpr std::uintptr_t update_view_angles_address = 0x14068D7B0;
		// CL_ExecuteKey sends the binding through this native reliable-command
		// queue before updating movement keys. notifyoncommand waits on that
		// route, not usercmd.buttons (favela_escape's playerjump is one example).
		constexpr std::uintptr_t notify_command_address = 0x1403D3A90;
		constexpr int jump_binding = 37; // Native whitelist: +gostand.
		constexpr std::uintptr_t client_angles_address = 0x141E8A65C;
		constexpr std::uintptr_t client_yaw_address = 0x141E8A660;
		utils::hook::detour create_command_hook;
		game::dvar_t* enabled{};
		game::dvar_t* deadzone{};
		game::dvar_t* snap_angle{};
		game::dvar_t* turn_mode{};
		game::dvar_t* turn_speed{};
		game::dvar_t* turn_deadzone{};
		controller_input::locomotion locomotion;
		controller_input::vehicle_locomotion driving_locomotion;
		controller_input::remote_look remote_locomotion;
		bool remote_axes_verified{};
		std::atomic_uint64_t remote_axis_commands{},remote_stick_commands{};
		bool driving_inputs{};
		controller_input::command_buttons buttons;
		controller_input::stance_gesture stance;
		bool stance_verified{};
		constexpr std::uintptr_t execute_binding_address=0x1403CF1E0;
		constexpr std::uintptr_t native_stance_address=0x141E8A61C;
		std::atomic<std::uint64_t> stance_count{};
		gameplay::sequences::actions story_actions;
		gameplay::sentry::placement_input sentry_placement;
		weapons::trigger_policy firing;
		weapons::ads_policy ads;
		game::dvar_t* auto_ads{};
		bool ads_verified{};
		std::atomic<bool> ads_requested{};
		std::atomic<std::uint64_t> ads_command_count{};
		thread_local int building_client = -1;
		thread_local bool normal_input_seen{};
		thread_local bool remote_input_seen{};
		thread_local bool tracked_view_applied{};
		controller_input::clock::time_point last_command{};
		std::atomic<bool> alive{true};
		std::atomic<std::uint64_t> command_count{};
		std::atomic<std::uint64_t> movement_count{};
		std::atomic<std::uint64_t> turn_count{};
		std::atomic<std::uint64_t> sprint_count{};
		std::atomic<std::uint64_t> jump_count{};
		std::atomic<std::uint64_t> jump_notify_count{};
		std::atomic<std::uint64_t> attack_count{};
		std::atomic<std::uint64_t> story_brake_count{},story_melee_count{};
		bool hooks_installed{};

		static_assert(sizeof(game::usercmd_s) == 64);
		static_assert(game::BUTTON_ADS == 0x800);
		static_assert(offsetof(game::usercmd_s, buttons) == 4);
		static_assert(offsetof(game::usercmd_s, forwardmove) == 28);
		static_assert(offsetof(game::usercmd_s, rightmove) == 29);
		static_assert(offsetof(game::playerState_s, commandTime) == 0x4c);
		static_assert(offsetof(game::playerState_s, delta_angles) == 0x98);
		static_assert(offsetof(game::playerState_s, viewangles) == 0x108);
		static_assert(offsetof(game::usercmd_s, angles) == 8);
		static_assert(offsetof(game::usercmd_s,remoteControlAngles)==0x3e);
		std::array<float,2> remote_rates()noexcept
		{
			// BG's remote-angle evaluator reads these two native float dvars.
			// Hashes and pointer loads are tied to the checked native instructions.
			std::array<float,2> result{};
			constexpr std::array<std::uint32_t,2> hashes{0x75a9d11a,0xcf6fd58b};
			for(unsigned i=0;i<2;++i)
			{
				const auto* value=*reinterpret_cast<const game::dvar_t* const*>(0x14CE00E30+i*8);
				if(!value || std::uint32_t(value->hash)!=hashes[i] || value->type!=game::dvar_type::value)return {};
				result[i]=value->current.value;
			}
			return result;
		}
		void remote_input_stub(int client,game::usercmd_s* cmd)
		{
			utils::hook::invoke<void>(remote_input_address,client,cmd);
			if(client==0 && building_client==0)remote_input_seen=true;
		}

		void update_view_angles_stub(game::playerState_s* const ps, const game::usercmd_s* const cmd)
		{
			const auto delta = ps->delta_angles[0];
			float pitch{};
			const bool tracked = alive.load(std::memory_order_relaxed) &&
				(ps == game::CG_GetPredictedPlayerState(0) ||
					ps == reinterpret_cast<game::playerState_s*>(game::g_entities[0].client)) &&
				head_pose_bridge::resolve_game_pitch(cmd->serverTime, cmd->angles[0], delta, pitch);
			// Keep native yaw/roll and riot-shield side effects. Only undo the
			// ordinary pitch clamp for this exact HMD command, including its delta
			// correction. Parent Pmove still owns prone/mounted/script constraints.
			utils::hook::invoke<void>(update_view_angles_address, ps, cmd);
			if (tracked)
			{
				ps->delta_angles[0] = delta;
				ps->viewangles[0] = pitch;
			}
		}

		bool normal_gameplay()
		{
			const auto* paused = game::Dvar_FindVar("cl_paused");
			return game::CL_IsCgameInitialized() && *game::keyCatchers == 0 && paused &&
				paused->current.integer == 0;
		}

		void finish_move_stub(const int local_client, game::usercmd_s* const cmd)
		{
			const auto sniper=local_client==0 ? gameplay::fixed_sniper::current().epoch : 0;
			const auto remote=local_client==0 ? gameplay::equipment::special::notebook::camera_epoch() : 0;
			controller_input::remote_stick stick{};bool remote_allowed{};std::uint64_t remote_epoch{};
			if(local_client==0 && building_client==0 && cmd)
			{
				const auto input=controller_input::latest();const auto head=head_pose_bridge::get_status();
				const bool allowed=alive && enabled && enabled->current.enabled && head.enabled && head.pose_available &&
					!head.recenter_pending && head.recenter_count==input.reference_generation && normal_gameplay();
				remote_allowed=allowed && input.focused;
				stick=remote_locomotion.consume(input,remote,remote_input_seen,remote_allowed,turn_deadzone->current.value,controller_input::clock::now());
				if(remote && !remote_allowed)head_pose_bridge::suspend_remote_view();
				gameplay::fixed_sniper::command(input,allowed,cmd,reinterpret_cast<float*>(client_angles_address),
					deadzone->current.value,turn_speed->current.value);
			}
			// The native CreateCmd pitch clamp and other input sources run after
			// normal_input_stub. Set the actual game view at the final packing
			// boundary, independently of controller availability/neutral arming.
			if (local_client == 0 && building_client == 0 && (normal_input_seen || sniper || remote) && cmd &&
				alive.load(std::memory_order_relaxed) && normal_gameplay())
			{
				const auto* const ps = game::CG_GetPredictedPlayerState(0);
				const auto mounted_camera=gameplay::mounted::camera_request();
				if (ps)
				{
					const auto request=gameplay::sequences::camera_request_for(gameplay::sequences::for_player(ps),sniper,mounted_camera,remote);
					remote_epoch=remote?request.epoch:0;
					auto* angles=reinterpret_cast<float*>(client_angles_address);
					const bool view_input=!remote || (normal_input_seen && remote_allowed);
					if(remote && view_input)
					{
						angles[0]-=stick.pitch*turn_speed->current.value*stick.seconds;
						angles[1]=std::remainder(angles[1]-stick.yaw*turn_speed->current.value*stick.seconds,360.f);
					}
					const bool applied=view_input && head_pose_bridge::apply_game_view(angles,ps->delta_angles,request.epoch,remote!=0);
					// UAV aim retains native clamps; the ordinary HMD pitch-clamp
					// override must not undo the remote camera's constraints.
					tracked_view_applied=applied && !remote;
				}
			}
			utils::hook::invoke<void>(finish_move_address, local_client, cmd);
			if(remote && remote_epoch && remote_allowed && remote_input_seen && remote_axes_verified && cmd)
			{
				if(head_pose_bridge::apply_remote_control(cmd->remoteControlAngles,remote_rates(),{stick.pitch,stick.yaw},cmd->serverTime,remote_epoch))
					++remote_axis_commands;
			}
			if(remote && (stick.pitch!=0 || stick.yaw!=0))++remote_stick_commands;
		}

		void apply_locomotion(game::usercmd_s* cmd,const controller_input::frame& input,bool gameplay,bool can_move,bool can_turn,controller_input::clock::time_point now,bool driving=false)
		{
			const bool entering=driving && !driving_inputs;
			if(driving!=driving_inputs){locomotion.reset();driving_locomotion.reset();driving_inputs=driving;}
			auto movement = driving ? driving_locomotion.consume(input,gameplay && can_move,deadzone->current.value,now,entering) : locomotion.consume(
				input, gameplay && (can_move || can_turn),
				deadzone->current.value,
				{
					static_cast<controller_input::turn_mode>(turn_mode->current.integer),
					turn_deadzone->current.value,
					turn_speed->current.value,
					snap_angle->current.value,
				},
				now);
			if(driving && gameplay && can_move)
			{
				const auto physical=gameplay::vehicles::controls(input,now);
				movement.right=std::clamp(movement.right+physical.steering,-1.f,1.f);
				movement.forward=gameplay::vehicles::merge_driver_throttle(movement.forward,physical.throttle);
				movement.active=movement.active || physical.steering!=0 || physical.throttle!=0;
			}
			if (!movement.active)
				return;
			const auto add_axis = [](const char original, const float axis) {
				return static_cast<char>(std::clamp(static_cast<int>(static_cast<signed char>(original)) +
														static_cast<int>(std::lround(axis * 127.0f)),
													-127, 127));
			};
			if(can_move) {cmd->forwardmove = add_axis(cmd->forwardmove, movement.forward);
				cmd->rightmove = add_axis(cmd->rightmove, movement.right);}
			if (can_move && (movement.forward != 0 || movement.right != 0))
				++movement_count;
			if (can_turn && movement.yaw_delta != 0)
			{
				auto& yaw = *reinterpret_cast<float*>(client_yaw_address);
				if (std::isfinite(yaw))
				{
					yaw = std::remainder(yaw + movement.yaw_delta, 360.0f);
					// Smooth yaw is ordinary continuous camera motion. Resetting history
					// every command would break temporal effects during a held turn.
					if (movement.discontinuous_turn)
						engine_stereo_owner_pass::request_temporal_history_reset();
					++turn_count;
				}
			}
		}

		void normal_input_stub(game::usercmd_s* const cmd, const float frame_time)
		{
			utils::hook::invoke<void>(normal_input_address, cmd, frame_time);
			if (building_client != 0 || !cmd || !alive.load(std::memory_order_relaxed))
				return;
			normal_input_seen = true;
			const auto head = head_pose_bridge::get_status();
			const auto input = controller_input::latest();
			const bool gameplay = enabled && enabled->current.enabled && head.enabled &&
								  head.pose_available && !head.recenter_pending &&
								  head.recenter_count == input.reference_generation &&
								  normal_gameplay();
			const auto now = controller_input::clock::now();
			const auto story=gameplay::sequences::for_player(game::CG_GetPredictedPlayerState(0));
			const bool agm=gameplay::equipment::special::notebook::controlling();
			const bool climbing=gameplay::cliffhanger_physical::owns_movement() || gameplay::ladders::owns_movement();
			if(gameplay::ladders::owns_movement())
			{cmd->forwardmove=cmd->rightmove=0;cmd->buttons&=~(game::BUTTON_JUMP|game::BUTTON_SPRINT|game::BUTTON_DUCK|game::BUTTON_PRONE);}
			const bool scripted=story.epoch!=0 && story.suspend_weapons;
			const bool can_move=!agm && !climbing && (!scripted || story.allow_movement),can_turn=!scripted || story.allow_turn;
			const bool weapon_gameplay=gameplay && !agm && !gameplay::equipment::special::cliffhanger::native_c4_session() && gameplay::scripted_control::predicted_allowed();
			weapons::independent_fire::command(weapon_gameplay);
			const bool sniper=gameplay::fixed_sniper::current().epoch!=0;
			const bool turret=gameplay::mounted::active() || sniper;
			const auto button_state = buttons.consume(input, gameplay && !turret && !scripted && !agm && !climbing, now);
			auto button_mask = button_state.held;
			const auto* posture_ps=game::CG_GetPredictedPlayerState(0);
			const auto posture=posture_ps ? controller_input::native_posture(posture_ps->pm_flags):controller_input::posture::unknown;
			const bool stance_allowed=gameplay && !turret && !scripted && !agm && !climbing && stance_verified && posture_ps &&
				!(posture_ps->pm_flags&(game::PMF_MANTLE|game::PMF_LADDER));
			const auto posture_command=stance.consume(input,posture,stance_allowed,now,
				(button_state.pressed&controller_input::jump_button)!=0,(button_mask&controller_input::jump_button)!=0);
			if(posture_command.target)
			{
				// Reuse H2's own input-stance commands, not playerState writes or
				// forced pose bits. Native movement retains clearance/script authority.
				const int binding=controller_input::stance_binding(*posture_command.target,*reinterpret_cast<const int*>(native_stance_address));
				if(binding){utils::hook::invoke<void>(execute_binding_address,building_client,binding,0,0u);++stance_count;}
			}
			if(posture_command.suppress_jump)button_mask&=~controller_input::jump_button;
			cmd->buttons |= button_mask; // Never clear a keyboard/native button.
			cmd->buttons |= gameplay::interaction::command(input,gameplay && !agm && (weapon_gameplay || story.allow_world_use),now);
			if (button_state.pressed & controller_input::jump_button)
			{
				// One native binding notification per accepted press, including a
				// tap released before command construction. Do not invoke the GSC
				// VM from this thread or synthesize keyboard state/another jump.
				utils::hook::invoke<void>(notify_command_address, building_client, jump_binding, 0);
				++jump_notify_count;
			}
			if (button_mask & controller_input::sprint_button)
				++sprint_count;
			if (button_mask & controller_input::jump_button)
				++jump_count;
			const auto owner = weapons::sample_native_equipped();
			weapons::javelin_screen::input(input,owner,weapon_gameplay && !turret && !scripted && !agm);
			gameplay::weapon_hud::update(input, owner, weapon_gameplay, now);
			const bool independent=weapons::independent_fire::owns_native(game::CG_GetPredictedPlayerState(0));
			const bool mechanical_trigger=weapons::break_action::allows_trigger(owner,input.reference_generation,now) && weapons::launcher::allows_trigger(owner,input.reference_generation,now) && weapons::javelin_screen::allows_fire(owner);
			const bool device=weapons::controller_fire_delivery(owner.weapon)==weapons::fire_delivery::scripted_device;
			const bool device_ready=!device || gameplay::equipment::special::designator_events::can_fire();
			if (firing.consume(input, owner, weapon_gameplay && weapons::firing_enabled() && !independent && mechanical_trigger,
							   device_ready && weapons::firing_ready(owner, input.reference_generation, now), now,device))
			{
				cmd->buttons |= weapons::attack_button;
				++attack_count;
			}
			if (independent || !device_ready || (weapon_gameplay && !mechanical_trigger)) cmd->buttons &= ~weapons::attack_button;
			const bool turret_command=gameplay::mounted::command(input,gameplay,cmd->buttons);
			apply_locomotion(cmd,input,gameplay && !turret_command && !sniper && !agm,can_move,can_turn,now,story.vehicle!=gameplay::vehicles::kind::none);
		}

		game::usercmd_s* create_command_stub(game::usercmd_s* output, const int local_client)
		{
			if(local_client==0)
			{
				const bool gameplay=alive.load() && normal_gameplay();
				controller_input::set_gameplay_active(gameplay);
				if(!gameplay)gameplay::fixed_sniper::suspend_input(); // Also covers native menu branches skipping FinishMove.
				if(!gameplay){remote_locomotion.reset();head_pose_bridge::suspend_remote_view();}
			}
			tracked_view_applied = false;
			const auto now = controller_input::clock::now();
			if (now - last_command > std::chrono::milliseconds(150))
			{
				if (local_client==0) gameplay::interaction::command(controller_input::latest(),false,now);
				locomotion.reset();driving_locomotion.reset();
				buttons.reset();
				stance.reset();
				story_actions.reset();
				sentry_placement.reset();
				firing.reset();
				gameplay::weapon_hud::suspend();
			}
			last_command = now;
			building_client = local_client;
			normal_input_seen = remote_input_seen = false;
			bool vehicle_input_seen=false;
			const auto result = create_command_hook.invoke<game::usercmd_s*>(output, local_client);
			if (local_client==0 && result)
			{
				// Also runs when native scripted branches bypass normal locomotion.
				// This is still before insertion into the native usercmd ring.
				const auto input=controller_input::latest();const auto head=head_pose_bridge::get_status();
				const auto story=gameplay::sequences::for_player(game::CG_GetPredictedPlayerState(0));
				const bool gameplay=enabled && enabled->current.enabled && head.enabled && head.pose_available &&
					!head.recenter_pending && head.recenter_count==input.reference_generation && normal_gameplay();
				if(!normal_input_seen && story.vehicle!=gameplay::vehicles::kind::none)
				{
					// Vehicle/script command branches may skip CL_NormalInput. Fill the
					// same native movement axes once, without writing HMD/turn yaw.
					apply_locomotion(result,input,gameplay,story.allow_movement,false,now,true);vehicle_input_seen=true;
				}
				const bool allowed=gameplay && story.epoch!=0;
				const int story_buttons=story_actions.consume(input,story.stage,story.epoch,allowed,now,story.melee_mode);
				result->buttons |= story_buttons;
				// Carrying a sentry disables firearms and may bypass normal input.
				// Feed its native attack/use waiter before the command enters the ring.
				if(sentry_placement.consume(input,gameplay::sentry::placement_epoch(),gameplay,now))
					result->buttons |= weapons::attack_button;
				if(story_buttons&gameplay::sequences::brake_button)++story_brake_count;
				if(story_buttons&gameplay::sequences::melee_button)++story_melee_count;
				const auto owner=weapons::sample_native_equipped();
				const auto* ps=game::CG_GetPredictedPlayerState(0);
				const bool ads_allowed=gameplay && normal_input_seen && ads_verified && auto_ads && auto_ads->current.enabled &&
					!story.suspend_weapons && gameplay::scripted_control::predicted_allowed() &&
					!gameplay::mounted::active() && ps && !(ps->pm_flags&(game::PMF_MANTLE|game::PMF_LADDER)) &&
					owner.weapon==result->weapon && weapons::firing_enabled() &&
					(weapons::controller_fire_delivery(owner.weapon)==weapons::fire_delivery::bullets || weapons::launcher::aim_supported(owner));
				ads_requested=ads.consume(input,owner,weapons::launcher::ads_muzzle(owner,weapons::current_muzzle()),ads_allowed,now);
				if(weapons::javelin_screen::enabled() && weapons::current_muzzle().profile_id=="javelin")
				{
					ads.reset();
					if(!normal_input_seen)weapons::javelin_screen::input(input,owner,false);
					ads_requested=weapons::javelin_screen::requested(owner);
				}
				weapons::optics::request(owner,{ads_allowed,ads_requested.load()},input);
				// FinishMove has packed the selected weapon. Add only this command's
				// VR request; native keyboard ADS and PM_Weapon gates keep authority.
				if (ads_requested.load()) {result->buttons |= game::BUTTON_ADS;++ads_command_count;}
				gameplay::equipment::special::notebook::command(input,gameplay,result->buttons);
				gameplay::equipment::special::cliffhanger::command(input,gameplay,result->buttons);
			}
			else if (local_client==0) {sentry_placement.reset();ads.reset();ads_requested=false;weapons::optics::request({},{},{});weapons::javelin_screen::input({},{},false);}
			if (local_client == 0 && result) head_pose_bridge::record_game_command(
				result->serverTime, result->angles[0], tracked_view_applied);
			// Scripted/suppressed branches must rearm even if they never poll controls.
			if (!normal_input_seen)
			{
				if (local_client==0 && result)
				{
					// Vehicle commands can bypass CL_NormalInput while gameplay is
					// active. Mounted controls retain their own fresh-input/fire gate.
					const auto input=controller_input::latest();const auto head=head_pose_bridge::get_status();
					const bool gameplay=enabled && enabled->current.enabled && head.enabled && head.pose_available &&
						!head.recenter_pending && head.recenter_count==input.reference_generation && normal_gameplay();
					gameplay::mounted::command(input,gameplay,result->buttons);
				}
				weapons::independent_fire::command(false);
				if (local_client==0) gameplay::interaction::command(controller_input::latest(),false,now);
				gameplay::weapon_hud::suspend();
				if(!vehicle_input_seen){locomotion.reset();driving_locomotion.reset();}
				buttons.reset();
				firing.reset();
			}
			building_client = -1;
			++command_count;
			return result;
		}

		void print_status()
		{
			console::info("[VR input] auto_ads=%d verified=%d requested=%d ads_commands=%llu (request only, not native acceptance)\n",
				auto_ads && auto_ads->current.enabled,ads_verified,ads_requested.load(),ads_command_count.load());
			console::info("[VR input] story_brake_commands=%llu story_melee_commands=%llu\n",story_brake_count.load(),story_melee_count.load());
			console::info("[VR input] remote_axes_verified=%d remote_axis_commands=%llu remote_stick_commands=%llu\n",
				remote_axes_verified,remote_axis_commands.load(),remote_stick_commands.load());
			const auto input = controller_input::latest();
			const auto head = head_pose_bridge::get_status();
			console::info("[VR input] hand_angle_pitch=%.2f yaw=%.2f roll=%.2f settling=%d runtime_aim_preserved=1\n",
				input.orientation_degrees[0],input.orientation_degrees[1],input.orientation_degrees[2],input.orientation_settling);
			console::info("[VR input] pose_pipeline=%s\n", controller_pose_pipeline::name(input.pose_pipeline));
			console::info("[VR input] wrist_calibration=grip_local_point inward/back/up_m=%.4f/%.4f/%.4f\n",
				input.position_offsets_meters[0],input.position_offsets_meters[1],input.position_offsets_meters[2]);
			console::info("[VR input] game_view=hmd pitch_yaw_commands=%llu history_misses=%llu "
				"camera_command_head_yaw=%.3f tracking_world_yaw=%.3f\n",
				head.game_view_applications, head.game_view_history_misses,
				head.game_view_yaw_contribution, head.base_yaw_degrees);
			console::info("[VR input] turn_mode=%s turn_speed=%.1f turn_deadzone=%.2f snap_angle=%.1f\n",
						  turn_mode->current.integer == 0 ? "smooth" : "snap", turn_speed->current.value,
						  turn_deadzone->current.value, snap_angle->current.value);
			const auto now = controller_input::clock::now();
			const bool sampled = input.sequence != 0 && input.sampled_at != controller_input::clock::time_point{} &&
			                     now >= input.sampled_at;
			// Invalidated publication has no sample age; -1 avoids printing system uptime.
			const auto age = sampled
			                     ? std::chrono::duration_cast<std::chrono::milliseconds>(now - input.sampled_at).count()
			                     : -1;
			console::info("[VR input] hooks=%d enabled=%d frame=%llu age_ms=%lld focus=%d move=%d(%.3f,%.3f) "
						  "turn=%d(%.3f,%.3f) grip=%d/%d aim=%d/%d commands=%llu moving=%llu turns=%llu\n",
						  hooks_installed, enabled && enabled->current.enabled,
						  static_cast<unsigned long long>(input.sequence), static_cast<long long>(age),
						  input.focused, input.move_active, input.move[0], input.move[1], input.turn_active,
						  input.turn[0], input.turn[1], input.grip[0].valid, input.grip[1].valid,
						  input.aim[0].valid, input.aim[1].valid, command_count.load(), movement_count.load(),
						  turn_count.load());
			console::info("[VR input] sample_available=%d head_pose=%d recenter_pending=%d recenter_count=%llu\n",
			              sampled, head.pose_available, head.recenter_pending, head.recenter_count);
			console::info("[VR input] sprint_active=%d down=%d presses=%llu jump_active=%d down=%d "
						  "presses=%llu sprint_commands=%llu jump_commands=%llu jump_notifies=%llu\n",
						  input.sprint.active, input.sprint.down, input.sprint.presses, input.jump.active,
						  input.jump.down, input.jump.presses, sprint_count.load(), jump_count.load(),
						  jump_notify_count.load());
			console::info("[VR input] trigger_active/down L=%d/%d R=%d/%d attack_commands=%llu\n",
						  input.trigger[0].active, input.trigger[0].down, input.trigger[1].active,
						  input.trigger[1].down, attack_count.load());
			console::info("[VR input] squeeze_active/down L=%d/%d R=%d/%d\n", input.squeeze[0].active,
						  input.squeeze[0].down, input.squeeze[1].active, input.squeeze[1].down);
			const auto* ps=game::CL_IsCgameInitialized()?game::CG_GetPredictedPlayerState(0):nullptr;
			console::info("[VR input] stance_verified=%d requests=%llu requested=%d actual=%d vertical=0.90 lateral_limit=none dwell_ms=0 repeat_ms=450\n",
				stance_verified,stance_count.load(),stance_verified && ps ? *reinterpret_cast<const int*>(native_stance_address):-1,
					ps ? int(controller_input::native_posture(ps->pm_flags)):-1);
			std::ostringstream history_report;
			diagnostics::append_input_history(history_report,controller_input::get_input_history(),
				controller_input::clock::now(),"[VR input] ");
			console::print_text(console::con_type_info,history_report.str());
			std::ostringstream setup_report;
			diagnostics::append_steamvr_input(setup_report,runtime::get().get_status().openvr_input_diagnostics,
				controller_input::clock::now());
			console::print_text(console::con_type_info,setup_report.str());
			// Keep the decisive facts at the bottom for reports that only contain a screenshot.
			std::ostringstream overview;
			diagnostics::append_input_overview(overview,controller_input::get_input_history(),
				controller_input::clock::now(),"[VR input summary] ");
			console::print_text(console::con_type_info,overview.str());
			const auto* vr_enabled = game::Dvar_FindVar("vr_enable");
			if (diagnostics::write_status_snapshot(vr_enabled && vr_enabled->current.enabled))
				console::info("[VR input] Complete report saved to %s. Share this file.\n",diagnostics::status_snapshot_path);
			else console::error("[VR input] Report save FAILED; an older file may remain. Share this console output.\n");
		}
	} // namespace

	class component final : public component_interface
	{
	  public:
		void post_unpack() override
		{
			// Menus can stop the simulation/command consumers while tracking keeps
			// publishing. Publish that context discontinuity on the native main loop.
			scheduler::loop([] {controller_input::set_gameplay_active(alive.load() && normal_gameplay());},scheduler::pipeline::main);
			// Preserve evidence even when the player never runs a console command.
			// One background attempt per process; allow setup/probe publication to
			// settle first. No renderer-thread disk I/O or runtime API calls here.
			scheduler::loop([] {
				static bool attempted{};
				if (attempted || !alive.load()) return;
				const auto history = controller_input::get_input_history();
				const auto now = controller_input::clock::now();
				if (!history.first_api_failure.seen || now < history.first_api_failure.at ||
				    now-history.first_api_failure.at < std::chrono::seconds(1)) return;
				attempted = true;
				if (diagnostics::write_status_snapshot(runtime::get().requested_enabled()))
					console::info("[VR input] Input API failure report saved automatically to %s.\n",diagnostics::status_snapshot_path);
				else console::error("[VR input] Automatic report save FAILED; run vr_input_status to retry.\n");
			},scheduler::pipeline::async,std::chrono::seconds(1));
			enabled = dvars::register_bool("vr_controllers", true, game::DVAR_FLAG_SAVED,
										   "Enable SteamVR controller locomotion");
			auto_ads = dvars::register_bool("vr_autoAds", true, game::DVAR_FLAG_SAVED,
				"Request native ADS when a held firearm is raised and aligned with the head");
			deadzone = dvars::register_float("vr_moveDeadzone", 0.2f, 0.05f, 0.5f, game::DVAR_FLAG_SAVED,
											 "Radial movement stick deadzone");
			snap_angle = dvars::register_float(settings::snap_angle.name, settings::snap_angle.default_value,
				settings::snap_angle.min, settings::snap_angle.max, game::DVAR_FLAG_SAVED,
											   "Degrees per right-stick snap turn (release to rearm)");
			static const char* turn_modes[]{"smooth", "snap", nullptr};
			turn_mode =
				dvars::register_enum(settings::turn_mode, turn_modes, 0, game::DVAR_FLAG_SAVED,
									 "Controller turning: smooth or snap; release sticks after switching");
			turn_speed =
				dvars::register_float(settings::turn_speed.name, settings::turn_speed.default_value,
					settings::turn_speed.min, settings::turn_speed.max, game::DVAR_FLAG_SAVED,
									  "Smooth turning speed in degrees per second at full deflection");
			turn_deadzone = dvars::register_float("vr_turnDeadzone", 0.2f, 0.05f, 0.5f, game::DVAR_FLAG_SAVED,
												  "Smooth turning stick deadzone");
			command::add("vr_input_status", print_status);
			// Captured H2 keyboard command builder sets/clears usercmd+4 bit 11.
			// Reject this feature alone if the native input contract has changed.
			constexpr std::uint8_t ads_set[]{0x81,0x4b,0x04,0x00,0x08,0x00,0x00};
			constexpr std::uint8_t ads_clear[]{0x81,0x63,0x04,0xff,0xf7,0xff,0xff};
			constexpr std::uint8_t ads_mask[]{255,255,255,255,255,255,255};
			ads_verified=bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x1403D0F82),
				{ads_set,ads_mask,sizeof(ads_set)})) &&
				bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x1403D0F79),
					{ads_clear,ads_mask,sizeof(ads_clear)}));
			if (!ads_verified) console::error("[VR input] Native ADS command contract rejected; automatic ADS disabled\n");
			// The dispatcher is already wrapped by the binding component; validate
			// its native stance branches and whitelist instead of detour entry bytes.
			constexpr std::uint8_t crouch_store[]{0x44,0x89,0x74,0x28,0x1c};
			constexpr std::uint8_t prone_store[]{0x89,0x4c,0x28,0x1c};
			constexpr std::uint8_t stance_base[]{0x48,0x8d,0x2d,0x88,0xa5,0xab,0x01};
			constexpr std::uint8_t base_mask[]{255,255,255,255,255,255,255};
			constexpr std::uint8_t full_mask[]{255,255,255,255,255};
			stance_verified=bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x1403D007E),{crouch_store,full_mask,sizeof(crouch_store)})) &&
				bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x1403D0032),{prone_store,full_mask,sizeof(prone_store)})) &&
				bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x1403D0071),{stance_base,base_mask,sizeof(stance_base)}));
			for(const auto [index,name]:std::array<std::pair<int,const char*>,4>{{{89,"+togglecrouch"},{99,"toggleprone"},{100,"goprone"},{101,"gocrouch"}}})
				stance_verified=stance_verified && game::command_whitelist[index] && std::strcmp(game::command_whitelist[index],name)==0;
			if(!stance_verified)console::error("[VR input] Native stance contract rejected; vertical stick stance disabled\n");
			constexpr std::uint8_t entry[]{0x48, 0x89, 0x5c, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x40};
			constexpr std::uint8_t entry_mask[]{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
			constexpr std::uint8_t call[]{0xe8, 0x98, 0xf1, 0xff, 0xff};
			constexpr std::uint8_t call_mask[]{0xff, 0xff, 0xff, 0xff, 0xff};
			constexpr std::uint8_t finish_call[]{0xe8, 0x59, 0xe0, 0xff, 0xff};
			constexpr std::uint8_t update_angles_call[]{0xe8, 0x2b, 0x01, 0x00, 0x00};
			constexpr std::uint8_t pitch_store[]{0xf3, 0x0f, 0x11, 0x73, 0x70};
			// FinishMove reads clientActive+0x5c/60/64; Pmove commits cmd.time
			// to ps+0x4c and constructs direction vectors from ps+0x108.
			constexpr std::uint8_t pack_angles[]{0xf3, 0x41, 0x0f, 0x10, 0x44, 0x36, 0x54};
			constexpr std::uint8_t pack_mask[]{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
			constexpr std::uint8_t command_time[]{0x89, 0x53, 0x4c};
			constexpr std::uint8_t time_mask[]{0xff, 0xff, 0xff};
			constexpr std::uint8_t sprint_bit[]{0x83, 0x4b, 0x04, 0x02};
			constexpr std::uint8_t sprint_mask[]{0xff, 0xff, 0xff, 0xff};
			constexpr std::uint8_t jump_bit[]{0x81, 0x4a, 0x04, 0x00, 0x04, 0x00, 0x00};
			constexpr std::uint8_t jump_mask[]{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
			constexpr std::uint8_t notify_entry[]{0x40, 0x53, 0x55, 0x57, 0x48, 0x81, 0xec, 0x30, 0x04, 0x00, 0x00};
			constexpr std::uint8_t notify_mask[]{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
			constexpr std::uint8_t notify_call[]{0xe8, 0x6f, 0x48, 0x00, 0x00};
			if (!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(create_command_address),
															 {entry, entry_mask, sizeof(entry)}) ||
				!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(normal_input_call),
															 {call, call_mask, sizeof(call)}) ||
				!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(finish_move_call),
					{finish_call, call_mask, sizeof(finish_call)}) ||
				!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(update_view_angles_call),
					{update_angles_call, call_mask, sizeof(update_angles_call)}) ||
				!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x14068D909),
					{pitch_store, call_mask, sizeof(pitch_store)}) ||
				!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x1403D03B0),
					{pack_angles, pack_mask, sizeof(pack_angles)}) ||
				!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x14068F81B),
					{command_time, time_mask, sizeof(command_time)}) ||
				!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x1403D108F),
															 {sprint_bit, sprint_mask, sizeof(sprint_bit)}) ||
				!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x1403CEF8E),
															 {jump_bit, jump_mask, sizeof(jump_bit)}) ||
				!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(notify_command_address),
					{notify_entry, notify_mask, sizeof(notify_entry)}) ||
				!utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x1403CF21C),
					{notify_call, call_mask, sizeof(notify_call)}) ||
				!game::command_whitelist[jump_binding] ||
				std::strcmp(game::command_whitelist[jump_binding], "+gostand") != 0)
			{
				console::error("[VR input] H2 command hook signature mismatch; native HMD view and controllers "
							   "are not connected to gameplay\n");
				return;
			}
			create_command_hook.create(create_command_address, create_command_stub);
			utils::hook::call(normal_input_call, normal_input_stub);
			utils::hook::call(finish_move_call, finish_move_stub);
			utils::hook::call(update_view_angles_call, update_view_angles_stub);
			const auto remote_check=[](std::uintptr_t address,std::initializer_list<std::uint8_t> bytes) {
				std::array<std::uint8_t,16> mask{};mask.fill(255);
				return bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(address),{bytes.begin(),mask.data(),bytes.size()}));
			};
			remote_axes_verified=remote_check(remote_input_call,{0xe8,0xa1,0xf6,0xff,0xff}) &&
				remote_check(0x1403D195F,{0x81,0x4a,0x04,0x00,0x00,0x10,0x00}) &&
				remote_check(0x1403D1AE0,{0x88,0x43,0x3e}) && remote_check(0x1403D1B2D,{0x88,0x43,0x3f}) &&
				remote_check(0x140683043,{0x0f,0xbe,0x42,0x3e}) && remote_check(0x140683083,{0x0f,0xbe,0x42,0x3f}) &&
				remote_check(0x14068305B,{0x48,0x8b,0x05,0xce,0xdd,0x77,0x0c}) &&
				remote_check(0x140683094,{0x48,0x8b,0x05,0x9d,0xdd,0x77,0x0c});
			if(remote_axes_verified)utils::hook::call(remote_input_call,remote_input_stub);
			else console::error("[VR input] native remote-control command contract rejected\n");
			hooks_installed = true;
		}

		void pre_destroy() override
		{
			alive.store(false, std::memory_order_relaxed);
		}
	};
} // namespace vr::controllers

REGISTER_COMPONENT(vr::controllers::component)
