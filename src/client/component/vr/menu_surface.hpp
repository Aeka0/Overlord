#pragma once

#include "controller_input.hpp"
#include "component/vr/digital_button_gate.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace vr::menu_surface
{
	using vec3 = std::array<float, 3>;
	inline constexpr unsigned maximum_menus = 4;
	inline constexpr unsigned background_slot = maximum_menus;
	inline constexpr unsigned backdrop_slot = maximum_menus + 1;
	inline constexpr unsigned cursor_slot = maximum_menus + 2;
	inline constexpr unsigned surface_count = maximum_menus + 3;
	inline constexpr float menu_depth = .5f, theater_depth = 3.f, layer_gap = .25f;
	inline constexpr float menu_width = .9f;
	inline constexpr float dark_layer_gap = .05f;
	inline constexpr float pi = 3.14159265358979323846f;
	inline vec3 add(vec3 a, const vec3& b) noexcept { for (int i=0;i<3;++i) a[i]+=b[i]; return a; }
	inline vec3 mul(vec3 a, float s) noexcept { for (auto& x:a) x*=s; return a; }
	inline float dot(const vec3& a,const vec3& b) noexcept { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
	inline bool finite(const vec3& v) noexcept { for(float x:v) if(!std::isfinite(x)||std::abs(x)>10000) return false; return true; }
	struct anchor
	{
		vec3 position{}, right{1,0,0}, up{0,1,0}, forward{0,0,-1};
		bool valid{};
	};
	inline anchor anchored(const head_pose_bridge::tracking_pose& pose) noexcept
	{
		anchor a;
		if (!finite(pose.position_meters)) return a;
		vec3 f{-pose.orientation[0][2],0,-pose.orientation[2][2]};
		float length=std::sqrt(dot(f,f));
		if(!std::isfinite(length))return a;
		if(length<.05f)
		{
			// At vertical pitch, the projected right axis still supplies yaw.
			f={pose.orientation[2][0],0,-pose.orientation[0][0]};length=std::sqrt(dot(f,f));
			if(!std::isfinite(length)||length<.05f)return a;
		}
		a.position=pose.position_meters;a.forward=mul(f,1/length);
		a.right={-a.forward[2],0,a.forward[0]};a.valid=true;return a;
	}
	struct geometry
	{
		anchor basis{};
		float distance{}, width{}, height{}, radius{}; // Radius zero selects a plane; width is arc length otherwise.
	};
	inline geometry layout(const anchor& a,bool theater,float aspect,unsigned behind) noexcept
	{
		geometry g;g.basis=a;
		if(!std::isfinite(aspect)||aspect<.2f||aspect>8||behind>=maximum_menus) return g;
		g.distance=(theater?theater_depth:menu_depth)+float(behind)*layer_gap;
		g.radius=theater?theater_depth:0;
		g.width=theater?theater_depth*pi*.5f:menu_width;
		g.height=g.width/aspect;return g;
	}
	// Interruption starts from the current location; no queued animations and
	// no accumulated depth drift after repeated push/pop or top replacement.
	inline float approach_depth(float current,float target,float seconds) noexcept
	{
		if(!std::isfinite(current)||!std::isfinite(target)||!std::isfinite(seconds))return target;
		const float step=std::clamp(seconds,0.f,.1f)*2.f;
		return current+std::clamp(target-current,-step,step);
	}
	struct buttons
	{
		bool click{},confirm{},back{},click_held{};
		int horizontal{},vertical{};
	};
	class pointer_owner
	{
		unsigned hand_{1};
		std::array<controller_input::digital_press_gate,2> gates_{};
		std::uint64_t sequence_{},reference_{},continuity_{};
	public:
		unsigned hand() const noexcept {return hand_;}
		void cancel_input() noexcept {gates_={};sequence_=0;}
		void reset() noexcept {*this={};}
		void update(const controller_input::frame& input,bool allowed,controller_input::clock::time_point now) noexcept
		{
			if(!allowed||!input.focused||!input.sequence||now<input.sampled_at||
				now-input.sampled_at>std::chrono::milliseconds(150)){cancel_input();return;}
			if(sequence_>input.sequence||reference_!=input.reference_generation||continuity_!=input.continuity_generation)cancel_input();
			if(sequence_==input.sequence)return;
			sequence_=input.sequence;reference_=input.reference_generation;continuity_=input.continuity_generation;
			std::array<bool,2> presses{};
			for(unsigned h=0;h<2;++h)
			{
				if(!input.runtime_aim[h].valid){gates_[h]={};continue;}
				presses[h]=gates_[h].consume(input.trigger[h]);
			}
			// Hit/miss is intentionally absent: leaving the panel never transfers
			// ownership. Simultaneous presses preserve the current owner.
			const auto other=1-hand_;
			if(presses[other]&&!presses[hand_])hand_=other;
		}
	};
	// This policy owns only VR edges. Every target/focus/generation change
	// neutral-arms all buttons and axes before the next menu can accept input.
	enum class menu_action {none,toggle,recenter};
	class menu_button
	{
		std::uint64_t context_{},reference_{},continuity_{},generation_{},sequence_{},presses_{};
		controller_input::clock::time_point last_{},pressed_{};
		bool armed_{},held_{},fired_{};
	public:
		void reset() noexcept {*this={};}
		menu_action consume(const controller_input::frame& frame,std::uint64_t context,bool allowed,
			controller_input::clock::time_point now) noexcept
		{
			const auto& button=frame.menu_recenter;
			if(!allowed||!frame.focused||!frame.sequence||!button.active||now<frame.sampled_at||
				now-frame.sampled_at>std::chrono::milliseconds(150)){reset();return menu_action::none;}
			if(!sequence_||sequence_>frame.sequence||context_!=context||reference_!=frame.reference_generation||
				continuity_!=frame.continuity_generation||generation_!=button.generation||
				frame.sampled_at<last_||frame.sampled_at-last_>std::chrono::milliseconds(150))
			{
				reset();context_=context;reference_=frame.reference_generation;continuity_=frame.continuity_generation;
				generation_=button.generation;sequence_=frame.sequence;last_=frame.sampled_at;presses_=button.presses;
				armed_=!button.down;return menu_action::none;
			}
			if(sequence_==frame.sequence)return menu_action::none;
			sequence_=frame.sequence;last_=frame.sampled_at;
			const bool new_press=button.presses>presses_;presses_=button.presses;
			if(!armed_){armed_=!button.down;return menu_action::none;}
			if(button.down&&!held_){held_=true;fired_=false;pressed_=frame.sampled_at;}
			auto result=menu_action::none;
			if(held_&&!fired_&&frame.sampled_at-pressed_>=std::chrono::seconds(1))
			{fired_=true;result=menu_action::recenter;}
			if(!button.down)
			{
				if((held_&&!fired_)||(!held_&&new_press))result=menu_action::toggle;
				held_=fired_=false;
			}
			return result;
		}
	};
	class navigation
	{
		std::uint64_t target_{},continuity_{},sequence_{};
		bool armed_{},click_{},confirm_{},back_{};
		std::uint64_t click_presses_{},confirm_presses_{},back_presses_{};
		std::array<std::uint64_t,3> action_generations_{};
		unsigned hand_{2};bool move_active_{};
		int direction_{};
		controller_input::clock::time_point repeat_{};
	public:
		void reset() noexcept { *this={}; }
		buttons consume(const controller_input::frame& f,std::uint64_t target,bool allowed,
			unsigned hand,bool hit_valid,controller_input::clock::time_point now) noexcept
		{
			buttons out;
			if(!allowed||!f.focused||hand>1||now<f.sampled_at||now-f.sampled_at>std::chrono::milliseconds(150))
			{reset();return out;}
			const std::array generations{f.trigger[hand].generation,f.primary[hand].generation,f.secondary[hand].generation};
			if(target_!=target||continuity_!=f.continuity_generation||hand_!=hand||action_generations_!=generations||move_active_!=f.move_active)
			{reset();target_=target;continuity_=f.continuity_generation;hand_=hand;action_generations_=generations;move_active_=f.move_active;}
			if(sequence_==f.sequence)return out;sequence_=f.sequence;
			const bool click=f.trigger[hand].active&&f.trigger[hand].down;
			const bool confirm=f.primary[hand].active&&f.primary[hand].down;
			const bool back=f.secondary[hand].active&&f.secondary[hand].down;
			const float x=f.move_active&&std::isfinite(f.move[0])?f.move[0]:0;
			const float y=f.move_active&&std::isfinite(f.move[1])?f.move[1]:0;
			if(!armed_)
			{
				armed_=!click&&!confirm&&!back&&std::abs(x)<.25f&&std::abs(y)<.25f;
				click_presses_=f.trigger[hand].presses;confirm_presses_=f.primary[hand].presses;back_presses_=f.secondary[hand].presses;
				return out;
			}
			out.click=((click&&!click_)||f.trigger[hand].presses>click_presses_)&&hit_valid;
			out.confirm=(confirm&&!confirm_)||f.primary[hand].presses>confirm_presses_;
			out.back=(back&&!back_)||f.secondary[hand].presses>back_presses_;out.click_held=click;
			click_presses_=f.trigger[hand].presses;confirm_presses_=f.primary[hand].presses;back_presses_=f.secondary[hand].presses;
			click_=click;confirm_=confirm;back_=back;
			const int direction=std::max(std::abs(x),std::abs(y))<.65f?0:
				std::abs(y)>=std::abs(x)?(y>0?1:2):(x>0?3:4);
			if(direction&&(direction!=direction_||now>=repeat_))
			{
				out.vertical=direction==1?1:direction==2?-1:0;
				out.horizontal=direction==3?1:direction==4?-1:0;
				repeat_=now+std::chrono::milliseconds(direction!=direction_?350:100);
			}
			direction_=direction;return out;
		}
	};
}
