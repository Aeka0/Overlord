#include <std_include.hpp>
#include "fixed_sniper.hpp"
#include "fixed_sniper_hand_aim.hpp"
#include "native_carry.hpp"
#include "../auxiliary_scene.hpp"
#include "../native_thermal.hpp"
#include "../settings.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "component/scripting.hpp"
#include "game/game.hpp"
#include "game/dvars.hpp"
#include "loader/component_loader.hpp"
#include <utils/io.hpp>

namespace vr::gameplay::fixed_sniper
{
	namespace
	{
		std::mutex mutex;
		state published;
		std::uint64_t next_epoch{},entity_generation{};
		controller input_control;
		hand_aim hand_control;
		game::dvar_t* hand_travel_dvar{},*hand_noise_dvar{};
		std::atomic_uint64_t hand_updates{};
		std::atomic_uint hand_mask{};
		bool installed{};
		std::atomic_bool alive{true};
		std::atomic_uint64_t commands{},shots{};
		template<class T> T read(const void* p,std::size_t offset)
		{T value{};std::memcpy(&value,static_cast<const std::byte*>(p)+offset,sizeof(value));return value;}
		void invalidate(){const std::lock_guard lock(mutex);published={};entity_generation=0;}
		void observe()
		{
			state next{};std::uint64_t generation{};
			if(alive && installed && head_pose_bridge::get_status().enabled && game::CL_IsCgameInitialized())
			{
				const auto* ps=reinterpret_cast<const game::playerState_s*>(game::g_entities[0].client);
				const int entity=ps?read<std::uint16_t>(ps,0x1e):-1;
				if(ps && mounted::attached(ps->e_flags) && entity>0 && entity<4000)
				{
					const auto* ent=&game::g_entities[entity];const auto token=read<unsigned>(ent,0x80)&511;
					const auto* definition=token?game::weapon_defs[token]:nullptr;
					const auto* name=definition?read<const char*>(definition,0):nullptr;
					if(name && accepts(ps->e_flags,entity,{name,strnlen_s(name,128)}) && read<std::uint16_t>(ent,0x10c)==1 && read<void*>(ent,0x138))
					{next.entity=entity;next.time=ps->commandTime;next.player=reinterpret_cast<std::uintptr_t>(ps);generation=weapons::native_carry::entity_key(entity).generation;}
				}
			}
			const std::lock_guard lock(mutex);
			if(next.entity>0)
			{
				const bool same=published.epoch && next.entity==published.entity && next.player==published.player &&
					next.time>=published.time && generation==entity_generation;
				next.epoch=same?published.epoch:++next_epoch;next.started=same?published.started:next.time;
			}
			published=next;entity_generation=generation;
		}
	}
	state current(const game::playerState_s* ps) noexcept
	{
		if(!alive || !installed || !game::CL_IsCgameInitialized())return {};
		if(!ps)ps=game::CG_GetPredictedPlayerState(0);
		state value;{const std::lock_guard lock(mutex);value=published;}
		if(!ps || !value.epoch || !mounted::attached(ps->e_flags) || read<std::uint16_t>(ps,0x1e)!=value.entity ||
			std::int64_t(ps->commandTime)<std::int64_t(value.started)-150)return {};
		return value;
	}
	float head_gain() noexcept
	{
		const auto* value=game::Dvar_FindVar(settings::scripted_head_gain.name);
		return value?value->current.value:settings::scripted_head_gain.default_value;
	}
	bool command(const controller_input::frame& input,bool gameplay,game::usercmd_s* cmd,float* angles,float deadzone,float speed) noexcept
	{
		const auto owner=current();
		const auto now=controller_input::clock::now();
		const auto value=input_control.consume(input,owner.epoch,gameplay,deadzone,speed,now);
		head_pose_bridge::tracking_reference reference;
		hand_context context;
		const bool valid=gameplay && owner.epoch && cmd && angles && head_pose_bridge::get_tracking_reference(reference) &&
			reference.generation==input.reference_generation;
		if(valid)
		{
			context.basis=reference.head_axis;
			static_assert(offsetof(game::refdef_t,fovY)==0x14);
			context.tan_half_y=game::refdef->fovY; // Native scene tangent, not the runtime HMD FOV.
			context.travel=hand_travel_dvar?hand_travel_dvar->current.value:hand_travel.default_value;
			context.noise=hand_noise_dvar?hand_noise_dvar->current.value:hand_noise.default_value;
			for(unsigned h=0;h<2;++h)
				if(input.runtime_grip[h].valid && head_pose_bridge::tracking_position(reference,input.runtime_grip[h].tracking.position_meters,context.positions[h]))
					context.tracked|=1u<<h;
		}
		const auto hand=hand_control.consume(input,owner.epoch,valid,context,now);
		hand_mask=hand.active;if(hand.pitch!=0 || hand.yaw!=0)++hand_updates;
		if(!owner.epoch || !cmd || !angles)return false;
		cmd->rightmove=0;
		// Keyboard zoom is still native. Controller X never becomes lateral travel.
		cmd->forwardmove=static_cast<char>(std::clamp(int(static_cast<signed char>(cmd->forwardmove))+int(std::lround(value.zoom*127)),-127,127));
		if(std::isfinite(angles[0]) && std::isfinite(angles[1]))
		{angles[0]=std::remainder(angles[0]+value.pitch+hand.pitch,360.f);angles[1]=std::remainder(angles[1]+value.yaw+hand.yaw,360.f);}
		if(value.fire){cmd->buttons|=1;++shots;}
		if(value.exit)cmd->buttons|=game::BUTTON_USE_RELOAD;
		++commands;return true;
	}
	void suspend_input() noexcept{input_control.reset();hand_control.reset();hand_mask=0;}
	class component final:public component_interface
	{
		void post_unpack() override
		{
			hand_travel_dvar=dvars::register_float(hand_travel.name,hand_travel.default_value,hand_travel.min,hand_travel.max,
				game::DVAR_FLAG_SAVED,"Hand translation in meters per vertical fixed-sniper view; larger values aim more slowly");
			hand_noise_dvar=dvars::register_float(hand_noise.name,hand_noise.default_value,hand_noise.min,hand_noise.max,
				game::DVAR_FLAG_SAVED,"Fixed-sniper hand translation noise radius in meters; spatial deadband without a smoothing tail");
			// Native PostFX bit 0 selects thermal; bit 1 removes the flat scope
			// stencil and skips the ordinary-color redraw. Only our private scope
			// view uses that mode; HMD world eyes and the native tail remain exact.
			installed=native_thermal::ready();
			if(!installed){console::error("[VR fixed sniper] native thermal contract rejected\n");return;}
			auxiliary_scene::screen_scope_epoch.store(+[]() noexcept{return current().epoch;});
			scheduler::loop(observe,scheduler::pipeline::server);
			scripting::on_level_start(invalidate);scripting::on_shutdown([](bool,bool after){if(!after)invalidate();});
			::command::add("vr_fixedSniper_status",[]
			{
				const auto s=current();
				const auto report=std::format("epoch={} entity={} commands={} shots={} head_gain={}\nhand_mask={} hand_updates={} hand_travel_m={} hand_deadzone_m={}\n",
					s.epoch,s.entity,commands.load(),shots.load(),head_gain(),hand_mask.load(),hand_updates.load(),hand_travel_dvar->current.value,hand_noise_dvar->current.value);
				console::info("[VR fixed sniper] %s",report.c_str());utils::io::write_file_atomic("minidumps/overlord-fixed-sniper-input.txt",report);
			});
		}
		void pre_destroy() override{alive=false;auxiliary_scene::screen_scope_epoch=nullptr;invalidate();}
	};
}
REGISTER_COMPONENT(vr::gameplay::fixed_sniper::component)
