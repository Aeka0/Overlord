#pragma once
#include "fixed_sniper_policy.hpp"
#include <string>
namespace game {struct playerState_s;struct usercmd_s;}
namespace vr::gameplay::fixed_sniper
{
	struct state {std::uint64_t epoch{};int entity{-1},started{},time{};std::uintptr_t player{};};
	state current(const game::playerState_s* ps=nullptr) noexcept;
	// Called once before native command packing, including scripted input branches.
	bool command(const controller_input::frame&,bool gameplay,game::usercmd_s*,float* angles,float deadzone,float speed) noexcept;
	void suspend_input() noexcept;
	float head_gain() noexcept;
	std::string format_input_status();
	std::string format_render_status();
}
