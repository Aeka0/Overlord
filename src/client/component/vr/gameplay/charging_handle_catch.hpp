#pragma once
#include "hands/pose_library.hpp"

namespace vr::gameplay::weapons
{
	// Receiver-local raised orientation and actual tab contact. Rotation occurs
	// about the part-local hinge (zero for native bone pivots), so an offset
	// tab follows its guide rather than the gun origin. Distances use native units.
	struct charging_handle_catch { hands::quat raised; hands::vec contact, hinge{}; };
	inline bool valid_catch(const charging_handle_catch& p) noexcept
	{
		float square{};
		for (float x:p.raised) { if (!std::isfinite(x)) return false; square+=x*x; }
		for (auto v:{p.contact,p.hinge}) for (float x:v) if (!std::isfinite(x) || std::abs(x)>100) return false;
		return std::abs(square-1)<.001f;
	}
	inline hands::anchor handle_pose(hands::anchor rest,const charging_handle_catch* p,
		hands::vec axis,float travel,float amount) noexcept
	{
		rest.position=hands::add(rest.position,hands::scale(axis,travel));
		if (p)
		{
			const auto rotation=hands::blend_quat(rest.rotation,p->raised,amount);
			// The animation bone may sit on the receiver centreline rather than
			// on the actual guide. Keep the authored hinge fixed during rotation.
			rest.position=hands::add(rest.position,hands::sub(hands::rotate(rest.rotation,p->hinge),hands::rotate(rotation,p->hinge)));
			rest.rotation=rotation;
		}
		return rest;
	}
	inline hands::anchor carry_with_handle(hands::anchor rest,hands::anchor moved,hands::anchor value) noexcept
	{
		const auto delta=hands::normalize(hands::multiply(moved.rotation,hands::conjugate(rest.rotation)));
		return {hands::add(moved.position,hands::rotate(delta,hands::sub(value.position,rest.position))),
			hands::normalize(hands::multiply(delta,value.rotation))};
	}
	inline std::array<hands::vec,2> handle_bounds(hands::anchor rest,hands::anchor moved,hands::vec low,hands::vec high) noexcept
	{
		std::array<hands::vec,2> out;
		out[0]=out[1]=carry_with_handle(rest,moved,{low,{0,0,0,1}}).position;
		for (int bits=1;bits<8;++bits)
		{
			hands::vec corner; for (int i=0;i<3;++i) corner[i]=(bits&(1<<i)) ? high[i] : low[i];
			const auto v=carry_with_handle(rest,moved,{corner,{0,0,0,1}}).position;
			for (int i=0;i<3;++i) { out[0][i]=std::min(out[0][i],v[i]); out[1][i]=std::max(out[1][i],v[i]); }
		}
		return out;
	}
}
