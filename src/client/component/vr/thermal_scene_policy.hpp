#pragma once
#include "engine_stereo_view.hpp"
#include <atomic>
#include <cmath>
#include <cstring>

namespace vr::eye_composition {struct event;}
namespace vr::thermal_scene
{
	inline constexpr std::size_t flags_offset=0x204,ssr_offset=0x2ca4;
	struct world_request {float ssr_scale{};bool valid{};};
	using planner=world_request(*)(const eye_composition::event&) noexcept;
	inline std::atomic<planner> plan{};
	inline std::atomic_uint64_t world_pairs{};
	inline std::atomic<float> last_native_ssr{},last_world_ssr{};
	using record=std::array<std::uint8_t,engine_stereo_view::h2_scene_record_size>;
	struct parameters {std::uint8_t flags{};float ssr{};};
	inline parameters read(const void* source) noexcept
	{
		parameters value;const auto* bytes=static_cast<const std::uint8_t*>(source);
		value.flags=bytes[flags_offset];std::memcpy(&value.ssr,bytes+ssr_offset,4);return value;
	}
	inline void write(void* destination,const parameters& value) noexcept
	{
		auto* bytes=static_cast<std::uint8_t*>(destination);bytes[flags_offset]=value.flags;
		std::memcpy(bytes+ssr_offset,&value.ssr,4);
	}
	inline bool valid_scale(float value) noexcept {return std::isfinite(value) && value>=0 && value<=10;}
	inline bool normalize_world(engine_stereo_view::scene_record_pair& pair,const world_request& request,
		const engine_stereo_view::slot_pair& views) noexcept
	{
		if(!request.valid || !valid_scale(request.ssr_scale) || !pair.pair_id || !pair.publication ||
			views.screen_scope_epoch || views.weapon_display_epoch)return false;
		const auto left=read(pair.left.data()),right=read(pair.right.data());
		// Native full-view thermal is scripted ownership. Only split a carried
		// scope's stencil mode, keeping unrelated high bits and both cameras exact.
		if((left.flags&3)!=1 || left.flags!=right.flags || !valid_scale(left.ssr) || left.ssr!=right.ssr)return false;
		const parameters ordinary{static_cast<std::uint8_t>(left.flags&~3u),request.ssr_scale};
		write(pair.left.data(),ordinary);write(pair.right.data(),ordinary);return true;
	}
	// Eye 0 borrows H2's arena record. Explicit owner-thread lease: restore just
	// these five bytes after display and on cancellation, keeping native updates
	// to target routing, uploads, model state and all other record fields intact.
	struct arena_lease
	{
		void* destination{};parameters original{};
		bool install(void* target,const record& world) noexcept
		{
			if(destination || !target)return false;
			const auto before=read(target),after=read(world.data());
			if((before.flags&3)!=1 || (after.flags&3) || (before.flags&~3u)!=after.flags ||
				!valid_scale(before.ssr) || !valid_scale(after.ssr))return false;
			original=before;destination=target;write(target,after);return true;
		}
		void restore() noexcept
		{
			if(destination)write(destination,original);
			destination=nullptr;original={};
		}
	};
}
