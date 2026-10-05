#pragma once
#include "weapon_holding.hpp"
#include "../controller_input.hpp"
#include "javelin_display_geometry.hpp"

namespace vr::gameplay::weapons::javelin_screen
{
	struct control_state {bool open{},fire_ready{};};
	class proximity_controls
	{
		hold owner_{};std::uint64_t reference_{};control_state value_{};
	public:
		control_state consume(const controller_input::frame& input,const hold& owner,bool allowed,const muzzle_frame& muzzle,controller_input::clock::time_point now) noexcept
		{
			if(!allowed || owner.rear!=hand::right || !input.focused || !input.grip[1].valid || !input.sequence ||
				now<input.sampled_at || now-input.sampled_at>std::chrono::milliseconds(150)) {*this={};return {};}
			if(owner_.id()!=owner.id() || owner_.rear_revision!=owner.rear_revision || reference_!=input.reference_generation)value_={};
			owner_=owner;reference_=input.reference_generation;
			const bool was_open=value_.open;value_.open=near_eyepiece(muzzle,was_open);
			if(!value_.open || !was_open || !input.trigger[1].active)value_.fire_ready=false;
			if(value_.open && input.trigger[1].active && !input.trigger[1].down)value_.fire_ready=true;
			return value_;
		}
	};
	// Retained for the temporarily disabled button-control experiment.
	class controls
	{
		hold owner_{};std::uint64_t reference_{},continuity_{},sequence_{};
		std::array<std::uint64_t,3> presses_{},generations_{};
		std::array<bool,3> armed_{};
		control_state value_{};
	public:
		void reset() noexcept{*this={};}
		control_state consume(const controller_input::frame& input,const hold& owner,bool allowed,controller_input::clock::time_point now) noexcept
		{
			if(!allowed || owner.rear!=hand::right || !input.focused || !input.sequence || !input.grip[1].valid ||
				now<input.sampled_at || now-input.sampled_at>std::chrono::milliseconds(150)){reset();return {};}
			const std::array<controller_input::digital_action,3> buttons{input.trigger[1],input.secondary[0],input.secondary[1]};
			const bool changed=owner_.id()!=owner.id() || owner_.rear_revision!=owner.rear_revision ||
				reference_!=input.reference_generation || continuity_!=input.continuity_generation || input.sequence<sequence_;
			if(changed)
			{
				reset();owner_=owner;reference_=input.reference_generation;continuity_=input.continuity_generation;
				for(unsigned i=0;i<3;++i){presses_[i]=buttons[i].presses;generations_[i]=buttons[i].generation;armed_[i]=buttons[i].active && !buttons[i].down;}
				sequence_=input.sequence;return {};
			}
			if(input.sequence==sequence_)return value_;
			sequence_=input.sequence;std::array<bool,3> pressed{};
			for(unsigned i=0;i<3;++i)
			{
				const auto& b=buttons[i];
				if(!b.active || b.generation!=generations_[i] || b.presses<presses_[i])
				{armed_[i]=false;if(i==0)value_.fire_ready=false;}
				else pressed[i]=armed_[i] && b.presses>presses_[i];
				presses_[i]=b.presses;generations_[i]=b.generation;
				if(!b.down)armed_[i]=b.active;else if(pressed[i])armed_[i]=false;
			}
			if(pressed[1] || pressed[2]){value_.open=!value_.open;value_.fire_ready=false;}
			else if(!value_.open && pressed[0]){value_.open=true;value_.fire_ready=false;}
			else if(value_.open && buttons[0].active && !buttons[0].down)value_.fire_ready=true;
			if(!value_.open)value_.fire_ready=false;
			return value_;
		}
	};
}
