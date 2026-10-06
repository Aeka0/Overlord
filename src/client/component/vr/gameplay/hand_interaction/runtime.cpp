#include <std_include.hpp>
#include "runtime.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "loader/component_loader.hpp"
#include <mutex>
#include <sstream>
#include <utils/io.hpp>

namespace vr::gameplay::hand_interaction
{
	namespace
	{
		arbiter authority;input_history history;frame context;bool active{},suspended{true};
		std::mutex publication;std::array<session,8> published{};
		std::array<std::pair<hand,grasp>,8> witnesses{};size_t witness_count{};
		std::array<std::pair<hand,target>,8> commands{};size_t command_count{};
		std::array<decision,64> last_decisions{};size_t last_count{};std::uint64_t last_frame{};
		std::uint64_t frames{},overflows{},unplanned{};
		bool server()noexcept{return scheduler::is_executing(scheduler::pipeline::server);}
	}
	void begin(const frame& value,bool weapons_suspended)noexcept
	{
		if(!server())return;context=value;active=true;suspended=weapons_suspended;witness_count=0;command_count=0;
		history.update(context.input,true,context.input.sampled_at);authority.begin(context.input.sequence,context.input.reference_generation);++frames;
	}
	const frame* simulation()noexcept{return server() && active?&context:nullptr;}
	edges input(hand actor,button b)noexcept{return server() && active?history.get(actor,b):edges{};}
	void offer(candidate c)noexcept
	{
		if(!server() || !active || !vr::valid_hand(c.actor) || !(context.valid_hands&(1u<<unsigned(c.actor))))return;
		if(c.desired.destination.provider!=domain::world && history.get(c.actor,c.desired.maintained).release)return;
		authority.offer(c);
	}
	void select_supply(hand actor,domain provider,object_identity id)noexcept
	{if(server() && active)authority.select_supply(actor,object(provider,id));}
	bool exchange_supply(hand actor,target from,grasp to,supply_exchange_fn commit,void* data)noexcept
	{
		if(!server() || !active || !commit || !vr::valid_hand(actor) || !(context.valid_hands&(1u<<unsigned(actor))))return false;
		const auto trigger=history.get(actor,button::trigger);
		return trigger.down && trigger.armed && !trigger.release && authority.exchange_supply(actor,from,to,[&]{return commit(data);});
	}
	void resolve()noexcept
	{
		if(!server() || !active)return;
		// Reservations are private to this batch. Only witnessed native/domain
		// results survive finish() and become visible to render consumers.
		authority.resolve([](const candidate&){return true;});
		if(authority.overflow())++overflows;
	}
	bool granted(hand actor,domain provider,object_identity id,button source,role purpose)noexcept
	{
		if(!server() || !active)return false;
		for(const auto& d:authority.decisions())if(d.result==rejection::none && d.request.actor==actor && d.request.desired.destination.provider==provider && d.request.desired.destination.object==id &&
			(source==button::none || d.request.desired.maintained==source) && (purpose==role::none || d.request.desired.purpose==purpose))return true;
		return false;
	}
	std::array<session,8> current()noexcept
	{const std::lock_guard lock(publication);return published;}
	pose_plan pose(hand actor)noexcept{return server() && active?compose_pose(actor,authority.sessions()):compose_pose(actor,current());}
	bool permits(hand actor,domain provider,object_identity id,recipe pose,role purpose)noexcept
	{
		if(!vr::valid_hand(actor))return false;
		grasp requested{object(provider,id),purpose,button::trigger,pose,{}};
		const auto entries=server() && active?std::array<session,8>{}:current();
		const auto view=server() && active?authority.sessions():std::span<const session>{entries};
		for(const auto& s:view)if(s && s.actor==actor && s.held.destination.provider==provider && s.held.destination.object==id){requested=s.held;break;}
		for(const auto& s:view)if(s && s.actor==actor)
		{
			if(s.held.destination.provider==provider && s.held.destination.object==id)continue;
			if(s.settling || !compatible(s.held,requested))return false;
		}
		return true;
	}
	bool free(hand actor)noexcept
	{
		if(!vr::valid_hand(actor))return false;
		if(server() && active)return authority.empty(actor);
		for(const auto& s:current())if(s && s.actor==actor)return false;return true;
	}
	bool has(hand actor,domain provider)noexcept
	{
		const auto entries=server() && active?std::array<session,8>{}:current();const auto view=server() && active?authority.sessions():std::span<const session>{entries};
		for(const auto& s:view)if(s && s.actor==actor && s.held.destination.provider==provider)return true;return false;
	}
	bool allows_melee(hand actor)noexcept
	{
		const auto entries=server() && active?std::array<session,8>{}:current();const auto view=server() && active?authority.sessions():std::span<const session>{entries};
		return compose_pose(actor,view).melee;
	}
	void observed(hand actor,grasp held)noexcept
	{
		if(!server() || !active || !vr::valid_hand(actor) || !held.destination)return;
		for(size_t i=0;i<witness_count;++i)if(witnesses[i].first==actor && witnesses[i].second.destination==held.destination){witnesses[i].second=held;return;}
		if(witness_count<witnesses.size())witnesses[witness_count++]={actor,held};else ++overflows;
	}
	void observe_carry(std::span<const weapons::carry::instance> owned)noexcept
	{
		for(const auto& i:owned)if(i.at==weapons::carry::location::held)
		{
			if(vr::valid_hand(i.owner.rear))
			{
				// A script/engine inventory replacement wins over an old equipment
				// session. The equipment provider receives this same inventory frame.
				for(size_t n=0;n<witness_count;)if(witnesses[n].first==i.owner.rear && witnesses[n].second.destination.provider!=domain::carry)witnesses[n]=witnesses[--witness_count];else ++n;
				observed(i.owner.rear,{object(domain::carry,i.id,i.owner.attachment==weapons::control_attachment::moving?2:0),role::control,button::grip,recipe::single,capability::fire});
			}
			if(vr::valid_hand(i.owner.support))
			{
				bool module=false;for(size_t n=0;n<witness_count;++n)if(witnesses[n].first==i.owner.support && witnesses[n].second.destination.provider==domain::underbarrel && witnesses[n].second.purpose!=role::supply)module=true;
				if(!module)observed(i.owner.support,{object(domain::carry,i.id,1),role::support,button::grip,recipe::single,capability::aim});
			}
		}
	}
	void completed(hand actor,target value)noexcept
	{if(server() && active && command_count<commands.size())commands[command_count++]={actor,value};}
	void publish()noexcept
	{const std::lock_guard lock(publication);std::copy(authority.sessions().begin(),authority.sessions().end(),published.begin());}
	void synchronize(bool release_inputs)noexcept
	{
		if(!server() || !active)return;
		if(release_inputs)for(size_t n=0;n<witness_count;)
		{
			const auto& w=witnesses[n];
			if(w.second.destination.provider!=domain::carry && w.second.maintained==button::grip && history.get(w.first,button::grip).release)witnesses[n]=witnesses[--witness_count];else ++n;
		}
		authority.retain([](const session& s){for(size_t i=0;i<witness_count;++i)if(witnesses[i].first==s.actor && witnesses[i].second.destination==s.held.destination)return true;return false;});
		for(size_t i=0;i<witness_count;++i)if(!authority.observe(witnesses[i].first,witnesses[i].second))++unplanned;
		witness_count=0;
	}
	void finish()noexcept
	{
		if(!server() || !active)return;
		if(!authority.decisions().empty())
		{
			last_frame=context.input.sequence;
			last_count=authority.decisions().size();std::copy(authority.decisions().begin(),authority.decisions().end(),last_decisions.begin());
			for(size_t n=0;n<last_count;++n){auto& d=last_decisions[n];if(d.result!=rejection::none)continue;bool witnessed=false;
				for(size_t i=0;i<command_count;++i)if(commands[i].first==d.request.actor && commands[i].second==d.request.desired.destination)witnessed=true;
				for(size_t i=0;i<witness_count;++i)if(witnesses[i].first==d.request.actor && (witnesses[i].second.destination==d.request.desired.destination ||
					(d.request.desired.destination.provider==domain::world && witnesses[i].second.purpose==role::control)))witnessed=true;
				if(!witnessed)d.result=rejection::commit_failed;}
		}
		synchronize();publish();active=false;
	}
	void suspend(bool preserve_use_input)noexcept
	{if(!server())return;active=false;suspended=true;if(!preserve_use_input)history.invalidate();}
	bool weapons_suspended()noexcept{return suspended;}
	std::uint64_t input_sequence()noexcept{return context.input.sequence;}
	std::uint64_t input_reference()noexcept{return context.input.reference_generation;}
	void begin_reconciliation()noexcept{if(server()){active=true;witness_count=0;}}
	void end_reconciliation()noexcept{if(server()){synchronize();publish();active=false;}}
	void invalidate_input()noexcept{if(server())history.invalidate();}
	class component final:public component_interface
	{
		void post_unpack()override
		{
			command::add("vr_hand_interaction_status",[]{scheduler::once([]{
				std::ostringstream out;out<<"hand interaction frames="<<frames<<" overflows="<<overflows<<" incompatible observations="<<unplanned<<'\n';
				for(const auto& s:authority.sessions())if(s)out<<"session="<<s.id<<" hand="<<int(s.actor)<<" domain="<<name(s.held.destination.provider)<<" host="<<s.held.destination.object.value<<" generation="<<s.held.destination.object.generation<<" component="<<s.held.destination.component<<" binding="<<s.held.destination.binding<<" role="<<name(s.held.purpose)<<" recipe="<<unsigned(s.held.pose)<<'\n';
				for(size_t i=0;i<last_count;++i){const auto& d=last_decisions[i];out<<"frame="<<last_frame<<" candidate hand="<<int(d.request.actor)<<" domain="<<name(d.request.desired.destination.provider)<<" host="<<d.request.desired.destination.object.value<<" role="<<name(d.request.desired.purpose)<<" event="<<d.request.event<<" distance="<<d.request.distance<<" result="<<name(d.result)<<" blocking_session="<<d.blocking_session<<'\n';}
				const auto text=out.str();console::info("%s",text.c_str());scheduler::once([text]{utils::io::write_file_atomic("minidumps/overlord-hand-interaction.txt",text);},scheduler::pipeline::async);
			},scheduler::pipeline::server);});
		}
	};
}
REGISTER_COMPONENT(vr::gameplay::hand_interaction::component)
