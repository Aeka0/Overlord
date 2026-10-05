#include <std_include.hpp>
#include "carry_interaction.hpp"
#include "interaction_schedule.hpp"
#include "hand_interaction/runtime.hpp"
#include "vehicle_runtime.hpp"
#include "physical_reload_runtime.hpp"
#include "cylinder_runtime.hpp"
#include "tube_runtime.hpp"
#include "break_action_runtime.hpp"
#include "launcher_runtime.hpp"
#include "reload_item_runtime.hpp"
#include "underbarrel_runtime.hpp"
#include "equipment_runtime.hpp"
#include "grenade_runtime.hpp"
#include "special_equipment_runtime.hpp"
#include "nightvision_runtime.hpp"
#include "heartbeat_runtime.hpp"
#include "world_interaction.hpp"
#include "ladder_runtime.hpp"
#include "component/scheduler.hpp"
#include "loader/component_loader.hpp"

namespace vr::gameplay::interaction_coordinator
{
	namespace
	{
		namespace hi = hand_interaction;
		namespace w = weapons;
		namespace schedule = interaction_schedule;
		struct provider
		{
			schedule::provider_id id;
			void (*report)(){};
			void (*collect)(const hi::frame&){};
			void (*settle)(){};
			void (*lifecycle)(bool){};
		};
		constexpr std::array providers{
		    provider{.id = schedule::provider_id::reload_items,
		             .report = reload_items::report_interactions,
		             .collect = reload_items::collect_interactions,
		             .settle = reload_items::update_interactions,
		             .lifecycle = reload_items::lifecycle},
		    provider{.id = schedule::provider_id::detachable_reload,
		             .report = w::physical_reload::report_interactions,
		             .collect = w::physical_reload::collect_interactions,
		             .settle = w::physical_reload::update_interactions,
		             .lifecycle = w::physical_reload::update_lifecycle},
		    provider{.id = schedule::provider_id::cylinder_reload,
		             .report = w::cylinder::report_interactions,
		             .collect = w::cylinder::collect_interactions,
		             .settle = w::cylinder::update_interactions,
		             .lifecycle = w::cylinder::update_lifecycle},
		    provider{.id = schedule::provider_id::tube_reload,
		             .report = w::tube::report_interactions,
		             .collect = w::tube::collect_interactions,
		             .settle = w::tube::update_interactions,
		             .lifecycle = w::tube::update_lifecycle},
		    provider{.id = schedule::provider_id::break_action_reload,
		             .report = w::break_action::report_interactions,
		             .collect = w::break_action::collect_interactions,
		             .settle = w::break_action::update_interactions,
		             .lifecycle = w::break_action::update_lifecycle},
		    provider{.id = schedule::provider_id::launcher_reload,
		             .report = w::launcher::report_interactions,
		             .collect = w::launcher::collect_interactions,
		             .settle = w::launcher::update_interactions,
		             .lifecycle = w::launcher::update_lifecycle},
		    provider{.id = schedule::provider_id::underbarrel,
		             .report = w::underbarrel::report_interactions,
		             .collect = w::underbarrel::collect_interactions},
		    provider{.id = schedule::provider_id::equipment,
		             .report = equipment::report_interactions,
		             .collect = equipment::collect_interactions},
		    provider{.id = schedule::provider_id::grenades,
		             .report = grenades::report_interactions,
		             .collect = grenades::collect_interactions,
		             .settle = grenades::update_interactions,
		             .lifecycle = grenades::lifecycle},
		    provider{.id = schedule::provider_id::mission_equipment,
		             .report = equipment::special::report_interactions,
		             .collect = equipment::special::collect_interactions,
		             .settle = equipment::special::update_interactions,
		             .lifecycle = equipment::special::lifecycle},
		    provider{.id = schedule::provider_id::nightvision,
		             .report = equipment::nightvision::report_interactions,
		             .collect = equipment::nightvision::collect_interactions,
		             .settle = equipment::nightvision::update_interactions,
		             .lifecycle = equipment::nightvision::lifecycle},
		    provider{.id = schedule::provider_id::heartbeat,
		             .report = w::heartbeat::report_interactions,
		             .collect = w::heartbeat::collect_interactions},
		    provider{.id = schedule::provider_id::world,
		             .report = interaction::report_interactions,
		             .collect = interaction::collect_interactions},
		    provider{.id = schedule::provider_id::ladders,
		             .report = ladders::report_interactions,
		             .collect = ladders::collect_interactions,
		             .settle = ladders::update_interactions,
		             .lifecycle = ladders::lifecycle},
		};
		static_assert(schedule::valid_bindings(providers),
		              "Interaction phases must cover every declared provider capability");
		void report_domains(schedule::reporting mode = schedule::reporting::acquisition)
		{
			const auto& phase = mode == schedule::reporting::acquisition ? schedule::acquisition_reports
			                                                             : schedule::reconciliation_reports;
			for (auto id : phase)
				providers[std::size_t(id)].report();
			if (mode == schedule::reporting::reconciliation)
			{
				const auto held = w::carry::held_instances();
				hi::observe_carry(held);
			}
			else
				hi::observe_carry(w::carry::interaction_instances());
		}
		template <std::size_t N>
		void reconcile_lifecycle(const std::array<schedule::provider_id, N>& phase, bool suspended)
		{
			for (auto id : phase)
				providers[std::size_t(id)].lifecycle(suspended);
		}
		void run(w::carry::interaction_context& batch)
		{
			auto& f = batch.frame;
			hi::begin(f);
			report_domains();
			hi::synchronize(true);
			w::underbarrel::exchange_supply(f);
			for (auto id : schedule::collections)
				providers[std::size_t(id)].collect(f);
			w::carry::collect_interactions();
			hi::resolve();
			for (auto id : schedule::settlements)
				providers[std::size_t(id)].settle();
			w::physical_reload::reconcile_carry_support();
			w::underbarrel::settle_interactions(f, batch.raw_input);
			w::carry::refresh_interaction_objects();
			unsigned claimed = equipment::settle_interactions(f, batch.raw_input, batch.edges);
			w::heartbeat::update(f.input,
			                     w::carry::interaction_instances(),
			                     f.objects,
			                     f.wrists,
			                     f.body.units_per_meter,
			                     f.valid_hands & ~claimed);
			claimed = w::carry::apply_grips(claimed);
			interaction::update(f.input,
			                    f.body,
			                    w::carry::world_use_hands() & f.valid_hands & ~claimed,
			                    batch.edges.pressed & ~claimed,
			                    batch.edges.released,
			                    w::carry::pickup);
			w::carry::publish_interactions();
			w::launcher::support_feedback(batch.previous,
			                              w::carry::interaction_instances(),
			                              f.input,
			                              f.wrists,
			                              f.valid_hands,
			                              batch.edges.resumed);
			report_domains();
			hi::finish();
			w::carry::finish_interactions();
		}
		void update()
		{
			vehicles::reconcile_lifecycle();
			if (auto* batch = w::carry::prepare_interactions())
			{
				run(*batch);
				reconcile_lifecycle(schedule::continuous_lifecycle, hi::weapons_suspended());
				return;
			}
			const bool suspended = hi::weapons_suspended();
			reconcile_lifecycle(schedule::continuous_lifecycle, suspended);
			reconcile_lifecycle(schedule::idle_weapon_lifecycle, suspended);
			if (vehicles::update_interactions())
				return;
			hi::frame use_frame;
			const bool use = suspended && interaction::scripted_frame(use_frame);
			if (suspended && !use)
			{
				hi::invalidate_input();
				interaction::suspend();
			}
			if (use && (use_frame.input.sequence != hi::input_sequence() ||
			            use_frame.input.reference_generation != hi::input_reference()))
			{
				hi::begin(use_frame, true);
				report_domains(schedule::reporting::reconciliation);
				hi::synchronize(true);
				interaction::collect_interactions(use_frame);
				hi::resolve();
				unsigned available{}, pressed{}, released{};
				for (int h = 0; h < 2; ++h)
				{
					const auto actor = hand(h);
					const auto edge = hi::input(actor, hi::button::grip);
					if (hi::free(actor) || hi::has(actor, hi::domain::world))
						available |= 1u << h;
					if (edge.press)
						pressed |= 1u << h;
					if (edge.release)
						released |= 1u << h;
				}
				interaction::update(use_frame.input,
				                    use_frame.body,
				                    available & use_frame.valid_hands,
				                    pressed,
				                    released,
				                    [](const auto&, int) { return false; });
				report_domains(schedule::reporting::reconciliation);
				hi::finish();
				return;
			}
			// Repeated input still settles native identities/escrow. It does not
			// start another acquisition batch or swallow deferred input releases.
			hi::begin_reconciliation();
			report_domains(schedule::reporting::reconciliation);
			hi::end_reconciliation();
		}
	}
	class component final : public component_interface
	{
		void post_unpack() override
		{
			scheduler::loop(update, scheduler::pipeline::server);
		}
	};
}
REGISTER_COMPONENT(vr::gameplay::interaction_coordinator::component)
