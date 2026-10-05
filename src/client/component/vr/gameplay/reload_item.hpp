#pragma once
#include "falling_trajectory.hpp"
#include "physical_reload_geometry.hpp"
#include "swept_box_contact.hpp"
#include "weapon_holding.hpp"
#include "ammunition_transfer.hpp"
#include "../controller_input.hpp"
#include <array>
#include <limits>

namespace vr::gameplay::reload_items
{
	using clock=controller_input::clock;
	using vr::hand;
	using hands::anchor;using hands::vec;
	inline constexpr size_t capacity=32;
	inline constexpr float catch_radius=.065f;
	using motion::flight_lifetime;
	enum class kind { magazine, speedloader };
	enum class phase { vacant, reserved, falling, held, settling };
	struct key
	{
		std::uint32_t slot{};std::uint64_t generation{};
		bool operator==(const key&)const=default;
		explicit operator bool()const noexcept{return slot<capacity && generation;}
	};
	using motion::flight;
	struct item
	{
		key id{};std::uint64_t revision{},pose_revision{},reference{};
		kind type{};phase state{};
		weapons::weapon_identity origin{};
		int rounds{},limit{};
		hand holder{hand::none};
		flight motion{};
		weapons::ammunition::disposition_reason disposition{weapons::ammunition::disposition_reason::deliberate_discard};
		bool owned()const noexcept{return id && state!=phase::vacant && state!=phase::reserved;}
	};
	// The simulation owns this ledger. A reservation owns no ammunition until
	// its source's native compare succeeds. Rendering only receives copies.
	class inventory
	{
		std::array<item,capacity> items_{};std::uint64_t serial_{};
		std::uint64_t serial()noexcept{return serial_==UINT64_MAX?0:++serial_;}
	public:
		const auto& items()const noexcept{return items_;}
		void clear()noexcept{items_={};} // Keep serials unique across scene resets.
		const item* find(key id)const noexcept
		{return id && items_[id.slot].id==id?&items_[id.slot]:nullptr;}
		bool room()const noexcept
		{for(const auto& i:items_)if(!i.id)return serial_<UINT64_MAX;return false;}
		const item* held(hand h)const noexcept
		{for(const auto& i:items_)if(i.state==phase::held && i.holder==h)return &i;return nullptr;}
		std::int64_t rounds()const noexcept
		{std::int64_t n{};for(const auto& i:items_)if(i.owned())n+=i.rounds;return n;}
		key reserve(kind type,weapons::weapon_identity origin,int rounds,int limit,std::uint64_t reference,const flight& motion)noexcept
		{
			if((type!=kind::magazine && type!=kind::speedloader) || !origin.weapon || !reference || !motion.valid() || limit<=0 || limit>1000 || rounds<0 || rounds>limit)return {};
			for(size_t n=0;n<capacity;++n)if(!items_[n].id)
			{
				const auto sequence=serial();if(!sequence)return {};
				const key id{static_cast<std::uint32_t>(n),sequence};
				items_[n]={id,sequence,sequence,reference,type,phase::reserved,origin,rounds,limit,hand::none,motion};return id;
			}
			return {};
		}
		bool activate(key id,bool committed)noexcept
		{
			const auto* p=find(id);if(!p || p->state!=phase::reserved)return false;
			if(!committed){items_[id.slot]={};return true;}
			items_[id.slot].state=phase::falling;return true;
		}
		void advance(key id,clock::time_point now,const anchor* attached=nullptr)noexcept
		{
			const auto* p=find(id);if(!p || p->state!=phase::falling)return;
			auto& i=items_[id.slot];
			if(now<i.motion.born)return;
			if(!i.motion.alive(now)){i.state=phase::settling;return;}
			i.motion.advance(now,attached);
		}
		void expire(key id,weapons::ammunition::disposition_reason reason=weapons::ammunition::disposition_reason::deliberate_discard)noexcept
		{if(const auto* p=find(id);p && p->owned()){auto& i=items_[id.slot];i.state=phase::settling;i.holder=hand::none;
			i.disposition=reason;}}
		bool catch_item(key id,hand actor,clock::time_point now)noexcept
		{
			const auto* p=find(id);if(!p || p->state!=phase::falling || !p->motion.alive(now) || !vr::valid_hand(actor) || held(actor))return false;
			const auto revision=serial();if(!revision)return false;
			auto& i=items_[id.slot];i.state=phase::held;i.holder=actor;i.revision=i.pose_revision=revision;return true;
		}
		bool release(key id,hand actor,const flight& motion)noexcept
		{
			const auto* p=find(id);if(!p || p->state!=phase::held || p->holder!=actor || !motion.valid())return false;
			const auto revision=serial();if(!revision)return false;
			auto& i=items_[id.slot];i.state=phase::falling;i.holder=hand::none;i.motion=motion;i.revision=i.pose_revision=revision;return true;
		}
		// Only call after the receiving feed has committed the same payload.
		bool inserted(key id,hand actor,int rounds)noexcept
		{
			const auto* p=find(id);if(!p || p->state!=phase::held || p->holder!=actor || rounds!=p->rounds)return false;
			if(p->type==kind::magazine){items_[id.slot]={};return true;}
			const auto revision=serial();if(!revision)return false;
			auto& i=items_[id.slot];i.rounds=0;i.revision=revision;return true;
		}
		void rebase(std::uint64_t reference)noexcept
		{
			for(auto& i:items_)if(i.owned() && i.reference!=reference)
			{
				if(i.state==phase::held){i.reference=reference;i.revision=i.pose_revision=serial();}
				else {i.state=phase::settling;i.disposition=weapons::ammunition::disposition_reason::forced_cleanup;}
			}
		}
		template<class Refund> bool settle(key id,Refund&& refund,bool penalty=false)noexcept
		{
			const auto* p=find(id);if(!p || p->state!=phase::settling)return false;
			const auto disposed=weapons::ammunition::dispose(p->rounds,p->disposition,penalty);
			if(disposed.returned && !refund(p->origin,disposed.returned))return false;
			items_[id.slot]={};return true;
		}
	};
	struct catch_probe
	{
		weapons::physical_reload::box_motion previous{};clock::time_point at{};std::uint64_t item{};bool sampled{};
		bool test(std::uint64_t id,const weapons::physical_reload::box_motion& current,clock::time_point now)noexcept
		{
			using namespace weapons::physical_reload;
			if(!valid(current)){*this={};return false;}
			const float dt=std::chrono::duration<float>(now-at).count();
			const bool same=sampled && item==id && dt>0 && dt<=.15f;
			const box_sweep sweep(previous,current);
			const bool continuous=same && sweep.bound<=.5f;
			vec contact{};
			const bool hit=(!same || continuous) && (closest_box(current,0).distance<=catch_radius ||
				(continuous && sweep.contact(0,catch_radius,contact)));
			previous=current;at=now;item=id;sampled=true;return hit;
		}
	};
}
