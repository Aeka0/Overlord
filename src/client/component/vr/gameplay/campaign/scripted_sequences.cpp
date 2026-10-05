#include <std_include.hpp>
#include "cliffhanger/physical.hpp"
#include "../ladder_runtime.hpp"
#include "scripted_sequences.hpp"
#include "../scripted_camera_reference.hpp"
#include "../native_player_life.hpp"
#include "../native_carry.hpp"
#include "sequences/rappel.hpp"
#include "sequences/oilrig.hpp"
#include "sequences/breach.hpp"
#include "sequences/estate.hpp"
#include "sequences/favela.hpp"
#include "sequences/gulag.hpp"
#include "sequences/vehicle.hpp"
#include "sequences/sliding.hpp"
#include "sequences/ending.hpp"
#include "ending/runtime.hpp"
#include "sequences/trainer.hpp"
#include "sequences/cliffhanger.hpp"
#include "sequences/roadkill.hpp"
#include "sequences/dcemp.hpp"
#include "sequences/airport.hpp"
#include "../../settings.hpp"
#include "game/scripting/execution.hpp"
#include "../../head_pose_bridge.hpp"
#include <utils/native_memory.hpp>
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "component/scripting.hpp"
#include "game/game.hpp"
#include "game/dvars.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook_validation.hpp>
#include <utils/io.hpp>

