#include <std_include.hpp>
#include "carry_interaction.hpp"
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
		namespace hi=hand_interaction;
		namespace w=weapons;
		struct provider
		{
			void (*report)(){};
			void (*collect)(const hi::frame&){};
			void (*update)(){};
			void (*lifecycle)(bool){};
			unsigned report_rank,collect_rank,update_rank,lifecycle_rank;
			bool always_reconcile;
			unsigned reconciliation_rank;
		};
		// One domain inventory for every path. Ranks preserve the existing native
		// settlement order; incompatible grasps are resolved only by the arbiter.
		constexpr std::array providers{
			provider{
				.report=ladders::report_interactions,
				.collect=ladders::collect_interactions,
				.update=ladders::update_interactions,
				.lifecycle=ladders::lifecycle,
				.report_rank=13,
				.collect_rank=13,
				.update_rank=9,
				.lifecycle_rank=6,
				.always_reconcile=true,
				.reconciliation_rank=13
			},
			provider{
				.report=equipment::nightvision::report_interactions,
				.collect=equipment::nightvision::collect_interactions,
				.update=equipment::nightvision::update_interactions,
				.lifecycle=equipment::nightvision::lifecycle,
				.report_rank=10,
				.collect_rank=0,
				.update_rank=0,
				.lifecycle_rank=2,
				.always_reconcile=true,
				.reconciliation_rank=12
			},
			provider{
				.report=reload_items::report_interactions,
				.collect=reload_items::collect_interactions,
				.update=reload_items::update_interactions,
				.lifecycle=reload_items::lifecycle,
				.report_rank=0,
				.collect_rank=1,
				.update_rank=8,
				.lifecycle_rank=5,
				.always_reconcile=false,
				.reconciliation_rank=5
			},
			provider{
				.report=w::underbarrel::report_interactions,
				.collect=w::underbarrel::collect_interactions,
				.update=nullptr,
				.lifecycle=nullptr,
				.report_rank=6,
				.collect_rank=2,
				.update_rank=99,
				.lifecycle_rank=99,
				.always_reconcile=false,
				.reconciliation_rank=6
			},
			provider{
				.report=w::physical_reload::report_interactions,
				.collect=w::physical_reload::collect_interactions,
				.update=w::physical_reload::update_interactions,
				.lifecycle=w::physical_reload::update_lifecycle,
				.report_rank=1,
				.collect_rank=3,
				.update_rank=3,
				.lifecycle_rank=0,
				.always_reconcile=false,
				.reconciliation_rank=0
			},
			provider{
				.report=w::cylinder::report_interactions,
				.collect=w::cylinder::collect_interactions,
				.update=w::cylinder::update_interactions,
				.lifecycle=w::cylinder::update_lifecycle,
				.report_rank=2,
				.collect_rank=4,
				.update_rank=4,
				.lifecycle_rank=1,
				.always_reconcile=false,
				.reconciliation_rank=1
			},
			provider{
				.report=w::tube::report_interactions,
				.collect=w::tube::collect_interactions,
				.update=w::tube::update_interactions,
				.lifecycle=w::tube::update_lifecycle,
				.report_rank=3,
				.collect_rank=5,
				.update_rank=5,
				.lifecycle_rank=2,
				.always_reconcile=false,
				.reconciliation_rank=2
			},
			provider{
				.report=w::break_action::report_interactions,
				.collect=w::break_action::collect_interactions,
				.update=w::break_action::update_interactions,
				.lifecycle=w::break_action::update_lifecycle,
				.report_rank=4,
				.collect_rank=6,
				.update_rank=6,
				.lifecycle_rank=3,
				.always_reconcile=false,
				.reconciliation_rank=3
			},
			provider{
				.report=w::launcher::report_interactions,
				.collect=w::launcher::collect_interactions,
				.update=w::launcher::update_interactions,
				.lifecycle=w::launcher::update_lifecycle,
				.report_rank=5,
				.collect_rank=7,
				.update_rank=7,
				.lifecycle_rank=4,
				.always_reconcile=false,
				.reconciliation_rank=4
			},
			provider{
				.report=equipment::report_interactions,
				.collect=equipment::collect_interactions,
				.update=nullptr,
				.lifecycle=nullptr,
				.report_rank=7,
				.collect_rank=8,
				.update_rank=99,
				.lifecycle_rank=99,
				.always_reconcile=false,
				.reconciliation_rank=7
			},
			provider{
				.report=grenades::report_interactions,
				.collect=grenades::collect_interactions,
				.update=grenades::update_interactions,
				.lifecycle=grenades::lifecycle,
				.report_rank=8,
				.collect_rank=9,
				.update_rank=1,
				.lifecycle_rank=0,
				.always_reconcile=true,
				.reconciliation_rank=8
			},
			provider{
				.report=equipment::special::report_interactions,
				.collect=equipment::special::collect_interactions,
				.update=equipment::special::update_interactions,
				.lifecycle=equipment::special::lifecycle,
				.report_rank=9,
				.collect_rank=10,
				.update_rank=2,
				.lifecycle_rank=1,
				.always_reconcile=true,
				.reconciliation_rank=9
			},
			provider{
				.report=w::heartbeat::report_interactions,
				.collect=w::heartbeat::collect_interactions,
				.update=nullptr,
				.lifecycle=nullptr,
				.report_rank=11,
				.collect_rank=11,
				.update_rank=99,
				.lifecycle_rank=99,
				.always_reconcile=false,
				.reconciliation_rank=10
			},
			provider{
				.report=interaction::report_interactions,
				.collect=interaction::collect_interactions,
				.update=nullptr,
				.lifecycle=nullptr,
				.report_rank=12,
				.collect_rank=12,
				.update_rank=99,
				.lifecycle_rank=99,
				.always_reconcile=false,
				.reconciliation_rank=11
			}
		};
		constexpr bool valid_provider_order()
		{
			for(size_t i=0;i<providers.size();++i)
			{
				const auto& p=providers[i];
				if(!p.report || !p.collect || p.report_rank>=providers.size() || p.collect_rank>=providers.size() || p.reconciliation_rank>=providers.size())return false;
				if((p.update && p.update_rank>=providers.size()) || (p.lifecycle && p.lifecycle_rank>=providers.size()))return false;
				for(size_t j=0;j<i;++j)
				{
					const auto& q=providers[j];
					if(p.report_rank==q.report_rank || p.collect_rank==q.collect_rank || p.reconciliation_rank==q.reconciliation_rank)return false;
					if(p.update && q.update && p.update_rank==q.update_rank)return false;
					if(p.lifecycle && q.lifecycle && p.always_reconcile==q.always_reconcile && p.lifecycle_rank==q.lifecycle_rank)return false;
				}
			}
			return true;
		}
		static_assert(valid_provider_order(),"Each interaction phase must have an explicit, unique provider order");
		constexpr auto order(unsigned provider::* rank)
		{
			std::array<unsigned,providers.size()> result{};
			for(unsigned i=0;i<result.size();++i)
			{
				unsigned at=i;
				while(at && providers[result[at-1]].*rank>providers[i].*rank)
				{result[at]=result[at-1];--at;}
				result[at]=i;
			}
			return result;
		}
		constexpr auto reports=order(&provider::report_rank),collections=order(&provider::collect_rank),updates=order(&provider::update_rank),lifecycles=order(&provider::lifecycle_rank),reconciliation_reports=order(&provider::reconciliation_rank);
		void report_domains(bool reconciling=false)
		{
			for(auto i:reconciling?reconciliation_reports:reports)providers[i].report();
			if(reconciling){const auto held=w::carry::held_instances();hi::observe_carry(held);}
			else hi::observe_carry(w::carry::interaction_instances());
		}
		void reconcile(bool suspended,bool always)
		{
			for(auto i:lifecycles)
				if(providers[i].lifecycle && providers[i].always_reconcile==always)providers[i].lifecycle(suspended);
		}
		void run(w::carry::interaction_context& batch)
		{
			auto& f=batch.frame;const auto& input=batch.raw_input;
			hi::begin(f);report_domains();hi::synchronize(true);
			w::underbarrel::exchange_supply(f);
			for(auto i:collections)providers[i].collect(f);
			w::carry::collect_interactions();hi::resolve();
			for(auto i:updates)if(providers[i].update)providers[i].update();
			bool changed{};
			for(const auto& v:w::carry::interaction_instances())if(v.at==w::carry::location::held && v.owner.can_fire() && valid_hand(v.owner.support))
			{
				const auto magazine=w::physical_reload::current(v.id);
				if(magazine.active && magazine.definition && magazine.definition->interaction.support_magazine_catch && magazine.ammo.magazine_hand==v.owner.support)
					changed=w::carry::release_support(v.id)||changed;
			}
			if(changed)w::carry::publish_topology();
			w::underbarrel::update(f.input,w::carry::interaction_instances(),f.objects,f.wrists,f.body);
			changed=false;
			for(const auto& v:w::carry::interaction_instances())if(v.at==w::carry::location::held && v.owner.can_fire())
			{
				const auto module=w::underbarrel::current(v.id);if(!module.owns_support)continue;
				const auto off=hand(1-int(v.owner.rear));
				if(module.grip!=w::underbarrel::lease::none && input.squeeze[int(off)].down && !w::carry::interaction_hand_occupied(off))
					changed=w::carry::acquire_support(v.id,off)||changed;
				else if(module.grip==w::underbarrel::lease::none && v.owner.support==off)
					changed=w::carry::release_support(v.id)||changed;
			}
			if(changed)w::carry::publish_topology();
			w::carry::refresh_interaction_objects();
			unsigned equipment_available{};
			for(int h=0;h<2;++h)if(hi::has(hand(h),hi::domain::knife))equipment_available|=1u<<h;
			unsigned claimed=equipment::update(input,f.body,f.wrists,f.valid_hands,equipment_available,batch.edges.pressed,batch.edges.released,w::carry::interaction_instances(),f.objects);
			w::heartbeat::update(f.input,w::carry::interaction_instances(),f.objects,f.wrists,f.body.units_per_meter,f.valid_hands&~claimed);
			claimed=w::carry::apply_grips(claimed);
			interaction::update(f.input,f.body,w::carry::world_use_hands()&f.valid_hands&~claimed,batch.edges.pressed&~claimed,batch.edges.released,w::carry::pickup);
			w::carry::publish_interactions();
			w::launcher::support_feedback(batch.previous,w::carry::interaction_instances(),f.input,f.wrists,f.valid_hands,batch.edges.resumed);
			report_domains();hi::finish();w::carry::finish_interactions();
		}
		void update()
		{
			vehicles::reconcile_lifecycle();
			if(auto* batch=w::carry::prepare_interactions())
			{run(*batch);reconcile(hi::weapons_suspended(),true);return;}
			const bool suspended=hi::weapons_suspended();
			reconcile(suspended,true);reconcile(suspended,false);
			if(vehicles::update_interactions())return;
			hi::frame use_frame;const bool use=suspended && interaction::scripted_frame(use_frame);
			if(suspended && !use){hi::invalidate_input();interaction::suspend();}
			if(use && (use_frame.input.sequence!=hi::input_sequence() || use_frame.input.reference_generation!=hi::input_reference()))
			{
				hi::begin(use_frame,true);report_domains(true);hi::synchronize(true);
				interaction::collect_interactions(use_frame);hi::resolve();
				unsigned available{},pressed{},released{};
				for(int h=0;h<2;++h)
				{
					const auto actor=hand(h);const auto edge=hi::input(actor,hi::button::grip);
					if(hi::free(actor) || hi::has(actor,hi::domain::world))available|=1u<<h;
					if(edge.press)pressed|=1u<<h;if(edge.release)released|=1u<<h;
				}
				interaction::update(use_frame.input,use_frame.body,available&use_frame.valid_hands,pressed,released,[](const auto&,int){return false;});
				report_domains(true);hi::finish();return;
			}
			// Repeated input still settles native identities/escrow. It does not
			// start another acquisition batch or swallow deferred input releases.
			hi::begin_reconciliation();report_domains(true);hi::end_reconciliation();
		}
	}
	class component final:public component_interface
	{
		void post_unpack()override{scheduler::loop(update,scheduler::pipeline::server);}
	};
}
REGISTER_COMPONENT(vr::gameplay::interaction_coordinator::component)
