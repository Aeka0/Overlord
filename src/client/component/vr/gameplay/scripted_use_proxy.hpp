#pragma once
#include "interaction_volume.hpp"
#include <string_view>
#include <span>
#include <optional>
#include <cstring>

namespace vr::gameplay::interaction::scripted_use
{
	struct binding {std::string_view map,trigger,visual,tag;};
	inline constexpr binding dsm{"estate","dsm_usetrigger","dsm_obj","tag_origin"};
	inline const binding* for_map(std::string_view map) noexcept{return map==dsm.map?&dsm:nullptr;}
	struct snapshot
	{
		target_key trigger{},visual{};
		oriented_volume volume{};
		vec native_center{};
		bool enabled{};
		prompt_kind prompt{};
	};
	inline target score(const snapshot& value,const ray& aim) noexcept
	{
		if(!value.enabled || !value.trigger || !value.visual || !valid(aim))return {};
		const auto center=value.volume.world(value.volume.center);
		const auto distance=hands::length(hands::sub(center,value.native_center));
		// Native scripts disable the DSM trigger by moving it far below the map.
		// A visual proxy must never resurrect that inactive trigger.
		if(!std::isfinite(distance) || distance>.75f*aim.units)return {};
		return score_volume(aim,value.trigger,value.volume);
	}
	inline target finish(const snapshot& value,target proposal,bool native_admitted,bool visible) noexcept
	{
		if(!value.enabled || !value.volume.valid || !proposal || proposal.key!=value.trigger || !native_admitted || !visible)return {};
		proposal.position=value.volume.world(value.volume.center);
		proposal.prompt=value.prompt;
		if(value.prompt==prompt_kind::resupply)
			for(unsigned i=0;i<3;++i)proposal.position[2]+=std::abs(value.volume.axis[i][2])*value.volume.half[i];
		return proposal;
	}
	// Only the shared ammo-cache gaze helper's verified dot-product call is
	// replaced. The following native callback and makeusable/unusable stay intact.
	inline std::optional<std::size_t> ammo_gaze_call(std::span<const std::byte> code) noexcept
	{
		constexpr unsigned char pattern[]{0x1c,0xfb,0x00,0x44,0x6f,0x75,0x06,0x6e};
		if(code.size()>512)return {};
		std::optional<std::size_t> found;
		for(std::size_t i=0;i+sizeof(pattern)<=code.size();++i)
			if(!std::memcmp(code.data()+i,pattern,sizeof(pattern))) {if(found)return {};found=i+3;}
		return found;
	}
	inline bool ammo_gaze_override(bool vr,bool callsite,target_key self,target_key linked_trigger) noexcept
	{return vr && callsite && self && self==linked_trigger;}
	struct candidate {const snapshot* source{};target proposal{};};
	inline std::array<candidate,4> candidates(std::span<const snapshot> values,const ray& aim,target_key held) noexcept
	{
		std::array<candidate,4> out{};
		for(const auto& value:values)
		{
			candidate next{&value,score(value,aim)};if(!next.proposal)continue;
			for(auto& slot:out)if(prefer(next.proposal,slot.proposal,held))std::swap(next,slot);
		}
		return out;
	}
	// Server-only per-command snapshot; no VM objects or asset pointers escape.
	std::span<const snapshot> sample(const ray&);
}
