#include <std_include.hpp>
#include "native_scripted_control.hpp"
#include "independent_fire_runtime.hpp"
#include "weapon_recoil.hpp"
#include "independent_fire_clock.hpp"
#include "native_ballistics.hpp"
#include "native_carry_model.hpp"
#include "native_carry.hpp"
#include "weapon_instance_cache.hpp"
#include "manual_feed_boundary.hpp"
#include "weapon_feedback.hpp"
#include "weapon_interaction.hpp"
#include "empty_hands_native.hpp"
#include "aim_assist.hpp"
#include "component/vr/gameplay/hands/pose_math.hpp"
#include "component/scheduler.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/io.hpp>

namespace vr::gameplay::weapons::independent_fire
{
	 namespace
	 {
		std::atomic_bool initialized{},alive{true};
		std::mutex gate_mutex;
		bool gate{};controller_input::clock::time_point gate_at{};
		struct instance_clock
		{
			carry::identity id{};shot_clock clock{};int consecutive{},last_shot{};
			std::uint64_t attempts{},shots{};const char* reason{"waiting for neutral trigger"};
			native_ballistics::outcome outcome{};int before{},after{};
		};
		instance_cache<instance_clock,carry::inventory::capacity+128> instances;
		const void* player{};int last_command{};std::uint64_t same_tick_pairs{};
		bool dual()
		{
			if (!alive || !carry::active() || !firing_enabled()) return false;
			const auto held=carry::held_instances();return held[0].id && held[1].id && held[0].id!=held[1].id;
		}
		bool independent()
		{
			if (!alive || !carry::active() || !firing_enabled()) return false;
			return dual() || (hands::owned_native::active() && carry::current_hold().weapon);
		}
		bool settle(const native_ammunition::snapshot& before,const native_ammunition::snapshot& after,bool sustained) noexcept
		{
			const auto* ps=reinterpret_cast<const game::playerState_s*>(game::g_entities[0].client);if (!ps) return false;
			manual_feed::consumed(ps,ps->commandTime,before.weapon,false,1,0,before,after,sustained);
			const auto magazine=physical_reload::current(before.id());
			const auto revolver=cylinder::current(before.id());
			const auto shotgun=tube::current(before.id());
			const auto hinged=break_action::current(before.id());
			return (!magazine.active || (!magazine.fault && mechanics::native_ammo(magazine.ammo).loaded==after.loaded)) &&
				(!revolver.active || (!revolver.fault && revolver.ammo.live==after.loaded)) &&
				(!shotgun.active || (!shotgun.fault && tube::native_ammo(shotgun.ammo).loaded==after.loaded)) &&
				(!hinged.active || (!hinged.fault && break_action::native_ammo(hinged.ammo).loaded==after.loaded));
		}
	 }
	void command(bool gameplay) noexcept
	{const std::lock_guard lock(gate_mutex);gate=gameplay;gate_at=controller_input::clock::now();}
	bool owns_native(const void* ps) noexcept
	{
		if (native_ammunition::local_role(ps)<0) return false;
		if (sequences::airport::opening::active()) return true;
		if (mounted::attached(static_cast<const game::playerState_s*>(ps)->e_flags) || !independent()) return false;
		// Owned DObjs do not imply an independent ballistic implementation. The
		// projected launcher needs native +attack for automatic ADS and lock-on;
		// a bullet weapon in the other hand can still use its independent clock.
		return suppress_native_attack(controller_fire_delivery(carry::current_hold().weapon),true);
	}
	void update()
	{
		if (!scheduler::is_executing(scheduler::pipeline::server) || !game::CL_IsCgameInitialized()) return;
		const auto* ps=reinterpret_cast<const game::playerState_s*>(game::g_entities[0].client);if (!ps) return;
		if (player!=ps || ps->commandTime<last_command) {instances={};player=ps;}
		const int command_time=ps->commandTime;last_command=command_time;
		const auto input=controller_input::latest();const auto now=controller_input::clock::now();
		bool command_enabled;
		{const std::lock_guard lock(gate_mutex);command_enabled=gate && now>=gate_at && now-gate_at<=150ms;}
		const auto head=head_pose_bridge::get_status();
		const auto* paused=game::Dvar_FindVar("cl_paused");
		const bool enabled=initialized && independent() && scripted_control::firing_allowed(ps) && command_enabled && input.focused && head.enabled && head.pose_available &&
			!head.recenter_pending && head.recenter_count==input.reference_generation && paused && !paused->current.integer &&
			!*game::keyCatchers && !(ps->e_flags&0x103000) && now>=input.sampled_at && now-input.sampled_at<=150ms;
		const auto held=carry::held_instances();
		instances.retain([](weapon_identity id){return carry::contains(id) || native_carry::tracks(id);});
		for (auto& entry:instances.entries())
		{
			auto& value=entry.value;
			if (value.id && (!enabled || (value.id!=held[0].id && value.id!=held[1].id))) value.clock.cancel();
		}
		if (!enabled) return;
		// Stable instance order, never concurrent engine calls. One side's empty
		// feed or covered muzzle does not prevent the other side's transaction.
		auto ordered=held;if (ordered[0].id.generation>ordered[1].id.generation) std::swap(ordered[0],ordered[1]);
		unsigned emitted_this_tick{};
		for (const auto& item:ordered)
		{
			auto* clock=instances.acquire(item.id);if (!clock) continue;
			auto& value=*clock;if (value.id!=item.id) {value={};value.id=item.id;}
			const auto scene=carry::firing_scene(item.id);
			const auto owner=carry::held(item.id);
			if (controller_fire_delivery(item.id.weapon)!=fire_delivery::bullets)
			{value.clock.cancel();value.reason="weapon uses native projectile or unsupported delivery";continue;}
			const bool fresh=scene.has_muzzle && owner.can_fire() && owner.revision==item.owner.revision &&
				scene.reference==input.reference_generation && scene.sequence && now>=scene.at && now-scene.at<=100ms;
			timing t;
			if (!native_ammunition::exclusive_loaded_feed(ps,item.id.weapon))
			{value.clock.cancel();value.reason="aliased or invalid loaded feed";continue;}
			if (!native_ballistics::firing_timing(item.id.weapon,value.consecutive,t))
			{value.clock.cancel();value.reason="unsupported native firing timing";continue;}
			const auto manual=tube::current(item.id);
			if(manual.active && manual.definition && tube::levered(manual.definition->ammunition))t=mechanical_cycle_timing();
			if (!value.clock.due(input,owner,true,fresh,now,command_time,t))
			{value.reason=fresh ? "waiting for trigger/cadence" : "waiting for current weapon pose";continue;}
			++value.attempts;
			const auto ammo=native_ammunition::observe_carried(ps,item.id);
			if (!physical_reload::allow_owned_shot(owner,ammo) || !cylinder::allow_owned_shot(owner,ammo) || !tube::allow_owned_shot(owner,ammo) || !break_action::allow_owned_shot(owner,ammo))
			{value.clock.rejected(command_time);value.reason="physical feed not ready";continue;}
			const auto world=hands::pose_math::compose(scene.gun,scene.muzzle);
			shot_geometry geometry{hands::rotate(world.rotation,{1,0,0}),hands::rotate(world.rotation,{0,-1,0}),
				hands::rotate(world.rotation,{0,0,1}),world.position};
			const auto* assist=game::Dvar_FindVar("vr_aimAssistStrength");
			(void)aim_assist::apply(geometry,assist ? assist->current.value : 0.f,0,aim_assist::shot_route::independent);
			// Revalidate after A's native damage/scripts before B's debit.
			if (game::g_entities[0].client!=reinterpret_cast<const game::gclient_s*>(ps) || ps->commandTime!=command_time ||
				!independent() || !scripted_control::firing_allowed(ps) || carry::held(item.id).revision!=owner.revision)
			{value.clock.cancel();continue;}
			const auto posture=controller_input::native_posture(ps->pm_flags);
			const auto fired=native_ballistics::fire_owned(item.id,geometry,command_time,settle);
			const bool emitted=fired.status==native_ballistics::outcome::emitted;
			value.clock.settled(command_time,t,emitted);value.outcome=fired.status;
			value.before=fired.before.loaded;value.after=fired.after.loaded;
			value.reason=emitted ? "independent native shot committed" : "native shot rejected";
			if (!emitted) continue;
			recoil::confirmed_shot(owner,input.reference_generation,now,posture);
			++value.shots;++emitted_this_tick;value.consecutive=command_time-value.last_shot>150 ? 1 : std::min(value.consecutive+1,31);value.last_shot=command_time;
			feedback::event feedback{mechanics::effect::shot,owner,input.reference_generation,now,world.position};
			feedback.independent_shot=true;feedback.muzzle=world;feedback.last_shot=fired.after.loaded==0;
			if (scene.ejection.valid && !cylinder::current(item.id).active && !break_action::current(item.id).active)
			{
				feedback.brass=hands::pose_math::compose(scene.gun,scene.ejection.local);
				feedback.has_brass=true;
			}
			else if (!scene.authored && !cylinder::current(item.id).active && !break_action::current(item.id).active)
			{
				const auto model=native_carry::geometry(item.id.weapon);
				if (model.valid && model.has_muzzle && model.has_brass)
				{
				// World/view receivers have different roots. Their muzzle markers
				// provide the same bridge used by carried-model presentation. A left
				// grip changes the gun pose, never the real side of its ejection port.
				feedback.brass=hands::pose_math::compose(world,hands::pose_math::compose(hands::pose_math::inverse(model.muzzle),model.brass));
				feedback.has_brass=true;
				}
			}
			feedback::publish(feedback);
		}
		if (emitted_this_tick==2) ++same_tick_pairs;
	}
	class component final:public component_interface
	{
		void post_unpack() override
		{
			initialized=native_ballistics::initialize();
			command::add("vr_dual_fire_status",[] {scheduler::once([] {
				std::ostringstream out;out << "initialized=" << initialized << " dual=" << dual() << " command_time=" << last_command << " same_tick_pairs=" << same_tick_pairs << '\n';
				for (const auto& entry:instances.entries()) if (const auto& v=entry.value;v.id) out << "weapon=" << v.id.weapon << " instance=" << v.id.generation << " attempts=" << v.attempts
					<< " shots=" << v.shots << " last_command=" << v.last_shot << " ammo=" << v.before << "->" << v.after << " outcome=" << int(v.outcome) << " reason=" << v.reason << '\n';
				out<<aim_assist::status();
				const auto text=out.str();console::info("%s",text.c_str());scheduler::once([text] {
					utils::io::write_file_atomic("minidumps/overlord-dual-fire.txt",text);},scheduler::pipeline::async);
			},scheduler::pipeline::server);});
		}
		void pre_destroy() override {alive=false;}
	};
}
REGISTER_COMPONENT(vr::gameplay::weapons::independent_fire::component)