namespace vr::gameplay::sequences
{
	namespace
	{
		std::mutex mutex;
		view published{};
		std::atomic_uint64_t generation{1}, failures{}, updates{};
		std::atomic_uint64_t load_generation{1},load_recenters{};
		game_view::load_recenter load_camera;
		std::uintptr_t player{};
		int last_time{};
		std::uint64_t observed_generation{};
		std::uint64_t position_generation{};
		game::dvar_t* head_gain{};
		bool native_input_contract()
		{
			// Flat capture: attackbuttonpressed reads e918|e90c bit 0;
			// ismeleeing reads player weapon state 14..16. The witnessed E edge
			// supplied usercmd bit 2 (0x4), before those native states appeared.
			constexpr std::uint8_t attack[]{0x8b,0x88,0x18,0xe9,0,0,0x0b,0x88,0x0c,0xe9,0,0,0xf6,0xc1,1};
			constexpr std::uint8_t melee[]{0x8b,0x80,0xc0,2,0,0,0x83,0xe8,0x0e,0x83,0xf8,2};
			std::array<std::uint8_t,sizeof(attack)> a{};a.fill(0xff);
			std::array<std::uint8_t,sizeof(melee)> b{};b.fill(0xff);
			return utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x1404B3A53),{attack,a.data(),a.size()}) &&
				utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x1404B6B28),{melee,b.data(),b.size()});
		}
		void invalidate()
		{
			++generation;
			{const std::lock_guard lock(mutex);published={};}
			head_pose_bridge::reset_game_view();
		}
		void tick()
		{
			view next{};
			bool observed{},alive{};auto load=load_generation.load();
			const auto head=head_pose_bridge::get_status();
			if (head.enabled && game::CL_IsCgameInitialized() && *game::levelEntityId)
			{
				const auto* ps=reinterpret_cast<const game::playerState_s*>(game::g_entities[0].client);
				if (ps)
				{
					try
					{
						const auto previous=latest();
						// Shared native rigs identify a breach on any map, including
						// af_caves after its separate rappel sequence has completed.
						const bool dead=player_life::dead(ps);
						// Airport's authored shooting sequence can report native death.
						// Only its verified linked ending body takes priority over the
						// generic free-look death camera, never ordinary airport combat.
						if(airport::supported())next=airport::observe();
						if(next.stage==phase::none)next=dead ? death_view() : breach::presentation(breach::observe());
						if(next.stage==phase::none)next=vehicle::observe();
						if(next.stage==phase::none && estate::supported())next=estate::observe(ps);
						if(next.stage==phase::none && favela::supported())next=favela::observe();
						if(next.stage==phase::none && gulag::supported())next=gulag::observe();
						if(next.stage==phase::none && ending::supported())
						{::vr::gameplay::ending::update();next=ending::observe();}
						if(next.stage==phase::none && trainer::supported())next=trainer::observe(ps);
						if(next.stage==phase::none && rappel::supported())
						{
							next.scene=scenario::rappel;
							next.stage=rappel::observe(previous.epoch!=0 && previous.scene==scenario::rappel && observed_generation==generation.load() && ps->commandTime>=last_time);
							next.camera=scene_cameras::rappel;next.instruction=rappel::instruction_key(next.stage);
							next.suspend_weapons=next.stage!=phase::none;
						}
						else if(next.stage==phase::none && oilrig::supported())
						{
							next.scene=scenario::oilrig;next.stage=oilrig::observe();
							next.camera=scene_cameras::oilrig_input;
							if(next.stage==phase::execution || next.stage==phase::recovery)next.camera=scene_cameras::oilrig_native;
							next.melee_mode=melee_delivery::polled;
							next.allow_movement=oilrig::moving(next.stage);next.allow_turn=oilrig::turning(next.stage);
							next.suspend_weapons=next.stage!=phase::none;
						}
						// Each adapter owns its control policy. A camera-only vehicle
						// scene must not suppress native stance-binding notifications.
						const scripting::entity actor{game::scr_entref_t{0,0}};
						alive=!dead && scripting::call<int>("isalive",{actor})!=0;
						if(dead && next.scene!=scenario::airport)
						{
							const bool same=previous.scene==scenario::death && previous.player==reinterpret_cast<std::uintptr_t>(ps) &&
								ps->commandTime>=previous.command_time && observed_generation==generation.load();
							next.position_epoch=same && previous.position_epoch ? previous.position_epoch : ++position_generation;
							next.camera.translation_gain=head_gain?head_gain->current.value:settings::scripted_head_gain.default_value;
						}
						else if((alive || next.scene==scenario::airport) && actor.call("islinked").as<int>())
						{
							const auto parent=actor.call("getlinkedparent");
							if(parent.is<scripting::entity>())
							{
								if(next.stage==phase::none && oilrig::supported())
									next=oilrig::observe_evacuation(parent.as<scripting::entity>());
								if(next.stage==phase::none && cliffhanger::supported())
									next=cliffhanger::observe(parent.as<scripting::entity>());
								if(next.stage==phase::none && roadkill::supported())
									next=roadkill::observe(parent.as<scripting::entity>(),ps->e_flags);
								if(next.stage==phase::none && dcemp::supported())
									next=dcemp::observe(actor,parent.as<scripting::entity>());
								if(next.stage==phase::none)
								{
									const auto slide=sliding::observe(actor,parent.as<scripting::entity>());
									if(slide.stage!=phase::none)
									{
										// Sliding owns the camera, not independently observed HUD state.
										const bool progress=next.dsm_progress;next=slide;next.dsm_progress=progress;
									}
								}
								next.linked_entity=parent.as<scripting::entity>().get_entity_reference().entnum;
								next.linked_object=parent.as<scripting::entity>().get_entity_id();
								next.linked_generation=weapons::native_carry::entity_key(next.linked_entity).generation;
								next.position_epoch=same_position_owner(previous,next,reinterpret_cast<std::uintptr_t>(ps),ps->commandTime,
									observed_generation==generation.load()) ? previous.position_epoch:++position_generation;
								if(next.scene==scenario::none)next.camera=ladders::owns_carrier(next.linked_entity)?scene_cameras::physical_ladder:scene_cameras::linked_gameplay;
								if(next.camera.translation==game_view::head_translation::attenuated)
									next.camera.translation_gain=head_gain?head_gain->current.value:settings::scripted_head_gain.default_value;
							}
						}
						if(next.scene==scenario::ending && !next.position_epoch)
						{
							// The playable approach is temporarily unlinked, but still uses
							// the same attenuated physical head translation policy.
							next.position_epoch=previous.scene==scenario::ending && previous.position_epoch && ps->commandTime>=previous.command_time ? previous.position_epoch:++position_generation;
							// Free locomotion must share ordinary command/HMD yaw. A
							// cinematic waiting for its link preserves heading until its
							// actual native tag can perform the requested entry alignment.
							if(next.camera.owns_rotation())next.camera=scene_cameras::ending_unlinked;
							next.rotation_tag=game_view::scripted_camera_tag::none;
							next.camera.translation_gain=head_gain?head_gain->current.value:settings::scripted_head_gain.default_value;
						}
						const auto current=reinterpret_cast<std::uintptr_t>(ps);
						if(player && (player!=current || ps->commandTime<last_time))load=++load_generation;
						if (player!=current || ps->commandTime<last_time || observed_generation!=generation.load() || (previous.epoch && previous.scene!=next.scene))
						{++generation;player=current;observed_generation=generation.load();}
						last_time=ps->commandTime;next.command_time=last_time;
						next.player=current;
						if (next.stage!=phase::none) next.epoch=generation.load();
						observed=true;
					}
					catch (const std::exception&)
					{
						++failures;
						// Loss of a script observation cannot grant weapons or a QTE.
						next=latest();if(next.epoch) {next.stage=phase::release;next.command_time=ps->commandTime;
								next.allow_movement=next.allow_turn=next.allow_world_use=false;next.suspend_weapons=true;next.hide_body_arms=next.retain_weapon=false;next.arms.mode=scripted_arms::control::authored;
								next.independent_hands=false;next.hidden_entities.fill(-1);next.instruction.reset();}
					}
				}
			}
			// Loading/rollback gets one reset only after valid live script and
			// tracking observations. A normal first frame consumes the load too,
			// so a later story sequence cannot cause an unrelated recenter.
			const bool scripted=next.position_epoch || (next.stage!=phase::none && next.stage!=phase::death);
			const auto current_load=load_generation.load();
			if(load_camera.consume(current_load,observed && alive && head.pose_available && !head.recenter_pending &&
				load==current_load,scripted))
			{head_pose_bridge::request_recenter(true);++load_recenters;}
			const std::lock_guard lock(mutex);
			if (next.stage!=phase::none)
			{
				if (published.stage==phase::none) next.epoch=++generation;
				next.started_time=next.epoch==published.epoch ? published.started_time : next.command_time;
			}
			else if(next.position_epoch)next.started_time=next.position_epoch==published.position_epoch?published.started_time:next.command_time;
			// Keep epoch stable through native weapon enable and rig handoff.
			observed_generation=generation.load();published=next;++updates;
		}
	}
	view latest() noexcept {const std::lock_guard lock(mutex);return published;}
	view for_player(const void* ps) noexcept
	{
		if (!ps) return {};
		const auto state=latest();
		if ((!state.epoch && !state.position_epoch) || (reinterpret_cast<std::uintptr_t>(ps)!=state.player && ps!=game::CG_GetPredictedPlayerState(0))) return {};
		int time{};
		if (!utils::native_memory::read_bytes(&time,static_cast<const std::byte*>(ps)+0x4c,sizeof(time))) return {};
		// A backwards timeline must not inherit a publication before on_level_start.
		// Do not expire body ownership on a slow frame: only native exit/lifecycle
		// evidence may restore hands. Prediction can precede the server at entry.
		return std::int64_t(time)>=std::int64_t(state.started_time)-150 ? state : view{};
	}
	bool owns_body(const void* ps) noexcept {const auto value=for_player(ps);return value.epoch && value.suspend_weapons;}
	bool owns_arms(const void* ps) noexcept {const auto value=for_player(ps);return value.epoch && value.arms.reserved();}
	bool independent_hands(const void* ps) noexcept {const auto value=for_player(ps);return value.epoch && value.independent_hands;}
	bool chest_equipment_visible() noexcept {return !for_player(game::CG_GetPredictedPlayerState(0)).hide_chest_equipment;}
	class component final:public component_interface
	{
		void post_unpack() override
		{
			const auto& setting=settings::scripted_head_gain;
			head_gain=dvars::register_float(setting.name,setting.default_value,setting.min,setting.max,game::DVAR_FLAG_SAVED,"Physical head translation gain during scripted camera ownership, including death");
			if (!native_input_contract()) {console::error("[VR sequence] native input contract rejected\n");return;}
			scripting::on_level_start([]{++load_generation;invalidate();});
			scripting::on_shutdown([](bool,bool after){if(!after)invalidate();});
			scheduler::loop(tick,scheduler::pipeline::server);
			command::add("vr_sequence_status",[] {
				const auto v=latest();
				const auto report=std::format("scene={} phase={} epoch={} native_time={} updates={} failures={} camera={} suspend_weapons={} move={} turn={} melee_delivery={}\n",
					int(v.scene),name(v.stage),v.epoch,v.command_time,updates.load(),failures.load(),game_view::name(v.camera),v.suspend_weapons,v.allow_movement,v.allow_turn,
					v.melee_mode==melee_delivery::polled ? "polled" : "press")+
					std::format("position_epoch={} linked_entity={} linked_object={} linked_generation={} head_gain={} retain_weapon={} hide_body_arms={} dsm_progress={} allow_world_use={} rotation_tag={}\n",
						v.position_epoch,v.linked_entity,v.linked_object,v.linked_generation,v.camera.translation_gain,v.retain_weapon,v.hide_body_arms,v.dsm_progress,v.allow_world_use,game_view::name(v.rotation_tag))+
					std::format("camera_head={} translation={} script={} axes={} source={} entry={} translation_limit={}\n",
						game_view::name(v.camera.head),game_view::name(v.camera.translation),game_view::name(v.camera.script),game_view::name(v.camera.axes),
						game_view::name(v.camera.source),game_view::name(v.camera.entry),v.camera.translation_limit)+
					std::format("camera_owner={} angle_min={},{},{} angle_max={},{},{}\n",v.camera.owns_rotation()?"script":"player",
						v.camera.limits.minimum[0],v.camera.limits.minimum[1],v.camera.limits.minimum[2],
						v.camera.limits.maximum[0],v.camera.limits.maximum[1],v.camera.limits.maximum[2])+
					std::format("body_arms_profile={} body_entity={} arms_control={} hands={}\n",int(v.arms.profile),v.arms.entity,int(v.arms.mode),v.arms.hands)+
					std::format("scripted_load_recenters={}\n",load_recenters.load())+
					camera_reference::status()+prompt_status()+oilrig::angle_status();
				console::info("[VR sequence] %s",report.c_str());
				utils::io::write_file_atomic("minidumps/h2-mod-vr-sequence.txt",report);
			});
		}
		void pre_destroy() override {invalidate();}
	};
}
REGISTER_COMPONENT(vr::gameplay::sequences::component)
