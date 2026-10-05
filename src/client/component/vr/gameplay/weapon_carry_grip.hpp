#pragma once
#include "weapon_holsters.hpp"
#include "weapon_profile.hpp"

namespace vr::gameplay::weapons::carry
{
	// Same-frame acquisition uses authored local contacts and current tracking,
	// never a render worker's previous squeeze decision.
	inline bool support_contact(const profile& p,const hands::anchor& gun,const hands::anchor& local,
		hands::vec rear,hands::vec other,float units) noexcept
	{
		if (!p.support_enabled || !std::isfinite(units) || units<=0 || !std::isfinite(p.acquire_meters) || p.acquire_meters<=0) return false;
		const auto separation=hands::length(hands::sub(other,rear))/units;
		if (!std::isfinite(separation) || (p.aiming==aim_rule::two_hand && separation<.08f)) return false;
		const auto contact=hands::add(gun.position,hands::rotate(gun.rotation,local.position));
		const auto distance=hands::length(hands::sub(other,contact))/units;
		return std::isfinite(distance) && distance<=p.acquire_meters;
	}
	inline location draw_contact(const inventory& owned,const holsters& slots,hands::vec point) noexcept
	{
		unsigned occupied{};
		if (owned.in_slot(location::left_waist)) occupied|=1;
		if (owned.in_slot(location::right_waist)) occupied|=2;
		if (owned.next_back()) occupied|=4;
		return hit(slots,point,occupied);
	}
	struct control_contact {bool valid{};control_attachment attachment{};float distance{};};
	inline control_contact choose_control_contact(hands::anchor gun,hands::anchor fixed,hands::anchor moving,
		bool has_moving,hands::vec point,float units)noexcept
	{
		if(!std::isfinite(units) || units<=0)return {};
		const auto distance=[&](hands::anchor local){return hands::length(hands::sub(point,hands::add(gun.position,hands::rotate(gun.rotation,local.position))))/(.15f*units);};
		const float ordinary=distance(fixed),mechanical=has_moving?distance(moving):INFINITY;
		const bool selected=mechanical<ordinary;const float nearest=selected?mechanical:ordinary;
		return {std::isfinite(nearest) && nearest<=1,selected?control_attachment::moving:control_attachment::fixed,nearest};
	}
	struct grip_claim { bool consumed{},pose_changed{};result change{}; };
	inline grip_claim claim_grip(inventory& owned,hand h,bool pressed,bool released,bool available,
		identity support,identity control,location slot,control_attachment attachment=control_attachment::fixed) noexcept
	{
		if (!pressed || released || !available || !valid_hand(h) || owned.in_hand(h)) return {};
		// A matching high-priority attempt consumes this press even if its target
		// changed before commit. Never reinterpret a failed draw as native use.
		if (support) return {true,owned.support(support,h),{}};
		if (control) return {true,owned.control(control,h,attachment),{}};
		if (slot!=location::absent) return {true,false,owned.draw(slot,h)};
		return {};
	}
}
