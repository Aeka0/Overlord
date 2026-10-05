#pragma once
#include "weapon_holding.hpp"
#include "../controller_input.hpp"
#include <algorithm>
#include <array>
#include <string_view>

namespace vr::gameplay::weapons::quick_reload
{
	// Authored feed IDs, independent of recoil and native weapon classes. Gold
	// Desert Eagle shares de50's feed; suppressed USP has its own descriptor.
	inline constexpr std::array<std::string_view,11> supported_feeds{
		"m9","usp","usp_silencer","magnum44","de50","m1911","g18","tmp","miniuzi","pp2000","ranger"};
	inline bool supported(std::string_view id) noexcept
	{return std::find(supported_feeds.begin(),supported_feeds.end(),id)!=supported_feeds.end();}
	inline constexpr std::array<std::string_view,2> start_sound_files{
		"wpn_handgun_ads_down_01","wpn_handgun_ads_down_02"};
	// Live SOUND-pool witness: this stock alias owns both recordings and adds
	// the native ADS clothing layer. That companion is not an unrelated variant.
	inline constexpr const char* start_sound_alias="wpn_handgun_ads_down_plr";
	inline bool start_sound_companions(std::string_view secondary,std::string_view chain) noexcept
	{return chain.empty() && (secondary.empty() || secondary=="h2_wpn_foley_ads_plr");}

	class dwell
	{
		using clock=controller_input::clock;
		hold owner_{};
		std::uint64_t instance_{},reference_{},continuity_{},sequence_{};
		clock::time_point started_{},sampled_{};
		int slot_{-1};
		bool began_{};
	public:
		void reset() noexcept {*this={};}
		bool began() const noexcept {return began_;}
		bool active(const hold& owner,const controller_input::frame& input,clock::time_point now) const noexcept
		{
			return sequence_ && valid_hand(owner.holding_hand()) && owner.id()==owner_.id() && owner.holding_hand()==owner_.holding_hand() &&
				owner.rear_revision==owner_.rear_revision && input.reference_generation==reference_ &&
				input.continuity_generation==continuity_ && input.focused && !input.orientation_settling &&
				input.grip[unsigned(owner.holding_hand())].valid && input.aim[unsigned(owner.holding_hand())].valid &&
				input.sequence>=sequence_ && now>=sampled_ && now-sampled_<=std::chrono::milliseconds(150) &&
				now>=input.sampled_at && now-input.sampled_at<=std::chrono::milliseconds(150);
		}
		bool update(const hold& owner,std::uint64_t instance,const controller_input::frame& input,
			int slot,bool eligible,clock::time_point now,bool chest_enabled=false) noexcept
		{
			using namespace std::chrono_literals;
			began_=false;
			if(!eligible || !owner.id() || !valid_hand(owner.holding_hand()) || !instance ||
				slot<0 || slot>(chest_enabled?2:1) || !input.sequence || !input.reference_generation || !input.focused ||
				input.orientation_settling || now<input.sampled_at || now-input.sampled_at>150ms)
			{reset();return false;}
			const bool restart=!sequence_ || owner.id()!=owner_.id() || owner.holding_hand()!=owner_.holding_hand() ||
				owner.rear_revision!=owner_.rear_revision || instance!=instance_ || slot!=slot_ ||
				input.reference_generation!=reference_ || input.continuity_generation!=continuity_ ||
				input.sequence<sequence_ || input.sampled_at<sampled_ ||
				(!input.continuity_generation && input.sampled_at-sampled_>150ms);
			if(restart)
			{
				owner_=owner;instance_=instance;reference_=input.reference_generation;continuity_=input.continuity_generation;
				sequence_=input.sequence;started_=sampled_=input.sampled_at;slot_=slot;began_=true;return false;
			}
			if(input.sequence==sequence_)return false;
			sequence_=input.sequence;sampled_=input.sampled_at;
			return sampled_-started_>=1s;
		}
	};
}
