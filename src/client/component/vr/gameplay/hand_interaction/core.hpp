#pragma once
#include "../../hand.hpp"
#include "object_identity.hpp"
#include "../../controller_input.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <string_view>

namespace vr::gameplay::hand_interaction
{
	using vr::hand;
		using clock=controller_input::clock;
	enum class domain : unsigned { none, carry, magazine, cylinder, tube, hinge, underbarrel, knife, sensor, world, tactical, gesture, grenade, special, launcher, reload_item, vehicle, ladder };
	enum class role : unsigned { none, control, support, foregrip, firing, part, supply, knife, sensor, world, tactical };
	enum class button : unsigned { none, grip, trigger, secondary };
	enum class capability : unsigned { none=0, aim=1, action=2, fire=4, melee=8 };
	constexpr capability operator|(capability a,capability b)noexcept{return capability(unsigned(a)|unsigned(b));}
	constexpr bool has(capability a,capability b)noexcept{return (unsigned(a)&unsigned(b))==unsigned(b);}
	struct target
	{
		domain provider{};object_identity object{};unsigned component{};std::uint64_t binding{};
		bool operator==(const target&)const=default;
		explicit operator bool()const noexcept
		{
			// World targets store a native entity index here, not a weapon token.
			// Initial map entities legitimately have generation zero; freeing the
			// entity increments it. Preserve that identity for native live checks.
			if(provider==domain::world)return object.value>0 && object.value<4000;
			// Head equipment has no native firearm token. Its generation is the
			// tracking reference, so reset/checkpoint data cannot keep a grasp.
			if(provider==domain::gesture)return !object.value && object.generation!=0;
			return provider!=domain::none && object.value && object.generation;
		}
	};
	inline target world_object(std::uint32_t entity,std::uint64_t generation) noexcept
	{return {domain::world,{entity,generation},0,0};}
	inline target head_gesture_object(std::uint64_t reference) noexcept
	{return {domain::gesture,{0,reference},0,0};}
	// A recipe is permission for a specific physical combination, not a global
	// exemption for a "busy" hand. Its identity stays fixed until pinch release.
	enum class recipe : unsigned { single, knife_magazine, knife_part };
	struct grasp
	{
		target destination{};role purpose{};button maintained{};recipe pose{};capability abilities{};
		bool operator==(const grasp&)const=default;
	};
	struct session
	{
		std::uint64_t id{},reference{},started{},event{};
		hand actor{hand::none};grasp held{};bool settling{};
		explicit operator bool()const noexcept{return id!=0;}
	};
	inline bool compatible(const grasp& a,const grasp& b)noexcept
	{
		if(a==b)return true;
		const grasp* knife=&a;const grasp* pinch=&b;
		if(b.purpose==role::knife)std::swap(knife,pinch);
		return knife->purpose==role::knife && knife->maintained==button::grip && pinch->maintained==button::trigger &&
			((pinch->pose==recipe::knife_magazine && pinch->purpose==role::supply && pinch->destination.provider==domain::magazine) ||
			 (pinch->pose==recipe::knife_part && pinch->purpose==role::part && pinch->destination.provider==domain::magazine));
	}
	// A rear-hand release can exchange this exact support lease for its own
	// magazine. This is a replacement, never permission to share the hand.
	inline bool support_magazine_transfer(const grasp& from,const grasp& to)noexcept
	{
		return from.destination.provider==domain::carry && from.purpose==role::support &&
			from.destination.component==1 && from.maintained==button::grip &&
			to.destination.provider==domain::magazine && to.purpose==role::supply &&
			to.maintained==button::trigger && to.pose==recipe::single && from.destination.object==to.destination.object;
	}
	inline bool supply_exchange(const grasp& from,const grasp& to)noexcept
	{
		return from.purpose==role::supply && to.purpose==role::supply && from.maintained==button::trigger && to.maintained==button::trigger &&
			from.pose==recipe::single && to.pose==recipe::single && from.destination.object==to.destination.object &&
			((from.destination.provider==domain::magazine && to.destination.provider==domain::underbarrel) ||
			 (from.destination.provider==domain::underbarrel && to.destination.provider==domain::magazine));
	}
	struct edges {bool press{},release{},down{},armed{};std::uint64_t event{};};
	inline domain supply_selection(domain held,edges trigger,edges grip,bool waist,bool smart=false)noexcept
	{
		if(!waist || !trigger.down || !trigger.armed || trigger.release || !grip.armed)return domain::none;
		if(smart)
		{
			if(!((grip.press && grip.down && !grip.release) || (grip.release && !grip.down && !grip.press)))return domain::none;
			return held==domain::magazine ? domain::underbarrel : held==domain::underbarrel ? domain::magazine : domain::none;
		}
		if(held==domain::magazine && grip.press && grip.down)return domain::underbarrel;
		if(held==domain::underbarrel && grip.release && !grip.down)return domain::magazine;
		return domain::none;
	}
	class input_history
	{
		struct cursor {std::uint64_t generation{},presses{},releases{};bool seen{},armed{};std::uint64_t event{};};
		std::array<std::array<cursor,3>,2> cursors_{};
		std::array<std::array<edges,3>,2> frame_{};
		std::uint64_t sequence_{},reference_{},serial_{},continuity_seen_{};
		controller_input::consumer_continuity continuity_;
	public:
		void invalidate()noexcept{cursors_={};frame_={};sequence_=reference_=continuity_seen_=0;continuity_={};}
		bool update(const controller_input::frame& input,bool gameplay,clock::time_point now)noexcept
		{
			const bool fresh=gameplay && input.focused && input.sequence && input.reference_generation && now>=input.sampled_at && now-input.sampled_at<=std::chrono::milliseconds(150);
			if(fresh && input.sequence==sequence_ && input.reference_generation==reference_ && input.continuity_generation==continuity_seen_)return false;
			const bool reset=continuity_.update(input,now) || reference_!=input.reference_generation || input.sequence<sequence_;
			frame_={};
			for(unsigned h=0;h<2;++h)for(unsigned b=0;b<3;++b)
			{
				const auto& value=b==2?input.secondary[h]:b?input.trigger[h]:input.squeeze[h];auto& c=cursors_[h][b];auto& e=frame_[h][b];
				const bool changed=reset || !c.seen || value.generation!=c.generation || value.presses<c.presses || value.releases<c.releases;
				e.down=fresh && value.active && value.down;
				if(!fresh || !value.active || changed)
				{
					const bool released=!changed && value.releases>c.releases;
					c={value.generation,value.presses,value.releases,fresh && value.active,fresh && value.active && !value.down};
					// A proven physical release ends its session even when pose is lost.
					e.release=released;
				}
				else
				{
					e.release=value.releases>c.releases;
					e.press=c.armed && value.presses>c.presses;
					if(e.press)c.event=++serial_;
					if(!value.down)c.armed=true;
					c.presses=value.presses;c.releases=value.releases;
				}
				e.armed=c.armed;e.event=c.event;
			}
			sequence_=input.sequence;reference_=input.reference_generation;continuity_seen_=input.continuity_generation;return true;
		}
		edges get(hand h,button b)const noexcept
		{return vr::valid_hand(h) && b>=button::grip && b<=button::secondary?frame_[unsigned(h)][unsigned(b)-1]:edges{};}
	};
	enum class rejection : unsigned { none, invalid, no_edge, incompatible, occupied_target, lower_priority, capacity, commit_failed, wrong_route };
	struct candidate
	{
		hand actor{hand::none};grasp desired{};std::uint64_t event{};
		unsigned priority{};float distance{},orientation{};
		// Some domains reuse existing controllers as mechanical planners. They
		// must present already validated input intent, never infer it by order.
		bool intent{},exclusive_target{true};
		target replaces{}; // Only the reviewed support-to-magazine transfer is admitted.
	};
	struct decision {candidate request{};rejection result{rejection::invalid};std::uint64_t session_id{},blocking_session{};};
	inline std::string_view name(domain value)noexcept
	{constexpr std::array names{"none","carry","magazine","cylinder","tube","hinge","underbarrel","knife","sensor","world","tactical","gesture","grenade","special","launcher","reload_item","vehicle","ladder"};return unsigned(value)<names.size()?names[unsigned(value)]:"invalid";}
	inline std::string_view name(role value)noexcept
	{constexpr std::array names{"none","control","support","foregrip","firing","part","supply","knife","sensor","world","tactical"};return unsigned(value)<names.size()?names[unsigned(value)]:"invalid";}
	inline std::string_view name(rejection value)noexcept
	{constexpr std::array names{"accepted","invalid","no new edge","incompatible composition","target owned","lower priority","capacity exceeded","domain rejected","different supply selected"};return unsigned(value)<names.size()?names[unsigned(value)]:"invalid";}
	class arbiter
	{
		std::array<session,8> sessions_{};
		std::array<decision,64> decisions_{};size_t count_{};
		std::uint64_t serial_{},sequence_{},reference_{};bool overflow_{};
		std::array<target,2> supplies_{};
		std::array<std::array<std::uint64_t,2>,2> consumed_events_{};
		static bool before(const candidate& a,const candidate& b)noexcept
		{
			const bool av=std::isfinite(a.distance) && std::isfinite(a.orientation),bv=std::isfinite(b.distance) && std::isfinite(b.orientation);
			if(av!=bv)return av;
			if(a.priority!=b.priority)return a.priority<b.priority;
			if(av && a.distance!=b.distance)return a.distance<b.distance;
			if(av && a.orientation!=b.orientation)return a.orientation>b.orientation;
			const auto& x=a.desired.destination;const auto& y=b.desired.destination;
			if(x.object.value!=y.object.value)return x.object.value<y.object.value;
			if(x.object.generation!=y.object.generation)return x.object.generation<y.object.generation;
			if(x.provider!=y.provider)return x.provider<y.provider;
			if(x.component!=y.component)return x.component<y.component;
			if(x.binding!=y.binding)return x.binding<y.binding;
			if(a.desired.purpose!=b.desired.purpose)return a.desired.purpose<b.desired.purpose;
			if(a.actor!=b.actor)return a.actor<b.actor;
			if(a.desired.pose!=b.desired.pose)return a.desired.pose<b.desired.pose;
			if(a.desired.maintained!=b.desired.maintained)return a.desired.maintained<b.desired.maintained;
			if(a.desired.abilities!=b.desired.abilities)return a.desired.abilities<b.desired.abilities;
			return a.event<b.event;
		}
	public:
		void begin(std::uint64_t sequence,std::uint64_t reference)noexcept
		{sequence_=sequence;reference_=reference;count_=0;overflow_=false;supplies_={};}
		void select_supply(hand actor,target selected)noexcept
		{if(vr::valid_hand(actor))supplies_[unsigned(actor)]=selected;}
		std::span<const session> sessions()const noexcept{return sessions_;}
		std::span<const decision> decisions()const noexcept{return {decisions_.data(),count_};}
		bool overflow()const noexcept{return overflow_;}
		bool empty(hand h)const noexcept
		{for(const auto& s:sessions_)if(s && s.actor==h)return false;return true;}
		bool permits(hand h,const grasp& g)const noexcept
		{if(!vr::valid_hand(h))return false;for(const auto& s:sessions_)if(s && s.actor==h && (s.settling || !compatible(s.held,g)))return false;return true;}
		const session* find(hand h,const target& t)const noexcept
		{for(const auto& s:sessions_)if(s && s.actor==h && s.held.destination==t)return &s;return nullptr;}
		bool release(std::uint64_t id,bool settled)noexcept
		{for(auto& s:sessions_)if(s.id==id && s){if(settled)s={};else s.settling=true;return settled;}return false;}
		template<class Commit> bool exchange_supply(hand actor,target from,grasp to,Commit&& commit)noexcept
		{
			const auto* old=find(actor,from);
			if(!sequence_ || !reference_ || !old || old->settling || old->reference!=reference_ || !to.destination || !supply_exchange(old->held,to) || serial_==UINT64_MAX)return false;
			for(const auto& s:sessions_)if(s && &s!=old && (s.actor==actor || s.held.destination==to.destination))return false;
			if(!commit())return false;
			for(auto& s:sessions_)if(&s==old){s={++serial_,reference_,sequence_,s.event,actor,to,false};return true;}
			return false;
		}
		// Engine inventory restoration is an external observation, not a new
		// controller press. The coordinator is the only caller of this boundary.
		bool observe(hand actor,grasp held)noexcept
		{
			if(!vr::valid_hand(actor) || !held.destination)return false;
			for(auto& s:sessions_)if(s && s.actor==actor && s.held.destination==held.destination)
			{s.held=held;s.reference=reference_;return true;}
			if(!permits(actor,held))return false;
			if(serial_==UINT64_MAX)return false;
			for(auto& s:sessions_)if(!s){s={++serial_,reference_,sequence_,0,actor,held,false};return true;}
			return false;
		}
		template<class Keep> void retain(Keep&& keep)noexcept
		{for(auto& s:sessions_)if(s && !keep(s))s={};}
		bool offer(candidate c)noexcept
		{
			if(count_==decisions_.size()){overflow_=true;return false;}
			decisions_[count_++]={c,rejection::invalid,0};return true;
		}
		template<class Commit> void resolve(Commit&& commit)
		{
			std::sort(decisions_.begin(),decisions_.begin()+count_,[](const auto& a,const auto& b){return before(a.request,b.request);});
			std::array<std::array<bool,2>,2> consumed{};
			std::array<std::array<std::uint64_t,2>,2> observed_events{};
			for(auto& d:std::span{decisions_.data(),count_})
			{
				const auto& c=d.request;const auto& g=c.desired;
				if(!vr::valid_hand(c.actor) || !g.destination || !std::isfinite(c.distance) || c.distance<0 || !std::isfinite(c.orientation) || (g.maintained!=button::grip && g.maintained!=button::trigger) || !reference_){d.result=rejection::invalid;continue;}
				if(!c.intent){d.result=rejection::no_edge;continue;}
				const auto h=unsigned(c.actor),b=g.maintained==button::trigger?1u:0u;
				if(c.event && consumed_events_[h][b]==c.event){d.result=rejection::no_edge;continue;}
				observed_events[h][b]=c.event;
				const auto& selection=supplies_[h];
				if(g.purpose==role::supply && selection && (selection.provider!=g.destination.provider || selection.object!=g.destination.object)){d.result=rejection::wrong_route;continue;}
				if(overflow_){d.result=rejection::capacity;continue;} // Never let truncation choose an arbitrary winner.
				auto& used=consumed[unsigned(c.actor)][g.maintained==button::trigger?1:0];
				if(used){d.result=rejection::lower_priority;continue;}
				const auto* replaced=c.replaces?find(c.actor,c.replaces):nullptr;
				if(c.replaces && (!replaced || replaced->settling || !support_magazine_transfer(replaced->held,g)))
				{d.result=rejection::incompatible;continue;}
				bool permitted=true;
				for(const auto& s:sessions_)if(s && s.actor==c.actor && &s!=replaced && (s.settling || !compatible(s.held,g)))
				{permitted=false;d.blocking_session=s.id;break;}
				if(!permitted){d.result=rejection::incompatible;continue;}
				bool occupied=false;for(const auto& s:sessions_)if(s && s.held.destination==g.destination && s.actor!=c.actor && c.exclusive_target){occupied=true;d.blocking_session=s.id;break;}
				if(occupied){d.result=rejection::occupied_target;continue;}
				if(const auto* s=find(c.actor,g.destination)){d.result=rejection::none;d.session_id=s->id;used=true;continue;}
				auto free=std::find_if(sessions_.begin(),sessions_.end(),[&](const auto& s){return replaced?&s==replaced:!s;});
				if(free==sessions_.end() || serial_==UINT64_MAX){d.result=rejection::capacity;continue;}
				used=true; // A refused native request must not turn into a different action.
				if(!commit(c)){d.result=rejection::commit_failed;continue;}
				*free={++serial_,reference_,sequence_,c.event,c.actor,g,false};d.result=rejection::none;d.session_id=free->id;
			}
			for(unsigned h=0;h<2;++h)for(unsigned b=0;b<2;++b)if(observed_events[h][b])consumed_events_[h][b]=observed_events[h][b];
		}
	};
}
