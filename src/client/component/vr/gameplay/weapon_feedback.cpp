#include <std_include.hpp>
#include "weapon_feedback.hpp"
#include "vehicle_runtime.hpp"
#include "break_action_runtime.hpp"
#include "launcher_runtime.hpp"
#include "weapon_interaction.hpp"
#include "native_weapon_sound.hpp"
#include "native_weapon_fx.hpp"
#include "physical_reload_runtime.hpp"
#include "cylinder_runtime.hpp"
#include "tube_runtime.hpp"
#include "underbarrel_runtime.hpp"
#include "weapon_carry_runtime.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "game/dvars.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <mutex>
#include <utils/io.hpp>

namespace vr::gameplay::weapons::feedback
{
	namespace
	{
		std::mutex mutex;
		std::array<event, 16> pending{};
		size_t count{};
		std::uint64_t overflow{};
		std::atomic_uint64_t played{}, missing{}, discarded{}, pulses{};
		std::atomic_bool alive{true};
		game::dvar_t *sound_enabled{}, *haptic_gain{};
		std::atomic<float> published_haptic_gain{};
		bool sound_ready{};
		void tick()
		{
			std::array<event, 16> batch;
			size_t size{};
			{ const std::lock_guard lock(mutex); size = count; batch = pending; count = 0; }
			if (!alive.load()) return;
			const auto* paused = game::Dvar_FindVar("cl_paused");
			const bool gameplay = game::CL_IsCgameInitialized() && *game::keyCatchers == 0 && paused && paused->current.integer == 0;
			const auto input = controller_input::latest();
			const auto now = clock::now();
			const float gain = haptic_gain ? haptic_gain->current.value : 0;
			published_haptic_gain.store(gain);
			for (size_t i = 0; i < size; ++i)
			{
				const auto& e = batch[i];
				const auto owner=e.vehicle ? (vehicles::presentation_allowed()?vehicles::latest().owner:hold{}) : carry::active() ? carry::held(e.owner.id()) : current_hold();
				if (!fresh(e, owner, input, gameplay, now)) { ++discarded; continue; }
				if(e.secondary_definition)
				{
					const auto module=underbarrel::current(owner.id());
					if(!module.active || module.fault || module.ammo.id.definition!=e.secondary_definition){++discarded;continue;}
				}
				if (e.melee_impact)
				{
					if (sound_enabled && sound_enabled->current.enabled)
					{
						if (sound_ready && native_weapon_sound::play_melee_impact(e.position)) ++played;else ++missing;
					}
					continue;
				}
				if(e.launcher_definition)
				{
					const auto state=launcher::current(owner.id());
					if(!state.active || state.fault || state.owner.instance_generation!=e.mechanical_instance || state.definition!=e.launcher_definition || state.reference!=e.reference)
					{++discarded;continue;}
				}
				else if (e.mechanical_instance && e.cylinder_definition)
				{
					const auto state=cylinder::current(owner.id());
					if (!state.active || state.fault || state.ammo.instance_generation!=e.mechanical_instance || state.definition!=e.cylinder_definition)
					{ ++discarded; continue; }
				}
				else if (e.mechanical_instance && e.tube_definition)
				{
					const auto state=tube::current(owner.id());
					if(!state.active || state.fault || state.ammo.instance_generation!=e.mechanical_instance || state.definition!=e.tube_definition)
					{++discarded;continue;}
				}
				else if(e.mechanical_instance && e.break_definition)
				{
					const auto state=break_action::current(owner.id());
					if(!state.active || state.fault || state.ammo.instance_generation!=e.mechanical_instance || state.definition!=e.break_definition)
					{++discarded;continue;}
				}
				else if (e.mechanical_instance)
				{
					const auto physical = physical_reload::current(owner.id());
					if (!physical.active || physical.fault || physical.ammo.instance_generation != e.mechanical_instance ||
						physical.definition != e.definition)
					{ ++discarded; continue; }
				}
				if(e.quick_reload_start)
				{
					if(sound_enabled && sound_enabled->current.enabled)
					{
						if(sound_ready && (e.vehicle?native_weapon_sound::play_quick_reload_start_at(e.position):native_weapon_sound::play_quick_reload_start(owner.weapon,e.position)))++played;else ++missing;
					}
					continue;
				}
				const auto p = pattern(e.kind);
				if (e.independent_shot || e.kind==mechanics::effect::case_eject) native_weapon_fx::play(e);
				const int rear = static_cast<int>(owner.holding_hand());
				const auto vibrate = [&](int hand, float amplitude) {
					if (amplitude * gain <= 0) return;
					controller_haptics::request(hand, {p.seconds, p.frequency, amplitude * gain, e.reference, e.at});
					++pulses;
				};
				vibrate(e.secondary_definition?1-rear:rear, p.rear_amplitude);
				if (e.kind != mechanics::effect::shot || (e.owner.support != hand::none && owner.support == e.owner.support))
					vibrate(e.secondary_definition?rear:1-rear, p.off_amplitude);
				const auto sound = (e.cylinder_definition || e.tube_definition || e.break_definition || e.launcher_definition) ? e.explicit_sound :
					e.definition ? e.definition->interaction_sound(e.kind) : sound_reference{};
				if (e.independent_shot && e.kind==mechanics::effect::shot && sound_enabled && sound_enabled->current.enabled)
				{
					if (sound_ready && (e.secondary_definition ? native_weapon_sound::play_module_shot(owner.weapon,e.secondary_definition,e.position) : native_weapon_sound::play_shot(owner.weapon,e.position))) ++played;else ++missing;
				}
				if(e.secondary_definition && e.explicit_sound.name && sound_enabled && sound_enabled->current.enabled)
				{
					if(sound_ready && native_weapon_sound::play_module(owner.weapon,e.secondary_definition,e.explicit_sound,e.position))++played;else ++missing;
				}
				if (sound.name && sound_enabled && sound_enabled->current.enabled)
				{
					const bool ok=sound_ready && (e.vehicle ? native_weapon_sound::play_vehicle(e.owner.weapon,sound,e.position) : e.launcher_definition ? native_weapon_sound::play(e.owner.weapon,*e.launcher_definition,sound,e.position) : e.break_definition ? native_weapon_sound::play(e.owner.weapon,e.break_definition->native_name,int(e.break_definition->ammunition.capacity),sound,e.position) : e.tube_definition ? native_weapon_sound::play(e.owner.weapon,*e.tube_definition,sound,e.position) : e.cylinder_definition ? native_weapon_sound::play(e.owner.weapon,
						*e.cylinder_definition,sound,e.position) :
						native_weapon_sound::play(e.owner.weapon,*e.definition,sound,e.position));
					if (ok) ++played;
					else ++missing;
				}
			}
			if (!gameplay) controller_haptics::clear();
		}
	}
	void publish(event value) noexcept
	{
		const std::lock_guard lock(mutex);
		if (count == pending.size())
		{
			std::move(pending.begin()+1, pending.end(), pending.begin());
			--count; ++overflow;
		}
		pending[count++] = value;
	}
	void carry_confirmation(hand actor,const controller_input::frame& input) noexcept
	{
		const auto gain=published_haptic_gain.load();
		if (!alive || !valid_hand(actor) || gain<=0 || !input.focused) return;
		controller_haptics::request(static_cast<int>(actor),{.022f,110.f,.14f*gain,input.reference_generation,input.sampled_at});
		++pulses;
	}
	void melee_confirmation(const hold& owner,const controller_input::frame& input,const std::array<float,3>& position) noexcept
	{
		event value;value.owner=owner;value.reference=input.reference_generation;value.at=input.sampled_at;
		value.position=position;value.melee_impact=true;publish(value);
	}
	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			sound_enabled = dvars::register_bool("vr_weaponInteractionSound", true, game::DVAR_FLAG_SAVED,
				"Native positional sounds for confirmed physical weapon interactions");
			haptic_gain = dvars::register_float("vr_weaponHapticScale", 1, 0, 2, game::DVAR_FLAG_SAVED,
				"Controller weapon feedback strength; zero disables vibration");
			sound_ready = native_weapon_sound::initialize();
			native_weapon_fx::initialize();
			command::add("vr_weapon_fx_status",[] {
				const auto text=native_weapon_fx::status();console::info("%s",text.c_str());
				scheduler::once([text]{utils::io::write_file_atomic("minidumps/h2-mod-vr-weapon-fx.txt",text);},scheduler::pipeline::async);
			});
			command::add("vr_weapon_feedback_status", [] {
				std::uint64_t dropped{}; { const std::lock_guard lock(mutex); dropped = overflow; }
				console::info("[VR weapon feedback] sound_contract=%d played=%llu unavailable=%llu stale=%llu queued_pulses=%llu overflow=%llu\n",
					sound_ready, played.load(), missing.load(), discarded.load(), pulses.load(), dropped);
				const auto h = controller_haptics::diagnostics();
				console::info("haptic_handles=%d/%d sent=%llu/%llu failed=%llu/%llu last_error=%d/%d\n",
					h.bound[0], h.bound[1], h.sent[0], h.sent[1], h.failed[0], h.failed[1], h.last_error[0], h.last_error[1]);
			});
			scheduler::loop(tick, scheduler::pipeline::main);
		}
		void pre_destroy() override { alive = false; controller_haptics::clear(); }
	};
}
REGISTER_COMPONENT(vr::gameplay::weapons::feedback::component)
