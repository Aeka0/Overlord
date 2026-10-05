#pragma once
#include "special_equipment_policy.hpp"
#include "native_carry.hpp"
#include <functional>
namespace game {struct XModel;}
namespace vr::gameplay::equipment::special::native
{
	struct selection {action_slot slot{};bool claymore{};game::XModel* model{};int quantity{};bool notebook{},designator{},carried{};};
	struct placement {bool valid{};anchor pose{};vec ground{},normal{};};
	bool initialize();
	selection selected();
	placement trace(const anchor& held,vec head,float units,float bottom);
	bool place(unsigned weapon,const placement&);
	weapons::native_carry::world_key target(vec origin,vec forward,float units);
	bool recover(weapons::native_carry::world_key,unsigned weapon);
	void activate(action_slot,std::function<bool()> authorized={});
	struct agm_state {bool valid{},using_uav{},remote{};};
	agm_state observe_agm()noexcept;
	const char* status()noexcept;
}
