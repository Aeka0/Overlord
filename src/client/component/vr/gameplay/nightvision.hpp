#pragma once
#include "hand_interaction/frame.hpp"
#include <optional>
#include <cstring>

namespace vr::gameplay::equipment::nightvision
{
	using hands::vec;
	using hand_interaction::button;
	using hand_interaction::clock;
	using vr::hand;
	struct device_timing
	{
		int fade_ms{},power_ms{};
		int duration(bool on)const noexcept{return fade_ms+(on?power_ms:0);}
	};
	inline std::optional<device_timing> timing(float fade,float power)noexcept
	{
		if(!std::isfinite(fade) || !std::isfinite(power) || fade<0 || power<0 || fade>10 || power>10)return {};
		return device_timing{static_cast<int>(std::ceil(fade*1000.f)),static_cast<int>(std::ceil(power*1000.f))};
	}
	struct device_view {bool owned{},vision{},target{};float visibility{1};std::uint64_t serial{};int time{};};
	// Head-equipment presentation has no weapon/hand/model key. Native input
	// still decides whether a toggle is accepted; its command/timeline identity
	// deduplicates prediction replays and fences checkpoint/level replacement.
	class device_transition
	{
		std::uint64_t epoch_{},serial_{},sound_serial_{},foley_serial_{};int command_{},started_{};
		device_timing timing_{};bool valid_{},on_{},black_seen_{};int sound_at_{};
		int black_time()const noexcept{return on_?timing_.fade_ms:0;}
	public:
		void reset()noexcept{valid_=false;epoch_=0;}
		std::uint64_t serial()const noexcept{return serial_;}
		bool sound_claimed(std::uint64_t serial)const noexcept{return serial==sound_serial_;}
		bool foley_claimed(std::uint64_t serial)const noexcept{return serial==foley_serial_;}
		bool claim_foley(std::uint64_t serial)noexcept
		{if(!valid_ || serial!=serial_ || foley_claimed(serial))return false;foley_serial_=serial;return true;}
		bool sound_ready(std::uint64_t serial,int now)const noexcept
		{
			const auto elapsed=static_cast<std::int64_t>(now)-started_;
			return valid_ && serial==serial_ && !sound_claimed(serial) && black_seen_ &&
				elapsed>=black_time() && elapsed<=black_time()+250;
		}
		bool claim_sound(std::uint64_t serial,int now)noexcept
		{
			if(!sound_ready(serial,now))return false;
			sound_serial_=serial;sound_at_=now;return true;
		}
		bool expired(std::uint64_t epoch,int now)const noexcept
		{return !valid_ || epoch!=epoch_ || static_cast<std::int64_t>(now)-started_>timing_.duration(on_)+250;}
		bool begin(std::uint64_t epoch,int command,int now,bool on,device_timing time)noexcept
		{
			if(!epoch || command<0 || now<0 || time.fade_ms<0 || time.power_ms<0 || time.fade_ms>10000 || time.power_ms>10000 ||
				(valid_ && epoch==epoch_ && command<=command_))return false;
			epoch_=epoch;command_=command;started_=now;timing_=time;on_=on;valid_=true;black_seen_=false;++serial_;return true;
		}
		device_view sample(std::uint64_t epoch,int now,bool native_on)const noexcept
		{
			const auto elapsed=static_cast<std::int64_t>(now)-started_;
			if(!valid_ || epoch!=epoch_ || native_on!=on_ || elapsed<0 || elapsed>timing_.duration(on_)+250)return {};
			device_view out{true,on_,on_,1,serial_,now};
			if(on_ && elapsed<timing_.fade_ms)
			{out.vision=false;out.visibility=1-float(elapsed)/timing_.fade_ms;}
			else
			{
				const int duration=on_?timing_.power_ms:timing_.fade_ms;
				// Render must observe full black before the main thread dispatches
				// power audio. Keep that black phase through dispatch, then begin
				// the fade from the same game-clock instant. Missing audio/render
				// acknowledgement releases after 250 ms instead of sticking black.
				const auto release=sound_claimed(serial_)?static_cast<std::int64_t>(sound_at_)-started_:black_time()+250;
				const auto progress=elapsed-release;
				out.visibility=progress<=0?0.f:(duration?std::clamp(float(progress)/duration,0.f,1.f):1.f);
			}
			return out;
		}
		device_view render(std::uint64_t epoch,int now,bool native_on)noexcept
		{
			const auto view=sample(epoch,now,native_on);
			if(view.owned && static_cast<std::int64_t>(now)-started_>=black_time() && view.visibility==0)black_seen_=true;
			return view;
		}
	};
	struct native_state {int slot{-1};bool on{};explicit operator bool()const noexcept{return slot>=0 && slot<4;}};
	inline native_state decode(const std::array<unsigned,4>& types,unsigned flags)noexcept
	{
		native_state out;out.on=(flags&0x40)!=0;
		for(int i=0;i<4;++i)if(types[i]==3){if(out.slot>=0)return {};out.slot=i;}
		return out;
	}
	inline native_state decode(std::span<const std::byte> ps)noexcept
	{
		if(ps.size()<0x1fb0)return {};
		unsigned flags{};std::array<unsigned,4> types{};std::memcpy(&flags,ps.data()+0x3c0,4);
		// H2 setactionslot's "nightvision" branch stores type 3 at PS+1fa0.
		std::memcpy(types.data(),ps.data()+0x1fa0,sizeof(types));return decode(types,flags);
	}
	inline std::optional<vec> head_local(const head_pose_bridge::spatial_frame& body,vec point)noexcept
	{
		if(!std::isfinite(body.units_per_meter) || body.units_per_meter<=0)return {};
		for(const auto& v:{body.head_position,body.head_forward,body.head_up,point})for(float x:v)if(!std::isfinite(x))return {};
		const auto& forward=body.head_forward;const auto& up=body.head_up;
		if(std::abs(hands::length(forward)-1)>.02f || std::abs(hands::length(up)-1)>.02f || std::abs(hands::dot(forward,up))>.02f)return {};
		const auto delta=hands::scale(hands::sub(point,body.head_position),1/body.units_per_meter);
		return vec{hands::dot(delta,forward),hands::dot(delta,hands::cross(up,forward)),hands::dot(delta,up)};
	}
	inline bool envelope(vec p)noexcept
	{
		for(float x:p)if(!std::isfinite(x))return false;
		return p[0]>=-.12f && p[0]<=.48f && std::abs(p[1])<=.30f && p[2]>=-.22f && p[2]<=.40f;
	}
	inline bool acquisition(bool on,vec p)noexcept
	{
		// The stowed goggles span forehead/top of the head, not only the air
		// in front of the eyes. Coordinates refer to the physical grip point.
		return envelope(p) && p[0]>=(on?0.f:-.10f) && p[0]<=.35f && std::abs(p[1])<=.28f &&
			(on ? p[2]>=-.12f && p[2]<=.10f : p[2]>=.04f && p[2]<=.30f);
	}
	inline bool command_hand_free(std::span<const hand_interaction::session> sessions,hand actor,std::uint64_t reference)noexcept
	{
		if(!vr::valid_hand(actor) || !reference)return false;
		for(const auto& s:sessions)if(s && s.actor==actor && (s.settling ||
			s.held.destination.provider!=hand_interaction::domain::gesture ||
			s.held.destination.object!=weapons::weapon_identity{0,reference}))return false;
		return true;
	}
	class drag
	{
		hand actor_{hand::none};button source_{};bool on_{};
		vec start_{},last_{};clock::time_point began_{};
		std::uint64_t reference_{},continuity_{};
	public:
		void reset()noexcept{*this={};}
		bool active()const noexcept{return vr::valid_hand(actor_);}
		hand actor()const noexcept{return actor_;}
		button source()const noexcept{return source_;}
		bool original_on()const noexcept{return on_;}
		std::uint64_t reference()const noexcept{return reference_;}
		bool begin(hand actor,button source,bool on,vec p,const controller_input::frame& input)noexcept
		{
			if(active() || !vr::valid_hand(actor) || (source!=button::grip && source!=button::trigger) ||
				!input.reference_generation || !acquisition(on,p))return false;
			actor_=actor;source_=source;on_=on;start_=last_=p;began_=input.sampled_at;
			reference_=input.reference_generation;continuity_=input.continuity_generation;return true;
		}
		bool update(vec p,bool on,bool down,bool allowed,const controller_input::frame& input)noexcept
		{
			if(!active())return false;
			if(!allowed || !down || on!=on_ || !input.focused || input.orientation_settling ||
				input.reference_generation!=reference_ || input.continuity_generation!=continuity_ ||
				input.sampled_at<began_ || input.sampled_at-began_>std::chrono::seconds(2) ||
				!envelope(p) || hands::length(hands::sub(p,last_))>.30f){reset();return false;}
			last_=p;const auto travel=(p[2]-start_[2])*(on_?1.f:-1.f);
			const bool complete=travel>=.12f && (on_ ? p[2]>=.10f : p[2]<=.08f);
			if(complete)reset();return complete;
		}
	};
}
