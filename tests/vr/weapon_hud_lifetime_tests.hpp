#pragma once
#include "component/vr/gameplay/weapon_hud_lifetime.hpp"
#include "component/vr/gameplay/weapon_carry.hpp"

namespace weapon_hud_lifetime_tests
{
	template<class Check> void run(Check check)
	{
		using namespace vr::gameplay::weapons;
		using namespace carry;
		using namespace vr::gameplay::weapon_hud;
		struct pixels_and_pose {int ink{}, anchor{};};
		presentation_cache<pixels_and_pose> panel;
		inventory bag;
		const std::array<owned_weapon,2> items{{{7,{true,true}}, {8,{true,true}}}};
		const auto drop=[](const instance&) {return true;};
		const auto owner=[&]() {const auto* v=bag.in_hand(hand::right);return v ? v->owner : hold{};};
		const auto empty=[&]() {return !panel.value.ink && !panel.value.anchor;};
		bag.reconcile(items); bag.equip_definition(7,hand::right);
		const auto first=owner();
		panel.synchronize(first); panel.value={31,47};
		check(!panel.synchronize(owner()) && panel.value.ink==31 && panel.value.anchor==47,
			"HUD retains native pixels and position while the same hold has no new render/tracking data");
		bag.support(bag.find_definition(7)->id,hand::left);
		check(!panel.synchronize(owner()) && panel.accepts(first) && !empty(),
			"support contact revisions do not discard the weapon HUD");
		bag.release(bag.find_definition(7)->id,1,location::absent,true,drop);
		check(!panel.synchronize(owner()) && !empty(),"support release retains primary weapon HUD");
		check(bag.release(bag.find_definition(7)->id,2,location::absent,false,drop).action==outcome::rejected,
			"HUD regression setup rejects clipped drop");
		check(!panel.synchronize(owner()) && !empty(),"rejected release keeps the existing HUD lease");
		check(bag.release(bag.find_definition(7)->id,2,location::absent,true,drop).action==outcome::dropped,
			"HUD regression pickup/drop transaction commits");
		check(panel.synchronize(owner()) && empty() && !panel.accepts(first),
			"last weapon drop clears BOTH native ink and world anchor and rejects a late old scene");
		bag.reconcile(items); bag.equip_definition(7,hand::right);
		panel.synchronize(owner());
		check(empty() && !panel.accepts(first),"same-token pickup cannot resurrect a previous hold's presentation");
		panel.value={42,58};
		const auto before_stow=owner();
		bag.release(bag.find_definition(7)->id,2,location::back,true,drop);
		check(bag.find_definition(7) && panel.synchronize(owner()) && empty() && !panel.accepts(before_stow),
			"holstered native-owned weapon has no held HUD");
		bag.draw(location::back,hand::right);panel.synchronize(owner());panel.value={12,19};
		const auto before_redraw=owner();
		bag.release(bag.find_definition(7)->id,2,location::back,true,drop);bag.draw(location::back,hand::right);
		check(panel.synchronize(owner()) && empty() && !panel.accepts(before_redraw),
			"stow/redraw between render samples invalidates even when the empty state was never sampled");
		panel.value={21,33};const auto outgoing=owner();
		bag.equip_definition(8,hand::left);bag.release(bag.find_definition(7)->id,2,location::absent,true,drop);
		const auto remaining=bag.in_hand(hand::left)->owner;
		check(panel.synchronize(remaining) && empty() && !panel.accepts(outgoing) && panel.accepts(remaining),
			"dropping one of two weapons selects only the remaining weapon's own pose and pixels");
		panel.value={19,27};bag.support(bag.find_definition(8)->id,hand::right);
		bag.release(bag.find_definition(8)->id,1,location::absent,true,drop);
		check(panel.synchronize(owner()) && empty() && !panel.accepts(remaining),
			"pistol handover rejects the previous hand's late pose");
		panel.value={9,11};bag.clear();bag.reconcile(items);bag.equip_definition(8,hand::right);
		check(panel.synchronize(owner()) && empty(),"inventory reset cannot reuse a previous level's HUD lease");
		hold body_item{7,1,hand::none,hand::none,hold_source::interaction,1};
		check(!has_presentation_owner(body_item),"native ownership alone cannot authorize a held HUD");
		body_item.support=hand::left;
		check(has_presentation_owner(body_item),"foregrip-only carrying remains a held weapon, independent of firing permission");
	}
}
