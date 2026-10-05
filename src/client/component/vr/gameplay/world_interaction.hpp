#pragma once
#include "world_interaction_policy.hpp"
#include "../controller_input.hpp"
#include "../head_pose_bridge.hpp"
#include "hand_interaction/frame.hpp"
#include <functional>
#include <string>

namespace vr::gameplay::interaction
{
	using pickup_callback=std::function<bool(const target&,int)>;
	// Called by the server hand arbiter after held parts and body-slot claims.
	void update(const controller_input::frame&,const head_pose_bridge::spatial_frame&,
		unsigned available,unsigned pressed,unsigned released,const pickup_callback&);
	void suspend() noexcept;
	bool hand_leased(int hand) noexcept;
	void collect_interactions(const hand_interaction::frame&)noexcept;
	void report_interactions()noexcept;
	// Native scripts may permit a particular use while keeping firearms disabled.
	// The shared coordinator consumes this frame after mechanical suspension.
	bool scripted_frame(hand_interaction::frame&)noexcept;
	// Called only by the existing command owner; forwards reliable +activate /
	// -activate notifications separately from the native use button level.
	int command(const controller_input::frame&,bool gameplay,controller_input::clock::time_point now);
	struct presentation
	{
		std::array<target,2> targets{};
		float units{};
		std::uint64_t reference{};
		controller_input::clock::time_point at{};
	};
	presentation latest() noexcept;
	std::string prompt_status();
}
