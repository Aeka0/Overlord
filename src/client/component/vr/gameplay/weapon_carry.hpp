#pragma once
#include "weapon_holding.hpp"
#include <array>
#include <algorithm>
#include <cstdint>
#include <span>

namespace vr::gameplay::weapons::carry
{
	// Physical locations belong to instances; native definition ownership is a projection.
	enum class location { absent, held, left_waist, right_waist, back, overflow, abdominal };
	inline bool ordinary_slot(location p) noexcept
	{ return p==location::left_waist || p==location::right_waist || p==location::back; }
	struct rules
	{
		bool waist{};
		bool promote_support{};
		bool abdominal{}; // Mission equipment returns to its own body slot, never the world.
		bool right_control{}; // Asymmetric launcher: right fires, left only supports.
	};
	using identity = weapon_identity;
	struct instance
	{
		identity id{};
		rules policy{};
		location at{location::absent};
		hold owner{};
		std::uint64_t overflow_order{};
	};
	struct owned_weapon { std::uint32_t weapon{}; rules policy{}; };
	struct owned_instance { identity id{}; rules policy{}; };
	enum class outcome { unchanged, rejected, support_released, promoted, carry_only, stowed, exchanged, dropped, drawn, retained };
	struct result { outcome action{outcome::unchanged}; identity subject{}, received{}; };
	class inventory
	{
	public:
		static constexpr std::size_t capacity=15; // Engine ownership-array bound, not physical carrying capacity.
		void clear() noexcept {values_={};++revision_;}
		bool restore_identity(identity previous,identity id) noexcept
		{
			auto* v=mutable_find(previous);
			if (!v || !id || previous.weapon!=id.weapon || (previous!=id && find(id))) return false;
			v->id=id;v->owner.instance_generation=id.generation;
			v->owner.revision=v->owner.rear_revision=++revision_;
			generation_=std::max(generation_,id.generation);return true;
		}
		void put_away() noexcept
		{
			for (auto& v:values_) if (v.at==location::held)
			{v.at=free_slot(v);if (v.at==location::overflow) v.overflow_order=++overflow_sequence_;assign(v,hand::none,hand::none);}
		}
			const auto& instances() const noexcept { return values_; }
			// Explicit topology restoration, including empty hands/slots and overflow
			// order. Reconciliation's default free-slot placement is not a restore.
			bool restore(std::span<const instance> saved) noexcept
			{
				if(saved.size()>capacity)return false;
				auto next=*this;next.values_={};next.overflow_sequence_=0;
				for(std::size_t i=0;i<saved.size();++i)
				{
					auto v=saved[i];if(!v.id || v.at==location::absent || v.at>location::abdominal)return false;
					if(v.at!=location::held && (valid_hand(v.owner.rear)||valid_hand(v.owner.support)))return false;
					v.owner.weapon=v.id.weapon;v.owner.instance_generation=v.id.generation;
					v.owner.source=hold_source::interaction;
					v.owner.revision=v.owner.rear_revision=++next.revision_;
					next.values_[i]=v;next.generation_=std::max(next.generation_,v.id.generation);
					next.overflow_sequence_=std::max(next.overflow_sequence_,v.overflow_order);
				}
				if(!next.invariant())return false;*this=next;return true;
			}
		const instance* find(identity id) const noexcept
		{ for (const auto& v:values_) if (v.id==id && v.at!=location::absent) return &v; return nullptr; }
		// Explicit compatibility lookup. A definition cannot choose between copies.
		const instance* find_definition(std::uint32_t weapon) const noexcept
		{
			const instance* found{};
			for (const auto& v:values_) if (v.id.weapon==weapon && v.at!=location::absent)
			{ if (found) return nullptr; found=&v; }
			return found;
		}
		const instance* in_hand(hand h) const noexcept
		{
			if (!valid_hand(h)) return nullptr;
			for (const auto& v:values_) if (v.at==location::held && (v.owner.rear==h || v.owner.support==h)) return &v;
			return nullptr;
		}
		const instance* in_slot(location p) const noexcept
		{ if (ordinary_slot(p)) for (const auto& v:values_) if (v.at==p) return &v; return nullptr; }
		const instance* next_back() const noexcept
		{
			if (const auto* normal=in_slot(location::back)) return normal;
			const instance* first{};
			for (const auto& v:values_) if (v.at==location::overflow && (!first || v.overflow_order<first->overflow_order)) first=&v;
			return first;
		}
		static bool accepts(const instance& v,location p) noexcept
		{ return !v.policy.abdominal && (p==location::back || ((p==location::left_waist || p==location::right_waist) && v.policy.waist)); }
		// Definition-only native adapter. Explicit instances enter through
		// reconcile_instances; neither route moves surviving held/holstered guns.
		bool reconcile(std::span<const owned_weapon> observed) noexcept
		{
			if (observed.size()>capacity) return false;
			for (std::size_t i=0;i<observed.size();++i)
			{
				if (!observed[i].weapon) return false;
				for (std::size_t j=0;j<i;++j) if (observed[i].weapon==observed[j].weapon) return false;
			}
			std::array<owned_instance,capacity> instances{};
			auto next=generation_;
			for (std::size_t i=0;i<observed.size();++i)
			{
				const auto* existing=find_definition(observed[i].weapon);
				// Do not collapse a previously explicit duplicate-instance snapshot.
				if (!existing) for (const auto& v:values_) if (v.at!=location::absent && v.id.weapon==observed[i].weapon) return false;
				if (!existing && ++next==0) return false;
				instances[i]={existing ? existing->id : identity{observed[i].weapon,next},observed[i].policy};
			}
			return reconcile_instances({instances.data(),observed.size()});
		}
		bool reconcile_instances(std::span<const owned_instance> observed) noexcept
		{
			if (observed.size()>capacity) return false;
			for (std::size_t i=0;i<observed.size();++i)
			{
				if (!observed[i].id) return false;
				for (std::size_t j=0;j<i;++j) if (observed[i].id==observed[j].id) return false;
			}
			for (auto& v:values_)
			{
				bool owned=false;
				for (const auto& w:observed) if (w.id==v.id) {owned=true;break;}
				if (!owned) v={};
			}
			for(const auto& w:observed)if(auto* v=mutable_find(w.id))
			{v->policy=w.policy;assign(*v,v->owner.rear,v->owner.support);if(w.policy.abdominal && v->at!=location::held)v->at=location::abdominal;else if(!w.policy.abdominal && v->at==location::abdominal)v->at=free_slot(*v);}
			for (const auto& w:observed) if (!find(w.id))
			{
				for (auto& v:values_) if (v.at==location::absent)
				{
					v.id=w.id; v.policy=w.policy;generation_=std::max(generation_,w.id.generation);
					v.at=free_slot(v);
					if (v.at==location::overflow) v.overflow_order=++overflow_sequence_;
					v.owner={w.id.weapon,++revision_,hand::none,hand::none,hold_source::interaction,revision_};
					v.owner.instance_generation=w.id.generation;
					break;
				}
			}
			return true;
		}
		// Checkpoint/script adapter only. A displaced instance uses the incoming
		// item's compatible ordinary slot first, then an empty slot. No give/take.
		bool equip_definition(std::uint32_t weapon,hand h) noexcept
		{ const auto* v=find_definition(weapon); return v && equip(v->id,h); }
		bool equip(identity id,hand h) noexcept
		{
			auto* v=mutable_find(id);
			if (!v || !valid_hand(h)) return false;
			if (v->at==location::held && v->owner.rear==h) return true;
			if(v->policy.right_control && v->at==location::held && v->owner.support==h)return true;
			const auto old_at=v->at;
			if (const auto* held=in_hand(h); held && held->id!=v->id)
			{
				auto* displaced=mutable_find(held->id);
				v->at=location::absent;
				displaced->at=ordinary_slot(old_at) && accepts(*displaced,old_at) ? old_at : free_slot(*displaced);
				if (displaced->at==location::overflow) displaced->overflow_order=++overflow_sequence_;
				assign(*displaced,hand::none,hand::none);
			}
			v->at=location::held; assign(*v,h,hand::none); return true;
		}
		bool support(identity id,hand h) noexcept
		{
			auto* v=mutable_find(id);
			if(v && v->policy.right_control && h!=hand::left)return false;
			if (!v || v->id!=id || v->at!=location::held || !valid_hand(h) || v->owner.rear==h || in_hand(h)) return false;
			assign(*v,v->owner.rear,h); return true;
		}
		bool control(identity id,hand h,control_attachment attachment=control_attachment::fixed) noexcept
		{
			if(attachment!=control_attachment::fixed && attachment!=control_attachment::moving)return false;
			auto* v=mutable_find(id);
			if(v && v->policy.right_control && h!=hand::right)return false;
			if (!v || v->id!=id || v->at!=location::held || v->owner.rear!=hand::none || !valid_hand(h) || in_hand(h)) return false;
			assign(*v,h,v->owner.support);v->owner.attachment=attachment;return true;
		}
		result draw(location p,hand h) noexcept
		{
			if (!valid_hand(h) || in_hand(h) || !ordinary_slot(p)) return {outcome::rejected};
			const auto* found=p==location::back ? next_back() : in_slot(p);
			if (!found) return {};
			auto* v=mutable_find(found->id); v->at=location::held; assign(*v,h,hand::none);
			return {outcome::drawn,v->id};
		}
		// One frame's two release bits are resolved together, BEFORE slot/drop work.
		// The callback confirms native removal plus a real world entity. Rejection
		// retains the final holding hand; the input edge gate controls retry timing.
			template<class Drop> result release(identity id,unsigned released,location target,bool clear,Drop&& drop,bool allow_drop=true,bool allow_storage=true)
		{
			auto* v=mutable_find(id);
			if (!v || v->id!=id || v->at!=location::held) return {outcome::rejected,id};
			const auto before=v->owner;
			const auto up=[&](hand h) {return valid_hand(h) && (released&(1u<<static_cast<unsigned>(h)));};
			auto rear=up(before.rear) ? hand::none : before.rear;
			auto support=up(before.support) ? hand::none : before.support;
			if (rear==before.rear && support==before.support) return {};
			if (valid_hand(rear)) {assign(*v,rear,support);return {outcome::support_released,id};}
			if (valid_hand(support))
			{
				const bool promote=v->policy.promote_support && !v->policy.right_control;
				assign(*v,promote ? support : hand::none,promote ? hand::none : support);
				return {promote ? outcome::promoted : outcome::carry_only,id};
			}
			const auto last=valid_hand(before.rear) ? before.rear : before.support;
			if(v->policy.abdominal){v->at=location::abdominal;assign(*v,hand::none,hand::none);return {outcome::stowed,id};}
			const auto reject=[&]() {assign(*v,valid_hand(before.rear) ? last : hand::none,
				valid_hand(before.rear) ? hand::none : last);return result{outcome::rejected,id};};
				if(target==location::absent && !allow_drop)
				{reject();return {outcome::retained,id};}
				if(target!=location::absent && !allow_storage)
				{reject();return {outcome::retained,id};}
			if (!clear) return reject();
			if (target!=location::absent)
			{
				if (!ordinary_slot(target) || !accepts(*v,target)) return reject();
				identity exchanged{};
				if (const auto* occupied=in_slot(target))
				{
					auto* other=mutable_find(occupied->id); exchanged=other->id;
					other->at=location::held; assign(*other,last,hand::none);
				}
				v->at=target; assign(*v,hand::none,hand::none);
				return {exchanged ? outcome::exchanged : outcome::stowed,id,exchanged};
			}
			if (!drop(*v)) return reject();
			*v={}; return {outcome::dropped,id};
		}
		bool invariant() const noexcept
		{
			for (std::size_t i=0;i<values_.size();++i)
			{
				const auto& v=values_[i]; if (v.at==location::absent) continue;
				if (!v.id || v.owner.id()!=v.id || (ordinary_slot(v.at) && !accepts(v,v.at)) || (v.at==location::abdominal && !v.policy.abdominal)) return false;
				if(v.policy.right_control && (v.owner.rear==hand::left || v.owner.support==hand::right))return false;
				if (v.at==location::held)
				{ if ((!valid_hand(v.owner.rear) && !valid_hand(v.owner.support)) || (valid_hand(v.owner.rear) && v.owner.rear==v.owner.support)) return false; }
				else if (v.owner.rear!=hand::none || v.owner.support!=hand::none) return false;
				for (std::size_t j=0;j<i;++j)
				{
					const auto& other=values_[j]; if (other.at==location::absent) continue;
					if (v.id==other.id || (ordinary_slot(v.at) && v.at==other.at)) return false;
					if (v.at==location::held && other.at==location::held)
						for (auto h:{hand::left,hand::right})
							if ((v.owner.rear==h || v.owner.support==h) && (other.owner.rear==h || other.owner.support==h)) return false;
				}
			}
			return true;
		}
	private:
		instance* mutable_find(identity id) noexcept
		{ for (auto& v:values_) if (v.id==id && v.at!=location::absent) return &v; return nullptr; }
		location free_slot(const instance& v) const noexcept
		// Default loadout placement prefers the right hip; explicit stows keep
		// the player's chosen slot and reconciliation never moves surviving guns.
		{ if(v.policy.abdominal)return location::abdominal;for (auto p:{location::right_waist,location::left_waist,location::back}) if (accepts(v,p) && !in_slot(p)) return p; return location::overflow; }
		void assign(instance& v,hand rear,hand support) noexcept
		{
			if(v.policy.right_control)
			{
				const bool right=rear==hand::right || support==hand::right,left=rear==hand::left || support==hand::left;
				rear=right?hand::right:hand::none;support=left?hand::left:hand::none;v.owner.pose_rear=hand::right;
			}
			if (v.owner.rear==rear && v.owner.support==support) return;
			v.owner.revision=++revision_;
			if (v.owner.rear!=rear){v.owner.rear_revision=revision_;v.owner.attachment=control_attachment::fixed;}
			v.owner.rear=rear; v.owner.support=support; v.owner.source=hold_source::interaction;
			if (valid_hand(rear)) v.owner.pose_rear=rear;
		}
		std::array<instance,capacity> values_{};
		std::uint64_t generation_{},revision_{},overflow_sequence_{};
	};
}
