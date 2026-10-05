#pragma once
#include "special_equipment_runtime.hpp"
#include "native_claymore.hpp"
namespace vr::gameplay::equipment::special::notebook
{
	void initialize();
	void retire();
	void collect(const hand_interaction::frame&,native::selection,bool enabled)noexcept;
	void update()noexcept;
	void report()noexcept;
	void lifecycle(bool suspended)noexcept;
	void present(const hands::interaction_rig&,const hands::rig&,const controller_input::frame&,
		const std::array<hands::anchor,2>&,const std::array<hands::vec,2>&,const std::array<hands::vec,3>&,
		float,std::span<hands::bone>,unsigned,unsigned)noexcept;
	std::string status();
	bool controlling()noexcept;
	bool native_control()noexcept;
	// Latched after native camera acknowledgement, until native UAV cleanup.
	std::uint64_t camera_epoch()noexcept;
	bool available()noexcept;
	void command(const controller_input::frame&,bool gameplay,int& buttons)noexcept;
}
